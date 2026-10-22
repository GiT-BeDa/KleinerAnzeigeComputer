#!/usr/bin/env python3
"""kac.py – small helper library for your own KAC dashboards (Python standard library only).

Copy this file next to your script (or add tools/ to PYTHONPATH) and:

    import kac
    tv = kac.Device("192.168.1.50", key="yourkey")          # or kac.Device.by_mac("48:3f:da:12:34:56", key=...)
    tv.send([
        kac.page("Home", [
            kac.heading("Living room", "21:30"),
            kac.big("Temperature", "21.5 °C"),
            kac.spark("Power", [10, 40, 35, 80, 60], "600 W"),
            kac.bar("Battery", 72, "72 %"),
            kac.row("Window", "open", alert=True),
        ]),
    ])
    tv.upload_image("photo.jpg", nr=2)       # image page (prepare files with tools/kac_medium.py)
    print(tv.status())

Rows (see README chapter 3.3): heading, row, big, bar, down, up, spark, empty. Texts: ASCII plus "°", about 24
characters per row, titles up to 15 characters, up to 8 rows per page. Device.send() splits large page sets
into several requests automatically (firmware >= 0.8). Several devices: create one Device per display.
"""
import json
import re
import subprocess
import urllib.error
import urllib.request
import uuid

MAX_BYTES = 3800   # the device accepts 4096 bytes per request; keep a margin


# ------------------------------------------------------------------------------------------- rows
def _pct(p):
    return max(0, min(100, int(round(p))))


def heading(label, value="", alert=False):
    return {"y": "h", "l": label, "v": value, "a": int(alert)}


def row(label, value, alert=False):
    return {"y": "k", "l": label, "v": value, "a": int(alert)}


def big(label, value, alert=False):
    """Value in a large bold font (firmware >= 0.8.10)."""
    return {"y": "g", "l": label, "v": value, "a": int(alert)}


def bar(label, percent, value, alert=False):
    return {"y": "b", "l": label, "p": _pct(percent), "v": value, "a": int(alert)}


def down(percent, value, alert=False):
    return {"y": "d", "p": _pct(percent), "v": value, "a": int(alert)}


def up(percent, value, alert=False):
    return {"y": "u", "p": _pct(percent), "v": value, "a": int(alert)}


def spark(label, values, value, alert=False, maximum=None):
    """Sparkline (firmware >= 0.8.10): up to 20 columns, oldest first. Values are scaled to 0..100 by
    `maximum` (default: the largest value). Label up to 7 characters."""
    values = list(values)[-20:]
    top = maximum or max([v for v in values if v is not None] + [1])
    return {"y": "s", "l": label[:7], "v": value, "a": int(alert),
            "w": [_pct(100 * (v or 0) / top) for v in values]}


def empty():
    return {"y": "-"}


def page(title, rows):
    return {"t": title[:15], "z": rows[:8]}


# ------------------------------------------------------------------------------------------- device
class Device:
    def __init__(self, host, key, timeout=10):
        self.host, self.key, self.timeout = host, key, timeout

    @classmethod
    def by_mac(cls, mac, key, **kw):
        """Finds the device by its MAC in this computer's neighbour table (Linux `ip neigh`, otherwise `arp -an`).
        Works when the device is in the same network and has talked to this computer recently; a ping to the
        broadcast address or the old address helps to fill the table."""
        ip = find_ip(mac)
        if not ip:
            raise LookupError("MAC %s not in the neighbour table" % mac)
        return cls(ip, key, **kw)

    def _request(self, path, data=None, method="GET", headers=None):
        req = urllib.request.Request("http://%s%s" % (self.host, path), data=data, method=method,
                                     headers=dict({"X-Token": self.key}, **(headers or {})))
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                return json.loads(r.read().decode() or "{}")
        except urllib.error.HTTPError as e:
            body = e.read().decode()
            raise RuntimeError("HTTP %d: %s%s" % (e.code, body, " (wrong key?)" if e.code == 403 else ""))

    def status(self):
        return self._request("/api/status")

    def send(self, pages, seconds_per_page=None, **extra):
        """Replaces all pages. extra: e.g. hell_tag=90, hell_nacht=10, nacht_von=23, nacht_bis=7, tz="..."."""
        base = dict(extra)
        if seconds_per_page:
            base["rot"] = int(seconds_per_page)
        parts, part = [], []
        for p in pages:
            if part and len(json.dumps(dict(base, seiten=part + [p], ab=99, gesamt=99)).encode()) > MAX_BYTES:
                parts.append(part)
                part = []
            part.append(p)
        parts.append(part)
        answer, first = None, 0
        for part in parts:
            body = dict(base, seiten=part) if len(parts) == 1 else dict(base, seiten=part, ab=first, gesamt=len(pages))
            answer = self._request("/api/seiten", json.dumps(body).encode(), "POST", {"Content-Type": "application/json"})
            if answer.get("voll"):   # device memory full: it keeps what fit
                break
            first += len(part)
        return answer

    def upload_image(self, path_or_bytes, nr=1, name="image"):
        """Stores a GIF or baseline JPG (<= 240x240, <= 1 MB) as image page nr 1..5. After the first GIF the device
        restarts once (answer "restart": true)."""
        data = open(path_or_bytes, "rb").read() if isinstance(path_or_bytes, str) else path_or_bytes
        ext = "gif" if data[:4] == b"GIF8" else "jpg"
        border = uuid.uuid4().hex
        body = ("--%s\r\nContent-Disposition: form-data; name=\"medium\"; filename=\"%s.%s\"\r\n"
                "Content-Type: image/%s\r\n\r\n" % (border, name, ext, "gif" if ext == "gif" else "jpeg")).encode() \
            + data + ("\r\n--%s--\r\n" % border).encode()
        return self._request("/api/medium?nr=%d" % nr, body, "POST",
                             {"Content-Type": "multipart/form-data; boundary=" + border})

    def delete_image(self, nr=None):
        """Removes image page nr, or all image pages."""
        return self._request("/api/medium" + ("?nr=%d" % nr if nr else ""), method="DELETE")


def find_ip(mac):
    """IPv4 address of a MAC from the local neighbour table, or None."""
    mac = mac.lower().replace("-", ":")
    for cmd in (["ip", "-4", "neigh"], ["arp", "-an"]):
        try:
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=5).stdout.lower()
        except (OSError, subprocess.TimeoutExpired):
            continue
        for line in out.splitlines():
            if mac in line:
                m = re.search(r"(\d+\.\d+\.\d+\.\d+)", line)
                if m:
                    return m.group(1)
    return None
