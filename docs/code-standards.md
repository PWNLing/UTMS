# Qt/C++ Coding Standards

## 基本原则

- 保持整个项目风格统一。
- 优先保证代码清晰、简单、可维护。
- 一个类只负责一类功能。
- 一个函数尽量只做一件事。
- 修改代码时优先遵循项目已有风格。

## 命名规范

### 2.1 类、结构体、枚举类型

使用大驼峰命名：

```C++
class TcpServer;
class DeviceManager;

struct FrameInfo;

enum class ConnectionState;
```

名称应表达明确职责，避免使用含糊名称：

```C++
// 不推荐
class Manager;
class Helper;
class Utils;

// 推荐
class DeviceManager;
class FileParser;
class NetworkClient;
```

### 2.2 函数

使用小驼峰命名，通常以动词开头：

```C++
void startServer();
void stopServer();
void readData();
void processFrame();
```

布尔函数使用明确的判断语义：

```C++
bool isRunning() const;
bool hasError() const;
bool canWrite() const;
```

避免过于含糊的名称：

```C++
// 不推荐
void doWork();
void handle();
void process();

// 推荐
void handleSocketError();
void processIncomingFrame();
```

Qt 属性访问函数不使用 `get` 前缀：

```C++
QString name() const;
void setName(const QString& name);
```

### 2.3 普通变量

使用小写字母加下划线：

```C++
int retry_count;
QString device_name;
QByteArray received_data;
```

变量名应体现含义，避免：

```C++
int a;
QString str;
QByteArray tmp;
```

短循环变量可以使用：

```C++
for (int i = 0; i < count; ++i) {
}
```

### 2.4 成员变量

使用小写字母加下划线，并以 `_` 结尾：

```C++
class DeviceManager {
private:
    QString device_name_;
    int timeout_ms_ = 0;
    bool running_ = false;
};
```

禁止混用：

```C++
int timeout_;
int m_timeout;
int timeout;
```

### 2.5 常量

使用 `k` 加大驼峰：

```C++
constexpr int kMaxBufferSize = 4096;
constexpr int kDefaultTimeoutMs = 3000;
```

不要使用宏定义普通常量：

```C++
// 不推荐
#define MAX_BUFFER_SIZE 4096
```

### 2.6 枚举值

使用 `enum class`，枚举值使用 `k` 加大驼峰：

```C++
enum class DeviceState {
    kDisconnected,
    kConnecting,
    kConnected
};
```

### 2.7 信号和槽

信号表示已经发生的事件：

```C++
signals:
    void connected();
    void disconnected();
    void dataReceived(const QByteArray& data);
    void errorOccurred(const QString& message);
```

槽函数表示处理动作：

```C++
private slots:
    void handleStartButtonClicked();
    void handleSocketReadyRead();
    void handleConnectionTimeout();
```

### 2.8 文件命名

文件名与核心类名保持一致：

```Plain
TcpServer.h
TcpServer.cpp

DeviceManager.h
DeviceManager.cpp
```

### 2.9 控件命名

Qt Designer 控件名称使用“用途 + 类型”：

```Plain
startButton
stopButton
statusLabel
deviceComboBox
logPlainTextEdit
```

禁止保留默认名称：

```Plain
pushButton
pushButton_2
label_3
widget
```

### 2.10 单位命名

时间、长度、频率等变量必须包含单位：

```C++
int timeout_ms_;
qint64 timestamp_us_;
double frequency_hz_;
int distance_mm_;
```

1. ## 头文件规范

使用：

```C++
#pragma once
```

禁止在头文件中写：

```C++
using namespace std;
```

头文件主要放声明，复杂实现放 `.cpp`。

能够前置声明时，优先前置声明：

```C++
class QTimer;
class QTcpSocket;
```

1. ## include 顺序

```C++
#include "TcpServer.h"

#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include <QDebug>
#include <QTcpSocket>

#include "Logger.h"
```

顺序：

1. 当前文件对应头文件
2. C++ 标准库
3. C 或系统头文件
4. Qt 头文件
5. 项目内部头文件

1. ## 格式规范

- 使用 4 个空格缩进。
- 不使用 Tab。
- 运算符两侧加空格。
- 逗号后加空格。
- 每行建议不超过 120 个字符。
- 使用 `.clang-format` 自动格式化。

```C++
if (count > 0) {
    total += count;
}
```

1. ## 类设计规范

成员变量默认设为 `private`。

```C++
class Device {
public:
    explicit Device(const QString& path);

    bool start();
    void stop();
    bool isRunning() const;

private:
    QString path_;
    bool running_ = false;
};
```

单参数构造函数默认使用 `explicit`：

```C++
explicit Device(const QString& path);
```

重写虚函数必须使用 `override`：

```C++
void closeEvent(QCloseEvent* event) override;
```

