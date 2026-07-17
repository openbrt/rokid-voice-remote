# Device research notes

These findings apply to the inspected Amlogic A113 Rokid factory images from
2019/2020 and the matching source tree. Other firmware revisions must be
verified before installation.

## Custom phrase count

- The factory activation UI deliberately deletes every existing custom word
  before inserting a new one, so the product feature exposes **one** custom
  wake word.
- BlackSiren's proxy stores words in a dynamic `std::vector`; the processor
  allocates its word array from the current count. The matching closed R2
  detector also allocates per-word state dynamically and contains no fixed
  maximum comparison in the inspected setter.
- This does not mean the practical count is unlimited. Every additional phrase
  consumes memory and detector time and may hurt false-accept/false-reject
  rates. This project therefore enforces **32 command phrases** as a
  conservative, project-owned safety limit pending stress testing.

The factory baseline trigger configuration contains two defaults (`若琪|洛奇`
and `没事了`). Those are separate from the project command list.

## Matching source locations

The private matching tree is intentionally not copied here. Relevant areas are:

- Rokid activation Lua word management;
- `robot/openvoice/blacksiren` proxy and processor;
- Rokid's `RKLuaSiren` bridge;
- Broadcom BSA Linux HID Device headers and examples.

The public BlackSiren mirror provides useful architecture context, but device
ABI decisions were checked against the exact SDK and factory binaries.

## Device-control limitations

HID Consumer usage `0x0030` is **Power**, a toggle. It is not discrete ON and
OFF. Consequently, the default “打开” and “关掉” commands intentionally send
the same key. Idempotent power control requires target state feedback plus
HDMI-CEC, infrared, LAN control, or a vendor protocol.

A TV or projector that turns its Bluetooth host off in standby cannot be
cold-started over HID. For reliable multi-device control, pair Rokid with one
always-on receiver (Android box, Home Assistant host, etc.) and let that
receiver issue CEC/IR/LAN commands.

## Public references

- [Rokid/BlackSiren](https://github.com/Rokid/BlackSiren)
- [Bluetooth SIG: Human Interface Device Profile 1.1.1](https://www.bluetooth.com/specifications/specs/human-interface-device-profile-1-1-1/)
- [USB-IF: HID Usage Tables 1.21](https://www.usb.org/sites/default/files/hut1_21_0.pdf)
