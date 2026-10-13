#!/usr/bin/env python3
"""KAC dashboard: shows internet uplinks, firewall and NAS status on a KleinerAnzeigeComputer (KAC) display.

Every INTERVALL s it reads the byte counters of an OPNsense firewall (down/up per uplink); every LANGSAM s it
reads IPv4/IPv6 latency (dpinger RRD), clients (ARP), today's traffic (traffic RRD), firewall system values
(system health RRD) and, optionally via SSH (read-only), RAID/SMART data of a Linux NAS. It sends up to three
pages as JSON to the device (POST /api/seiten, header X-Token). The device redraws only rows that changed,
keeps the clock itself and rotates the pages every ROT_S s.

Configuration: KEY=value file (mode 600), default /root/.config/kac/kac.env, other path via KAC_CONFIG.
Every key of CFG below can be set there; required: OPNSENSE_URL, OPNSENSE_KEY, OPNSENSE_SECRET, KAC_TOKEN.
The OPNsense API user only needs read privileges: "Reporting: Traffic", "Diagnostics: System Health",
"Diagnostics: ARP Table". Identifiers and the texts shown on the device are German.
Usage: kac_dashboard.py [--once]
"""
import json, os, subprocess, sys, time
import requests, urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

CFG = {
    "OPNSENSE_URL": "", "OPNSENSE_KEY": "", "OPNSENSE_SECRET": "",
    "TV_URL": "http://192.168.66.120",           # KAC device
    "KAC_TOKEN": "",                              # device key of the KAC firmware
    "INTERVALL": "10", "LANGSAM": "60", "ROT_S": "30",
    "HELL_TAG": "90", "HELL_NACHT": "10", "NACHT_VON": "23", "NACHT_BIS": "7",
    "TZ": "",                   # POSIX time zone for the device, e.g. CET-1CEST,M3.5.0,M10.5.0/3 (empty: keep)
    # Uplinks: "name:interface:max_down:max_up:gateway_v4,gateway_v6" separated by ";" (OPNsense interface key
    # and gateway names). max_* = full-scale bar in Mbit/s, ideally the narrowest point (line or shaper).
    "LINES": "WAN:wan:100:40:WAN_DHCP,WAN_DHCP6",
    "VPN_GATEWAY": "",          # gateway for the VPN latency in row 0 (empty: none)
    "NO_CLIENT_IFACES": "",     # ARP interface names/descriptions that are not clients (e.g. modem links)
    "FW_TITLE": "Firewall",
    "NAS_SSH": "",              # e.g. root@nas; empty: no NAS page
    "NAS_KNOWN_HOSTS": "", "NAS_TITLE": "NAS", "NAS_DISKS": "sda sdb", "NAS_MOUNT": "/mnt/nas",
}
LEITUNGEN = []              # (name, interface, {"dn": max, "up": max}), filled from LINES
GATEWAYS = {}               # name → gateway names, filled from LINES and VPN_GATEWAY
VERLUST_ALARM = 5          # % packet loss from which an uplink's heading turns red
KEINE_CLIENTS = set()      # ARP entries on these interfaces are not clients


def lade_cfg(pfad=os.environ.get("KAC_CONFIG", "/root/.config/kac/kac.env")):
    if os.path.exists(pfad):
        for zeile in open(pfad):
            zeile = zeile.strip()
            if zeile and not zeile.startswith("#") and "=" in zeile:
                k, v = zeile.split("=", 1)
                CFG[k.strip()] = v.strip().strip('"')
    LEITUNGEN.clear()
    GATEWAYS.clear()
    for teil in filter(None, CFG["LINES"].split(";")):
        name, iface, dn, up, gws = teil.split(":")
        LEITUNGEN.append((name, iface, {"dn": float(dn), "up": float(up)}))
        GATEWAYS[name] = tuple(gws.split(","))
    if CFG["VPN_GATEWAY"]:
        GATEWAYS["VPN"] = (CFG["VPN_GATEWAY"],)
    KEINE_CLIENTS.update(i for _, i, _ in LEITUNGEN)
    KEINE_CLIENTS.update(filter(None, CFG["NO_CLIENT_IFACES"].split(",")))


