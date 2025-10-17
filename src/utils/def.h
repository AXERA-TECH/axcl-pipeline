/**************************************************************************************************
 *
 * Copyright (c) 2019-2024 Axera Semiconductor Co., Ltd. All Rights Reserved.
 *
 * This source file is the property of Axera Semiconductor Co., Ltd. and
 * may not be copied or distributed in any isomorphic form without the prior
 * written consent of Axera Semiconductor Co., Ltd.
 *
 **************************************************************************************************/

#ifndef __SAMPLE_DEF_H__
#define __SAMPLE_DEF_H__

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

typedef enum
{
    h264_ax = 0,
    hevc_ax = 1,
    auto_ax = 2, // let ffmpeg pick by codec id
} AXFFmpegCodecID;

#endif /* __SAMPLE_DEF_H__ */