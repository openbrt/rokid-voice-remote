# Architecture

The runtime is split into small services for recognition, Bluetooth HID, and
configuration. It does not patch factory libraries or redistribute vendor
runtime files.

```text
microphone array
      |
factory BlackSiren + custom AWAKE words
      |
rklua log: trigger phrase + confirmed AWAKE event
      |
voice-listener.sh -- exact TSV lookup
      |
Unix socket (/run/rokid-voice-remote/hidd.sock)
      |
voice_remote_hid -- factory Broadcom BSA API
      |
Bluetooth Classic HID (keyboard / consumer control)
      |
TV, projector, or a central receiver
```

配置面是第三个独立服务，可由在线安装页经 USB 或由可信局域网直接访问：

```text
GitHub Pages installer              browser on trusted LAN
      | WebUSB ADB tcp:8090                  |
      +----------------------+---------------+
                             |
                  HTTP :8090 + X-Config-Token
      |
voice_remote_config
      | validate all rows, ranges and duplicates
atomic replacement of commands.tsv + targets.conf
      |
restart voice listener
```

首次 Wi-Fi 配置不经过 LAN HTTP。在线安装页通过 WebUSB ADB 调用设备现有的
`wpa_cli` 控制面；SSID 以 UTF-8 十六进制传递，WPA/WPA2 密码在浏览器内用
PBKDF2-SHA1 派生为 256 位 PSK 后再发送。页面不会把明文密码写入设备命令行或
安装日志，也不直接编辑 `wpa_supplicant.conf`。

静态页面不依赖 CDN。配置令牌由安装器从 `/dev/urandom` 生成，权限为 `0600`，
通过 URL fragment 导入浏览器的 session storage；fragment 不会随 HTTP 请求发送。
API 仍要求 `X-Config-Token`，并限制头部、正文和配置大小，拒绝 chunked 请求、
路径遍历、控制字符、重复短语、未知目标以及越界 HID usage。

## Why the log bridge exists

The factory Lua callback reports that an AWAKE event occurred but omits the
matched phrase. The same unmodified native module emits the exact phrase in its
`trigger: ..., start:` diagnostic line immediately before the event. The
supervisor remembers that phrase and dispatches it only after the Lua callback
prints `VOICE_REMOTE_AWAKE`. A trigger log alone never sends a key.

This keeps the project source-only and avoids distributing or patching Rokid's
native Lua module.

## HID reports

The factory BSA server exposes one HID Device instance with:

- report ID 1: keyboard;
- report ID 2: mouse;
- report ID 3: two 16-bit Consumer Page usages.

Consumer press is five bytes: report ID `03`, little-endian usage, and a zero
second slot. Release is five zero-usage bytes with report ID `03`. Keyboard
keys use BSA's regular-key request with auto-release.

The Bluetooth stack has a single active HID host. Named targets therefore
select and connect one bonded host before sending. Switching from a TV to a
projector is not instantaneous.

## Trust boundaries

Phrase lookup is exact. TSV values are never sourced or evaluated. Target
names, addresses, report types, usages, key codes, modifiers, and repeat counts
are validated before reaching the Unix socket and again inside the daemon.
The configuration page performs equivalent server-side validation before it
can replace either TSV file; browser-side checks are only a usability layer.

The WebUSB configurator also has a one-shot remote learning path. Standard
keyboard events cover ordinary navigation keys, while an explicitly granted
WebHID device can expose unblocked Consumer Control input reports. The browser
decodes only Keyboard Page (`0x07`) and Consumer Page (`0x0c`) usages accepted
by the firmware; vendor-defined usages are never guessed or persisted. The
captured report stays in browser memory and is reduced to one usage before the
normal configuration validation path.