def opnsense_quelle():
    s, url = requests.Session(), CFG["OPNSENSE_URL"].rstrip("/")
    s.auth = (CFG["OPNSENSE_KEY"], CFG["OPNSENSE_SECRET"])

    def lesen():
        r = s.get(url + "/api/diagnostics/traffic/interface", verify=False, timeout=10)
        r.raise_for_status()
        ifs = r.json()["interfaces"]
        return time.monotonic(), {k: (int(ifs[k]["bytes received"]), int(ifs[k]["bytes transmitted"]))
                                  for _, k, _ in LEITUNGEN}

    def ping(gw):
        # last complete one-minute value: (latency ms, loss %) or None
        r = s.get(url + "/api/diagnostics/systemhealth/get_system_health/%s-quality/0" % gw, verify=False, timeout=15)
        r.raise_for_status()
        reihen = {x["key"]: x["values"] for x in r.json()["set"]["data"]}
        for (_, d), (_, l) in list(zip(reihen["delay"], reihen["loss"]))[::-1]:
            if d is not None and l is not None:
                return d * 1000, l
        return None

    def latenz():
        erg = {}
        for name, gws in GATEWAYS.items():
            werte = []
            for gw in gws:
                try:
                    werte.append(ping(gw))
                except Exception:
                    werte.append(None)
            erg[name] = werte
        return erg

    def clients():
        # devices (MAC) that talked through the firewall in the last ~20 min (dynamic ARP entries)
        r = s.get(url + "/api/diagnostics/interface/get_arp", verify=False, timeout=15)
        r.raise_for_status()
        return len({a["mac"] for a in r.json()
                    if not a.get("permanent") and not a.get("expired")
                    and a.get("intf") not in KEINE_CLIENTS and a.get("intf_description") not in KEINE_CLIENTS})
    def heute():
        # traffic since midnight (bytes down, up) of both uplinks from the firewall's traffic RRDs:
        # one-minute values (last ~20 h), 5-minute values before that. in = download, out = upload (verified).
        mitternacht = time.mktime(time.strptime(time.strftime("%F"), "%Y-%m-%d"))
        summe = [0.0, 0.0]
        jetzt = time.time()
        for _, k, _ in LEITUNGEN:
            grenze = jetzt          # coarser values count up to here (start of the one-minute values)
            for stufe in (0, 1):
                r = s.get(url + "/api/diagnostics/systemhealth/get_system_health/%s-traffic/%d" % (k, stufe),
                          verify=False, timeout=30)
                r.raise_for_status()
                reihen = {x["key"]: x["values"] for x in r.json()["set"]["data"]}
                stempel = [ts / 1000 for ts, _ in reihen["inpass"]]
                schritt = min(b - a for a, b in zip(stempel, stempel[1:]))
                # RRD timestamp = end of interval: value covers (ts - step, ts]; clip proportionally to
                # [midnight, boundary] so that the resolutions do not overlap
                for i, schluessel in enumerate((("inpass", "inpass6"), ("outpass", "outpass6"))):
                    for key in schluessel:
                        for ts, v in reihen[key]:
                            ts /= 1000
                            anteil = min(ts, grenze) - max(ts - schritt, mitternacht)
                            if v is not None and anteil > 0:
                                summe[i] += v * anteil
                grenze = stempel[0] - schritt
        return summe

    def rrd_letzt(name):
        # last value of each series of a system RRD (e.g. "processor-system")
        r = s.get(url + "/api/diagnostics/systemhealth/get_system_health/%s/0" % name, verify=False, timeout=15)
        r.raise_for_status()
        erg = {}
        for x in r.json()["set"]["data"]:
            werte = [v for _, v in x["values"] if v is not None]
            erg[x["key"]] = werte[-1] if werte else None
        return erg

    def system():
        d = {"cpu": rrd_letzt("processor-system"), "mem": rrd_letzt("memory-system"),
             "states": rrd_letzt("states-system"), "temp": rrd_letzt("cputemp-system")}
        # blocked inbound packets of the last 60 min on both WAN interfaces (IPv4+IPv6)
        blocks = 0.0
        for _, k, _ in LEITUNGEN:
            r = s.get(url + "/api/diagnostics/systemhealth/get_system_health/%s-packets/0" % k,
                      verify=False, timeout=15)
            r.raise_for_status()
            for x in r.json()["set"]["data"]:
                if x["key"] in ("inblock", "inblock6"):
                    blocks += sum(v for _, v in x["values"][-60:] if v is not None) * 60   # packets/s * 60 s
        d["blocks_h"] = blocks
        return d
    return lesen, latenz, clients, heute, system


