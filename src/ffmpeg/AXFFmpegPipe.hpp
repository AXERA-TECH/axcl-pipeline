#pragma once
#include <condition_variable>
#include <opencv2/opencv.hpp>
#include <atomic>

#include "AXFFmpegDecoder.hpp"
#include "AXFFmpegEncoder.hpp"
#include "../libdet/include/libdet.h"

class AXFFmpegPipe
{
private:
    AXFFmpegEncoder encoder;
    AXFFmpegDecoder decoder;

    std::mutex mtx;
    std::condition_variable cv_request;
    std::condition_variable cv_done;

    cv::Mat nv12_frame;
    cv::Mat rgb_frame;

    std::atomic<bool> request_copy = false;
    std::atomic<bool> copy_done = false;

    std::queue<ax_det_result_t> q_det_results;
    ax_det_result_t last_result;  // 上一次检测结果
    bool has_last_result = false; // 是否有有效历史结果
    int hold_count = 0;           // 保留计数器
    int hold_max_count = 3; // 最大保留计数

    int64_t frame_count = 0; // 帧计数器
    void frame_cb(AVFrame *frame, void *user_data)
    {
        if (frame_count % 100 == 0)
        {
            printf("frame_cb, pts: %ld, width: %d, height: %d, format: %d line_size: %d %d %d\n", frame->pts, frame->width, frame->height, frame->format, frame->linesize[0], frame->linesize[1], frame->linesize[2]);
        }
        frame_count++;
        AXFFmpegEncoder *encoder = (AXFFmpegEncoder *)user_data;
        if (encoder)
        {
            ax_det_result_t result;
            bool use_new_result = false;

            if (!q_det_results.empty())
            {
                result = q_det_results.front();
                q_det_results.pop();
                last_result = result;
                has_last_result = true;
                hold_count = 0;
                use_new_result = true;
            }
            else if (has_last_result && hold_count < hold_max_count)
            {
                result = last_result;
                hold_count++;
            }
            else
            {
                has_last_result = false; 
            }

            if (has_last_result)
            {
                cv::Mat gray_frame(frame->height, frame->width, CV_8UC1, frame->data[0], frame->linesize[0]);

                for (int i = 0; i < result.num_objs; i++)
                {
                    ax_det_obj_t &obj = result.objects[i];
                    cv::Rect rect(obj.box.x, obj.box.y, obj.box.w, obj.box.h);
                    cv::rectangle(gray_frame, rect, cv::Scalar(255), 2);

                    char label_info[128];
                    sprintf(label_info, "%d %4.2f", obj.label, obj.score);
                    cv::putText(gray_frame, label_info, cv::Point(obj.box.x, obj.box.y - 10),
                                cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255), 2);

                    for (int j = 0; j < obj.num_kpt; j++)
                    {
                        cv::circle(gray_frame, cv::Point(obj.kpts[j].x, obj.kpts[j].y),
                                   5, cv::Scalar(255), -1);
                    }
                }
            }

            encoder->Encode(frame);
        }

        // 惰性拷贝逻辑
        std::unique_lock<std::mutex> lock(mtx);
        if (request_copy)
        {
            if (nv12_frame.empty())
                nv12_frame = cv::Mat(frame->height * 3 / 2, frame->width, CV_8UC1).clone();

            uint8_t *dst = nv12_frame.data;

            // copy Y plane
            for (int i = 0; i < frame->height; ++i)
            {
                memcpy(dst + i * frame->width, frame->data[0] + i * frame->linesize[0], frame->width);
            }

            // copy UV plane
            uint8_t *dst_uv = dst + frame->height * frame->width;
            for (int i = 0; i < frame->height / 2; ++i)
            {
                memcpy(dst_uv + i * frame->width, frame->data[1] + i * frame->linesize[1], frame->width);
            }

            copy_done = true;
            request_copy = false;
            cv_done.notify_one();
        }
    }

public:
    AXFFmpegPipe(/* args */) = default;
    ~AXFFmpegPipe() = default;

    int Init(const std::string input, std::string output, int device_index)
    {
        int ret = decoder.Init(input, AXFFmpegCodecID::auto_ax, device_index);
        if (ret < 0)
            return ret;

        ret = encoder.Init(output, AXFFmpegCodecID::auto_ax, decoder.GetWidth(), decoder.GetHeight(), decoder.GetFps(), device_index);
        if (ret < 0)
            return ret;

        return 0;
    }

    void Start()
    {
        decoder.Start([this](AVFrame *frame, void *user_data)
                      { this->frame_cb(frame, user_data); }, &encoder);
    }

    void Deinit()
    {
        decoder.Deinit();
    }

    cv::Mat GetFrame(int timeout_ms = 100, bool convert_to_rgb = false)
    {
        // 1. 发起请求
        {
            std::unique_lock<std::mutex> lock(mtx);
            request_copy = true;
            copy_done = false;
        }

        cv_request.notify_one(); // 通知回调可以拷贝了

        // 2. 等待回调拷贝完成
        {
            std::unique_lock<std::mutex> lock(mtx);
            bool done = cv_done.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]
                                         { return copy_done.load(); });
            if (!done)
            {
                printf("GetFrame timeout\n");
                return cv::Mat();
            }
        }

        if (convert_to_rgb)
        {
            cv::cvtColor(nv12_frame, rgb_frame, cv::COLOR_YUV2RGB_NV12);
            return rgb_frame;
        }
        else
        {
            cv::cvtColor(nv12_frame, rgb_frame, cv::COLOR_YUV2BGR_NV12);
            return rgb_frame;
        }
    }

    void PushDetResult(ax_det_result_t result)
    {
        q_det_results.push(result);
    }
};
