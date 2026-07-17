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

The configuration page listens on TCP port 8090 so it can be reached from a
phone or computer on the same LAN. It uses a 192-bit random bearer token and
strict same-origin browser policy, but factory firmware does not provide TLS.
Keep the speaker on a trusted network, do not port-forward 8090, rotate
`config/web-token` if it leaks, and restart
`rokid-voice-remote-config.service` after rotation.
