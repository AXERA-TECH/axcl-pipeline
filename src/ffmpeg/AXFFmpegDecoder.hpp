#pragma once

#include "ax_base_type.h"
#include "ax_global_type.h"
#include "axcl.h"

extern "C"
{
#include "libavcodec/codec_id.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libswscale/swscale.h"
}

#include "utils/logger.h"
#include "utils/def.h"

#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <algorithm>
#include <functional>

#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM

// typedef void (*AXFrameCallback)(AVFrame *frame, void *user_data);
using AXFrameCallback = std::function<void(AVFrame *frame, void *user_data)>;

class AXFFmpegDecoder
{
private:
    std::thread th_decode;
    AVBufferRef *hw_device_ctx = nullptr;
    AVFormatContext *pstAvFmtCtx = nullptr;

    const AVCodec *codec = NULL;
    AVCodecParameters *origin_par = NULL;
    AVCodecContext *avctx = NULL;

    AVDictionary *codec_opts = NULL; // used for avcodec_open2
    AVDictionary *input_opts = NULL; // used for avformat_open_input

    enum AVCodecID eCodecID = AV_CODEC_ID_H264;
    char device_index[16] = "0";
    char codec_names[128] = {0};

    int s32VideoIndex = -1;
    unsigned long long frame_num = 0;
    volatile int loop_exit = 0;

    // std::vector<unsigned char> nv12_frame_data;
    // std::mutex mtx_nv12_frame_data;

    AXFrameCallback frame_cb = nullptr;
    void *user_data = nullptr;

    static enum AVPixelFormat get_format(AVCodecContext *s, const enum AVPixelFormat *pix_fmts)
    {
        const enum AVPixelFormat *p;

        for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++)
        {
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);

