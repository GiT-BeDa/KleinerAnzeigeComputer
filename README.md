# KAC – KleinerAnzeigeComputer

Custom firmware for the **GeekMagic SmallTV Ultra** that turns the small cube display into a **status display
for your own dashboards**. A program on your network sends pages made of rows (headings, label/value pairs,
bar gauges), and the device shows them in rotation: flicker-free, with its own clock, and without an open
access point.

*KleinerAnzeigeComputer* is German for "little display computer". The device UI uses the short name **KAC**.
On the network each device calls itself `KleinerAnzeigeComputer-<chip ID>` (host name); its setup Wi-Fi is
`KAC-<chip ID>`. The chip ID is the last three bytes of the MAC address.

**The story behind it, with photos:** [KAC: A small cube becomes a status display for network, firewall and NAS](https://www.bk99.de/en/blog/2026/kac/)
on bk99.de ([German version](https://www.bk99.de/blog/2026/kac/)). The device's built-in guide page links there via QR code.

<p align="center"><img src="docs/images/two-devices.jpg" width="600" alt="Two SmallTV Ultra running KAC: a test page and the NAS page"></p>

**Contents**

1. [Overview](#1-overview)
2. [Hardware](#2-hardware)
3. [How it works](#3-how-it-works)
4. [Installation](#4-installation)
5. [Wi-Fi setup](#5-wi-fi-setup)
6. [API](#6-api)
7. [Building a dashboard](#7-building-a-dashboard)
8. [Building and updating the firmware](#8-building-and-updating-the-firmware)
9. [Troubleshooting](#9-troubleshooting)
10. [Limits](#10-limits)
11. [License](#11-license)

---

## 1. Overview

```
 ┌───────────────────────┐   POST /api/seiten (JSON, every n s)  ┌───────────────────────────┐
 │ Your program          │ ────────────────────────────────────▶ │ KAC (SmallTV Ultra)       │
 │ (Python, shell, ...)  │        header X-Token                 │ - stores ~25 pages (RAM)  │
 │ collects metrics      │ ◀──────────────────────────────────── │ - rotates every rot s     │
 └───────────────────────┘        {"ok":true,"seiten":3}         │ - redraws changes only    │
                                                                 │ - clock/date via NTP      │
                                                                 └───────────────────────────┘
```

| Feature | Implementation |
|---|---|
| No access point in normal operation | The device is a Wi-Fi client only. |
| Setup | Cut the power 3 times in a row → Wi-Fi `KAC-<chip ID>` with a **random password shown only on the display**. |
| Flicker-free | Each row is fully drawn in memory and transferred in one piece; only rows that changed are redrawn. |
| Clock and date | Set by the device itself via NTP; time zone configurable (default Europe/Berlin). |
| Protected | Data and firmware updates require a per-device key, created by the device itself. |
| Ready-made image | Download, upload via the factory update page, done – or build it yourself. |
| Settings page | `http://<device>/` (user `admin`, password = device key): status, brightness, time zone, clock and date format, NTP, page rotation, Wi-Fi, key, restart, factory reset |
| Over-the-air updates | `http://<device>/update` |

The API field names are German (`seiten` = pages, `z` = rows, …); they are explained in [chapter 6](#6-api).

## 2. Hardware

GeekMagic SmallTV Ultra (also sold as "SmallTV"): ESP8266 (ESP-12F, 4 MB flash, about 80 KB RAM), 240×240 IPS
display with an ST7789 controller. The USB-C port only supplies power; a serial port is only available via
solder pads inside the case.

| Function | GPIO | Note |
|---|---|---|
| SPI clock | 14 | hardware SPI, SPI mode 3, 40 MHz |
| SPI data (MOSI) | 13 | |
| Chip select | – | not connected |
| Display DC | 0 | |
| Display reset | 2 | |
| Backlight | 5 | PWM, **inverted** (LOW = bright) |

Color order BGR, colors inverted. Pinout source: ESPHome device page "GeekMagic Ultra".

## 3. How it works

### 3.1 Boot sequence

```
Power on
   │
   ├─ boot counter in flash +1
   │     └─ 3 boots in a row, each shorter than 10 s?  ──yes──▶ setup mode (chapter 5)
   │
   ├─ no Wi-Fi stored yet?                             ──yes──▶ setup mode
   │     (on the very first boot KAC takes over a Wi-Fi network stored in the
   │      ESP8266 system area, if there is one)
   │
   └─ normal operation
         ├─ no device key yet? create a random one (8 characters) and store it
         ├─ access point permanently off (also in the ESP8266 system settings)
         ├─ connect to the stored Wi-Fi, then reconnect automatically
         ├─ time via NTP (pool.ntp.org, time.cloudflare.com)
         └─ web server: /api/seiten, /api/status, /update
```

Once the device has been running for more than 10 seconds, the boot counter is reset to 0, so a normal restart
never triggers setup.

### 3.2 Screen layout

```
 y=0   ┌────────────────────────────────────┐
       │ 23:05 ●                 12/10/2026 │  header: clock, status dot, date,
       │                           Internet │          page title (orange)
 y=35  ├────────────────────────────────────┤
 y=38  │ row 0                              │  8 rows of 25 px each
       │ row 1                              │
       │ ...                                │
 y=213 │ row 7                              │
 y=240 └────────────────────────────────────┘
```

- **Status dot (red)** next to the clock: no Wi-Fi, or no new data for 3 minutes.
- **Built-in pages** without dashboard data: until the first data arrives, and again after 10 minutes without
  data, the device rotates two pages of its own instead of stale values:
  - **Device**: Wi-Fi, signal (dBm, red below −85), IP address, the **device key** (always, so a forgotten key
    can be read here), firmware version and chip ID, clock sync, uptime and time since the last data.
  - **Guide**: a QR code to the build guide at https://www.bk99.de/blog/2026/kac/ (German, English version linked).
- The date format is DD/MM/YYYY.

| Info page (no data yet, version 0.1.x) | Setup screen |
|---|---|
| <img src="docs/images/info-page.jpg" width="280" alt="KAC info page: clock, date, version, Wi-Fi, IP address, signal"> | <img src="docs/images/setup-screen.jpg" width="280" alt="KAC setup screen with Wi-Fi name, random password and http://192.168.4.1/"> |

*Photos of early versions (0.1.x): since then the device texts are English, the info page became the Device and Guide pages,
the setup Wi-Fi is called `KAC-<chip ID>`, and the empty part of the bars is grey instead of olive.*

### 3.3 Row types

| `y` | Appearance | Fields |
|---|---|---|
| `h` | **Heading**: label bold orange on the left, value white on the right | `l`, `v`, `a` |
| `k` | **Label/value**: label grey on the left, value white on the right | `l`, `v`, `a` |
| `d` | **Down bar**: blue ▼ arrow, bar, value on the right (e.g. download) | `p`, `v`, `a` |
| `u` | **Up bar**: orange ▲ arrow, bar, value on the right (e.g. upload) | `p`, `v`, `a` |
| `b` | **Labeled bar**: label on the left, blue bar, value on the right (e.g. CPU) | `l`, `p`, `v`, `a` |
| `g` | **Big value**: label grey on the left, value in a large bold font on the right (readable from a distance) | `l`, `v`, `a` |
| `s` | **Sparkline**: label (up to 7 characters), up to 20 columns (oldest left, newest right), value on the right | `l`, `w`, `v`, `a` |
| `-` | empty row | – |

- A value that does not fit next to its label or bar is drawn in a smaller font, and cut with `..` if it still does
  not fit (since 0.8.11); the label always stays readable.
- `l` = label, `v` = value, `p` = bar fill 0–100 (percent), `w` = list of values 0–100 for `s` (the last 20 are kept),
  e.g. `{"y":"s","l":"CRC/min","v":"1,2","w":[5,10,8,20,35]}`; with `a` = 1 the newest column is red
- `a` = 1 → **alert**: the value (for headings also the label) or the bar turns **red**

### 3.4 Flicker-free drawing

The device remembers what each of the 8 rows shows. About four times per second it compares the requested
content with what is on screen:

1. Unchanged rows are **not** touched.
2. A changed row is drawn completely into an off-screen buffer (sprite, 8-bit color) and then copied to the
   display **in one piece**. There is no visible clearing.
3. The header is only redrawn when the time, date, title or status changes.

You can therefore send data very often (tested: every second) without any flicker.

### 3.5 Page rotation

As many pages as fit into memory, about **25–28** with 8 full rows each (`max_seiten` in `GET /api/status`).
The firmware has no fixed page limit; a new dashboard never needs new firmware. Which page is shown depends on the wall clock:
`page = (Unix time / rot) mod count`. With `rot = 30` and 2 pages, page 1 appears on the full minute and page 2
on the half minute. Your program does not have to control the rotation; it just sends all pages.
A stored image (chapter 6, `POST /api/medium`) is one more page at the end of the rotation.

### 3.6 Brightness

The device dims itself at night, also without any dashboard: brightness by day and at night and the night
hours (local time, see the time zone) are settings, by default 90 %, 10 % and 23:00–07:00. Set them on the setup
page or with the API fields `hell_tag`, `hell_nacht`, `nacht_von`, `nacht_bis`; they are written to flash only when
they change, so a dashboard can send them with every update. A brightness sent as `b` overrides the schedule as
long as the dashboard keeps sending (up to 10 minutes after the last data).

### 3.7 Security

| What | Protection |
|---|---|
| Wi-Fi in normal operation | client only, no access point |
| Setup Wi-Fi | only after the power-cycle gesture or if no Wi-Fi is stored; WPA2 with a 10-character random password shown only on the display; ends after 15 minutes |
| Device key | created randomly by each device on first start, stored in flash; shown on the setup screen, on the setup page (where you can set your own, 1–32 printable characters) and on the built-in Device page; changeable via `POST /api/schluessel` with the current key |
| Sending data | header `X-Token` with the device key, otherwise `403` |
| Firmware update | HTTP Basic auth `admin` / device key, otherwise `401` |
| `GET /api/status` | no authentication, contains no secrets |

The connection is plain HTTP, so the device belongs in a separate network (IoT VLAN). The Wi-Fi password is
stored in the device's flash; anyone with physical access to the device can read it.

## 4. Installation

You can either **flash the ready-made image** or **build the firmware yourself** (chapter 8). Both give the
same result; the image contains no secret, every device creates its own key.

| Starting point | Path |
|---|---|
| **GeekMagic factory firmware** | upload the image via its update page, see below |
| **KAC already installed** | `/update` with user `admin` and the device key, see chapter 8 |
| Device no longer boots | serial: open the case, hold GPIO0 to GND at power-on, `esptool.py write_flash 0x0 firmware.bin` |

### 4.1 Flash the ready-made image (no compiler needed)

1. Download the latest image from [`firmware/`](firmware/), e.g.
   [kac-0.8.11.bin](https://github.com/GiT-BeDa/KleinerAnzeigeComputer/raw/main/firmware/kac-0.8.11.bin), and check its MD5
   (`a59a2958512506b1c1ab8f4d3ae35363` for 0.8.11, all values in [`firmware/README.md`](firmware/README.md)).
2. Find the device's address in your router (the factory firmware also shows it on the display) and open
   `http://<device>/update`.
3. Choose the file in the **firmware** field (not "filesystem") and click **Update**, or use curl:
   ```sh
   curl -F "firmware=@kac-0.8.11.bin" http://<device>/update
   # → "Update Success! Rebooting..."
   ```
4. The device restarts with KAC and shows **"Setup"**: continue with chapter 5, steps 2–5. Write down the
   **device key** shown there; your dashboard needs it.

Tested with factory firmware Ultra-V9.0.51. It works because KAC is built exactly like the factory image
(Arduino ESP8266 core, same eboot bootloader, DIO, 4 MB, 40 MHz). The image has to fit into the free space next
to the factory firmware, roughly 500 KB; KAC needs about 390 KB. If the upload is rejected
("Not Enough Space"), nothing has changed.

The factory firmware keeps its Wi-Fi settings in its own file system, which KAC does not read, so you enter
the Wi-Fi once more. KAC does not touch that file system either: the factory firmware including its settings
can be restored later by uploading the factory image (available from GeekMagic on GitHub) via KAC's `/update`.

### 4.2 Build it yourself

See chapter 8. No key is needed for building.

## 5. Wi-Fi setup

1. **Disconnect and reconnect the power 3 times in a row.** Let it run for a few seconds each time, but
   **less than 10 seconds**. (Not needed right after installing: without stored Wi-Fi, setup starts by itself.)
2. The display shows **"Setup"**, the Wi-Fi name `KAC-<chip ID>` (e.g. `KAC-4670fa`), a **password** and the
   **device key**.
3. Connect to this Wi-Fi and open `http://192.168.4.1/`.
4. Pick your network from the list (or type its name) and enter its password. Optionally set the time zone as
   a POSIX TZ string (default `CET-1CEST,M3.5.0,M10.5.0/3` = Europe/Berlin), the brightness by day/at night
   with the night hours, and your own **device key**
   (1–32 printable characters; empty keeps the current one). **Save**. To change only the time zone or the
   key, leave the Wi-Fi password empty: the stored one is kept for the same network.
5. The device restarts and connects. The access point is off again. While no dashboard data arrives, the
   built-in Device page shows the device key as well.

Without input, setup ends after **15 minutes** with a restart. The setup page also offers the firmware update as
a rescue path in case of broken firmware.

## 6. API

### `POST /api/seiten`

| | |
|---|---|
| Headers | `X-Token: <device key>`, `Content-Type: application/json` |
| Body | JSON, at most 4096 bytes |

```jsonc
{
  "b": 90,                       // brightness 0..100, overrides the schedule   (optional)
  "hell_tag": 90, "hell_nacht": 10,      // schedule: brightness by day/at night   (optional, stored on change)
  "nacht_von": 23, "nacht_bis": 7,       //           night hours, local time
  "rot": 30,                     // seconds per page, 5..255        (optional, default 30)
  "tz": "CET-1CEST,M3.5.0,M10.5.0/3",  // POSIX time zone             (optional, stored only when changed)
  "ab": 0, "gesamt": 2,          // only when sending in parts, see below   (optional)
  "seiten": [                    // pages
    {
      "t": "Internet",           // page title, up to 15 characters
      "z": [                     // rows, up to 8, top to bottom
        {"y": "h", "l": "Fiber", "v": "8 / 10 ms"},
        {"y": "d", "p": 12, "v": "12 Mbit/s"},
        {"y": "u", "p": 3,  "v": "1.2 Mbit/s"},
        {"y": "-"},
        {"y": "k", "l": "Clients 34", "v": "VPN 25 ms", "a": 1},
        {"y": "b", "l": "CPU", "p": 18, "v": "18 %"}
      ]
    }
  ]
}
```

| Field | Meaning |
|---|---|
| `b` | brightness (overrides the schedule) |
| `hell_tag`, `hell_nacht`, `nacht_von`, `nacht_bis` | brightness schedule: day %, night %, night from/to hour (chapter 3.6) |
| `rot` | rotation interval |
| `tz` | time zone (POSIX TZ string) |
| `seiten` | pages |
| `ab`, `gesamt` | first page index of this request, total number of pages (sending in parts) |
| `t` | page title |
| `z` | rows (*Zeilen*) |
| `y` | row type (chapter 3.3) |
| `l`, `v`, `p`, `a` | label, value, percent, alert |

| Response | Meaning |
|---|---|
| `200 {"ok":true,"seiten":n,"max_seiten":m}` | accepted, `n` pages stored, room for about `m` |
| `200 {…,"voll":true}` | memory full: the pages that fit are kept, the rest is dropped |
| `400 {"error":"ab"}` | `ab` larger than the number of stored pages |
| `400 {"error":"size"}` | empty or larger than 4096 bytes |
| `400 {"error":"json","detail":...}` | invalid JSON |
| `403 {"error":"token"}` | key missing or wrong |

Every update **replaces all pages**. Missing rows are empty.

**Many pages:** one request carries at most 4096 bytes (about 6 full pages). Send more in parts: the first
request with `"ab":0,"gesamt":N`, the next with `"ab":` the number of pages already sent, and so on. Pages
from `gesamt` on are dropped; pages not yet resent keep their previous content, so the display never runs short
in between. `tools/kac.py` (`Device.send()`) does this automatically.

### `GET /api/status`

```json
{"name":"KleinerAnzeigeComputer-4670fa","version":"0.2.1","uptime":3600,"ssid":"MyWiFi","rssi":-45,"heap":33800,
 "seiten":3,"seite":1,"daten_alter_s":4,"zeit":1791838000,"tz":"CET-1CEST,M3.5.0,M10.5.0/3",
 "hell":10,"hell_tag":90,"hell_nacht":10,"nacht_von":23,"nacht_bis":7,"max_seiten":27,"max_zeilen":8,"max_bytes":4096}
```

`daten_alter_s` = seconds since the last update (−1: never), `seite` = page currently shown (−1: built-in Device page, −2: built-in Guide page),
`zeit` = device time (Unix). Since 0.8.8/0.8.10 also `medien`, `medium_aktiv`, `medium_fehler`, `gif_ms`, `gif_block`
(image pages, see `POST /api/medium`) and `neustart` (reason of the last restart, e.g. `Software/System restart` or an
exception with its address – useful for bug reports).

### `POST /api/schluessel`

Changes the device key. Header `X-Token: <current key>`; body `{"key":"<new key>"}` (1–32 printable characters,
no spaces) or `{}` for a new random key. Answer `{"ok":true,"key":"<new key>"}`; the new key applies at once to the
data API and the firmware update.

### `GET /` – settings page

Web page for people (HTTP Basic auth: user `admin`, password = device key). Sections:

| Section | Settings |
|---|---|
| Status | device name, firmware, address, Wi-Fi and signal, uptime, data age, free RAM |
| Brightness | day and night sliders, "Dim at night" with night hours |
| Time zone | 20 common zones or a custom POSIX TZ string |
| Clock and pages | 12-hour clock, date format, own NTP server, seconds per page (a dashboard may override it) |
| Wi-Fi | network (with "search networks") and password; a new network is tried first, the device returns to the old one if it cannot connect within 20 s |
| Device key | change the key |
| Image pages | upload a GIF or JPG per slot (1–5), remove it |
| Backup | download all settings as JSON (contains the Wi-Fi password and the device key), import such a file – Wi-Fi and key only if ticked (e.g. to set up a second device) |
| Maintenance | restart, factory reset (erases Wi-Fi, key and settings, then setup starts), firmware update |

Saving applies at once without a restart and writes to flash only what changed.

### `POST /api/medium` – image page (GIF or JPG)

Stores up to **5 pictures or animations** (slots `nr=1..5`, only one of them a GIF) and shows each **full screen
as an extra page at the end of the rotation** (also after a restart). Multipart upload, field name `medium`,
header `X-Token`; the type is taken from the file content. A new upload replaces the image in its slot.

```bash
curl -H "X-Token: yourkey" -F "medium=@cat.gif" "http://192.168.1.50/api/medium?nr=1"
# {"ok":true,"nr":1,"medium":"gif","restart":true}
curl -H "X-Token: yourkey" -F "medium=@photo.jpg" "http://192.168.1.50/api/medium?nr=2"
curl -H "X-Token: yourkey" -X DELETE "http://192.168.1.50/api/medium?nr=2"   # remove slot 2
curl -H "X-Token: yourkey" -X DELETE http://192.168.1.50/api/medium          # remove all images
```

| Limit | Value |
|---|---|
| Size | up to 240×240 px (larger JPGs are scaled down by 2, 4 or 8; wider GIFs are refused) |
| JPG | baseline only; progressive JPGs are refused with `422` and a hint |
| File | at most 1 MB per image, up to 5 images, only one GIF (a second GIF is refused with `422`) |
| GIF speed | a full 240×240 frame takes about 60–140 ms; one palette for all frames is faster |

**Easiest way:** [`tools/kac_medium.py`](tools/kac_medium.py) (needs Pillow) turns any GIF/PNG/JPG/WebP file or URL
into a file that fits and sends it:

```bash
KAC_KEY=yourkey python3 tools/kac_medium.py https://example.org/animation.gif --send 192.168.1.50
```

**GIF and memory:** the GIF decoder needs about 23.7 KB in one piece. A device with a stored GIF reserves that
block at boot and shares it with the header graphics, so after uploading a GIF the device **restarts once**
(`"restart":true`). On the GIF page the clock is not shown. The reservation leaves about 12 KB free – enough for a
few dashboard pages; for a device with many pages prefer a JPG, which needs no reservation.

**Crash guard:** if the device crashes while decoding the image, the image page is blocked after the restart
(`medium_fehler: -3` in `GET /api/status`) until a new image is uploaded or it is removed.

Status fields: `medien` (per slot `gif`, `jpg` or empty, `!` = blocked, e.g. `gif,jpg,,,`), `medium_aktiv` (slot on
screen, 0 = none), `medium_fehler`
(0 ok, -1 no memory, -3 blocked after a crash, -4 GIF waits for the restart, >0 decoder error), `gif_ms` (time
of the last GIF frame), `gif_block` (reserved bytes).

### `GET|POST /update`

Firmware update (chapter 8).

## 7. Building a dashboard

A dashboard is a small program that regularly collects values, builds pages from them and sends them to the
device:

```
every n seconds:
   collect values  →  build pages as JSON  →  POST /api/seiten  →  on error: carry on
```

**New here? Start with [`examples/hello_world.py`](examples/hello_world.py)** (chapter 7.2) and turn it into your
own dashboard. For more than a few pages or several displays use the small library
[`tools/kac.py`](tools/kac.py) (chapter 7.5); pictures and animations go on image pages (chapter 7.6).

### 7.1 Designing pages

Each page has **8 rows**. The font is proportional. As a rule of thumb, **label + value together fit about
24 characters** per row, and bar rows leave **about 10 characters** for the value. Texts are cut after
27 bytes.

Good practice:

- **Heading (`h`) as a group header**, with the most important value on the right (e.g. temperature, latency).
- **Bars (`d`/`u`/`b`)** for anything with an upper limit (bandwidth, CPU, disk usage). Compute `p` yourself:
  `round(value * 100 / maximum)`.
- **Label/value (`k`)** for plain figures; **big value (`g`)** for the one figure that must be readable from across the room.
- **Sparkline (`s`)** for a trend: keep the last values in your program and send them every time (up to 20).
- **`a: 1`** only for real problems, so that red stands out.
- **No repetition** across pages. The clock is always in the header anyway.
- **Character set:** ASCII plus `°` only. Umlauts, `·`, `–`, arrows and emojis are not displayed.

### 7.2 Start here: `examples/`

| File | What it does |
|---|---|
| [`hello_world.sh`](examples/hello_world.sh) | one `curl` call that sends a single page – the quickest test whether address and key are right |
| [`hello_world.py`](examples/hello_world.py) | Python, standard library only, commented step by step: a **Hello** page with every row type and a **System** page with live CPU, RAM, disk and uptime of your computer, refreshed every 5 seconds |
| [`dashboard.py`](examples/dashboard.py) | the same idea with `tools/kac.py`: big value, sparkline with CPU history, several displays at once |
| [`image.sh`](examples/image.sh) | put a GIF or JPG on an image page with `curl` |
| [`kac-dashboard.service`](examples/kac-dashboard.service) | systemd unit to run your dashboard as a service |

```sh
KAC_HOST=192.168.1.50 KAC_KEY=yourkey sh examples/hello_world.sh
KAC_HOST=192.168.1.50 KAC_KEY=yourkey python3 examples/hello_world.py          # until Ctrl+C
KAC_HOST=192.168.1.50 KAC_KEY=yourkey python3 examples/hello_world.py --once   # send once, print the JSON
```

The key is shown on the display's **Device** page and on its setup page. `{"ok":true,...}` means it works;
`403` means the key is wrong.

To build your own dashboard, copy `hello_world.py` and replace `system_page()` with whatever you want to show:
a smart plug, a weather API, your CI pipeline, a printer, a server. The helper functions `heading()`, `row()`,
`bar()`, `bar_down()`, `bar_up()` and `empty()` build the rows; `send()` does the HTTP part.

### 7.3 Update rate

| What | Recommendation |
|---|---|
| Fast values (throughput, CPU) | every 1–10 s, no flicker |
| Slow values (temperatures, SMART, counters) | query every 30–60 s and cache, but include them in every update |
| Program stops | after 3 minutes without data the red status dot appears; the last values stay on screen |

The device writes **nothing to flash** when receiving data, so frequent updates cause no wear.

### 7.4 Running it as a service (systemd)

[`examples/kac-dashboard.service`](examples/kac-dashboard.service) runs `examples/dashboard.py`: copy the repository
to `/opt/kac`, put `KAC_HOST=…` and `KAC_KEY=…` into `/etc/kac/kac.env` (mode 600), then
`systemctl enable --now kac-dashboard.service`. For your own script the same pattern applies: credentials in a file
with mode 600, `Restart=always`, `ProtectSystem=strict`.

### 7.5 Library: `tools/kac.py`

One file, Python standard library only. Copy it next to your script (or add `tools/` to `PYTHONPATH`):

```python
import kac

tv = kac.Device("192.168.1.50", key="yourkey")
tv.send([
    kac.page("Home", [
        kac.heading("Living room", "21:30"),
        kac.big("Temperature", "21.5 °C"),
        kac.spark("Power", [120, 340, 310, 600, 580], "580 W"),   # scaled to the largest value
        kac.bar("Battery", 72, "72 %"),
        kac.row("Window", "open", alert=True),
    ]),
    kac.page("Garden", [kac.big("Rain", "2 mm")]),
], seconds_per_page=20)
```

| Function | Purpose |
|---|---|
| `heading`, `row`, `big`, `bar`, `down`, `up`, `spark`, `empty`, `page` | build rows and pages (chapter 3.3) |
| `Device(host, key)` | one display; `send(pages, seconds_per_page=None, **settings)` replaces all pages and splits large page sets into several requests |
| `Device.by_mac(mac, key)` | finds the display by its MAC in this computer's neighbour table (`ip neigh` / `arp -an`) – only works in the **same network segment**; behind a router use the address or a DHCP reservation |
| `upload_image(file, nr)`, `delete_image(nr)` | image pages (chapter 7.6) |
| `status()` | `GET /api/status` as a dict |

**Several displays:** create one `Device` per display and send each its own pages. Errors are raised as exceptions;
catch them per display so one unreachable display does not stop the others (see `examples/dashboard.py`).

### 7.6 Pictures and animations

Up to 5 GIFs or JPGs per display, each an extra full-screen page at the end of the rotation (chapter 6,
`POST /api/medium`). The easiest way is [`tools/kac_medium.py`](tools/kac_medium.py) (needs Pillow): it takes any
GIF/PNG/JPG/WebP file or URL, cuts or fits it to 240×240, gives a GIF one palette for all frames (faster, smaller),
writes a baseline JPG for still pictures and sends it:

```bash
KAC_KEY=yourkey python3 tools/kac_medium.py holiday.jpg --send 192.168.1.50 --nr 2
KAC_KEY=yourkey python3 tools/kac_medium.py https://example.org/animation.gif --send 192.168.1.50 --nr 1
KAC_KEY=yourkey python3 tools/kac_medium.py --delete --send 192.168.1.50 --nr 2
```

Or on the settings page `http://<device>/` → *Image pages*. Only one GIF per display: the GIF decoder needs about
23.4 KB in one piece, which the device reserves at boot (so uploading the first GIF restarts it once) – after that
only a few dashboard pages fit next to it. JPGs need no reservation.

## 8. Building and updating the firmware

```sh
pip install platformio
pio run                                   # image: .pio/build/smalltv/firmware.bin
curl -u "admin:<device key>" -F "firmware=@.pio/build/smalltv/firmware.bin" http://<device>/update
```

- The build needs no key; every device creates its own on first start and keeps it across updates.
- Optional, for your own fleet: `KAC_TOKEN=<key> pio run` builds an image that uses `<key>` as the **initial**
  key on devices that have none yet. Such an image contains the key – do not publish it.
- The Wi-Fi settings, time zone and key survive updates; they live in a separate flash sector.
- Version: `-DKAC_VERSION` in `platformio.ini`. Ready-made images go to `firmware/` with their checksums.

## 9. Troubleshooting

| Observation | Cause / remedy |
|---|---|
| Red dot next to the clock | no Wi-Fi or no data for 3 min: check your program and the network, `GET /api/status` |
| Clock shows `--:--` | no NTP response yet; allow DNS/NTP in the network |
| Device/Guide pages instead of your pages | no data for 10 min (or never): program not running, wrong key (`403`) or wrong address |
| `403` when sending | `X-Token` wrong or missing |
| Device key forgotten | power-cycle gesture (chapter 5): the setup screen and page show it, and you can set a new one |
| `400 json` | invalid JSON (e.g. comments included) |
| Characters missing | only ASCII and `°` are displayed |
| Text cut off or overlapping | shorten the text (chapter 7.1) |
| Device unreachable | setup via the power-cycle gesture (chapter 5), which also offers the firmware update |
| Keeps rebooting | if the firmware crashes within 10 s each time, 3 crashes in a row open the setup; upload working firmware there. Otherwise: power-cycle gesture |

## 10. Limits

- Pages limited by RAM (about 25–28), 8 rows each, texts up to 27 bytes, titles up to 15 bytes, JSON up to 4 KB per
  request (more pages in parts).
- Pages have no graphics and no free choice of colors (normal/alert only); one GIF or JPG can be shown as an extra
  full-screen page (chapter 6, `POST /api/medium`).
- No HTTPS (the ESP8266 has too little memory for it).
- A stored GIF reserves ~23.4 KB at boot; a device with a GIF then holds only a few dashboard pages.

## 11. License

GPL-3.0, see [LICENSE](LICENSE). Libraries used: TFT_eSPI (Bodmer, FreeBSD/MIT parts), ArduinoJson (MIT),
Arduino core for ESP8266 (LGPL-2.1). Pinout from the ESPHome device page "GeekMagic Ultra".
