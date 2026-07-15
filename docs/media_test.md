时间延迟测试：https://www.bjtime.net/

Qt拉流：

```
rtsp://127.0.0.1:8554/live/stream
```

推流OBS:

```
自定义服务器：rtmp://127.0.0.1:1935/live/
推流码：stream
```

服务器mediamtx（可以将各stream转换成rtsp）：

```
E:\Qt\sdk\mediamtx>E:\Qt\sdk\mediamtx\mediamtx.exe
```

MediaMTX 默认启用 RTSP 和 RTMP：

```
rtspAddress: :8554
rtmpAddress: :1935
```