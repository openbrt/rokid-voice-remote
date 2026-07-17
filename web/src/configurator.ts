export type DeviceRequestOptions = {
  method?: "GET" | "POST";
  contentType?: string;
  body?: string;
};

export type DeviceRequest = (
  path: string,
  options?: DeviceRequestOptions,
) => Promise<string>;

const MAX_COMMANDS = 5;
const ZERO_ADDRESS = "00:00:00:00:00:00";
const presets = [
  { label: "电源（切换）", kind: "consumer", code: "0x0030" },
  { label: "音量增加", kind: "consumer", code: "0x00e9" },
  { label: "音量减小", kind: "consumer", code: "0x00ea" },
  { label: "静音", kind: "consumer", code: "0x00e2" },
  { label: "播放 / 暂停", kind: "consumer", code: "0x00cd" },
  { label: "主页", kind: "consumer", code: "0x0223" },
  { label: "返回", kind: "consumer", code: "0x0224" },
  { label: "键盘 Enter", kind: "key", code: "0x28" },
  { label: "键盘 Escape", kind: "key", code: "0x29" },
  { label: "方向上", kind: "key", code: "0x52" },
  { label: "方向下", kind: "key", code: "0x51" },
  { label: "方向左", kind: "key", code: "0x50" },
  { label: "方向右", kind: "key", code: "0x4f" },
] as const;

type Target = { name: string; address: string };
type PairedDevice = { address: string; name: string };
type Command = {
  phrase: string;
  pinyin: string;
  target: string;
  kind: string;
  code: string;
  repeat: number;
};

const state: {
  request?: DeviceRequest;
  targets: Target[];
  commands: Command[];
  paired: PairedDevice[];
} = { targets: [], commands: [], paired: [] };

const element = <T extends HTMLElement>(id: string) => {
  const value = document.getElementById(id);
  if (!value) throw new Error(`missing element: ${id}`);
  return value as T;
};

const elements = {
  section: element<HTMLElement>("device-setup"),
  connection: element<HTMLElement>("config-connection"),
  targets: element<HTMLElement>("config-targets"),
  commands: element<HTMLElement>("config-commands"),
  count: element<HTMLElement>("config-command-count"),
  toast: element<HTMLElement>("config-toast"),
  addTarget: element<HTMLButtonElement>("config-add-target"),
  addCommand: element<HTMLButtonElement>("config-add-command"),
  reload: element<HTMLButtonElement>("config-reload"),
  save: element<HTMLButtonElement>("config-save"),
};

let toastTimer: number | undefined;

export function showConfigToast(message: string, error = false) {
  elements.toast.textContent = message;
  elements.toast.className = `config-toast show${error ? " error" : ""}`;
  if (toastTimer !== undefined) window.clearTimeout(toastTimer);
  toastTimer = window.setTimeout(() => {
    elements.toast.className = "config-toast";
  }, 4500);
}

function setConnection(label: string, type: "pending" | "ok" | "error") {
  elements.connection.textContent = label;
  elements.connection.className = `config-status ${type}`;
}

function parseRows(text: string, columns: number) {
  return text.split("\n")
    .map((line) => line.trimEnd())
    .filter((line) => line && !line.startsWith("#"))
    .map((line) => {
      const fields = line.split("\t");
      if (fields.length !== columns) throw new Error(`配置列数错误：${line}`);
      return fields;
    });
}

function option(value: string, label: string, selected = false) {
  const item = document.createElement("option");
  item.value = value;
  item.textContent = label;
  item.selected = selected;
  return item;
}

function fieldLabel(title: string, control: HTMLElement) {
  const label = document.createElement("label");
  const text = document.createElement("span");
  text.textContent = title;
  label.append(text, control);
  return label;
}

function makeInput(value: string, placeholder: string) {
  const input = document.createElement("input");
  input.value = value;
  input.placeholder = placeholder;
  return input;
}

function renderTargets() {
  elements.targets.replaceChildren();
  state.targets.forEach((target, index) => {
    const card = document.createElement("div");
    card.className = "target-card";
    const name = makeInput(target.name, "television");
    name.pattern = "[A-Za-z0-9_.-]+";
    name.addEventListener("change", () => {
      target.name = name.value.trim();
      renderCommands();
    });

    const address = document.createElement("select");
    address.append(option(
      target.address,
      target.address === ZERO_ADDRESS ? "尚未选择设备" : target.address,
      true,
    ));
    for (const device of state.paired) {
      if (device.address !== target.address) {
        address.append(option(device.address, `${device.name} · ${device.address}`));
      }
    }
    address.addEventListener("change", () => { target.address = address.value; });

    const remove = document.createElement("button");
    remove.type = "button";
    remove.className = "config-danger";
    remove.textContent = "删除";
    remove.addEventListener("click", () => {
      state.targets.splice(index, 1);
      renderTargets();
      renderCommands();
    });
    card.append(fieldLabel("目标名称", name), fieldLabel("已配对设备", address), remove);
    elements.targets.append(card);
  });
}

