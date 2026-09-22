# ROM delivery modes

FC ROM Vomitter accepts a ROM through three independent transports. Every
transport calls the same controller API, so iNES validation, inactive-slot
commit, flash CRC verification, SRAM readback, and console isolation remain
mandatory.

| Mode | Direction | Default | Intended use |
|---|---|---|---|
| SoftAP browser | phone/PC → cartridge | Enabled | Normal local use at `http://192.168.4.1` |
| USB-C framed upload | PC → cartridge | Enabled | Fast bench transfer without Wi-Fi |
| HTTPS cloud pull | cloud → cartridge | Disabled | Unattended retrieval while USB-powered |

## USB-C direct upload

Install [pyserial](https://pyserial.readthedocs.io/) and run:

```powershell
py -m pip install pyserial
py scripts/upload_rom_usb.py COM10 path\to\game.nes
```

On Linux, the port normally resembles `/dev/ttyACM0`. The uploader sends a
16-byte little-endian header followed by the original iNES bytes:

```text
offset  size  field
0       4     ASCII RVUP
4       1     protocol version = 1
5       1     flags = 0
6       2     header size = 16
8       4     payload length
12      4     CRC32 of the original iNES payload
```

The device replies with one line beginning `RVOK` or `RVER`. Bad magic,
unsupported versions, oversized/truncated payloads, CRC errors, and invalid
iNES files are rejected before flash commit. Close the uploader's serial port
before using PlatformIO to flash firmware.

## HTTPS cloud pull

Cloud mode is compile-time opt-in so public source never contains credentials.
Create the local ignored `firmware/sdkconfig.esp32-s3` through menuconfig:

```powershell
pio run -d firmware -e esp32-s3 -t menuconfig
```

Under **ROM Vomitter FC**, enable **HTTPS cloud ROM polling** and set:

- station Wi-Fi SSID and password;
- an HTTPS URL returning the raw `.nes` bytes with HTTP status 200;
- an optional bearer token;
- polling interval and request timeout.

The ESP-IDF certificate bundle validates HTTPS. After joining the station,
the cartridge obtains trusted time from `pool.ntp.org` before its first TLS
request so certificate validity dates are checked correctly. Plain HTTP is
rejected unless the explicitly unsafe development switch is enabled. The
original SoftAP remains active in AP+station mode as a recovery path.

By default, a cloud image is installed only while console power is absent.
This allows a USB-powered cartridge to update without freezing a running
Famicom. Enabling live-console reload is an explicit opt-in. A normalized
image identical to the active image is ignored, so polling does not consume
flash-slot sequence numbers or cause repeated SRAM reloads.

Credentials and bearer tokens are compiled into the local firmware image.
Do not commit `sdkconfig.*`, share the resulting binary publicly, or use a
high-value long-lived token. Prefer a narrowly scoped, revocable read token.

## Status

`GET /api/status` reports `usb_upload`, `cloud_pull`, and `cloud_status` in
addition to the existing image and console fields.
