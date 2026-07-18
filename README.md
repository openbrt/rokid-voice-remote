# rokid-voice-remote

把 Amlogic A113 Rokid 音箱变成一只完全离线的蓝牙语音遥控器。语音识别在
音箱本地完成，电视、投影或中控把它识别成蓝牙键盘/媒体遥控器。

> 当前版本是硬件验证预览版。源码和 A113 交叉编译已纳入发布流程；在你的
> 电视/投影型号上完成配对和端到端验证前，不应视为稳定版。

## 在线安装

使用桌面版 Chrome 或 Edge 打开：

<https://openbrt.github.io/rokid-voice-remote/>

在线安装器通过 WebUSB 直接建立 ADB 会话，不上传设备数据。它会先核对 A113
架构和出厂运行时摘要，再下载发布固件，并在浏览器和音箱两端分别校验
SHA-256 后安装。安装的是可卸载的应用层固件，不是 NAND/分区镜像。

音箱需正常开机并启用 USB ADB。如果本机 `adb` 已占用 USB 接口，先运行：

```sh
adb kill-server
```

安装成功后无需跳转，也不要求音箱预先联网：保持 USB 连接即可在同一页面
编辑语音指令，并用带到电脑边的原遥控器学习 HID 键值。Wi-Fi 仅用于
可选的后续维护。若检测到已有
本项目，页面会直接读取现有配置；若检测到其他占用相同原厂服务的方案，则会
停止，不会在没有本地备份时静默覆盖。

## 能定义多少条语音命令？

原厂产品界面只允许 **1 个**自定义唤醒词；底层 BlackSiren 词表虽然动态
分配、没有写死数量上限，但 A113 实机只有约 224 MB 内存。硬件分档测试中，
7 条会触发 OOM，6 条只剩约 26 MB 可用内存。因此本项目默认安装 **4 条**，
网页和服务端最多允许 **5 条**，为蓝牙配对与配置保存保留约 47 MB 余量。

默认带有：打开/关掉投影、打开/关掉电视。
所有识别都在音箱本地完成，不需要云端 ASR。

## 工作方式

1. 原厂 BlackSiren 把 `commands.tsv` 中的短语注册为 AWAKE 词；
2. 只在“短语日志 + AWAKE 确认事件”成对出现时精确匹配命令；
3. C 守护进程通过原厂 Broadcom BSA 发 Bluetooth Classic HID 报告；
4. 电视、投影或中控盒把它当作蓝牙键盘/媒体遥控器。

音箱是被连接的 Bluetooth HID 外设。学习好映射后，把音箱移到受控
设备旁，在电视/投影自己的屏幕上搜索 **Rokid Voice Remote** 并配对。
默认指令使用 `active` 目标，直接发给当前已连接的主机；配对后无需
让电脑和受控设备在同一处，也无需再通过局域网访问音箱。
音箱会记住上次成功连接主机的公开蓝牙地址，整机重启后主动回连；如果对方
不可用，则退回可发现的配对监听状态。链路密钥仍只由原厂 BSA 数据库保管。

## 先看两个限制

- HID 的 `Power (0x0030)` 是开关切换键，不区分“开”和“关”。默认四条电源
  命令因此都映射到 Power。要保证“打开”不会误关，必须增加状态反馈，再用
  HDMI-CEC、红外、局域网或厂商协议执行离散控制。
- 原厂 BSA HID Device 同一时刻只连接一个主机。默认是将所有指令发给当前
  连接的设备。高级命名目标可切换已配对主机，但需要断开/重连。更稳妥的
  方案是只配对一个常在线中控，由中控转发
  CEC/IR/LAN 命令。

## 配置方式

推荐始终从在线安装页通过 USB 配置。WebUSB ADB 会直接连接音箱本机的配置
API，因此音箱没有联网也能保存语音指令和遥控键值。Wi-Fi 密码只在浏览器本地
用于派生 WPA PSK，不上传，也不会写入安装日志或命令行。

固件内仍保留一份配置页面，用途是安装完成、拔掉 USB 后在可信局域网内进行
可选的日常维护。它不是首次安装、遥控学习或与电视/投影配对的前置条件。

使用命令行安装时会优先建立 ADB 端口转发，并在 macOS 上自动打开内置页：

```text
http://127.0.0.1:<ADB分配端口>/#<配置令牌>
```

拔掉 USB 后也可通过 `http://<音箱局域网 IP>:8090/#<配置令牌>` 访问。

配置页可以直接：

