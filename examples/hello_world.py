#!/usr/bin/env python3
"""Hello World for KAC – the starting point for your own dashboard.

It sends two pages to a KAC display:
  1. "Hello"  – a fixed page that shows every row type once
  2. "System" – live values of the computer running this script, refreshed every 5 seconds

Only the Python standard library is used. Run it with the address and the device key of your display
(the key is shown on the display's Device page and on its setup page):

    KAC_HOST=192.168.1.50 KAC_KEY=yourkey python3 hello_world.py          # runs until Ctrl+C
    KAC_HOST=192.168.1.50 KAC_KEY=yourkey python3 hello_world.py --once   # sends once and prints the JSON

Then change the pages below and build your own dashboard from there.
"""
import json
import os
import shutil
import socket
import sys
import time
import urllib.error
import urllib.request

HOST = os.environ.get("KAC_HOST", "192.168.1.50")   # address of your KAC display
KEY = os.environ.get("KAC_KEY", "")                  # device key – keep it out of the code
INTERVAL = 5                                         # seconds between updates


# ---------------------------------------------------------------------------------------------------------
# 1. Talking to the display: one HTTP POST with JSON, the device key in the header X-Token.
#    Every POST replaces all pages. The display redraws only rows that changed, so sending often is fine.
# ---------------------------------------------------------------------------------------------------------
def send(pages, seconds_per_page=10):
    body = json.dumps({"rot": seconds_per_page, "seiten": pages}).encode()
    request = urllib.request.Request(f"http://{HOST}/api/seiten", data=body, method="POST",
                                     headers={"X-Token": KEY, "Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=10) as answer:
        return answer.read().decode()          # {"ok":true,"seiten":2}


# ---------------------------------------------------------------------------------------------------------
# 2. Building rows. A page has a title "t" (up to 15 characters) and up to 8 rows "z".
#    Row types "y":  h heading · k label/value · b labeled bar · d / u bar with arrow down/up · - empty
#    Fields:         l label · v value (shown right-aligned) · p bar 0..100 · a 1 = red (alert)
#    Text: ASCII plus "°" only, about 24 characters per row in total.
# ---------------------------------------------------------------------------------------------------------
def heading(label, value=""):            return {"y": "h", "l": label, "v": value}
def row(label, value, alert=False):      return {"y": "k", "l": label, "v": value, "a": int(alert)}
def bar(label, percent, value, alert=False):
    return {"y": "b", "l": label, "p": max(0, min(100, round(percent))), "v": value, "a": int(alert)}
def bar_down(percent, value):            return {"y": "d", "p": max(0, min(100, round(percent))), "v": value}
def bar_up(percent, value):              return {"y": "u", "p": max(0, min(100, round(percent))), "v": value}
def empty():                             return {"y": "-"}


# ---------------------------------------------------------------------------------------------------------
# 3. Page 1: a fixed demo of all row types.
# ---------------------------------------------------------------------------------------------------------
def hello_page():
    return {"t": "Hello", "z": [
        heading("Hello World", "KAC"),
        row("Label", "value"),
        row("Temperature", "21 °C"),          # the degree sign is the only non-ASCII character
        bar("CPU", 42, "42 %"),
        bar_down(70, "70 Mbit/s"),
        bar_up(25, "10 Mbit/s"),
        row("Something wrong", "red", alert=True),
        empty(),
    ]}


# ---------------------------------------------------------------------------------------------------------
# 4. Page 2: live values of this computer (Linux/macOS; on Windows some values show "-").
#    Replace these with anything you can measure: a smart plug, a weather API, your CI, a printer ...
# ---------------------------------------------------------------------------------------------------------
def memory_used_percent():
    try:                                   # Linux
        info = dict(line.split(":", 1) for line in open("/proc/meminfo"))
        total = int(info["MemTotal"].split()[0])
        available = int(info["MemAvailable"].split()[0])
        return 100 * (total - available) / total
    except (OSError, KeyError, ValueError):
        return None


def uptime_text():
    try:                                   # Linux
        seconds = int(float(open("/proc/uptime").read().split()[0]))
    except (OSError, ValueError):
        return "-"
    days, hours = seconds // 86400, seconds % 86400 // 3600
    return f"{days} d {hours} h" if days else f"{hours} h {seconds % 3600 // 60} min"


def system_page():
    try:
        load = os.getloadavg()[0] * 100 / (os.cpu_count() or 1)   # 1-minute load as % of all cores
    except (AttributeError, OSError):
        load = None
    memory = memory_used_percent()
    disk = shutil.disk_usage("/")
    disk_percent = 100 * disk.used / disk.total
    rows = [heading(socket.gethostname()[:15], time.strftime("%H:%M:%S"))]
    rows.append(bar("CPU", load, f"{load:.0f} %", alert=load > 90) if load is not None else row("CPU", "-"))
    rows.append(bar("RAM", memory, f"{memory:.0f} %", alert=memory > 90) if memory is not None else row("RAM", "-"))
    rows.append(bar("Disk", disk_percent, f"{disk_percent:.0f} %", alert=disk_percent > 90))
    rows.append(row("Free", f"{disk.free / 1e9:.0f} GB"))
    rows.append(row("Uptime", uptime_text()))
    return {"t": "System", "z": rows}


# ---------------------------------------------------------------------------------------------------------
# 5. Main loop: build the pages, send them, wait, repeat. Errors (display briefly off, Wi-Fi hiccup)
#    are printed and the loop carries on – the display keeps the last pages meanwhile.
# ---------------------------------------------------------------------------------------------------------
def main():
    if not KEY:
        sys.exit("Set KAC_KEY to the device key (shown on the display's Device page).")
    once = "--once" in sys.argv
    while True:
        pages = [hello_page(), system_page()]
        try:
            print(time.strftime("%H:%M:%S"), send(pages))
        except urllib.error.HTTPError as e:
            print("Error", e.code, "– 403 means the key is wrong" if e.code == 403 else e.read().decode())
        except OSError as e:
            print("Display not reachable:", e)
        if once:
            print(json.dumps(pages, indent=1))
            return
        time.sleep(INTERVAL)


if __name__ == "__main__":
    main()
