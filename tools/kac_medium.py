#!/usr/bin/env python3
"""Prepares a picture or animation for the KAC image page and optionally sends it to the device.

The device shows one GIF or JPG full screen as an extra page in the rotation. Its limits: at most 240x240 pixels,
baseline (not progressive) JPG, at most 1 MB, and the ESP8266 decodes a full 240x240 GIF frame in roughly 60-140 ms.
This script turns any GIF/PNG/JPG/WebP (file or URL) into a file that fits:

  * size: centered square cut, scaled to 240x240 (--fit: whole picture, black borders instead)
  * GIF: one common palette for all frames (--colors, default 64) and only the changed pixels per frame; this
    halves the file size and speeds up decoding compared with one palette per frame
  * GIF: frame delays below --min-delay (default 100 ms; many GIFs say 0) are raised to it
  * still pictures become a baseline JPG (quality --quality, default 90)

Requires Pillow (pip install pillow). Examples:

    python3 kac_medium.py animation.gif                          # writes animation-kac.gif
    python3 kac_medium.py https://example.org/photo.jpg --fit    # writes photo-kac.jpg
    KAC_KEY=yourkey python3 kac_medium.py cat.gif --send 192.168.1.50
    python3 kac_medium.py --delete --send 192.168.1.50 --key-file ~/.kac.key

Sending uses POST /api/medium?nr=<1..5> (header X-Token = device key; up to 5 images, only one GIF);
--delete removes image --nr, or all images without --nr.
"""
import argparse, io, json, os, sys, urllib.parse, urllib.request
import uuid

try:
    from PIL import Image, ImageSequence
except ImportError:
    sys.exit("Pillow is missing: pip install pillow")

SIZE = 240


def load(source):
    if urllib.parse.urlparse(source).scheme in ("http", "https"):
        req = urllib.request.Request(source, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=30) as r:
            data = r.read(50 * 1024 * 1024)
        name = os.path.basename(urllib.parse.urlparse(source).path) or "picture"
    else:
        data = open(source, "rb").read()
        name = os.path.basename(source)
    return Image.open(io.BytesIO(data)), os.path.splitext(name)[0]


def fit(frame, whole):
    frame = frame.convert("RGB")
    w, h = frame.size
    if whole:   # whole picture, black borders
        scale = SIZE / max(w, h)
        frame = frame.resize((max(1, round(w * scale)), max(1, round(h * scale))), Image.LANCZOS)
        canvas = Image.new("RGB", (SIZE, SIZE))
        canvas.paste(frame, ((SIZE - frame.width) // 2, (SIZE - frame.height) // 2))
        return canvas
    side = min(w, h)   # centered square
    frame = frame.crop(((w - side) // 2, (h - side) // 2, (w - side) // 2 + side, (h - side) // 2 + side))
    return frame.resize((SIZE, SIZE), Image.LANCZOS)


def convert(img, args):
    """Returns (bytes, extension, description)."""
    frames = [f.copy() for f in ImageSequence.Iterator(img)] if getattr(img, "is_animated", False) else [img]
    if len(frames) == 1:
        out = io.BytesIO()
        fit(frames[0], args.fit).save(out, "JPEG", quality=args.quality, progressive=False, optimize=True)
        return out.getvalue(), "jpg", "JPG %dx%d" % (SIZE, SIZE)
    delays = [max(args.min_delay, int(f.info.get("duration") or 0)) for f in frames]
    rgb = [fit(f, args.fit) for f in frames]
    strip = Image.new("RGB", (SIZE, SIZE * len(rgb)))   # common palette from all frames
    for i, f in enumerate(rgb):
        strip.paste(f, (0, SIZE * i))
    palette = strip.quantize(colors=args.colors, method=Image.MEDIANCUT)
    pal = [f.quantize(palette=palette, dither=Image.Dither.NONE) for f in rgb]
    out = io.BytesIO()
    pal[0].save(out, "GIF", save_all=True, append_images=pal[1:], duration=delays, loop=0, optimize=True,
                disposal=1)
    return out.getvalue(), "gif", "GIF %dx%d, %d frames, %d colors, %d-%d ms" % (
        SIZE, SIZE, len(pal), args.colors, min(delays), max(delays))


def send(host, key, data=None, ext=None, nr=None):
    url = "http://%s/api/medium" % host + ("?nr=%d" % nr if nr else "")
    if data is None:
        req = urllib.request.Request(url, method="DELETE", headers={"X-Token": key})
    else:
        border = uuid.uuid4().hex
        body = (("--%s\r\nContent-Disposition: form-data; name=\"medium\"; filename=\"kac.%s\"\r\n"
                 "Content-Type: image/%s\r\n\r\n" % (border, ext, "jpeg" if ext == "jpg" else ext)).encode()
                + data + ("\r\n--%s--\r\n" % border).encode())
        req = urllib.request.Request(url, data=body, method="POST", headers={
            "X-Token": key, "Content-Type": "multipart/form-data; boundary=" + border})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


def main():
    p = argparse.ArgumentParser(description="Prepare a GIF/JPG for the KAC image page (240x240).")
    p.add_argument("source", nargs="?", help="picture file or http(s) URL")
    p.add_argument("-o", "--output", help="output file (default: <name>-kac.gif/.jpg)")
    p.add_argument("--fit", action="store_true", help="whole picture with black borders instead of a square cut")
    p.add_argument("--colors", type=int, default=64, help="GIF colors 2-256 (default 64)")
    p.add_argument("--min-delay", type=int, default=100, help="shortest GIF frame delay in ms (default 100)")
    p.add_argument("--quality", type=int, default=90, help="JPG quality (default 90)")
    p.add_argument("--send", metavar="HOST", help="send to this KAC device")
    p.add_argument("--key-file", help="file with the device key (otherwise environment KAC_KEY)")
    p.add_argument("--nr", type=int, choices=range(1, 6), help="image slot 1-5 (default 1; --delete: all)")
    p.add_argument("--delete", action="store_true", help="remove the image page(s) on --send HOST")
    args = p.parse_args()
    key = open(os.path.expanduser(args.key_file)).read().strip() if args.key_file else os.environ.get("KAC_KEY", "")
    if args.send and not key:
        sys.exit("device key missing: --key-file or KAC_KEY")
    if args.delete:
        if not args.send:
            sys.exit("--delete needs --send HOST")
        print(*send(args.send, key, nr=args.nr))
        return
    if not args.source:
        p.error("source missing")
    img, name = load(args.source)
    data, ext, text = convert(img, args)
    path = args.output or "%s-kac.%s" % (name, ext)
    open(path, "wb").write(data)
    print("%s: %s, %d KB" % (path, text, len(data) // 1024))
    if len(data) > 1024 * 1024:
        sys.exit("larger than 1 MB: the device will refuse it; try --colors 32 or a shorter animation")
    if args.send:
        status, answer = send(args.send, key, data, ext, args.nr or 1)
        print("device:", status, answer)
        if status != 200:
            sys.exit(1)


if __name__ == "__main__":
    main()
