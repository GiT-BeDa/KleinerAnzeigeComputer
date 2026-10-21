# Changelog

All versions run on the GeekMagic SmallTV Ultra (ESP8266, ST7789 240×240). Images are in [`firmware/`](firmware/).

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
