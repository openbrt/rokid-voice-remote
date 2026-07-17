# rokid-voice-remote

把 Amlogic A113 Rokid 音箱变成一只完全离线的蓝牙语音遥控器。它独立于
`rokid-chatgpt`：不依赖、不修改，也不会接管后者的任何文件。

> 当前版本是硬件验证预览版。源码和 A113 交叉编译已纳入发布流程；在你的
> 电视/投影型号上完成配对和端到端验证前，不应视为稳定版。

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

## 先看两个限制

- HID 的 `Power (0x0030)` 是开关切换键，不区分“开”和“关”。默认四条电源
  命令因此都映射到 Power。要保证“打开”不会误关，必须增加状态反馈，再用
  HDMI-CEC、红外、局域网或厂商协议执行离散控制。
- 原厂 BSA HID Device 同一时刻只连接一个主机。电视和投影可以分别绑定，
  但切换目标需要断开/重连。更稳妥的方案是只配对一个常在线中控，由中控转发
  CEC/IR/LAN 命令。

## 固件配置页

USB 安装完成后会优先建立 ADB 端口转发，并在 macOS 上自动打开：

```text
http://127.0.0.1:<ADB分配端口>/#<配置令牌>
```

拔掉 USB 后也可通过 `http://<音箱局域网 IP>:8090/#<配置令牌>` 访问。

配置页可以直接：

- 添加、删除最多 5 条唤醒短语/语音指令；
- 编辑带声调数字的拼音；
- 从 BSA 已配对设备中选择电视、投影或中控；
- 为每条语音选择 Power、音量、静音、播放暂停、主页、返回、方向键等预置；
- 切换 `consumer`/`key` 类型并填写原始 HID 键值；
- 服务端校验整套映射，保存后自动重启离线识别。

页面和 API 都运行在音箱固件内，不依赖云服务。配置 API 必须携带安装时生成的
48 位随机令牌；令牌通过 URL fragment 交给页面，不会作为 HTTP 请求路径发送。

底层命令表仍是 UTF-8 TSV，每行六列，适合版本管理和故障恢复：

```text
短语    带声调数字的拼音    目标    类型    按键值    重复次数
打开电视    da3kai1dian4shi4    television    consumer    0x0030    1
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

`targets.conf` 把逻辑名映射到完成蓝牙配对后的 BD_ADDR。地址保持全零时，相关
命令会安全失败，不会误发给当前连接。

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

## 安装与配对

连接音箱 ADB 后：

```sh
./tools/usb-install.sh
```

安装器会校验发布清单、CPU 架构和已知工厂库版本，记录原始 systemd 状态，
停用冲突的 Rokid 应用服务，并启用 HID、离线识别和配置页面三个独立服务。
若检测到 `/data/rokid-chatgpt`，默认会拒绝继续。明确替换时使用：

```sh
./tools/usb-install.sh --serial <Rokid序列号> --replace
```

工具会先把冲突方案保存到本机 `backups/`，再调用它自己的卸载脚本恢复原始
systemd 状态，之后安装本项目。使用 `--no-open` 可禁止自动打开浏览器。

接着在电视/投影蓝牙页面搜索 **Rokid Voice Remote** 并配对，把已绑定设备的
配对后回到配置页选择目标设备。也可用下面第一条命令列出 BSA 数据库中已经
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