            if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
                break;
        }

        return *p;
    }

    int set_hwframe_ctx(AVCodecContext *ctx, AVBufferRef *hw_device_ctx, int width, int height)
    {
        AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);
        if (!hw_frames_ref)
        {
            fprintf(stderr, "Failed to create hardware frame context.\n");
            return -1;
        }

        AVHWFramesContext *frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
        frames_ctx->format = AV_PIX_FMT_AXMM;
        frames_ctx->sw_format = AV_PIX_FMT_NV12;
        frames_ctx->width = width;
        frames_ctx->height = height;
        frames_ctx->initial_pool_size = 20;

        int err = av_hwframe_ctx_init(hw_frames_ref);
        if (err < 0)
        {
            fprintf(stderr, "Failed to initialize hw frame context. %d\n", err);
            av_buffer_unref(&hw_frames_ref);
            return err;
        }

        ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
        av_buffer_unref(&hw_frames_ref);
        return 0;
    }

    void func_th_decode()
    {
        int ret;
        AVFrame *frame = NULL;
        AVFrame *nv12_frame = NULL;
        AVPacket *pstAvPkt = NULL;
        frame = av_frame_alloc();
        if (!frame)
        {
            SAMPLE_LOG_E("Can't allocate frame\n");
            return;
        }

        // frame->format = AV_PIX_FMT_NV12;
        // frame->width = avctx->width;
        // frame->height = avctx->height;

        // // 注意这里对齐参数选得足够大（例如 256 或 512）
        // ret = av_frame_get_buffer(frame, 1);

        // if (!nv12_frame)
        // {
        //     SAMPLE_LOG_E("Can't allocate nv12 frame\n");
        //     av_frame_free(&frame);
        //     return;
        // }

        pstAvPkt = av_packet_alloc();
        if (!pstAvPkt)
        {
            SAMPLE_LOG_E("av_packet_alloc failed \n");
            av_frame_free(&frame);
            return;
        }

        while (!loop_exit)
        {
            ret = av_read_frame(pstAvFmtCtx, pstAvPkt);
            if (ret < 0)
            {
                if (ret == AVERROR_EOF || avio_feof(pstAvFmtCtx->pb))
                {
                    // flush the decoder
                    ret = avcodec_send_packet(avctx, NULL);
                    if (ret < 0)
                        SAMPLE_LOG_E("avcodec_send_packet(NULL) failed: %d", ret);
                    break;
                }
                else
                {
                    SAMPLE_LOG_E("av_read_frame fail, error: %d", ret);
                    break;
                }
            }
            else
            {
                // only send packets for video stream
                if (pstAvPkt->stream_index != s32VideoIndex)
                {
                    av_packet_unref(pstAvPkt);
                    continue;
                }

                avctx->codec_type = AVMEDIA_TYPE_VIDEO;
                avctx->codec_id = eCodecID;

                ret = avcodec_send_packet(avctx, pstAvPkt);
                if (ret == AVERROR(EAGAIN))
                {
                    SAMPLE_LOG_E("avcodec_send_packet EAGAIN\n");
                    av_packet_unref(pstAvPkt);
                    continue;
                }
                else if (ret == AVERROR_EOF)
                {
                    SAMPLE_LOG_I("decoder returned EOF on send_packet\n");
                }

                av_packet_unref(pstAvPkt);
            }

            while (ret >= 0)
            {
                ret = avcodec_receive_frame(avctx, frame);
                if (ret == AVERROR(EAGAIN))
                {
                    // SAMPLE_LOG_E("avcodec_receive_frame EAGAIN\n");
                    break; // need more input
                }
                else if (ret == AVERROR_EOF)
                {
                    // SAMPLE_LOG_I("decoder returned EOF on receive_frame\n");
                    break;
                }
                else if (ret < 0)
                {
                    SAMPLE_LOG_E("avcodec_receive_frame error: %d", ret);
                    break;
                }

                // printf("avcodec_receive_frame success, frame_num: %d, width: %d, height: %d, format: %d\n", frame_num, frame->width, frame->height, frame->format);

                // Copy Y and UV (assume NV12 layout)
                if (frame->format == AV_PIX_FMT_NV12 || frame->format == AV_PIX_FMT_NV21 || frame->format == AV_PIX_FMT_YUV420P)
                {
                    if (frame_cb)
                    {
                        frame_cb(frame, user_data);
                        // sws_freeContext(sws_ctx);
                    }

                    // std::lock_guard<std::mutex> lock(mtx_nv12_frame_data);

                    // // Ensure buffer is large enough
                    // size_t expected = static_cast<size_t>(avctx->width) * avctx->height * 3 / 2;
                    // if (nv12_frame_data.size() < expected)
                    //     nv12_frame_data.resize(expected);

                    // unsigned char *p_lu = frame->data[0];
                    // for (int i = 0; i < frame->height; i++)
                    // {
                    //     memcpy(nv12_frame_data.data() + i * frame->width, p_lu, frame->width);
                    //     p_lu += frame->linesize[0];
                    // }

                    // unsigned char *p_ch = frame->data[1];
                    // for (int i = 0; i < frame->height / 2; i++)
                    // {
                    //     memcpy(nv12_frame_data.data() + frame->width * frame->height + i * frame->width, p_ch, frame->width);
                    //     p_ch += frame->linesize[1];
                    // }
                }
                else
                {
                    // If HW frame or other format, you may want to convert to NV12 here (not implemented)
                    SAMPLE_LOG_W("Frame format is %d, copying skipped (conversion not implemented)", frame->format);
                }

                av_frame_unref(frame);

                frame_num++;
            }
        }

        av_frame_free(&frame);
        frame = NULL;
        av_frame_free(&nv12_frame);
        nv12_frame = NULL;
        av_packet_free(&pstAvPkt);
        pstAvPkt = NULL;
        SAMPLE_LOG_I("thread exit\n");
    }