优先使用组合，避免无意义继承。

1. ## 资源管理

优先使用 RAII。

普通 C++ 对象优先使用栈对象或智能指针：

```C++
Device device;

auto device = std::make_unique<Device>();
```

避免手动：

```C++
Device* device = new Device;
delete device;
```

`QObject` 对象优先使用父子对象机制：

```C++
timer_ = new QTimer(this);
socket_ = new QTcpSocket(this);
```

设置了父对象的 `QObject` 不要再使用智能指针管理。

1. ## const 和参数传递

能使用 `const` 就使用：

```C++
bool isRunning() const;
QString name() const;
```

小类型按值传递：

```C++
void setPort(quint16 port);
void setEnabled(bool enabled);
```

较大对象使用常量引用：

```C++
void setName(const QString& name);
void processData(const QByteArray& data);
```

必须存在的参数使用引用，允许为空的参数使用指针：

```C++
void printDevice(const Device& device);
void setParentDevice(Device* parent);
```

使用 `nullptr`，不要使用 `NULL` 或 `0`。

1. ## Qt 专项规范

使用新式信号槽语法：

```C++
connect(button_, &QPushButton::clicked,
        this, &MainWindow::handleStartButtonClicked);
```

lambda 连接必须提供上下文对象：

```C++
connect(socket_, &QTcpSocket::readyRead, this, [this]() {
    readSocketData();
});
```

用户可见文字使用：

```C++
tr("Connection failed")
```

Qt 固定字符串可以使用：

```C++
QStringLiteral("Connected")
```

界面类只负责界面交互，不要把设备、网络、协议解析等全部写入 `MainWindow`。

1. ## 线程规范

- GUI 线程中禁止执行耗时操作。
- 工作线程禁止直接修改界面控件。
- 跨线程通信优先使用信号槽。
- 不使用 `QThread::terminate()` 强制结束线程。
- 周期任务优先使用 `QTimer`，不要使用死循环加 `sleep`。
- 使用 RAII 加锁。

```C++
QMutexLocker locker(&mutex_);
```

1. ## 错误处理

所有可能失败的操作都必须检查结果：

```C++
QFile file(config_path);

if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Failed to open file:"
               << config_path
               << file.errorString();
    return false;
}
```

Linux 系统调用必须检查返回值和 `errno`。

错误信息应包含足够上下文，不能只写：

```C++
qWarning() << "Failed";
```

1. ## 日志规范

Qt 项目使用：

```C++
qDebug();
qInfo();
qWarning();
qCritical();
```

日志应包含模块、对象、路径或错误信息。

禁止在高频循环、视频帧或网络回调中大量刷日志。

1. ## 注释规范

注释解释“为什么”，不要重复代码。

不推荐：

```C++
++count;  // count 加 1
```

推荐：

```C++
// 关闭写事件监听，避免套接字持续可写时产生无效唤醒。
channel_->disableWriting();
```

复杂函数应说明：

- 作用
- 输入输出
- 线程要求
- 资源所有权
- 错误条件

1. ## 禁止事项

禁止生成以下代码：

- 头文件中使用 `using namespace`
- 使用无意义变量名
- 普通对象使用裸 `new/delete`
- 忽略函数返回值
- 使用 C 风格强制转换
- 使用宏代替 `constexpr`
- 在 GUI 线程中执行阻塞操作
- 工作线程直接操作界面
- 将所有逻辑写入 `MainWindow`
- 使用旧式 `SIGNAL/SLOT` 语法
- 使用未初始化成员变量
- 捕获异常后不处理
- 高频路径大量输出日志

1. ## AI 编码要求

AI 生成或修改代码时必须：

1. 优先保持现有项目命名和目录风格。
2. 不随意修改已有公共接口。
3. 新增成员变量必须初始化。
4. 新增资源必须明确所有权和释放方式。
5. 新增系统调用必须检查返回值。
6. 新增线程必须提供安全退出逻辑。
7. 不在 GUI 线程加入阻塞操作。
8. 代码必须完整，不省略关键实现。
9. 复杂逻辑添加简洁中文注释。
10. 输出代码应符合 `clang-format`。

1. ## 核心规则速记

```Plain
类名：大驼峰
函数名：小驼峰
变量名：小写加下划线
成员变量：小写加下划线并以 _ 结尾
常量：k 加大驼峰
枚举：enum class
枚举值：k 加大驼峰
控件名：用途加控件类型
时间和长度变量必须带单位
头文件禁止 using namespace
普通对象优先栈对象或 unique_ptr
QObject 优先父子对象管理
能加 const 就加 const
单参数构造函数使用 explicit
重写函数使用 override
信号槽使用新式语法
GUI 线程禁止阻塞
所有错误必须处理
注释解释为什么
格式交给 clang-format
```