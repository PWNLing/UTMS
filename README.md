# GUET-UTMS 雷达显控平台

UTMS 是面向 Windows 的 Qt Widgets 雷达显控应用。第一阶段接收 UDP JSON 当前帧快照，并同步显示雷达位置、目标地图标记、航迹表格、类别统计和运行状态。产品行为以 [`docs/PRD.md`](docs/PRD.md) 为准。

## 开发环境

正式验收环境：

- Windows 10/11 64 位；
- Qt 6.8 LTS，MSVC 2022 64 位套件；
- CMake 3.16 或更高版本；
- 支持 C++17 的 MSVC 2022；
- Python 3.9 或更高版本（仅模拟器需要）。

需要安装以下 Qt 模块：

- Core、Gui、Widgets；
- Network、Concurrent；
- Charts；
- WebEngineWidgets、WebChannel；
- Test（启用自动化测试时需要）。

第一阶段不需要 FFmpeg、ONNX Runtime、YOLO 或其他视频依赖。

## 构建与测试

在 “x64 Native Tools Command Prompt for VS 2022” 或已加载 MSVC 2022 环境的 PowerShell 中执行。将 `C:\Qt\6.8.3\msvc2022_64` 替换为本机 Qt 路径：

```powershell
cmake -S . -B build -G Ninja `
    -DCMAKE_BUILD_TYPE=Debug `
    -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 `
    -DPROJECT_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

若使用 Visual Studio 多配置生成器，构建和测试命令应带配置名：

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

`PROJECT_BUILD_TESTS` 默认关闭；只有配置为 `ON` 时才会生成 Qt Test 目标。测试覆盖 PRD 规定的 JSON 解析与容错、坐标校验、重复目标、类型映射、可选测量值、序号策略、当前帧统计、表格排序和筛选等核心规则。

## 从构建目录运行

启动 `UTMS.exe`，在“系统配置”页保留默认端口 `10000` 并点击“启动监听”。UDP 状态含义：

- 红色：监听未启动或绑定失败；
- 黄色：已监听，但尚无最近 3 秒内被接受的合法数据；
- 绿色：最近 3 秒内收到过被接受的合法数据。

程序不会自动启动 UDP 监听，也不会持久化端口、地图或窗口配置。

## 在线高德地图

复制配置示例并填写高德 Web JavaScript API Key 与安全密钥：

```powershell
Copy-Item config\amap.example.json config\amap.json
```

```json
{
    "key": "你的高德 Web 端 Key",
    "securityCode": "你的安全密钥"
}
```

