# Third-party boundary

This repository contains only original glue code and configuration. It does
not redistribute Rokid firmware, BlackSiren models, Broadcom BSA headers,
Broadcom example code, `libbsa.so`, or any other vendor binary.

The build expects the device owner's matching Rokid SDK on an SSH host and
compiles against its BSA public API headers. The resulting program dynamically
links to the factory `libbsa.so` already present on the user's device.

The runtime also calls the factory `rklua`, `rokidsiren.so`, BlackSiren assets,
and `prepare-bsiren` already installed by the device firmware. Users are
responsible for having the right to use those components.

Public BlackSiren source is available separately from Rokid under its own
license: <https://github.com/Rokid/BlackSiren>.

The optional browser installer uses the open-source `@yume-chan/adb`,
`@yume-chan/adb-credential-web`, `@yume-chan/adb-daemon-webusb`, and
`@yume-chan/stream-extra` packages. They are downloaded from npm during the
web build and remain subject to their respective licenses.