- 添加、删除最多 5 条唤醒短语/语音指令；
- 编辑带声调数字的拼音；
- 在电脑旁用原遥控器进行按键学习：输入语音指令、按一次原遥控器按键，页面自动
  生成拼音并加入标准 Keyboard/Consumer HID 映射；
- 默认把指令发给当前已连接的电视、投影或中控；
- 可选地为高级多主机切换配置命名目标；
- 为每条语音选择 Power、音量、静音、播放暂停、主页、返回、方向键等预置；
- 切换 `consumer`/`key` 类型并填写原始 HID 键值；
- 服务端校验整套映射，保存后自动重启离线识别。

按键学习支持系统能识别的蓝牙/USB HID 遥控器。普通方向键、确认键可直接通过
浏览器键盘事件识别；音量、电源、播放等 Consumer 键可在 Chrome/Edge 中授权
WebHID 后读取。纯红外遥控器需要 USB 红外接收器，厂商私有协议仍需手工映射或
专用接收器。原遥控器的输入报告只在当前页面解析，不会上传。

页面和 API 都运行在音箱固件内，不依赖云服务。配置 API 必须携带安装时生成的
48 位随机令牌；令牌通过 URL fragment 交给页面，不会作为 HTTP 请求路径发送。

底层命令表仍是 UTF-8 TSV，每行六列，适合版本管理和故障恢复：

```text
短语    带声调数字的拼音    目标    类型    按键值    重复次数
打开电视    da3kai1dian4shi4    active    consumer    0x0030    1
```

支持两种类型：

- `consumer`：16 位 HID Consumer Page usage，例如 `0x00e9` 音量加；
- `key`：8 位键盘 usage；此时第六列仍为重复次数，修饰键默认为 0。

需要脱离页面维护时仍可编辑：

```sh
vi /data/rokid-voice-remote/config/commands.tsv
vi /data/rokid-voice-remote/config/targets.conf
systemctl restart rokid-voice-remote-voice.service
```

普通用法不需要编辑 `targets.conf`。`active` 会直接使用当前 HID 连接。只有高级
多主机切换才需要把逻辑名映射到已配对主机的 BD_ADDR；全零占位地址会安全失败。

## 构建

默认使用 SSH 别名 `100` 上的匹配 Rokid SDK，并把远端源码树视为只读：

```sh
./scripts/build.sh
```

可选环境变量：

```sh
ROKID_BUILD_HOST=100 \
ROKID_SDK_ROOT=/home/csc/rokid_src/home/csc/rokid_src \
./scripts/build.sh
```

脚本只把本项目的一个 C 文件复制到远端 `/tmp` 编译，随后取回 AArch64
二进制并删除临时目录。仓库不包含 Broadcom 头文件或 Rokid 私有源码。

产物：`dist/rokid-voice-remote-<version>.tar.gz`。

## 命令行安装与配对

连接音箱 ADB 后：

```sh
./tools/usb-install.sh
```

安装器会校验发布清单、CPU 架构和已知工厂库版本，记录原始 systemd 状态，
停用冲突的 Rokid 应用服务，并启用 HID、离线识别和配置页面三个独立服务。
若检测到其他占用同一套服务的社区方案，默认会拒绝继续。明确迁移时使用：

```sh
./tools/usb-install.sh --serial <Rokid序列号> --replace
```

工具会先把冲突方案保存到本机 `backups/`，校验其可恢复状态，再调用原方案的
卸载脚本恢复 systemd 状态，之后安装本项目。使用 `--no-open` 可禁止自动
打开浏览器。

先在电脑边完成按键学习并保存，再把音箱移到受控设备旁。在电视/投影蓝牙
页面搜索 **Rokid Voice Remote** 并配对。音箱是无屏 HID 外设，使用 SSP
Just Works 完成认证；配对成功后可直接语音遥控，无需回到配置页。也可用下面
第一条命令列出 BSA 数据库中已经
建立 Classic 链路密钥的设备（不会输出密钥本身）：

```sh
/data/rokid-voice-remote/bin/paired-devices.sh
/data/rokid-voice-remote/bin/doctor.sh
/data/rokid-voice-remote/bin/voice_remote_hid ctl status
journalctl -u rokid-voice-remote-hid -u rokid-voice-remote-voice -f
```

卸载并恢复安装前的服务状态：

```sh
./tools/usb-uninstall.sh
```

更深入的设计和验证依据见 [架构](docs/ARCHITECTURE.md) 与
[研究记录](docs/RESEARCH.md)。第三方边界见 [THIRD_PARTY.md](THIRD_PARTY.md)。