public:
    AXFFmpegDecoder() = default;
    ~AXFFmpegDecoder()
    {
        Deinit();
    }

    // url can be an RTSP url (rtsp://...) or a local file path
    // device_id is optional and used when using a hardware child card decoder ("d" option)
    int Init(const std::string input, AXFFmpegCodecID codec_type, int device_id = 0)
    {
        // choose decoder mode
        if (codec_type == AXFFmpegCodecID::h264_ax)
        {
            eCodecID = AV_CODEC_ID_H264;
            snprintf(codec_names, sizeof(codec_names), "h264_axdec");
        }
        else if (codec_type == AXFFmpegCodecID::hevc_ax)
        {
            eCodecID = AV_CODEC_ID_HEVC;
            snprintf(codec_names, sizeof(codec_names), "hevc_axdec");
        }
        else // auto
        {
            eCodecID = AV_CODEC_ID_NONE;
            codec_names[0] = '\0';
        }

        snprintf(device_index, sizeof(device_index), "%d", device_id);
        SAMPLE_LOG_I("Init decoder %s, device_id %s, input=%s\n", codec_names[0] ? codec_names : "auto", device_index, input.c_str());

        pstAvFmtCtx = avformat_alloc_context();
        if (!pstAvFmtCtx)
        {
            SAMPLE_LOG_E("avformat_alloc_context() failed!");
            return AVERROR(ENOMEM);
        }

        // If url looks like rtsp, set some helpful input options (use tcp, set timeout)
        if (input.rfind("rtsp://", 0) == 0)
        {
            av_dict_set(&input_opts, "rtsp_transport", "tcp", 0);
            av_dict_set(&input_opts, "stimeout", "5000000", 0); // microseconds
            av_dict_set(&input_opts, "max_delay", "500000", 0);
        }

        int ret = avformat_open_input(&pstAvFmtCtx, input.c_str(), NULL, &input_opts);
        if (ret < 0)
        {
            AX_CHAR szError[128] = {0};
            av_strerror(ret, szError, sizeof(szError));
            SAMPLE_LOG_E("open %s fail, error: %d, %s", input.c_str(), ret, szError);
            av_dict_free(&input_opts);
            return -1;
        }
        av_dict_free(&input_opts);

        ret = avformat_find_stream_info(pstAvFmtCtx, NULL);
        if (ret < 0)
        {
            SAMPLE_LOG_E("avformat_find_stream_info fail, error = %d", ret);
            return -1;
        }

        s32VideoIndex = -1;
        for (int i = 0; i < (int)pstAvFmtCtx->nb_streams; i++)
        {
            if (AVMEDIA_TYPE_VIDEO == pstAvFmtCtx->streams[i]->codecpar->codec_type)
            {
                s32VideoIndex = i;
                break;
            }
        }

        if (s32VideoIndex < 0)
        {
            SAMPLE_LOG_E("No video stream found");
            return -1;
        }

        origin_par = pstAvFmtCtx->streams[s32VideoIndex]->codecpar;

        // If user requested auto_decoder, let FFmpeg pick based on codec id
        if (codec_type == AXFFmpegCodecID::auto_ax)
        {
            if (origin_par->codec_id == AV_CODEC_ID_H264)
            {
                eCodecID = AV_CODEC_ID_H264;
                snprintf(codec_names, sizeof(codec_names), "h264_axdec");
            }
            else if (origin_par->codec_id == AV_CODEC_ID_HEVC)
            {
                eCodecID = AV_CODEC_ID_HEVC;
                snprintf(codec_names, sizeof(codec_names), "hevc_axdec");
            }
            else
            {
                SAMPLE_LOG_E("Unsupported codec id %d", origin_par->codec_id);
                return -1;
            }
        }

        // try to find special hw child card decoder by name first
        codec = avcodec_find_decoder_by_name(codec_names);
        if (!codec)
        {
            SAMPLE_LOG_E("avcodec_find_decoder_by_name failed\n");
            return -1;
        }

        avctx = avcodec_alloc_context3(codec);
        if (!avctx)
        {
            SAMPLE_LOG_E("Can't allocate decoder context\n");
            return -1;
        }

        ret = avcodec_parameters_to_context(avctx, origin_par);
        if (ret < 0)
        {
            SAMPLE_LOG_E("avcodec_parameters_to_context error: %d\n", ret);
            avcodec_free_context(&avctx);
            return -1;
        }

        // Configure threads and limits
        avctx->thread_count = 2;
        avctx->thread_type = FF_THREAD_FRAME;
        avctx->debug = 0;
        avctx->max_pixels = avctx->width * avctx->height * 3 / 2;
        avctx->get_format = get_format;
        avctx->pix_fmt = AV_PIX_FMT_AXMM;

        // Setup codec options. If using hardware child card, set device index
        // codec_opts = (AVDictionary *)malloc(sizeof(AVDictionary *));

        av_dict_set(&codec_opts, "d", device_index, 0);

        if (0)
        { // 硬件上下文
            AVDictionary *dict = nullptr;
            char value[8];
            sprintf(value, "%d", device_id);
            int err = av_dict_set(&dict, "device_index", value, 0);
            if (err < 0)
                return err;
            av_dict_set(&dict, "alloc_blk", "1", 0);

            // 创建硬件上下文
            if ((err = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_AXMM, NULL, dict, 0)) < 0)
            {
                fprintf(stderr, "Failed to create hw device: %d\n", err);
                return -1;
            }

            av_dict_free(&dict);

            avctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
            if (!avctx->hw_device_ctx)
            {
                fprintf(stderr, "Failed to set hardware device context.\n");
                err = AVERROR(ENOMEM);
                return err;
            }

            if (set_hwframe_ctx(avctx, hw_device_ctx, avctx->width, avctx->height) < 0)
            {
                fprintf(stderr, "Failed to set hwframe context.\n");
                return -1;
            }
        }

        // open codec
        ret = avcodec_open2(avctx, codec, &codec_opts);
        if (ret < 0)
        {
            SAMPLE_LOG_E("Can't open decoder, ret=%d\n", ret);
            // av_dict_free(&codec_opts);
            avcodec_free_context(&avctx);
            return -1;
        }
        av_dict_free(&codec_opts);

        // allocate nv12 buffer based on negotiated size
        // if (avctx->width > 0 && avctx->height > 0)
        // {
        //     nv12_frame_data.resize(avctx->width * avctx->height * 3 / 2);
        // }

        return 0;
    }

    void Start(AXFrameCallback _frame_cb, void *_user_data = nullptr)
    {
        frame_cb = _frame_cb;
        user_data = _user_data;

        loop_exit = 0;

        // start decode thread
        th_decode = std::thread(&AXFFmpegDecoder::func_th_decode, this);
    }

    void Deinit()
    {
        loop_exit = 1;
        if (th_decode.joinable())
            th_decode.join();

        if (avctx)
        {
            avcodec_free_context(&avctx);
            avctx = NULL;
        }

        if (pstAvFmtCtx)
        {
            avformat_close_input(&pstAvFmtCtx);
            pstAvFmtCtx = NULL;
        }

        if (hw_device_ctx)
            av_buffer_unref(&hw_device_ctx);
    }

    // Pull latest NV12 frame into user buffer (must be large enough)
    // void GetFrame(unsigned char *nv12_frame)
    // {
    //     std::lock_guard<std::mutex> lock(mtx_nv12_frame_data);
    //     if (!nv12_frame_data.empty())
    //         memcpy(nv12_frame, nv12_frame_data.data(), nv12_frame_data.size());
    // }

    unsigned long long GetFrameCount() const { return frame_num; }

    int GetWidth() const { return avctx->width; }
    int GetHeight() const { return avctx->height; }

    float GetFps() const { return avctx->framerate.num / avctx->framerate.den; }
};