def balken(wert, maximum):
    return max(0, min(100, round(wert * 100 / maximum))) if maximum > 0 else 0


def mbit(wert):
    # below 10 Mbit/s one decimal (German comma), above that whole numbers
    if wert < 0.05:
        return "0 Mbit/s"
    return ("%.1f Mbit/s" % wert).replace(".", ",") if wert < 10 else "%d Mbit/s" % round(wert)


def ms(wert):
    if wert is None:
        return "-"
    d, l = wert
    return "aus" if l >= 50 else "%d" % round(d)


def gestoert(werte):
    return any(w is None or w[1] >= VERLUST_ALARM for w in werte)


def gb(b):
    # below 1 GB in MB, above that GB with one decimal
    return "%d MB" % round(b / 1e6) if b < 1e9 else ("%.1f GB" % (b / 1e9)).replace(".", ",")


def tausender(n):
    return "{:,}".format(round(n)).replace(",", ".")


def seite_internet(raten, lat, anzahl=None, vol=None):
    # row 0 clients | VPN, 1 today's traffic, then 3 rows per uplink (the device draws the header with clock/date)
    vpn = lat.get("VPN")
    z = [{"y": "k", "l": "Clients %s" % ("-" if anzahl is None else anzahl),
          "v": "VPN %s ms" % ms(vpn[0]) if vpn else "", "a": int(bool(vpn) and gestoert(vpn))},
         {"y": "k", "l": "v %s" % (gb(vol[0]) if vol else "-"), "v": "^ %s" % (gb(vol[1]) if vol else "-")}]
    for name, _, maxi in LEITUNGEN:
        dn, up = raten[name]
        v4, v6 = lat.get(name, (None, None))
        z.append({"y": "h", "l": name, "v": "%s / %s ms" % (ms(v4), ms(v6)), "a": int(gestoert((v4, v6)))})
        z.append({"y": "d", "p": balken(dn, maxi["dn"]), "v": mbit(dn)})
        z.append({"y": "u", "p": balken(up, maxi["up"]), "v": mbit(up)})
    return {"t": "Internet", "z": z}


def seite_firewall(d):
    # nothing that the internet page already shows
    temp = d["temp"].get("cpu0temp")
    c, m, st = d["cpu"], d["mem"], d["states"]
    cpu = sum(c.get(k) or 0 for k in ("user", "nice", "system", "interrupt"))
    ram = (m.get("active") or 0) + (m.get("wire") or 0)     # used = active + wired (without cache/ARC)
    return {"t": CFG["FW_TITLE"], "z": [
        {"y": "h", "l": CFG["FW_TITLE"], "v": "-" if temp is None else "%d \u00b0C" % round(temp),
         "a": int(temp is not None and temp >= 75)},
        {"y": "b", "l": "CPU", "p": balken(cpu, 100), "v": "%d %%" % round(cpu)},
        {"y": "b", "l": "RAM", "p": balken(ram, 100), "v": "%d %%" % round(ram)},
        {"y": "k", "l": "States", "v": tausender(st.get("pfstates") or 0)},
        {"y": "k", "l": "Neu/s", "v": "%d" % round(st.get("pfrate") or 0)},
        {"y": "k", "l": "Src/Dst-IPs", "v": "%d / %d" % (round(st.get("srcip") or 0), round(st.get("dstip") or 0))},
        {"y": "k", "l": "Prozesse", "v": "%d" % round(c.get("processes") or 0)},
        {"y": "k", "l": "Blocks/h", "v": tausender(d["blocks_h"])},
    ]}