function presetFor(command: Command) {
  return presets.find((item) => item.kind === command.kind
    && item.code.toLowerCase() === command.code.toLowerCase());
}

function renderCommands() {
  elements.commands.replaceChildren();
  state.commands.forEach((command, index) => {
    const row = document.createElement("div");
    row.className = `command-row${command.kind === "consumer"
      && command.code.toLowerCase() === "0x0030" ? " power" : ""}`;

    const phrase = makeInput(command.phrase, "打开电视");
    phrase.addEventListener("input", () => { command.phrase = phrase.value; });
    const pinyin = makeInput(command.pinyin, "da3kai1dian4shi4");
    pinyin.addEventListener("input", () => { command.pinyin = pinyin.value; });

    const target = document.createElement("select");
    target.append(option("active", "当前连接", command.target === "active"));
    for (const item of state.targets) {
      target.append(option(item.name, item.name, command.target === item.name));
    }
    target.addEventListener("change", () => { command.target = target.value; });

    const preset = document.createElement("select");
    const currentPreset = presetFor(command);
    preset.append(option("custom", "自定义键值", !currentPreset));
    presets.forEach((item, presetIndex) => {
      preset.append(option(String(presetIndex), item.label, item === currentPreset));
    });
    preset.addEventListener("change", () => {
      if (preset.value !== "custom") {
        const selected = presets[Number(preset.value)];
        command.kind = selected.kind;
        command.code = selected.code;
        renderCommands();
      }
    });

    const kind = document.createElement("select");
    kind.append(option("consumer", "媒体", command.kind === "consumer"));
    kind.append(option("key", "键盘", command.kind === "key"));
    kind.addEventListener("change", () => { command.kind = kind.value; });
    const code = makeInput(command.code, "0x0030");
    code.addEventListener("input", () => {
      command.code = code.value.trim();
      row.classList.toggle("power", command.kind === "consumer"
        && command.code.toLowerCase() === "0x0030");
    });
    const codeGroup = document.createElement("div");
    codeGroup.className = "code-group";
    codeGroup.append(kind, code);

    const repeat = makeInput(String(command.repeat), "1");
    repeat.type = "number";
    repeat.min = "1";
    repeat.max = "10";
    repeat.addEventListener("input", () => { command.repeat = Number(repeat.value); });

    const remove = document.createElement("button");
    remove.type = "button";
    remove.className = "config-danger compact";
    remove.textContent = "×";
    remove.title = "删除指令";
    remove.addEventListener("click", () => {
      state.commands.splice(index, 1);
      renderCommands();
    });

    row.append(phrase, pinyin, target, preset, codeGroup, repeat, remove);
    elements.commands.append(row);
  });
  elements.count.textContent = `${state.commands.length} / ${MAX_COMMANDS}`;
  elements.count.classList.toggle("limit", state.commands.length >= MAX_COMMANDS);
  elements.addCommand.disabled = state.commands.length >= MAX_COMMANDS;
}

function normalizeCode(kind: string, value: string) {
  if (!/^(0x[0-9a-f]+|[0-9]+)$/i.test(value)) throw new Error(`无效键值：${value}`);
  const number = Number(value);
  const maximum = kind === "consumer" ? 0x028c : 0xff;
  if (!Number.isInteger(number) || number < 0 || number > maximum) {
    throw new Error(`键值超出 ${kind} 范围：${value}`);
  }
  return `0x${number.toString(16).padStart(kind === "consumer" ? 4 : 2, "0")}`;
}

