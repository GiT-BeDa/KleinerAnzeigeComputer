#!/usr/bin/env python3
"""Example dashboard with tools/kac.py: one or more KAC displays, live values of this computer with history.

    KAC_HOST=192.168.1.50 KAC_KEY=yourkey python3 examples/dashboard.py
    KAC_MAC=48:3f:da:12:34:56 KAC_KEY=yourkey python3 examples/dashboard.py      # find the display by its MAC

Several displays: KAC_HOST="192.168.1.50,192.168.1.51" with KAC_KEY="key1,key2" – every display gets the same
pages here; give each its own pages by building a list per Device. Replace the measurements with your own.
"""
import os
import shutil
import sys
import time
from collections import deque

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import kac   # noqa: E402

INTERVAL = 10                      # seconds between updates
history = deque(maxlen=20)         # last 20 CPU values for the sparkline


def cpu_percent():
    try:
        return os.getloadavg()[0] * 100 / (os.cpu_count() or 1)
    except (AttributeError, OSError):
        return 0.0


def pages():
    cpu = cpu_percent()
    history.append(cpu)
    disk = shutil.disk_usage("/")
    used = 100 * disk.used / disk.total
    return [
        kac.page("Computer", [
            kac.heading(os.uname().nodename[:15] if hasattr(os, "uname") else "PC", time.strftime("%H:%M")),
            kac.big("CPU", "%.0f %%" % cpu, alert=cpu > 90),
            kac.spark("CPU", list(history), "%.0f %%" % cpu, maximum=100),
            kac.bar("Disk", used, "%.0f %%" % used, alert=used > 90),
            kac.row("Free", "%.0f GB" % (disk.free / 1e9)),
        ]),
    ]


def main():
    keys = os.environ.get("KAC_KEY", "").split(",")
    if not keys[0]:
        sys.exit("Set KAC_KEY (and KAC_HOST or KAC_MAC).")
    if os.environ.get("KAC_MAC"):
        devices = [kac.Device.by_mac(m, k) for m, k in zip(os.environ["KAC_MAC"].split(","), keys)]
    else:
        devices = [kac.Device(h, k) for h, k in zip(os.environ.get("KAC_HOST", "192.168.1.50").split(","), keys)]
    while True:
        p = pages()
        for d in devices:
            try:
                print(time.strftime("%H:%M:%S"), d.host, d.send(p), flush=True)
            except Exception as e:     # display briefly off: try again next time
                print(time.strftime("%H:%M:%S"), d.host, "error:", e, flush=True)
        time.sleep(INTERVAL)


if __name__ == "__main__":
    main()