# Reads only, on the NAS (Linux/OpenWrt with mdadm and smartctl): RAID status, volume, SMART per disk, SoC temperature, uptime.
# smartctl -n standby does not wake a sleeping disk (then "schlaeft" = asleep). Device fonts: ASCII + ° only.
NAS_BEFEHL = r"""
echo raid=$(grep -o '\[[U_]*\]' /proc/mdstat | head -1)
df -k __MOUNT__ | awk 'NR==2{print "vol="$2*1024" "$3*1024" "$4*1024}'
for d in __DISKS__; do
  echo disk=$d $(cat /sys/block/$d/device/model)
  smartctl -H -A -n standby /dev/$d | awk -v d=$d '
    /STANDBY|SLEEP/ {print d".schlaf=1"}
    /overall-health/ {print d".health="$NF}
    $2=="Reallocated_Sector_Ct"||$2=="Current_Pending_Sector"||$2=="Offline_Uncorrectable" {s+=$10}
    $2=="Power_On_Hours" {print d".std="$10}
    $2=="Temperature_Celsius" {print d".temp="$10}
    $2=="UDMA_CRC_Error_Count" {print d".crc="$10}
    END {print d".sekt="s+0}'
done
echo soc=$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null)
echo up=$(cut -d. -f1 /proc/uptime)
"""
PLATTEN_NAME = {"WDC WD80EFPX": "WD Red Plus", "ST8000VX": "Seagate SkyHawk"}


def nas():
    befehl = NAS_BEFEHL.replace("__MOUNT__", CFG["NAS_MOUNT"]).replace("__DISKS__", CFG["NAS_DISKS"])
    opt = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=8", "-o", "StrictHostKeyChecking=yes"]
    if CFG["NAS_KNOWN_HOSTS"]:
        opt += ["-o", "UserKnownHostsFile=" + CFG["NAS_KNOWN_HOSTS"]]
    r = subprocess.run(["ssh"] + opt + [CFG["NAS_SSH"], befehl], capture_output=True, text=True, timeout=40)
    if r.returncode != 0:
        raise RuntimeError("ssh %d: %s" % (r.returncode, r.stderr.strip()[:120]))
    d, platten = {}, []
    for zeile in r.stdout.splitlines():
        k, _, v = zeile.partition("=")
        if k == "disk":
            dev, _, modell = v.partition(" ")
            name = next((n for m, n in PLATTEN_NAME.items() if modell.startswith(m)), modell.strip()[:15])
            platten.append((dev, name))
        else:
            d[k] = v.strip()
    d["platten"] = platten
    return d


