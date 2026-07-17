import { Adb, AdbDaemonTransport } from "@yume-chan/adb";
import AdbWebCredentialStore from "@yume-chan/adb-credential-web";
import { AdbDaemonWebUsbDeviceManager } from "@yume-chan/adb-daemon-webusb";
import {
  ConcatStringStream,
  ReadableStream,
  TextDecoderStream,
} from "@yume-chan/stream-extra";
import "./style.css";

type Release = {
  version: string;
  archive: string;
  sha256: string;
  size: number;
};

const runtimeFiles = [
  "/usr/lib/libbsa.so",
  "/usr/lua/lib/rokidsiren.so",
] as const;

const runtimeVariants = [
  {
    name: "A113 2019",
    hashes: [
      "6e61a4369b8d758a0e9e060d183e015ed6758d2f8d1c1430f45b03ce322e93ae",
      "a3377d10dd39a973af55740baf3d74dd26069bcd230b94582e7460f1260828af",
    ],
  },
  {
    name: "A113 2020",
    hashes: [
      "6e61a4369b8d758a0e9e060d183e015ed6758d2f8d1c1430f45b03ce322e93ae",
      "3503568af5ebf83457a715c3bf0599636235bbca15a6ba887f69173cd9f08f5f",
    ],
  },
] as const;

const $ = <T extends HTMLElement>(id: string) =>
  document.getElementById(id) as T;

const buttons = {
  connect: $<HTMLButtonElement>("connect"),
  verify: $<HTMLButtonElement>("verify"),
  install: $<HTMLButtonElement>("install"),
};
const consent = $<HTMLInputElement>("consent");
const consoleElement = $<HTMLPreElement>("console");
const statusElement = $<HTMLSpanElement>("status");
const browserPill = $<HTMLDivElement>("browser-pill");
const configLink = $<HTMLAnchorElement>("config-link");

let adb: Adb | undefined;
let verified = false;
let installed = false;
let release: Release | undefined;

function log(message: string) {
  const timestamp = new Date().toLocaleTimeString("zh-CN", { hour12: false });
  consoleElement.textContent += `\n[${timestamp}] ${message}`;
  consoleElement.scrollTop = consoleElement.scrollHeight;
}

function setStatus(message: string) {
  statusElement.textContent = message;
}

function setStep(name: "connect" | "verify" | "install" | "configure") {
  for (const element of document.querySelectorAll<HTMLElement>(".step")) {
    element.classList.remove("active");
  }
  $<HTMLElement>(`step-${name}`).classList.add("active");
}

function setBusy(busy: boolean) {
  for (const button of Object.values(buttons)) {
    button.classList.toggle("busy", busy);
  }
  buttons.connect.disabled = busy || !consent.checked || adb !== undefined;
  buttons.verify.disabled = busy || !consent.checked || adb === undefined || verified;
  buttons.install.disabled = busy || !consent.checked || !verified || installed;
}

async function run(command: string | readonly string[]) {
  if (!adb) throw new Error("ADB 尚未连接");
  // The 2018 Buildroot adbd accepts the original shell service but rejects the
  // newer exec service. State-changing commands use explicit success markers.
  const process = await adb.subprocess.noneProtocol.pty(command);
  const output = await process.output
    .pipeThrough(new TextDecoderStream())
    .pipeThrough(new ConcatStringStream());
  return output.replace(/\r\n/g, "\n");
}

async function digestHex(data: ArrayBuffer) {
  const hash = new Uint8Array(await crypto.subtle.digest("SHA-256", data));
  return Array.from(hash, (value) => value.toString(16).padStart(2, "0")).join("");
}

function oneChunkStream(data: Uint8Array) {
  return new ReadableStream<Uint8Array>({
    start(controller) {
      controller.enqueue(data);
      controller.close();
    },
  });
}

async function loadRelease() {
  const response = await fetch("./firmware/release.json", { cache: "no-store" });
  if (!response.ok) throw new Error("无法读取固件发布信息");
  const value = (await response.json()) as Release;
  if (!/^[0-9a-z.-]+$/.test(value.version)
      || !/^[a-z0-9._-]+$/.test(value.archive)
      || !/^[0-9a-f]{64}$/.test(value.sha256)
      || !Number.isSafeInteger(value.size)
      || value.size < 1) {
    throw new Error("固件发布信息格式无效");
  }
  return value;
}

async function connect() {
  setBusy(true);
  setStatus("请求 USB 权限");
  try {
    const manager = AdbDaemonWebUsbDeviceManager.BROWSER;
    if (!manager) throw new Error("当前浏览器不支持 WebUSB");

    const device = await manager.requestDevice();
    if (!device) throw new Error("未选择 USB ADB 设备");

    log(`已选择 USB 设备：${device.name || "ADB device"}`);
    log("正在建立本地 ADB 会话；首次使用可能需要确认授权…");
    const credentialStore = new AdbWebCredentialStore(
      "rokid-voice-remote web installer",
    );
    const transport = await AdbDaemonTransport.authenticate({
      serial: device.serial,
      connection: await device.connect(),
      credentialStore,
    });
    adb = new Adb(transport);
    log("ADB 连接成功");
    setStatus("USB 已连接");
    setStep("verify");
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    log(`连接失败：${message}`);
    if (message.toLowerCase().includes("busy") || message.includes("claim")) {
      log("USB 接口可能被本机 adb 占用，请执行 adb kill-server 后重试。");
    }
    setStatus("连接失败");
  } finally {
    setBusy(false);
  }
}