function validateAndSerialize() {
  if (state.commands.length < 1 || state.commands.length > MAX_COMMANDS) {
    throw new Error("语音指令必须为 1 到 5 条");
  }
  const targetNames = new Set<string>();
  const targetLines = state.targets.map((target) => {
    const name = target.name.trim();
    if (!/^[A-Za-z0-9_.-]{1,32}$/.test(name)) {
      throw new Error(`目标名称无效：${name || "(空)"}`);
    }
    if (targetNames.has(name)) throw new Error(`目标名称重复：${name}`);
    if (!/^[0-9a-f]{2}(?::[0-9a-f]{2}){5}$/i.test(target.address)) {
      throw new Error(`蓝牙地址无效：${target.address}`);
    }
    targetNames.add(name);
    return `${name}\t${target.address.toUpperCase()}`;
  });

  const phrases = new Set<string>();
  const commandLines = state.commands.map((command) => {
    const phrase = command.phrase.trim();
    const pinyin = command.pinyin.trim();
    if (!phrase || /[\t\r\n]/.test(phrase)) throw new Error("语音指令不能为空或包含换行/Tab");
    if (phrases.has(phrase)) throw new Error(`语音指令重复：${phrase}`);
    if (!/^[A-Za-z0-9]{1,256}$/.test(pinyin)) {
      throw new Error(`拼音格式无效：${pinyin || "(空)"}`);
    }
    if (command.target !== "active" && !targetNames.has(command.target)) {
      throw new Error(`目标不存在：${command.target}`);
    }
    if (command.kind !== "consumer" && command.kind !== "key") {
      throw new Error("按键类型无效");
    }
    if (!Number.isInteger(command.repeat) || command.repeat < 1 || command.repeat > 10) {
      throw new Error(`重复次数无效：${command.repeat}`);
    }
    phrases.add(phrase);
    const code = normalizeCode(command.kind, command.code);
    return [phrase, pinyin, command.target, command.kind, code, command.repeat].join("\t");
  });
  return {
    commands: `# phrase<TAB>pinyin<TAB>target<TAB>kind<TAB>code<TAB>repeat\n${commandLines.join("\n")}\n`,
    targets: `# target<TAB>Bluetooth BD_ADDR\n${targetLines.join("\n")}${targetLines.length ? "\n" : ""}`,
  };
}

export async function loadConfigurator(request?: DeviceRequest) {
  if (request) state.request = request;
  if (!state.request) throw new Error("设备配置通道尚未连接");
  elements.section.classList.remove("hidden");
  setConnection("正在读取", "pending");
  elements.reload.disabled = true;
  elements.save.disabled = true;
  try {
    const [commandsText, targetsText, pairedText] = await Promise.all([
      state.request("/api/commands"),
      state.request("/api/targets"),
      state.request("/api/paired"),
    ]);
    state.targets = parseRows(targetsText, 2)
      .map(([name, address]) => ({ name, address }));
    state.commands = parseRows(commandsText, 6)
      .map(([phrase, pinyin, target, kind, code, repeat]) => ({
        phrase, pinyin, target, kind, code, repeat: Number(repeat),
      }));
    state.paired = parseRows(pairedText, 2)
      .map(([address, name]) => ({ address, name }));
    renderTargets();
    renderCommands();
    setConnection("USB 已连接", "ok");
  } catch (error) {
    setConnection("读取失败", "error");
    throw error;
  } finally {
    elements.reload.disabled = false;
    elements.save.disabled = false;
  }
}

async function saveConfiguration() {
  if (!state.request) return;
  elements.save.disabled = true;
  setConnection("正在保存", "pending");
  try {
    const configuration = validateAndSerialize();
    const body = new URLSearchParams(configuration).toString();
    const response = await state.request("/api/config", {
      method: "POST",
      contentType: "application/x-www-form-urlencoded; charset=utf-8",
      body,
    });
    showConfigToast(response.trim());
    setConnection("已保存", "ok");
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    showConfigToast(message, true);
    setConnection("保存失败", "error");
  } finally {
    elements.save.disabled = false;
  }
}

elements.addTarget.addEventListener("click", () => {
  let index = state.targets.length + 1;
  while (state.targets.some((target) => target.name === `device${index}`)) index += 1;
  state.targets.push({ name: `device${index}`, address: ZERO_ADDRESS });
  renderTargets();
  renderCommands();
});

elements.addCommand.addEventListener("click", () => {
  if (state.commands.length >= MAX_COMMANDS) return;
  state.commands.push({
    phrase: "",
    pinyin: "",
    target: state.targets[0]?.name || "active",
    kind: "consumer",
    code: "0x0030",
    repeat: 1,
  });
  renderCommands();
});

elements.reload.addEventListener("click", () => {
  loadConfigurator().catch((error) => {
    showConfigToast(error instanceof Error ? error.message : String(error), true);
  });
});
elements.save.addEventListener("click", saveConfiguration);
