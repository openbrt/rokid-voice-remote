'use strict';

const MAX_COMMANDS = 5;
const ZERO_ADDRESS = '00:00:00:00:00:00';
const presets = [
  { label: '电源（切换）', kind: 'consumer', code: '0x0030' },
  { label: '音量增加', kind: 'consumer', code: '0x00e9' },
  { label: '音量减小', kind: 'consumer', code: '0x00ea' },
  { label: '静音', kind: 'consumer', code: '0x00e2' },
  { label: '播放 / 暂停', kind: 'consumer', code: '0x00cd' },
  { label: '主页', kind: 'consumer', code: '0x0223' },
  { label: '返回', kind: 'consumer', code: '0x0224' },
  { label: '键盘 Enter', kind: 'key', code: '0x28' },
  { label: '键盘 Escape', kind: 'key', code: '0x29' },
  { label: '方向上', kind: 'key', code: '0x52' },
  { label: '方向下', kind: 'key', code: '0x51' },
  { label: '方向左', kind: 'key', code: '0x50' },
  { label: '方向右', kind: 'key', code: '0x4f' }
];

const state = { targets: [], commands: [], paired: [] };
const elements = {
  connection: document.querySelector('#connection'),
  targets: document.querySelector('#targets'),
  commands: document.querySelector('#commands'),
  count: document.querySelector('#command-count'),
  toast: document.querySelector('#toast'),
  pairingMode: document.querySelector('#pairing-mode')
};

function showToast(message, isError = false) {
  elements.toast.textContent = message;
  elements.toast.className = `toast show${isError ? ' error' : ''}`;
  window.clearTimeout(showToast.timer);
  showToast.timer = window.setTimeout(() => { elements.toast.className = 'toast'; }, 4200);
}

function setConnection(label, type) {
  elements.connection.textContent = label;
  elements.connection.className = `status ${type}`;
}

async function api(path, options = {}) {
  const headers = new Headers(options.headers || {});
  const response = await fetch(path, { ...options, headers, cache: 'no-store' });
  const body = await response.text();
  if (!response.ok) throw new Error(body.trim() || `HTTP ${response.status}`);
  return body;
}

function parseRows(text, columns) {
  return text.split('\n')
    .map((line) => line.trimEnd())
    .filter((line) => line && !line.startsWith('#'))
    .map((line) => {
      const fields = line.split('\t');
      if (fields.length !== columns) throw new Error(`配置列数错误：${line}`);
      return fields;
    });
}

function option(value, label, selected = false) {
  const item = document.createElement('option');
  item.value = value;
  item.textContent = label;
  item.selected = selected;
  return item;
}

function fieldLabel(title, control) {
  const label = document.createElement('label');
  const text = document.createElement('span');
  text.textContent = title;
  label.append(text, control);
  return label;
}

function makeInput(value, placeholder) {
  const input = document.createElement('input');
  input.value = value;
  input.placeholder = placeholder;
  return input;
}

function renderTargets() {
  elements.targets.replaceChildren();
  state.targets.forEach((target, index) => {
    const card = document.createElement('div');
    card.className = 'target-card';
    const name = makeInput(target.name, 'television');
    name.pattern = '[A-Za-z0-9_.-]+';
    name.addEventListener('change', () => {
      target.name = name.value.trim();
      renderCommands();
    });

    const address = document.createElement('select');
    address.append(option(target.address, target.address === ZERO_ADDRESS ? '尚未选择设备' : target.address, true));
    state.paired.forEach((device) => {
      if (device.address !== target.address) {
        address.append(option(device.address, `${device.name} · ${device.address}`));
      }
    });
    address.addEventListener('change', () => { target.address = address.value; });

    const remove = document.createElement('button');
    remove.type = 'button';
    remove.className = 'danger';
    remove.textContent = '删除';
    remove.addEventListener('click', () => {
      state.targets.splice(index, 1);
      renderTargets();
      renderCommands();
    });
    card.append(fieldLabel('目标名称', name), fieldLabel('已配对设备', address), remove);
    elements.targets.append(card);
  });
}

