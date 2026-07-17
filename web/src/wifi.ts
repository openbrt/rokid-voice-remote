import { showConfigToast } from "./configurator";

export type DeviceShell = (command: readonly string[]) => Promise<string>;

type WifiNetwork = {
  ssid: string;
  signal: number;
  flags: string;
};

type WifiStatus = Record<string, string>;

const element = <T extends HTMLElement>(id: string) => {
  const value = document.getElementById(id);
  if (!value) throw new Error(`missing element: ${id}`);
  return value as T;
};

const elements = {
  refresh: element<HTMLButtonElement>("wifi-refresh"),
  connect: element<HTMLButtonElement>("wifi-connect"),
  network: element<HTMLSelectElement>("wifi-network"),
  ssid: element<HTMLInputElement>("wifi-ssid"),
  security: element<HTMLSelectElement>("wifi-security"),
  password: element<HTMLInputElement>("wifi-password"),
  state: element<HTMLElement>("wifi-state"),
  address: element<HTMLElement>("wifi-address"),
};

let shell: DeviceShell | undefined;
let networks: WifiNetwork[] = [];

function setState(label: string, type: "pending" | "ok" | "error") {
  elements.state.textContent = label;
  elements.state.className = `config-status ${type}`;
}

function parseStatus(output: string) {
  const status: WifiStatus = {};
  for (const line of output.split(/\r?\n/)) {
    const separator = line.indexOf("=");
    if (separator > 0) status[line.slice(0, separator)] = line.slice(separator + 1);
  }
  return status;
}

function parseScanResults(output: string) {
  const bySsid = new Map<string, WifiNetwork>();
  for (const line of output.split(/\r?\n/).slice(1)) {
    const fields = line.split("\t");
    if (fields.length < 5) continue;
    const ssid = fields.slice(4).join("\t").trim();
    const signal = Number(fields[2]);
    if (!ssid || !Number.isFinite(signal)) continue;
    const candidate = { ssid, signal, flags: fields[3] };
    const current = bySsid.get(ssid);
    if (!current || candidate.signal > current.signal) bySsid.set(ssid, candidate);
  }
  return [...bySsid.values()].sort((left, right) => right.signal - left.signal);
}

function renderNetworks() {
  elements.network.replaceChildren();
  const manual = document.createElement("option");
  manual.value = "";
  manual.textContent = "手动输入网络";
  elements.network.append(manual);
  networks.forEach((network, index) => {
    const item = document.createElement("option");
    item.value = String(index);
    const lock = network.flags.includes("WPA") ? "🔒" : "○";
    item.textContent = `${lock} ${network.ssid} · ${network.signal} dBm`;
    elements.network.append(item);
  });
}

function selectedNetwork() {
  if (elements.network.value === "") return undefined;
  return networks[Number(elements.network.value)];
}

function updatePasswordState() {
  const open = elements.security.value === "open";
  elements.password.disabled = open;
  if (open) elements.password.value = "";
}

function delay(milliseconds: number) {
  return new Promise((resolve) => window.setTimeout(resolve, milliseconds));
}

async function wpa(...args: string[]) {
  if (!shell) throw new Error("USB 配网通道尚未连接");
  return (await shell(["wpa_cli", "-i", "wlan0", ...args])).trim();
}

function expectOk(output: string, action: string) {
  if (!output.split(/\r?\n/).includes("OK")) {
    throw new Error(`${action}失败：${output || "设备无响应"}`);
  }
}

async function readStatus() {
  const status = parseStatus(await wpa("status"));
  const connected = status.wpa_state === "COMPLETED";
  if (connected) {
    setState(`已连接：${status.ssid || "Wi‑Fi"}`, "ok");
    elements.address.textContent = status.ip_address ? `IP ${status.ip_address}` : "正在获取 IP";
    if (status.ip_address) {
      window.dispatchEvent(new CustomEvent("rokid-wifi-connected", {
        detail: { ip: status.ip_address },
      }));
    }
  } else {
    setState(status.wpa_state ? `未连接 · ${status.wpa_state}` : "Wi‑Fi 未连接", "pending");
    elements.address.textContent = "";
  }
  return status;
}

async function scanWifi() {
  if (!shell) return;
  elements.refresh.disabled = true;
  setState("正在扫描", "pending");
  try {
    await wpa("scan");
    let results: WifiNetwork[] = [];
    for (let attempt = 0; attempt < 8; attempt += 1) {
      await delay(700);
      results = parseScanResults(await wpa("scan_results"));
      if (results.length > 0) break;
    }
    networks = results;
    renderNetworks();
    await readStatus();
    showConfigToast(`发现 ${networks.length} 个 Wi‑Fi 网络`);
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    setState("扫描失败", "error");
    showConfigToast(message, true);
  } finally {
    elements.refresh.disabled = false;
  }
}

