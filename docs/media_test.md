# 本地 RTSP、录像与延迟测试

本指南使用 `E:\Qt\sdk` 中的 MediaMTX、FFmpeg 和 `dance.mp4` 搭建开发测试源。正式验收仍以 [`acceptance-checklist.md`](acceptance-checklist.md) 为准。

## 启动 MediaMTX

在独立 PowerShell 窗口运行：

```powershell
Set-Location E:\Qt\sdk\mediamtx
.\mediamtx.exe
```

MediaMTX 默认开放：

```text
RTSP: :8554
RTMP: :1935
```

## 发布循环视频

在第二个 PowerShell 窗口使用与其 shared DLL 匹配的 FFmpeg CLI：

```powershell
& E:\Qt\sdk\ffmpeg\ffmpeg.exe `
    -hide_banner -re -stream_loop -1 `
    -i E:\Qt\sdk\dance.mp4 `
    -c:v copy -an `
    -f rtsp -rtsp_transport tcp `
    rtsp://127.0.0.1:8554/live/stream
```

若 `ffmpeg.exe -version` 以 `0xC0000135` 退出，说明可执行文件加载到了不匹配的 shared DLL。不要改动项目依赖目录；可将 CLI 与同一 SDK 包的 `bin/*.dll` 放到临时目录后运行：

```powershell
$runtime = Join-Path $env:TEMP utms-ffmpeg-cli
New-Item -ItemType Directory -Force $runtime | Out-Null
Copy-Item E:\Qt\sdk\ffmpeg\ffmpeg.exe $runtime -Force
Copy-Item E:\Qt\sdk\ffmpeg\bin\*.dll $runtime -Force

& "$runtime\ffmpeg.exe" `
    -hide_banner -re -stream_loop -1 `
    -i E:\Qt\sdk\dance.mp4 `
    -c:v copy -an `
    -f rtsp -rtsp_transport tcp `
    rtsp://127.0.0.1:8554/live/stream
```

也可使用 OBS 发布 RTMP，由 MediaMTX 转为 RTSP：

```text
自定义服务器：rtmp://127.0.0.1:1935/live/
推流码：stream
```

## UTMS 手工测试

登录后在“视频流”Tab 连接：

```text
rtsp://127.0.0.1:8554/live/stream
```

依次验证预览、检测、开始/停止录像、打开录像目录。录像应保存在 `UTMS.exe` 旁的 `recordings/`，并且只包含 H.264/H.265 视频轨。中断发布端后，UTMS 应安全封尾当前录像并进入重连；恢复发布后预览和原检测开关应恢复，但录像不得自动恢复。

## 自动录像验收工具

使用 `PROJECT_BUILD_TESTS=ON` 完成全量开发构建后运行：

```powershell
.\build\acceptance\UTMS_rtsp_recording_acceptance.exe `
    rtsp://127.0.0.1:8554/live/stream
```

工具连接后自动录制约 4 秒，并通过 FFmpeg 重新打开文件、检查 H.264/H.265 视频轨和可读数据包。成功条件为退出码 `0`，且输出包含：

```text
RECORDING_VERIFIED=1
```

若要验证断流封尾与重连不恢复录像，使用 `--expect-reconnect` 启动工具，并在录像期间停止后再恢复发布端：

```powershell
.\build\acceptance\UTMS_rtsp_recording_acceptance.exe `
    rtsp://127.0.0.1:8554/live/stream `
    --expect-reconnect
```

成功时还应输出 `RECONNECT_RECORDING_STATE=0`。

## 端到端延迟

端到端延迟需要使用画面内带毫秒时间戳且与参考 PC 同步的 RTSP 测试源。可使用[北京时间](https://www.bjtime.net/)作为人工参考，但正式记录方法、样本数和 500 ms 阈值以人工验收清单为准。