真实 `config/amap.json` 已被 Git 忽略，不得提交。Key 可在[高德开放平台控制台](https://console.amap.com/dev/key/app)创建和管理。

应用从可执行文件旁的 `config/amap.json` 读取配置。配置缺失、JSON 无效或字段为空时，在线地图显示明确错误且不会自动切换离线地图。默认中心为经度 `110.416819`、纬度 `25.311724`，默认缩放级别为 `17`，允许范围为 `15` 至 `19`。

## 离线地图瓦片

将 GCJ-02 高德街道瓦片放到可执行文件旁的固定相对路径：

```text
data/map/amap/{z}/{x}/{y}.png
```

离线模式支持缩放级别 `15` 至 `19`，只支持街道图。瓦片缺失时显示灰色占位内容，地图仍可平移和缩放且不会连续弹窗。实际瓦片不提交到仓库；绿色发布目录的 `bin/data/map/amap/README.md` 也包含放置说明。

## UDP 模拟器

模拟器默认向 `127.0.0.1:10000` 以约 `11 FPS` 发送每帧 `10` 至 `20` 个目标。每个报文都是完整 `tracks` 快照，序号逐帧递增，包含合法雷达位置；默认目标池确定性覆盖汽车、卡车、行人、自行车和未知五类。

```powershell
python scripts\udp_simulator.py
```

可修改目标 IP、端口、目标数量或范围以及发送频率：

```powershell
python scripts\udp_simulator.py `
    --host 127.0.0.1 `
    --port 10000 `
    --targets 12-18 `
    --fps 11
```

`--targets` 接受单个数量（例如 `15`）或闭区间（例如 `10-20`）。按 `Ctrl+C` 停止模拟器。

## 运行日志

日志位于可执行文件旁的 `logs/utms.log`。应用记录程序生命周期、UDP 绑定、报文校验、序号拒绝或跳跃、序号基准重置、地图配置和瓦片缺失等事件，但不逐帧记录正常数据。

单个文件最大 10 MB，最多保留当前文件及轮转文件共 5 个。提交故障信息时请保留全部 `utms*.log`。

## Windows 绿色发布

使用 Release 配置生成独立安装前缀。Qt 6 的 CMake 部署 API 会在安装时收集所需的 Qt DLL、插件和 WebEngine 资源，CMake 同时把 MSVC 运行库 DLL 安装到可执行程序旁：

```powershell
cmake -S . -B build-release -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 `
    -DCMAKE_INSTALL_PREFIX=$PWD\dist\UTMS `
    -DPROJECT_BUILD_TESTS=ON
cmake --build build-release
ctest --test-dir build-release --output-on-failure
cmake --install build-release
```

安装后的关键结构：

```text
dist/UTMS/
├── README.md
├── acceptance-checklist.md
├── plugins/、resources/、translations/（Qt 运行资源）
└── bin/
    ├── UTMS.exe
    ├── Qt6*.dll、QtWebEngineProcess.exe、msvcp*.dll 和 vcruntime*.dll
    ├── config/
    │   └── amap.example.json
    ├── data/map/amap/
    │   └── README.md（以及本机已有的 *.png 瓦片）
    └── logs/
        └── README.md
```

发布前在绿色目录中按需将 `bin/config/amap.example.json` 复制为 `bin/config/amap.json` 并填写部署环境自己的 Key。不要把真实 Key 放入公开压缩包。随后在未配置 Qt 开发环境的 Windows 10/11 64 位机器上启动 `bin/UTMS.exe` 进行最终验收。

人工验收步骤和两小时稳定性记录表见 [`docs/acceptance-checklist.md`](docs/acceptance-checklist.md)。

## 常见问题

### CMake 找不到 Qt

确认 `CMAKE_PREFIX_PATH` 指向与编译器匹配的 Qt 目录，例如 `C:\Qt\6.8.3\msvc2022_64`，并确认已安装 Charts、WebEngine 和 WebChannel。

### 程序提示缺少 DLL 或平台插件

不要直接复制单个构建产物作为发布包。重新执行 `cmake --install build-release`，并从完整的安装前缀运行。确认构建 Qt 与目标机器架构均为 64 位。

### 在线地图空白或提示配置错误

检查 `bin/config/amap.json` 是否存在、是否为合法 JSON，以及 `key` 和 `securityCode` 是否非空。再检查 WebEngine 网络访问、防火墙和高德控制台中的 Key 类型与安全配置。

### 离线地图显示“暂无离线地图”

检查目录是否严格为 `bin/data/map/amap/{z}/{x}/{y}.png`，缩放级别是否在 `15` 至 `19`，瓦片是否为高德 GCJ-02 街道瓦片。

### UDP 一直为黄色或绑定失败

先确认已经点击“启动监听”，模拟器 IP/端口与界面一致，且 Windows 防火墙允许 UDP。绑定失败时检查端口是否被其他程序占用；详细原因见 `bin/logs/utms.log`。

### 数据到达但部分目标不显示

目标必须包含可转换的 `track_id`、`position.latitude` 和 `position.longitude`，坐标必须在合法范围内且不能为 `(0, 0)`。单个非法目标会被跳过，原因写入运行日志。

## 第一阶段范围边界

应用只展示单雷达的最新当前帧快照，不保存历史点迹、不绘制轨迹线、不做多雷达融合或坐标转换，也不持久化配置。视频流 Tab 仅为第二阶段提示；第一阶段不包含 RTSP、FFmpeg、YOLO 或 ONNX Runtime。
