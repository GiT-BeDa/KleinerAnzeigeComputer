# Changelog

All versions run on the GeekMagic SmallTV Ultra (ESP8266, ST7789 240×240). Images are in [`firmware/`](firmware/).

## 0.8.10 – 2026-10-22
- Up to 5 image pages (`POST /api/medium?nr=1..5`, only one GIF); settings page with one row per slot.
- New row types: `g` big value (large bold font), `s` sparkline with up to 20 columns (`w`: values 0–100).
- Settings backup: download all settings as JSON, import them (Wi-Fi and key optional) on the settings page.
- `GET /api/status`: `medien` (per slot), `medium_aktiv` (slot number), `neustart` (reason of the last restart).
- New: `tools/kac.py`, a small library (standard library only) for your own dashboards: row/page helpers incl.
  `big` and `spark`, `Device.send()` with automatic parts, image upload, several displays, lookup by MAC.
- New examples: `examples/dashboard.py` (library, sparkline, several displays), `examples/image.sh`,
  `examples/kac-dashboard.service`; `hello_world.py` shows the new row types.
- `tools/kac_medium.py --nr` for the image slot.
- Removed: `tools/kac_dashboard.py` – it mirrored one particular home network (firewall, NAS, hypervisor). The
  repository now contains only the firmware and general tools and examples for building your own dashboards.
- 0.8.9 was a test build.

## 0.8.8 – 2026-10-22
- Image page: one GIF or JPG (up to 240×240, max. 1 MB) is shown full screen as an extra page in the rotation and
  stays after a restart. Upload via `POST /api/medium` (`X-Token`) or the settings page, remove via
  `DELETE /api/medium`. Progressive JPGs and other formats are refused with a clear message.
- GIF: the decoder memory (~23.7 KB) is reserved at boot and shared with the header graphics, so it cannot be
  fragmented; uploading a GIF restarts the device once. Bundled AnimatedGIF 2.2.3 with smaller buffers
  (`lib/AnimatedGIF/KAC.md`). JPG: decoder memory only while drawing.
- Crash guard: a crash while decoding blocks the image page after the restart (`medium_fehler: -3`).
- CPU at 160 MHz (was 80 MHz) for faster GIF decoding. The display SPI stays at 40 MHz (80 MHz left it black).
- `GET /api/status`: `medium`, `medium_aktiv`, `medium_fehler`, `gif_ms`, `gif_block`.
- `tools/kac_medium.py`: converts any GIF/PNG/JPG/WebP (file or URL) to 240×240 with one palette for all GIF frames
  and sends it to the device.
- 0.8.1–0.8.7 were test builds on one device only.

## 0.8.0 – 2026-10-21
- No fixed page limit: pages are allocated on demand, limited only by free memory (about 25–28 full pages).
- `POST /api/seiten` accepts pages in parts (`ab`, `gesamt`); answers `max_seiten` and `voll` when memory is full.
- `GET /api/status` reports `max_seiten`, `max_zeilen`, `max_bytes`.
- Dashboard `tools/kac_dashboard.py` splits large page sets into parts automatically.

## 0.7.3 – 2026-10-21
- Up to 4 pages (was 3).
- Dashboard `tools/kac_dashboard.py`: optional Proxmox VE page via SSH (`PVE_SSH`); firewall page tolerates a
  missing temperature series (e.g. OPNsense in a VM); counter jumps no longer inflate blocked packets/h.

## 0.7.2 – 2026-10-14
- Device page reordered: Wi-Fi, signal, IP, key first, then firmware, clock, uptime, data.

## 0.7.1 – 2026-10-14
- The built-in Device page always shows the device key, so a forgotten key can be read on the display
  (previously only until the first data arrived).

## 0.7.0 – 2026-10-14
- Settings page `http://<device>/` covers everything: status, brightness schedule, time zone, 12-hour clock,
  date format (DD/MM/YYYY, DD.MM.YYYY, YYYY-MM-DD, MM/DD/YYYY), own NTP server, seconds per page, Wi-Fi with
  network search, device key, restart and factory reset.
- Wi-Fi changes are tried first: if the device cannot connect within 20 s, it returns to the previous network.

## 0.6.0
- Time zone as a selection list (20 common zones plus custom POSIX TZ), brightness as sliders, night hours as
  time selection with an on/off switch.

## 0.5.x
- Brightness schedule in the device (day/night %, night hours); it dims by itself, also without a dashboard.
- Settings page in normal operation (HTTP Basic auth: admin / device key).
- Only a data update with at least one page hides the key on the Device page.

## 0.4.x
- Built-in pages without dashboard data (never, or for 10 min): **Device** (version, chip ID, IP, Wi-Fi, signal,
  clock sync, uptime, data age, key until first data) and **Guide** (QR code to the build guide).
- `POST /api/schluessel` changes the device key; random keys have 8 characters.

## 0.3.x
- **Per-device key** instead of a key built into the image: images contain no secret and can be published.
  Any own key of 1–32 printable characters.
- Setup gesture: 3 short power cycles instead of 5.
- Ready-made images in `firmware/`; installable directly from the GeekMagic factory firmware.

## 0.2.x
- QR code to the build guide; RGB332-safe colors; right text margin; host name
  `KleinerAnzeigeComputer-<chip ID>`, setup Wi-Fi `KAC-<chip ID>`; English device texts; configurable time zone.

## 0.1.x
- First version: Wi-Fi client only, setup via power-cycle gesture with a random password on the display,
  flicker-free page drawing, NTP clock, firmware update over Wi-Fi, labeled bars.