function presetFor(command) {
  return presets.find((item) => item.kind === command.kind &&
    item.code.toLowerCase() === command.code.toLowerCase());
}

function renderCommands() {
  elements.commands.replaceChildren();
  state.commands.forEach((command, index) => {
    const row = document.createElement('div');
    row.className = `command-row${command.kind === 'consumer' && command.code.toLowerCase() === '0x0030' ? ' power' : ''}`;

    const phrase = makeInput(command.phrase, '打开电视');
    phrase.addEventListener('input', () => { command.phrase = phrase.value; });
    const pinyin = makeInput(command.pinyin, 'da3kai1dian4shi4');
    pinyin.addEventListener('input', () => { command.pinyin = pinyin.value; });

    const target = document.createElement('select');
    target.append(option('active', '当前连接', command.target === 'active'));
    state.targets.forEach((item) => target.append(option(item.name, item.name, command.target === item.name)));
    target.addEventListener('change', () => { command.target = target.value; });

    const preset = document.createElement('select');
    const currentPreset = presetFor(command);
    preset.append(option('custom', '自定义键值', !currentPreset));
    presets.forEach((item, presetIndex) => preset.append(option(String(presetIndex), item.label, item === currentPreset)));
    preset.addEventListener('change', () => {
      if (preset.value !== 'custom') {
        const selected = presets[Number(preset.value)];
        command.kind = selected.kind;
        command.code = selected.code;
        renderCommands();
      }
    });

    const kind = document.createElement('select');
    kind.append(option('consumer', '媒体', command.kind === 'consumer'));
    kind.append(option('key', '键盘', command.kind === 'key'));
    kind.addEventListener('change', () => { command.kind = kind.value; });
    const code = makeInput(command.code, '0x0030');
    code.addEventListener('input', () => {
      command.code = code.value.trim();
      row.classList.toggle('power', command.kind === 'consumer' && command.code.toLowerCase() === '0x0030');
    });
    const codeGroup = document.createElement('div');
    codeGroup.className = 'code-group';
    codeGroup.append(kind, code);

    const repeat = makeInput(String(command.repeat), '1');
    repeat.type = 'number';
    repeat.min = '1';
    repeat.max = '10';
    repeat.addEventListener('input', () => { command.repeat = Number(repeat.value); });

    const remove = document.createElement('button');
    remove.type = 'button';
    remove.className = 'danger';
    remove.textContent = '×';
    remove.title = '删除指令';
    remove.addEventListener('click', () => {
      state.commands.splice(index, 1);
      renderCommands();
    });

    row.append(phrase, pinyin, target, preset, codeGroup, repeat, remove);
    elements.commands.append(row);
  });
  elements.count.textContent = `${state.commands.length} / ${MAX_COMMANDS}`;
  elements.count.classList.toggle('limit', state.commands.length >= MAX_COMMANDS);
  document.querySelector('#add-command').disabled = state.commands.length >= MAX_COMMANDS;
}

function normalizeCode(kind, value) {
  if (!/^(0x[0-9a-f]+|[0-9]+)$/i.test(value)) throw new Error(`无效键值：${value}`);
  const number = Number(value);
  const maximum = kind === 'consumer' ? 0x028c : 0xff;
  if (!Number.isInteger(number) || number < 0 || number > maximum) throw new Error(`键值超出 ${kind} 范围：${value}`);
  return `0x${number.toString(16).padStart(kind === 'consumer' ? 4 : 2, '0')}`;
}

