#pragma once
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <unistd.h>

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/hwcontext.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
}

#include "utils/def.h"

class AXFFmpegEncoder
{
private:
    AVFrame *hw_frame = nullptr;
    AVCodecContext *avctx = nullptr;
    const AVCodec *codec = nullptr;
    char *enc_name = (char *)"h264_axenc";
    AVDictionary *dict = nullptr;
    AVBufferRef *hw_device_ctx = nullptr;

    // 输出
    AVFormatContext *ofmt_ctx = nullptr;
    AVStream *out_stream = nullptr;
    bool is_rtsp = false;
    int64_t frame_count = 0;

    int64_t encode_pts = 0;

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

public:
    AXFFmpegEncoder() = default;

    ~AXFFmpegEncoder()
    {
        if (ofmt_ctx)
        {
            av_write_trailer(ofmt_ctx);
            if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
                avio_closep(&ofmt_ctx->pb);
            avformat_free_context(ofmt_ctx);
        }

        if (hw_frame)
            av_frame_free(&hw_frame);
        if (avctx)
            avcodec_free_context(&avctx);
        if (hw_device_ctx)
            av_buffer_unref(&hw_device_ctx);
        if (dict)
            av_dict_free(&dict);
    }

    int Init(const std::string &url, AXFFmpegCodecID codec_id,
             int width, int height, int fps, int device_index)
    {
        is_rtsp = (url.rfind("rtsp://", 0) == 0); // 判断开头是否是rtsp://

        if (device_index < 0 || device_index >= 256)
        {
            fprintf(stderr, "device index %d is out of range[0,255]\n", device_index);
            return -1;
        }

        char value[8];
        sprintf(value, "%d", device_index);
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

        switch (codec_id)
        {
        case h264_ax:
            enc_name = (char *)"h264_axenc";
            break;
        case hevc_ax:
            enc_name = (char *)"hevc_axenc";
            break;
        default:
            enc_name = (char *)"h264_axenc";
            break;
        }

        codec = avcodec_find_encoder_by_name(enc_name);
        if (!codec)
        {
            fprintf(stderr, "Could not find encoder: %s\n", enc_name);
            return -1;
        }

        avctx = avcodec_alloc_context3(codec);
        if (!avctx)
        {
            fprintf(stderr, "Failed to alloc codec context\n");
            return -1;
        }

        avctx->width = width;
        avctx->height = height;
        avctx->time_base = {1, fps};
        avctx->framerate = {fps, 1};
        avctx->sample_aspect_ratio = {1, 1};
        avctx->pix_fmt = AV_PIX_FMT_AXMM;
        avctx->bit_rate = width * height;

        av_opt_set(avctx->priv_data, "i_qmin", "12", 0);
        av_opt_set(avctx->priv_data, "i_qmax", "48", 0);
        av_opt_set(avctx, "qmin", "18", 0);
        av_opt_set(avctx, "qmax", "50", 0);

        AVDictionary *opts = NULL;
        av_dict_set(&opts, "qmin", "15", 0);
        av_opt_set_dict(avctx, &opts);
        av_dict_free(&opts);

        avctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
        if (!avctx->hw_device_ctx)
        {
            fprintf(stderr, "Failed to set hardware device context.\n");
            err = AVERROR(ENOMEM);
            return err;
        }

        if (set_hwframe_ctx(avctx, hw_device_ctx, width, height) < 0)
        {
            fprintf(stderr, "Failed to set hwframe context.\n");
            return -1;
        }

        if ((err = avcodec_open2(avctx, codec, nullptr)) < 0)
        {
            fprintf(stderr, "Cannot open encoder: %d\n", err);
            return -1;
        }

        // ---------------- 输出初始化 ----------------
        if (is_rtsp) {
            avformat_alloc_output_context2(&ofmt_ctx, NULL, "rtsp", url.c_str());
        } else {
            avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, url.c_str());
        }
        if (!ofmt_ctx) {
            fprintf(stderr, "Could not allocate output context\n");
            return -1;
        }

        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream)
        {
            fprintf(stderr, "Failed to create output stream\n");
            return -1;
        }

        avcodec_parameters_from_context(out_stream->codecpar, avctx);
        out_stream->time_base = avctx->time_base;

        if (is_rtsp)
        {
            av_dict_set(&dict, "rtsp_transport", "tcp", 0);
            av_dict_set(&dict, "muxdelay", "0.1", 0);
        }

        // 打开输出
        if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
        {
            if ((err = avio_open(&ofmt_ctx->pb, url.c_str(), AVIO_FLAG_WRITE)) < 0)
            {
                fprintf(stderr, "Failed to open output file: %d\n", err);
                return -1;
            }
        }

        if ((err = avformat_write_header(ofmt_ctx, &dict)) < 0)
        {
            fprintf(stderr, "Error occurred when writing header: %d\n", err);
            return -1;
        }

        // 分配硬件帧
        hw_frame = av_frame_alloc();
        if (!hw_frame)
            return AVERROR(ENOMEM);
        if ((err = av_hwframe_get_buffer(avctx->hw_frames_ctx, hw_frame, 0)) < 0)
            return err;

        printf("Encoder initialized for %s output.\n", is_rtsp ? "RTSP" : "File");
        return 0;
    }

    int Encode(AVFrame *frame)
    {
        int err = av_hwframe_transfer_data(hw_frame, frame, 0);
        if (err < 0)
        {
            fprintf(stderr, "Error transferring frame data: %d\n", err);
            return -1;
        }

        hw_frame->pts = frame_count++;

        if ((err = avcodec_send_frame(avctx, hw_frame)) < 0)
        {
            fprintf(stderr, "Error sending frame: %d\n", err);
            return -1;
        }

        while (true)
        {
            AVPacket *pkt = av_packet_alloc();
            err = avcodec_receive_packet(avctx, pkt);
            if (err == AVERROR(EAGAIN) || err == AVERROR_EOF)
            {
                av_packet_free(&pkt);
                break;
            }
            else if (err < 0)
            {
                fprintf(stderr, "Error receiving packet: %d\n", err);
                av_packet_free(&pkt);
                return -1;
            }

            if (pkt->pts == AV_NOPTS_VALUE)
            {
                pkt->pts = encode_pts;
                pkt->dts = encode_pts;
                encode_pts++;
            }

            av_packet_rescale_ts(pkt, (AVRational){1, 30}, out_stream->time_base);
            pkt->stream_index = out_stream->index;

            if ((err = av_interleaved_write_frame(ofmt_ctx, pkt)) < 0)
            {
                fprintf(stderr, "Error writing frame: %d\n", err);
                av_packet_free(&pkt);
                return -1;
            }
            av_packet_free(&pkt);
        }

        return 0;
    }
};