function bytesToHex(bytes: Uint8Array) {
  return Array.from(bytes, (value) => value.toString(16).padStart(2, "0")).join("");
}

async function derivePsk(ssid: string, password: string) {
  const encoder = new TextEncoder();
  const key = await crypto.subtle.importKey(
    "raw",
    encoder.encode(password),
    "PBKDF2",
    false,
    ["deriveBits"],
  );
  const bits = await crypto.subtle.deriveBits({
    name: "PBKDF2",
    salt: encoder.encode(ssid),
    iterations: 4096,
    hash: "SHA-1",
  }, key, 256);
  return bytesToHex(new Uint8Array(bits));
}

async function connectWifi() {
  if (!shell) return;
  const ssid = elements.ssid.value;
  const security = elements.security.value;
  const password = elements.password.value;
  const encoder = new TextEncoder();
  const ssidBytes = encoder.encode(ssid);
  const passwordBytes = encoder.encode(password);
  const selected = selectedNetwork();

  if (!ssid || ssidBytes.byteLength > 32 || /[\0\r\n]/.test(ssid)) {
    showConfigToast("SSID 必须为 1–32 字节且不能包含控制字符", true);
    return;
  }
  if (selected && (selected.flags.includes("WEP")
      || (selected.flags.includes("SAE") && !selected.flags.includes("WPA-PSK")))) {
    showConfigToast("这个网络的认证方式不受旧款 A113 支持", true);
    return;
  }
  if (security === "wpa-psk"
      && (passwordBytes.byteLength < 8 || passwordBytes.byteLength > 63)) {
    showConfigToast("WPA/WPA2 密码必须为 8–63 字节", true);
    return;
  }

  elements.connect.disabled = true;
  elements.refresh.disabled = true;
  setState(`正在连接：${ssid}`, "pending");
  let networkId = "";
  let previousId = "";
  try {
    const previous = await readStatus();
    previousId = previous.id || "";
    const addOutput = await wpa("add_network");
    networkId = addOutput.split(/\r?\n/).find((line) => /^\d+$/.test(line)) || "";
    if (!networkId) throw new Error(`无法创建 Wi‑Fi 配置：${addOutput}`);

    expectOk(await wpa("set_network", networkId, "ssid", bytesToHex(ssidBytes)), "设置 SSID");
    expectOk(await wpa("set_network", networkId, "scan_ssid", "1"), "设置隐藏网络扫描");
    if (security === "open") {
      expectOk(await wpa("set_network", networkId, "key_mgmt", "NONE"), "设置开放网络");
    } else {
      const psk = await derivePsk(ssid, password);
      expectOk(await wpa("set_network", networkId, "key_mgmt", "WPA-PSK"), "设置认证类型");
      expectOk(await wpa("set_network", networkId, "psk", psk), "设置网络密钥");
    }
    expectOk(await wpa("enable_network", networkId), "启用网络");
    expectOk(await wpa("select_network", networkId), "选择网络");

    let connected: WifiStatus | undefined;
    for (let attempt = 0; attempt < 30; attempt += 1) {
      await delay(1000);
      const status = await readStatus();
      if (status.wpa_state === "COMPLETED" && status.ssid === ssid) {
        connected = status;
        if (status.ip_address) break;
      }
    }
    if (!connected) throw new Error("连接超时，请检查密码、信号和认证方式");

    expectOk(await wpa("save_config"), "保存网络");
    await readStatus();
    showConfigToast(`Wi‑Fi 已连接：${ssid}`);
  } catch (error) {
    if (networkId) await wpa("remove_network", networkId).catch(() => "");
    if (previousId) await wpa("select_network", previousId).catch(() => "");
    const message = error instanceof Error ? error.message : String(error);
    setState("连接失败", "error");
    showConfigToast(message, true);
  } finally {
    elements.password.value = "";
    elements.connect.disabled = false;
    elements.refresh.disabled = false;
  }
}

export async function loadWifi(deviceShell?: DeviceShell) {
  if (deviceShell) shell = deviceShell;
  if (!shell) throw new Error("USB 配网通道尚未连接");
  await readStatus();
}

elements.network.addEventListener("change", () => {
  const network = selectedNetwork();
  if (!network) return;
  elements.ssid.value = network.ssid;
  elements.security.value = network.flags.includes("WPA") ? "wpa-psk" : "open";
  updatePasswordState();
});
elements.security.addEventListener("change", updatePasswordState);
elements.refresh.addEventListener("click", scanWifi);
elements.connect.addEventListener("click", connectWifi);
updatePasswordState();
