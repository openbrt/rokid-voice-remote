# Architecture

The project is independent of `rokid-chatgpt`. It uses two small services and
does not import, patch, stop, overwrite, or reuse files from that project.

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
