#include "ffmpeg/AXFFmpegPipe.hpp"

#include "utils/cmdline.hpp"
#include <unistd.h>

#include "libdet/include/libdet.h"

#include <signal.h>

volatile bool b_continue = true;

void sigint_handler(int signum)
{
    b_continue = false;
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, sigint_handler);

    ax_devices_t ax_devices;
    memset(&ax_devices, 0, sizeof(ax_devices_t));
    if (ax_dev_enum_devices(&ax_devices) != 0)
    {
        printf("enum devices failed\n");
        return -1;
    }

    if (ax_devices.host.available)
    {
        ax_dev_sys_init(host_device, -1);
    }

    if (ax_devices.devices.count > 0)
    {
        ax_dev_sys_init(axcl_device, 0);
    }

    if (!ax_devices.host.available && ax_devices.devices.count == 0)
    {
        printf("no device available\n");
        return -1;
    }

    ax_det_init_t init_info;
    memset(&init_info, 0, sizeof(init_info));

    cmdline::parser a;
    a.add<std::string>("url", 'u', "url", true, "");
    a.add<std::string>("output", 'o', "rtsp or xxx.mp4", false, "1.mp4");
    a.add<std::string>("model", 'm', "model", true, "");
    a.parse_check(argc, argv);

    std::string url = a.get<std::string>("url");
    std::string output = a.get<std::string>("output");

    if (ax_devices.host.available)
    {
        init_info.dev_type = host_device;
    }
    else if (ax_devices.devices.count > 0)
    {
        init_info.dev_type = axcl_device;
        init_info.devid = 0;
    }
    init_info.num_classes = 80;
    init_info.num_kpt = 0;
    init_info.model_type = ax_det_model_type_e::ax_det_model_type_yolov8;

    sprintf(init_info.model_path, "%s", a.get<std::string>("model").c_str());
    init_info.threshold = 0.25;
    ax_det_handle_t handle;
    int ret = ax_det_init(&init_info, &handle);
    if (ret != ax_det_errcode_success)
    {
        printf("ax_det_init failed\n");
        return -1;
    }

    AXFFmpegPipe pipe;
    pipe.Init(url, output, 0);
    pipe.Start();
    int cnt_fail = 0;
    while (b_continue)
    {
        cv::Mat src = pipe.GetFrame();
        if (src.empty())
        {
            printf("GetFrame failed\n");
            cnt_fail++;
            if (cnt_fail > 10)
            {
                printf("GetFrame failed 10 times, exit\n");
                b_continue = false;
                break;
            }
            continue;
        }
        cnt_fail = 0;

        ax_det_img_t img;
        img.data = src.data;
        img.width = src.cols;
        img.height = src.rows;
        img.channels = src.channels();
        img.stride = src.step;
        ax_det_result_t result;
        memset(&result, 0, sizeof(result));
        ret = ax_det(handle, &img, &result);
        if (ret != ax_det_errcode_success)
        {
            printf("ax_det failed\n");
            return -1;
        }
        printf("num_objs: %d\n", result.num_objs);
        if (result.num_objs > 0)
            pipe.PushDetResult(result);
        // for (int i = 0; i < result.num_objs; i++)
        // {
        //     ax_det_obj_t &obj = result.objects[i];
        //     cv::Rect rect(obj.box.x, obj.box.y, obj.box.w, obj.box.h);
        //     cv::rectangle(src, rect, cv::Scalar(0, 255, 0), 2);
        //     char label_info[128];
        //     sprintf(label_info, "%d %5.2f", obj.label, obj.score);
        //     cv::putText(src, label_info, cv::Point(obj.box.x, obj.box.y + 25), cv::FONT_HERSHEY_SIMPLEX, 1.5, cv::Scalar(0, 255, 0), 2);

        //     for (int j = 0; j < obj.num_kpt; j++)
        //     {
        //         cv::circle(src, cv::Point(obj.kpts[j].x, obj.kpts[j].y), 10, cv::Scalar(0, 255, 0), -1);
        //     }
        // }

        // char path[128];
        // sprintf(path, "output_%d.jpg", i);
        // cv::imwrite(path, src);

        usleep(1000);
    }
    pipe.Deinit();

    ax_det_deinit(handle);

    if (ax_devices.host.available)
    {
        ax_dev_sys_deinit(host_device, -1);
    }
    else if (ax_devices.devices.count > 0)
    {
        ax_dev_sys_deinit(axcl_device, 0);
    }
    return 0;
}