def dauer(sek):
    t, h = sek // 86400, sek % 86400 // 3600
    return "%d T %d h" % (t, h) if t else "%d h %d min" % (h, sek % 3600 // 60)


def seite_nas(d):
    z = []
    raid = d.get("raid", "")
    z.append({"y": "h", "l": "RAID 1", "v": "OK" if raid == "[UU]" else (raid or "?"), "a": int(raid != "[UU]")})
    try:
        gesamt, belegt, frei = (int(x) for x in d["vol"].split())
        z.append({"y": "b", "l": "Vol", "p": round(belegt * 100 / gesamt), "v": "%d %%" % round(belegt * 100 / gesamt)})
        z.append({"y": "k", "l": "Frei", "v": ("%.1f TB" % (frei / 1e12)).replace(".", ",")})
    except (KeyError, ValueError):
        z += [{"y": "k", "l": "Vol", "v": "nicht eingehaengt", "a": 1}, {"y": "-"}]
    for dev, name in d["platten"][:2]:
        if d.get(dev + ".schlaf"):
            z += [{"y": "h", "l": name, "v": "schlaeft"}, {"y": "-"}]
            continue
        temp, sekt = int(d.get(dev + ".temp", 0)), int(d.get(dev + ".sekt", 0))
        schlecht = d.get(dev + ".health") != "PASSED" or sekt > 0 or temp >= 50
        z.append({"y": "h", "l": name, "v": "%d \u00b0C" % temp if d.get(dev + ".health") == "PASSED" else "SMART!",
                  "a": int(schlecht)})
        z.append({"y": "k", "l": "%s h" % tausender(int(d.get(dev + ".std", 0))),
                  "v": "CRC %s / Sekt. %d" % (tausender(int(d.get(dev + ".crc", 0))), sekt), "a": int(sekt > 0)})
    soc = int(d.get("soc") or 0) // 1000
    z.append({"y": "k", "l": "SoC %d \u00b0C" % soc, "v": "up " + dauer(int(d.get("up") or 0))})
    return {"t": CFG["NAS_TITLE"], "z": z[:8]}


class Geraet:
    """KAC firmware (GiT-BeDa/KleinerAnzeigeComputer): POST /api/seiten with X-Token."""
    def senden(self, seiten):
        # brightness schedule: the device dims by itself (also when this program stops); stored only on change
        daten = {"rot": int(CFG["ROT_S"]), "seiten": seiten,
                 "hell_tag": int(CFG["HELL_TAG"]), "hell_nacht": int(CFG["HELL_NACHT"]),
                 "nacht_von": int(CFG["NACHT_VON"]), "nacht_bis": int(CFG["NACHT_BIS"])}
        if CFG["TZ"]:
            daten["tz"] = CFG["TZ"]
        body = json.dumps(daten, ensure_ascii=False)
        return requests.post(CFG["TV_URL"] + "/api/seiten", data=body.encode(), timeout=10,
                             headers={"X-Token": CFG["KAC_TOKEN"], "Content-Type": "application/json"})


def main():
    lade_cfg()
    once = "--once" in sys.argv
    fehlt = [k for k in ("OPNSENSE_URL", "OPNSENSE_KEY", "OPNSENSE_SECRET", "KAC_TOKEN") if not CFG[k]]
    if fehlt:
        sys.exit("missing in configuration (KAC_CONFIG): " + ", ".join(fehlt))
    # The device redraws only changed rows (no flicker) → rates every INTERVALL s; slow values
    # (latency, clients, traffic, firewall, NAS) only every LANGSAM s. The device handles page rotation.
    intervall, langsam = int(CFG["INTERVALL"]), int(CFG["LANGSAM"])
    (lesen, latenz, clients, heute, system), tv = opnsense_quelle(), Geraet()
    t0, z0 = lesen()
    cache, cache_zeit = {}, 0.0
    while True:
        time.sleep(5 if once else intervall - time.time() % intervall)
        try:
            t1, z1 = lesen()
            dt = t1 - t0
            raten = {name: ((z1[k][0] - z0[k][0]) * 8 / dt / 1e6, (z1[k][1] - z0[k][1]) * 8 / dt / 1e6)
                     for name, k, _ in LEITUNGEN}
            t0, z0 = t1, z1
            if time.monotonic() - cache_zeit >= langsam or not cache:
                cache = {"lat": latenz()}
                quellen = [("anzahl", clients), ("vol", heute), ("sys", system)]
                if CFG["NAS_SSH"]:
                    quellen.append(("nas", nas))
                cache["nas"] = None
                for name, f in quellen:
                    try:
                        cache[name] = f()
                    except Exception as e:
                        cache[name] = None
                        print(time.strftime("%F %T"), "Fehler %s:" % name, e, flush=True)
                cache_zeit = time.monotonic()
            seiten = [seite_internet(raten, cache["lat"], cache["anzahl"], cache["vol"])]
            if cache["sys"]:
                seiten.append(seite_firewall(cache["sys"]))
            if cache.get("nas"):
                seiten.append(seite_nas(cache["nas"]))
            r = tv.senden(seiten)
            if once or r.status_code != 200:
                print(time.strftime("%F %T"), r.status_code, r.text.strip(), flush=True)
        except Exception as e:            # device or firewall briefly unreachable: carry on
            print(time.strftime("%F %T"), "Fehler:", e, flush=True)
            if once:
                sys.exit(1)
            try:                          # e.g. expired web session: log in again, reset counters
                lesen, latenz, clients, heute, system = opnsense_quelle()
                t0, z0 = lesen()
            except Exception as e2:
                print(time.strftime("%F %T"), "Neuanmeldung fehlgeschlagen:", e2, flush=True)
        if once:
            print(json.dumps(seiten, ensure_ascii=False, indent=1))
            return


if __name__ == "__main__":
    main()
