# ffmpeg-axcl Pipeline

基于 **AXERA AXCL 加速卡** 的 FFmpeg 推理管线，实现了 **硬件编解码** + **AI 检测（NPU 推理）** 的高性能视频处理流程。
该项目支持从 **RTSP 流** 或 **MP4 文件** 获取视频，解码后通过 NPU 执行目标检测，并输出可直接播放的结果视频流或文件。

---

## ✨ 功能特性

* 🚀 **硬件加速编解码**：使用 AXCL 卡的 FFmpeg 插件进行视频编解码。
* 🧠 **AI 推理检测**：支持基于 AXERA 模型（`.axmodel`）的检测，如 YOLOv8。
* 📡 **RTSP 流转发**：支持输入 RTSP，输出检测后的视频流（RTSP）。
* 📼 **文件处理**：支持从本地 MP4 文件读取并生成带检测框的视频文件。
* ⚙️ **模块化设计**：采用 Pipeline 结构，方便替换或扩展模块。
* 🧩 **OpenCV 支持**：方便调试和结果可视化。

---

## 🧰 环境准备

### 1. 安装依赖

```bash
sudo apt update
sudo apt install -y libopencv-dev build-essential
sudo dpkg -i ~/axcl_host_x86_64_V....deb
```
---

## 🧱 编译步骤

```bash
git clone https://github.com/AXERA-TECH/axcl-pipeline.git
cd axcl-pipeline
git submodule update --init --recursive

mkdir build && cd build
cmake ..
make -j$(nproc)
```

编译成功后，会在 `build/` 目录生成可执行程序，例如：

* `sample_demux_npu_rtsp`

---

## ▶️ 运行示例

### ✅ RTSP 流输入 / 输出

#### 1. 启动 RTSP 服务（使用 `mediamtx`）

```bash
wget https://github.com/bluenviron/mediamtx/releases/download/v1.15.2/mediamtx_v1.15.2_linux_amd64.tar.gz
tar -xzvf mediamtx_v1.15.2_linux_amd64.tar.gz
./mediamtx
```

#### 2. 启动管线推理

```bash
LD_LIBRARY_PATH=/usr/lib/axcl/ffmpeg:$LD_LIBRARY_PATH \
./sample_demux_npu_rtsp \
  -u rtsp://admin:ax123456@10.126.33.100:554/stream \
  -m ~/libdet.axera/build/yolov8s.axmodel \
  -o rtsp://127.0.0.1:8554/axstream0
```

参数说明：

| 参数   | 说明                        |
| ---- | ------------------------- |
| `-u` | 输入 RTSP 流地址               |
| `-m` | 检测模型（AXERA `.axmodel` 文件） |
| `-o` | 输出 RTSP 流地址               |

#### 3. 播放结果

```bash
ffplay rtsp://127.0.0.1:8554/axstream0
```

---

### ✅ MP4 文件输入 / 输出

#### 1. 执行推理

```bash
LD_LIBRARY_PATH=/usr/lib/axcl/ffmpeg:$LD_LIBRARY_PATH \
./sample_demux_npu_rtsp \
  -i ~/test.mp4 \
  -m ~/libdet.axera/build/yolov8s.axmodel \
  -o ~/output.mp4
```

参数说明：

| 参数   | 说明       |
| ---- | -------- |
| `-i` | 输入视频文件路径 |
| `-m` | 检测模型文件路径 |
| `-o` | 输出视频文件路径 |

#### 2. 播放输出结果

```bash
ffplay ~/output.mp4
```

---

## 🤝 社区支持

💬 QQ 群：**139953715**

欢迎加入 AXERA 社区，一起探索更高性能的多媒体 AI 处理方案！