async function verify() {
  setBusy(true);
  setStatus("正在验证");
  try {
    const architecture = (await run(["uname", "-m"])).trim();
    if (architecture !== "aarch64") {
      throw new Error(`架构不受支持：${architecture || "unknown"}`);
    }
    log("架构：aarch64");

    const profileState = await run(
      "if test -e /data/rokid-voice-remote/state/original-state-v1; "
      + "then echo __VOICE_REMOTE_INSTALLED__; "
      + "elif test -e /data/rokid-chatgpt; then echo __OTHER_PROFILE__; "
      + "else echo __CLEAN__; fi",
    );
    if (profileState.includes("__VOICE_REMOTE_INSTALLED__")) {
      throw new Error("本项目已经安装；请使用固件配置页，或先卸载再升级");
    }
    if (profileState.includes("__OTHER_PROFILE__")) {
      throw new Error("检测到占用相同服务的其他固件；请使用仓库中的命令行迁移工具先做本地备份");
    }

    const output = await run(["sha256sum", ...runtimeFiles]);
    const actual = new Map<string, string>();
    for (const line of output.trim().split(/\r?\n/)) {
      const match = line.match(/^([0-9a-f]{64})\s+(.+)$/);
      if (match) actual.set(match[2], match[1]);
    }

    const variant = runtimeVariants.find(({ hashes }) =>
      runtimeFiles.every((file, index) => actual.get(file) === hashes[index]),
    );
    if (!variant) throw new Error("出厂组件组合不受支持");

    for (const file of runtimeFiles) log(`运行时匹配：${file}`);
    log(`兼容版本：${variant.name}`);

    release = await loadRelease();
    buttons.install.textContent = `安装 v${release.version}`;
    verified = true;
    log(`设备验证通过，固件版本 v${release.version}`);
    setStatus("验证通过");
    setStep("install");
  } catch (error) {
    log(`验证失败：${error instanceof Error ? error.message : String(error)}`);
    log("没有写入任何内容。请勿绕过兼容性检查。");
    setStatus("未通过");
  } finally {
    setBusy(false);
  }
}

async function install() {
  if (!adb || !release) return;
  setBusy(true);
  setStatus("下载固件");
  try {
    const response = await fetch(`./firmware/${release.archive}`, { cache: "no-store" });
    if (!response.ok) throw new Error("固件下载失败");
    const buffer = await response.arrayBuffer();
    if (buffer.byteLength !== release.size) throw new Error("固件大小与发布信息不一致");
    const localHash = await digestHex(buffer);
    if (localHash !== release.sha256) throw new Error("浏览器端固件哈希不匹配");
    log(`固件下载并校验完成：${Math.ceil(buffer.byteLength / 1024)} KiB`);

    const remoteArchive = "/data/local/tmp/rokid-voice-remote-web.tar.gz";
    const remoteDir = "/data/local/tmp/rokid-voice-remote-web-installer";
    setStatus("通过 USB 传输");
    const sync = await adb.sync();
    try {
      await sync.write({
        filename: remoteArchive,
        file: oneChunkStream(new Uint8Array(buffer)),
        permission: 0o644,
      });
    } finally {
      await sync.dispose();
    }
    log("USB 文件传输完成");

    const remoteHash = (await run(["sha256sum", remoteArchive])).trim().split(/\s+/)[0];
    if (remoteHash !== release.sha256) throw new Error("设备端固件哈希不匹配");
    log("设备端 SHA-256 校验通过");

    setStatus("正在安装");
    const command = [
      `rm -rf '${remoteDir}'`,
      `mkdir -p '${remoteDir}'`,
      `gzip -dc '${remoteArchive}' | tar -xf - -C '${remoteDir}'`,
      `cd '${remoteDir}'`,
      "ROKID_VOICE_REMOTE_REPLACE_CONFLICT=0 sh ./install.sh",
      "echo __VOICE_REMOTE_INSTALL_OK__",
    ].join(" && ");
    const output = await run(command);
    const configUrl = output.match(/^CONFIG_URL\s+(https?:\/\/\S+)$/m)?.[1];
    const safeOutput = output.replace(
      /^CONFIG_URL\s+\S+$/gm,
      "CONFIG_URL [已生成，请使用下方按钮打开]",
    );
    if (safeOutput.trim()) log(safeOutput.trim());
    if (!output.includes("__VOICE_REMOTE_INSTALL_OK__")) {
      throw new Error("设备安装脚本未报告成功");
    }

    installed = true;
    if (configUrl) {
      configLink.href = configUrl;
      configLink.classList.remove("hidden");
      log("固件配置页入口已生成");
    } else {
      log("音箱未取得局域网地址；可用命令行安装工具建立 ADB 端口转发。");
    }
    log("安装完成。下一步：配对目标设备，然后配置语音和 HID 键值。");
    setStatus("安装成功");
    setStep("configure");
  } catch (error) {
    log(`安装失败：${error instanceof Error ? error.message : String(error)}`);
    setStatus("安装失败");
  } finally {
    setBusy(false);
  }
}

const manager = AdbDaemonWebUsbDeviceManager.BROWSER;
if (manager && window.isSecureContext) {
  browserPill.textContent = "WebUSB 可用";
  browserPill.classList.add("ok");
} else {
  browserPill.textContent = "WebUSB 不可用";
  browserPill.classList.add("error");
  buttons.connect.disabled = true;
  log("请使用 HTTPS 或 localhost 上的桌面版 Chrome/Edge。");
}

consent.addEventListener("change", () => setBusy(false));
buttons.connect.addEventListener("click", connect);
buttons.verify.addEventListener("click", verify);
buttons.install.addEventListener("click", install);
setBusy(false);
