# rokid-voice-remote

把 Amlogic A113 Rokid 音箱变成一只完全离线的蓝牙语音遥控器。它独立于
`rokid-chatgpt`：不依赖、不修改，也不会接管后者的任何文件。

> 当前版本是硬件验证预览版。源码和 A113 交叉编译已纳入发布流程；在你的
> 电视/投影型号上完成配对和端到端验证前，不应视为稳定版。

## 能定义多少条语音命令？

原厂产品界面只允许 **1 个**自定义唤醒词；底层 BlackSiren 词表则是动态
分配，检查到的匹配实现没有写死数量上限。实际容量受内存、计算量和误唤醒率
约束，所以本项目主动限制为 **32 条命令**。32 是保守工程阈值，不是声称
BlackSiren 芯片或算法的极限。

默认带有：打开/关掉投影、打开/关掉电视、电视音量、静音、播放暂停和主页。
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

## 配置

命令表是 UTF-8 TSV，每行六列：

```text
短语    带声调数字的拼音    目标    类型    按键值    重复次数
打开电视    da3kai1dian4shi4    television    consumer    0x0030    1
```

支持两种类型：

- `consumer`：16 位 HID Consumer Page usage，例如 `0x00e9` 音量加；
- `key`：8 位键盘 usage；此时第六列仍为重复次数，修饰键默认为 0。

设备安装后编辑：

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
停用冲突的 Rokid 应用服务，并启用两个独立服务。若检测到
`/data/rokid-chatgpt`，安装会拒绝继续，绝不会替你删除或改动它。

接着在电视/投影蓝牙页面搜索 **Rokid Voice Remote** 并配对，把已绑定设备的
BD_ADDR 写进 `targets.conf`。可用下面第一条命令列出 BSA 数据库中已经建立
Classic 链路密钥的设备（不会输出密钥本身）：

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
