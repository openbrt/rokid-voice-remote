# Security

## Supported version

Only the newest tagged pre-release is maintained while hardware validation is
in progress.

## Reporting

Please open a GitHub security advisory instead of publishing device addresses,
pairing data, or a reproducible exploit in a public issue.

## Device model

The service runs as root because the factory image exposes Bluetooth control
and systemd installation only to root. Its local control socket is placed under
`/run/rokid-voice-remote`, accepts a small fixed command grammar, validates all
numeric ranges and Bluetooth addresses, and never evaluates configuration as
shell code.

Do not expose the control socket over TCP. Pair the speaker only with trusted
Bluetooth hosts. A paired HID device can inject keys into that host.