function validateAndSerialize() {
  if (state.commands.length < 1 || state.commands.length > MAX_COMMANDS) throw new Error('语音指令必须为 1 到 5 条');
  const targetNames = new Set();
  const targetLines = state.targets.map((target) => {
    const name = target.name.trim();
    if (!/^[A-Za-z0-9_.-]{1,32}$/.test(name)) throw new Error(`目标名称无效：${name || '(空)'}`);
    if (targetNames.has(name)) throw new Error(`目标名称重复：${name}`);
    if (!/^[0-9a-f]{2}(?::[0-9a-f]{2}){5}$/i.test(target.address)) throw new Error(`蓝牙地址无效：${target.address}`);
    targetNames.add(name);
    return `${name}\t${target.address.toUpperCase()}`;
  });

  const phrases = new Set();
  const commandLines = state.commands.map((command) => {
    const phrase = command.phrase.trim();
    const pinyin = command.pinyin.trim();
    if (!phrase || /[\t\r\n]/.test(phrase)) throw new Error('语音指令不能为空或包含换行/Tab');
    if (phrases.has(phrase)) throw new Error(`语音指令重复：${phrase}`);
    if (!/^[A-Za-z0-9]{1,256}$/.test(pinyin)) throw new Error(`拼音格式无效：${pinyin || '(空)'}`);
    if (command.target !== 'active' && !targetNames.has(command.target)) throw new Error(`目标不存在：${command.target}`);
    if (!['consumer', 'key'].includes(command.kind)) throw new Error('按键类型无效');
    if (!Number.isInteger(command.repeat) || command.repeat < 1 || command.repeat > 10) throw new Error(`重复次数无效：${command.repeat}`);
    phrases.add(phrase);
    const code = normalizeCode(command.kind, command.code);
    return [phrase, pinyin, command.target, command.kind, code, command.repeat].join('\t');
  });
  return {
    commands: `# phrase<TAB>pinyin<TAB>target<TAB>kind<TAB>code<TAB>repeat\n${commandLines.join('\n')}\n`,
    targets: `# target<TAB>Bluetooth BD_ADDR\n${targetLines.join('\n')}${targetLines.length ? '\n' : ''}`
  };
}

async function loadConfiguration() {
  setConnection('正在连接', 'pending');
  try {
    const [commandsText, targetsText, pairedText] = await Promise.all([
      api('/api/commands'), api('/api/targets'), api('/api/paired')
    ]);
    state.targets = parseRows(targetsText, 2).map(([name, address]) => ({ name, address }));
    state.commands = parseRows(commandsText, 6).map(([phrase, pinyin, target, kind, code, repeat]) => ({
      phrase, pinyin, target, kind, code, repeat: Number(repeat)
    }));
    state.paired = parseRows(pairedText, 2).map(([address, name]) => ({ address, name }));
    renderTargets();
    renderCommands();
    setConnection('已连接', 'ok');
  } catch (error) {
    setConnection('连接失败', 'error');
    showToast(error.message, true);
  }
}

async function saveConfiguration() {
  try {
    const configuration = validateAndSerialize();
    const body = new URLSearchParams(configuration).toString();
    const response = await api('/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded; charset=utf-8' },
      body
    });
    showToast(response.trim());
    setConnection('已保存', 'ok');
    window.setTimeout(loadConfiguration, 1200);
  } catch (error) {
    showToast(error.message, true);
  }
}

async function enterPairingMode() {
  if (!window.confirm('这会断开当前电视/投影。确定要让音箱进入新增设备配对模式吗？')) return;
  elements.pairingMode.disabled = true;
  try {
    await api('/api/hid/listen', { method: 'POST' });
    showToast('已进入配对模式；现在请在目标设备屏幕上添加 Rokid Voice Remote');
  } catch (error) {
    showToast(error.message, true);
  } finally {
    elements.pairingMode.disabled = false;
  }
}

document.querySelector('#add-target').addEventListener('click', () => {
  let index = state.targets.length + 1;
  while (state.targets.some((target) => target.name === `device${index}`)) index += 1;
  state.targets.push({ name: `device${index}`, address: ZERO_ADDRESS });
  renderTargets();
  renderCommands();
});
elements.pairingMode.addEventListener('click', enterPairingMode);

document.querySelector('#add-command').addEventListener('click', () => {
  if (state.commands.length >= MAX_COMMANDS) return;
  state.commands.push({
    phrase: '', pinyin: '', target: 'active',
    kind: 'consumer', code: '0x0030', repeat: 1
  });
  renderCommands();
});

document.querySelector('#reload').addEventListener('click', loadConfiguration);
document.querySelector('#save').addEventListener('click', saveConfiguration);
loadConfiguration();
