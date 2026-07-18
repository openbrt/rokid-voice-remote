export type LearnedHidMapping = {
  kind: "consumer" | "key";
  code: string;
  label: string;
  source: "keyboard" | "webhid";
};

const consumerKeys = new Map<string, number>([
  ["Power", 0x0030],
  ["MediaPlayPause", 0x00cd],
  ["MediaStop", 0x00b7],
  ["MediaTrackNext", 0x00b5],
  ["MediaTrackPrevious", 0x00b6],
  ["AudioVolumeMute", 0x00e2],
  ["AudioVolumeUp", 0x00e9],
  ["AudioVolumeDown", 0x00ea],
  ["BrowserSearch", 0x0221],
  ["BrowserHome", 0x0223],
  ["BrowserBack", 0x0224],
  ["BrowserForward", 0x0225],
  ["BrowserStop", 0x0226],
  ["BrowserRefresh", 0x0227],
  ["BrowserFavorites", 0x022a],
]);

const keyboardCodes = new Map<string, number>([
  ["Enter", 0x28], ["Escape", 0x29], ["Backspace", 0x2a], ["Tab", 0x2b],
  ["Space", 0x2c], ["Minus", 0x2d], ["Equal", 0x2e],
  ["BracketLeft", 0x2f], ["BracketRight", 0x30], ["Backslash", 0x31],
  ["Semicolon", 0x33], ["Quote", 0x34], ["Backquote", 0x35],
  ["Comma", 0x36], ["Period", 0x37], ["Slash", 0x38], ["CapsLock", 0x39],
  ["PrintScreen", 0x46], ["ScrollLock", 0x47], ["Pause", 0x48],
  ["Insert", 0x49], ["Home", 0x4a], ["PageUp", 0x4b], ["Delete", 0x4c],
  ["End", 0x4d], ["PageDown", 0x4e], ["ArrowRight", 0x4f],
  ["ArrowLeft", 0x50], ["ArrowDown", 0x51], ["ArrowUp", 0x52],
  ["NumLock", 0x53], ["NumpadDivide", 0x54], ["NumpadMultiply", 0x55],
  ["NumpadSubtract", 0x56], ["NumpadAdd", 0x57], ["NumpadEnter", 0x58],
  ["NumpadDecimal", 0x63], ["ContextMenu", 0x65], ["NumpadEqual", 0x67],
]);

for (let index = 0; index < 26; index += 1) {
  keyboardCodes.set(`Key${String.fromCharCode(65 + index)}`, 0x04 + index);
}
for (let index = 1; index <= 9; index += 1) {
  keyboardCodes.set(`Digit${index}`, 0x1d + index);
}
keyboardCodes.set("Digit0", 0x27);
for (let index = 1; index <= 12; index += 1) {
  keyboardCodes.set(`F${index}`, 0x39 + index);
}
for (let index = 1; index <= 9; index += 1) {
  keyboardCodes.set(`Numpad${index}`, 0x58 + index);
}
keyboardCodes.set("Numpad0", 0x62);

const labels = new Map<string, string>([
  ["consumer:0030", "电源（切换）"],
  ["consumer:00b5", "下一曲"],
  ["consumer:00b6", "上一曲"],
  ["consumer:00b7", "停止"],
  ["consumer:00cd", "播放 / 暂停"],
  ["consumer:00e2", "静音"],
  ["consumer:00e9", "音量增加"],
  ["consumer:00ea", "音量减小"],
  ["consumer:0221", "搜索"],
  ["consumer:0223", "主页"],
  ["consumer:0224", "返回"],
  ["consumer:0225", "前进"],
  ["consumer:0226", "停止加载"],
  ["consumer:0227", "刷新"],
  ["consumer:022a", "收藏"],
  ["key:28", "确认 / Enter"],
  ["key:29", "返回 / Escape"],
  ["key:4a", "Home"],
  ["key:4f", "方向右"],
  ["key:50", "方向左"],
  ["key:51", "方向下"],
  ["key:52", "方向上"],
]);

function mapping(kind: "consumer" | "key", usage: number, source: LearnedHidMapping["source"]) {
  if (kind === "consumer" && usage > 0x028c) return undefined;
  if (kind === "key" && (usage > 0xff || (usage >= 0xe0 && usage <= 0xe7))) {
    return undefined;
  }
  const width = kind === "consumer" ? 4 : 2;
  const digits = usage.toString(16).padStart(width, "0");
  const code = `0x${digits}`;
  let label = labels.get(`${kind}:${digits}`);
  if (!label && kind === "key" && usage >= 0x04 && usage <= 0x1d) {
    label = `键盘 ${String.fromCharCode(65 + usage - 0x04)}`;
  }
  if (!label && kind === "key" && usage >= 0x1e && usage <= 0x27) {
    label = `数字 ${usage === 0x27 ? 0 : usage - 0x1d}`;
  }
  return { kind, code, label: label || `HID ${code}`, source } satisfies LearnedHidMapping;
}

export function keyboardEventToMapping(event: KeyboardEvent) {
  const consumerUsage = consumerKeys.get(event.code) ?? consumerKeys.get(event.key);
  if (consumerUsage !== undefined) return mapping("consumer", consumerUsage, "keyboard");
  const keyboardUsage = keyboardCodes.get(event.code);
  if (keyboardUsage === undefined) return undefined;
  return mapping("key", keyboardUsage, "keyboard");
}

function readBits(data: DataView, bitOffset: number, size: number) {
  if (size < 1 || size > 32 || bitOffset + size > data.byteLength * 8) return undefined;
  let value = 0;
  for (let bit = 0; bit < size; bit += 1) {
    const absolute = bitOffset + bit;
    value += ((data.getUint8(Math.floor(absolute / 8)) >> (absolute % 8)) & 1) * (2 ** bit);
  }
  return value >>> 0;
}

function usageForField(item: HIDReportItem, field: number, value: number) {
  if (item.isArray) {
    if (item.isRange && item.usageMinimum !== undefined) {
      const page = item.usageMinimum & 0xffff0000;
      return page | (value & 0xffff);
    }
    const explicit = item.usages || [];
    const exact = explicit.find((usage) => (usage & 0xffff) === value);
    if (exact !== undefined) return exact;
    const index = value - (item.logicalMinimum || 0);
    return explicit[index];
  }
  if (item.isRange && item.usageMinimum !== undefined) {
    return item.usageMinimum + field;
  }
  return item.usages?.[Math.min(field, (item.usages?.length || 1) - 1)];
}

function decodeReport(report: HIDReportInfo, data: DataView) {
  let bitOffset = 0;
  for (const item of report.items || []) {
    const size = item.reportSize || 0;
    const count = item.reportCount || 0;
    for (let field = 0; field < count; field += 1) {
      const value = readBits(data, bitOffset + field * size, size);
      if (!item.isConstant && value) {
        const usage = usageForField(item, field, value);
        if (usage !== undefined) {
          const page = (usage >>> 16) & 0xffff;
          const usageId = usage & 0xffff;
          if (page === 0x0c) {
            const result = mapping("consumer", usageId, "webhid");
            if (result) return result;
          }
          if (page === 0x07) {
            const result = mapping("key", usageId, "webhid");
            if (result) return result;
          }
        }
      }
    }
    bitOffset += size * count;
  }
  return undefined;
}

export function decodeHidInput(device: HIDDevice, event: HIDInputReportEvent) {
  for (const collection of device.collections) {
    for (const report of collection.inputReports || []) {
      if ((report.reportId || 0) !== event.reportId) continue;
      const result = decodeReport(report, event.data);
      if (result) return result;
    }
  }
  return undefined;
}
