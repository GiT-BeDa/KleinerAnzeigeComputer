// KleinerAnzeigeComputer (KAC) – custom firmware for the GeekMagic SmallTV Ultra (ESP8266, ST7789 240x240)
//
// Normal operation: Wi-Fi client only (no access point). A program on the network sends pages as JSON
// (POST /api/seiten, header X-Token). The device shows them in rotation and redraws only rows that
// changed – each row is drawn completely in memory (sprite) and then transferred in one piece, so
// nothing flickers. The device sets the clock in the header itself via NTP.
//
// Setup: cut the power 3 times in a row (each run < 10 s) or no Wi-Fi stored → access point
// "KAC-<chip ID>" with a random password shown only on the display; setup page at
// http://192.168.4.1/. Restarts after 15 min without saving.
//
// Device key: protects the data API (header X-Token) and the firmware update (user admin). Each device creates a
// random key on first start and stores it in flash; the setup page shows it and lets you change it. Images
// therefore contain no secret and can be published. A private build may set KAC_TOKEN as the initial key.
//
// Firmware update: http://<ip>/update, user admin, password = device key.
//
// Identifiers are German; see README.md for the API field names. Texts shown on the device are English.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <qrcode.h>
#include <time.h>
extern "C" {
#include <user_interface.h>
}

#ifndef KAC_TOKEN
#define KAC_TOKEN ""        // public build: every device creates its own key
#endif

// ---------------------------------------------------------------- Settings
// Host name "KleinerAnzeigeComputer-" + chip ID (last 3 bytes of the MAC); the setup Wi-Fi uses the short
// form "KAC-" + chip ID so that it fits on one line of the setup screen
static char geraeteName[33];
static char apName[16];
static const char *TZ_STANDARD = "CET-1CEST,M3.5.0,M10.5.0/3";   // POSIX TZ, default Europe/Berlin
static const uint8_t PIN_BL = 5;                       // backlight, inverted
static const uint8_t GESTE_ANZAHL = 3;                  // boots in a row that start setup mode
static const uint32_t GESTE_FENSTER_MS = 10000;         // a boot counts if it runs shorter than this
static const uint32_t EINRICHTUNG_MAX_MS = 15UL * 60 * 1000;
static const uint32_t DATEN_ALT_MS = 3UL * 60 * 1000;   // after that: red dot in the header
// Shown as QR code on the info page until the first data arrives: the build guide on bk99.de
static const char *ANLEITUNG_URL = "https://www.bk99.de/blog/2026/kac/";
static const char *ANLEITUNG_TEXT[] = {"Guide:", "bk99.de", "/blog/2026", "/kac/"};
static const int RAND_R = 228;   // right text edge: the outermost pixels are hidden by the case

// Colors (RGB565). Rows are drawn in an 8-bit sprite (RGB332: 3 bits red, 3 green, 2 blue), so every color
// must survive that reduction; a dark grey with little blue would turn olive.
static const uint16_t C_BG = TFT_BLACK;
static const uint16_t C_TEXT = TFT_WHITE;
static const uint16_t C_LABEL = 0xB5B6;   // light grey (RGB332 0xB6)
static const uint16_t C_ACCENT = 0xFD20;  // orange
static const uint16_t C_ALERT = 0xF800;   // red
static const uint16_t C_DOWN = 0x26FF;    // light blue
static const uint16_t C_UP = 0xFD20;      // orange
static const uint16_t C_BALKEN = 0x4A69;  // dark grey (RGB332 0x49)
static const uint16_t C_LINIE = 0x6B6D;   // grey (RGB332 0x6D)

// Layout: header 0..34, line at 35, 8 rows of 25 px from y=38
static const int KOPF_H = 34, ZEILE_Y0 = 38, ZEILE_H = 25, ZEILEN = 8;

// ---------------------------------------------------------------- State
TFT_eSPI tft;
TFT_eSprite spr(&tft);
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

struct Zeile {
  char typ;        // 'h' heading, 'k' label/value, 'd'/'u' bar down/up, 'b' labeled bar, 0 empty
  char l[28];
  char v[28];
  uint8_t p;       // bar 0..100
  uint8_t a;       // 1 = alert (red)
  bool operator==(const Zeile &o) const {
    return typ == o.typ && p == o.p && a == o.a && !strcmp(l, o.l) && !strcmp(v, o.v);
  }
};
struct Seite {
  char titel[16];
  Zeile z[ZEILEN];
};

static Seite seiten[3];
static uint8_t seitenAnzahl = 0;
static Zeile gezeichnet[ZEILEN];
static bool zeileGueltig[ZEILEN];
static int aktSeite = -1;
static char kopfAlt[64] = "";
static uint8_t hell = 80, rotS = 30;
static uint32_t letzteDaten = 0;
static bool datenDa = false;
static char eigenerTitel[16] = "Device";   // header title of the built-in pages

struct Speicher {          // EEPROM (one flash sector), survives restarts and firmware updates
  uint8_t magic;
  uint8_t starts;
  char ssid[33];
  char pass[65];
  char tz[48];             // POSIX time zone; appended later, so older settings keep their Wi-Fi data
  char token[33];          // device key (1-32 characters); appended in 0.3.0
  uint8_t schonDaten;      // DATEN_MARKE once data has ever arrived; until then the info page shows the key
  uint8_t planMarke;       // PLAN_MARKE when the brightness schedule below is valid (appended in 0.5.0)
  uint8_t hellTag, hellNacht, nachtVon, nachtBis;   // brightness % by day/night, night hours (local time)
  uint8_t extraMarke;      // EXTRA_MARKE when the fields below are valid (appended in 0.7.0)
  uint8_t uhr12;           // 1 = 12-hour clock
  uint8_t datumFormat;     // index into DATUMSFORMATE
  uint8_t rotStandard;     // seconds per page unless the dashboard sends "rot"
  char ntp[40];            // own NTP server, empty = pool.ntp.org
};
static Speicher cfg;
static const uint8_t MAGIC = 0x4B;
static const uint8_t DATEN_MARKE = 0x5A;
static const uint8_t PLAN_MARKE = 0xA7;
static const uint8_t EXTRA_MARKE = 0xC3;
static const char *const DATUMSFORMATE[][2] = {
  {"%d/%m/%Y", "DD/MM/YYYY"}, {"%d.%m.%Y", "DD.MM.YYYY"}, {"%Y-%m-%d", "YYYY-MM-DD"}, {"%m/%d/%Y", "MM/DD/YYYY"}};
static const int DATUMSFORMATE_N = 4;
static int16_t dashHell = -1;   // brightness sent by the dashboard ("b"); -1 = none

static bool einrichtung = false;
static uint32_t einrichtungSeit = 0;
static bool gesteZurueckgesetzt = false;
static char apPass[11];

// ---------------------------------------------------------------- Helpers
static bool tzGueltig(const char *s, size_t max) {   // printable ASCII, 3..max-1 characters
  size_t n = strnlen(s, max);
  if (n < 3 || n >= max) return false;
  for (size_t i = 0; i < n; i++) if (s[i] < 0x20 || s[i] > 0x7E) return false;
  return true;
}

static void speichern();

static bool schluesselGueltig(const char *s) {   // 1..32 printable characters without space; length is the user's choice
  size_t n = strnlen(s, 33);
  if (n < 1 || n > 32) return false;
  for (size_t i = 0; i < n; i++) if (s[i] < 0x21 || s[i] > 0x7E) return false;
  return true;
}

static void neuerSchluessel() {   // 8 random characters without easily confused ones: 55^8 ≈ 8·10^13
  const char *zeichen = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789";
  for (int i = 0; i < 8; i++) cfg.token[i] = zeichen[RANDOM_REG32 % strlen(zeichen)];
  cfg.token[8] = 0;
}

// Make sure a device key exists. Called after the Wi-Fi radio is on: the ESP8266 random number generator
// draws on RF noise and is weaker before that.
static void schluesselSicherstellen() {
  if (schluesselGueltig(cfg.token)) return;
  if (schluesselGueltig(KAC_TOKEN)) {
    strlcpy(cfg.token, KAC_TOKEN, sizeof(cfg.token));      // private build: take over the old built-in key
  } else {
    neuerSchluessel();
  }
  speichern();
}

// Set the time zone; write to flash only if it changed (the dashboard sends it with every update)
// Brightness schedule: day/night brightness and night hours, written to flash only when it changes
static bool planGueltig(int tag, int nacht, int von, int bis) {
  return tag >= 0 && tag <= 100 && nacht >= 0 && nacht <= 100 && von >= 0 && von <= 23 && bis >= 0 && bis <= 23;
}

static void zeitplanSetzen(int tag, int nacht, int von, int bis) {
  if (!planGueltig(tag, nacht, von, bis)) return;
  if (cfg.planMarke == PLAN_MARKE && cfg.hellTag == tag && cfg.hellNacht == nacht && cfg.nachtVon == von && cfg.nachtBis == bis)
    return;
  cfg.planMarke = PLAN_MARKE;
  cfg.hellTag = tag; cfg.hellNacht = nacht; cfg.nachtVon = von; cfg.nachtBis = bis;
  speichern();
}

// The dashboard's "b" wins while its data is current; otherwise the device follows its own schedule.
static uint8_t sollHelligkeit() {
  if (dashHell >= 0 && datenDa && millis() - letzteDaten < 10UL * 60 * 1000) return dashHell;
  time_t jetzt = time(nullptr);
  if (jetzt < 1700000000) return cfg.hellTag;   // no time yet
  struct tm t;
  localtime_r(&jetzt, &t);
  int h = t.tm_hour, von = cfg.nachtVon, bis = cfg.nachtBis;
  bool nacht = von > bis ? (h >= von || h < bis) : (von < bis && h >= von && h < bis);
  return nacht ? cfg.hellNacht : cfg.hellTag;
}

static void zeitzoneSetzen(const char *tz) {
  if (!tzGueltig(tz, sizeof(cfg.tz))) return;
  if (strcmp(cfg.tz, tz) != 0) {
    strlcpy(cfg.tz, tz, sizeof(cfg.tz));
    speichern();
  }
  setenv("TZ", cfg.tz, 1);
  tzset();
}

static void helligkeit(uint8_t prozent) {
  uint32_t stufe = (uint32_t)min<uint8_t>(prozent, 100) * 1023 / 100;
  analogWrite(PIN_BL, 1023 - stufe);   // inverted: LOW = on
}

static void speichern() {
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

// Text with degree sign (UTF-8 C2 B0): the GFX fonts are ASCII only, so the circle is drawn
static int textBreite(const char *s) {
  char a[32], b[32];
  const char *g = strstr(s, "\xC2\xB0");
  if (!g) return spr.textWidth(s);
  size_t n = min<size_t>(g - s, sizeof(a) - 1);
  memcpy(a, s, n); a[n] = 0;
  strlcpy(b, g + 2, sizeof(b));
  return spr.textWidth(a) + 6 + spr.textWidth(b);
}

static void textZeichnen(const char *s, int x, int ym, uint16_t farbe) {   // left-aligned, vertically centered
  spr.setTextColor(farbe);
  spr.setTextDatum(ML_DATUM);
  const char *g = strstr(s, "\xC2\xB0");
  if (!g) { spr.drawString(s, x, ym); return; }
  char a[32], b[32];
  size_t n = min<size_t>(g - s, sizeof(a) - 1);
  memcpy(a, s, n); a[n] = 0;
  strlcpy(b, g + 2, sizeof(b));
  spr.drawString(a, x, ym);
  x += spr.textWidth(a);
  spr.drawCircle(x + 3, ym - 5, 2, farbe);
  spr.drawString(b, x + 6, ym);
}

static void textRechts(const char *s, int xr, int ym, uint16_t farbe) {
  textZeichnen(s, xr - textBreite(s), ym, farbe);
}

// ---------------------------------------------------------------- Drawing
static void zeileZeichnen(int i, const Zeile &z) {
  spr.fillSprite(C_BG);
  const int ym = ZEILE_H / 2;
  switch (z.typ) {
    case 'h':
      spr.setFreeFont(&FreeSansBold9pt7b);
      textZeichnen(z.l, 4, ym, z.a ? C_ALERT : C_ACCENT);
      spr.setFreeFont(&FreeSans9pt7b);
      textRechts(z.v, RAND_R, ym, z.a ? C_ALERT : C_TEXT);
      break;
    case 'k':
      spr.setFreeFont(&FreeSans9pt7b);
      textZeichnen(z.l, 4, ym, C_LABEL);
      textRechts(z.v, RAND_R, ym, z.a ? C_ALERT : C_TEXT);
      break;
    case 'd':
    case 'u': {
      uint16_t f = z.typ == 'd' ? C_DOWN : C_UP;
      if (z.typ == 'd') spr.fillTriangle(4, ym - 4, 14, ym - 4, 9, ym + 4, f);
      else spr.fillTriangle(4, ym + 4, 14, ym + 4, 9, ym - 4, f);
      const int bx = 20, bw = 112, bh = 10, by = ym - bh / 2;
      int w = (int)bw * min<uint8_t>(z.p, 100) / 100;
      if (w > 0) spr.fillRect(bx, by, w, bh, z.a ? C_ALERT : f);
      if (w < bw) spr.fillRect(bx + w, by, bw - w, bh, C_BALKEN);
      spr.setFreeFont(&FreeSans9pt7b);
      textRechts(z.v, RAND_R, ym, z.a ? C_ALERT : C_TEXT);
      break;
    }
    case 'b': {   // bar with label on the left (e.g. CPU/RAM)
      spr.setFreeFont(&FreeSans9pt7b);
      textZeichnen(z.l, 4, ym, C_LABEL);
      const int bx = 50, bw = 100, bh = 10, by = ym - bh / 2;
      int w = (int)bw * min<uint8_t>(z.p, 100) / 100;
      if (w > 0) spr.fillRect(bx, by, w, bh, z.a ? C_ALERT : C_DOWN);
      if (w < bw) spr.fillRect(bx + w, by, bw - w, bh, C_BALKEN);
      textRechts(z.v, RAND_R, ym, z.a ? C_ALERT : C_TEXT);
      break;
    }
    default:
      break;
  }
  spr.pushSprite(0, ZEILE_Y0 + i * ZEILE_H, 0, 0, 240, ZEILE_H);
}

static void kopfZeichnen(bool erzwingen) {
  char uhr[8] = "--:--", ampm[4] = "", datum[12] = "", titel[16] = "";
  time_t jetzt = time(nullptr);
  if (jetzt > 1700000000) {
    struct tm t;
    localtime_r(&jetzt, &t);
    if (cfg.uhr12) {
      int h12 = t.tm_hour % 12 ? t.tm_hour % 12 : 12;
      snprintf(uhr, sizeof(uhr), "%d:%02d", h12, t.tm_min);
      strlcpy(ampm, t.tm_hour < 12 ? "am" : "pm", sizeof(ampm));
    } else {
      strftime(uhr, sizeof(uhr), "%H:%M", &t);
    }
    strftime(datum, sizeof(datum), DATUMSFORMATE[cfg.datumFormat < DATUMSFORMATE_N ? cfg.datumFormat : 0][0], &t);
  }
  if (aktSeite >= 0 && aktSeite < seitenAnzahl) strlcpy(titel, seiten[aktSeite].titel, sizeof(titel));
  else strlcpy(titel, eigenerTitel, sizeof(titel));
  bool alt = !datenDa || millis() - letzteDaten > DATEN_ALT_MS;
  bool offline = WiFi.status() != WL_CONNECTED;
  char sig[64];
  snprintf(sig, sizeof(sig), "%s%s|%s|%s|%d|%d", uhr, ampm, datum, titel, alt, offline);
  if (!erzwingen && !strcmp(sig, kopfAlt)) return;
  strlcpy(kopfAlt, sig, sizeof(kopfAlt));

  spr.fillSprite(C_BG);
  spr.setFreeFont(&FreeSansBold18pt7b);
  spr.setTextColor(C_TEXT);
  spr.setTextDatum(ML_DATUM);
  spr.drawString(uhr, 4, KOPF_H / 2 + 1);
  int x = 4 + spr.textWidth(uhr);
  spr.setFreeFont(&FreeSans9pt7b);
  if (ampm[0]) { spr.drawString(ampm, x + 3, KOPF_H / 2 + 5); x += 3 + spr.textWidth(ampm); }
  if (offline || alt) spr.fillCircle(x + 8, KOPF_H / 2, 4, C_ALERT);
  textRechts(datum, RAND_R, 9, C_TEXT);
  textRechts(titel, RAND_R, 26, C_ACCENT);
  spr.pushSprite(0, 0, 0, 0, 240, KOPF_H);
}

// Built-in default pages, shown until the first data arrives and again after DATEN_WEG_MS without data:
// "Device" with everything the device knows about itself, "Guide" with a QR code to the build guide.
static const uint32_t DATEN_WEG_MS = 10UL * 60 * 1000;
static const int SEITE_DEVICE = -1, SEITE_GUIDE = -2;

static void dauerText(char *buf, size_t n, uint32_t sek) {
  if (sek >= 86400) snprintf(buf, n, "%lu d %lu h", (unsigned long)(sek / 86400), (unsigned long)(sek % 86400 / 3600));
  else if (sek >= 3600) snprintf(buf, n, "%lu h %lu min", (unsigned long)(sek / 3600), (unsigned long)(sek % 3600 / 60));
  else snprintf(buf, n, "%lu min", (unsigned long)(sek / 60));
}

static void zeile(Zeile &z, char typ, const char *l, const char *v, uint8_t p = 0, uint8_t a = 0) {
  z.typ = typ;
  strlcpy(z.l, l, sizeof(z.l));
  strlcpy(z.v, v, sizeof(z.v));
  z.p = p;
  z.a = a;
}

static void standardSeite(Seite &s, int art) {
  memset(&s, 0, sizeof(s));
  bool online = WiFi.status() == WL_CONNECTED;
  int rssi = online ? WiFi.RSSI() : -100;
  char rssiText[12] = "-", ip[16] = "-", chip[8], buf[28];
  if (online) {
    snprintf(rssiText, sizeof(rssiText), "%d dBm", (rssi / 3) * 3);   // 3-dB steps: fewer redraws
    strlcpy(ip, WiFi.localIP().toString().c_str(), sizeof(ip));
  }
  snprintf(chip, sizeof(chip), "%06x", ESP.getChipId());
  if (art == SEITE_DEVICE) {
    strlcpy(s.titel, "Device", sizeof(s.titel));
    zeile(s.z[0], 'h', "KAC " KAC_VERSION, chip);
    zeile(s.z[1], 'k', "IP", ip);
    zeile(s.z[2], 'k', "Wi-Fi", online ? cfg.ssid : "connecting ...", 0, !online);
    zeile(s.z[3], 'k', "Signal", rssiText, 0, online && rssi < -85);   // red when very weak
    zeile(s.z[4], 'k', "Clock", time(nullptr) > 1700000000 ? "NTP ok" : "not synced", 0, time(nullptr) <= 1700000000);
    dauerText(buf, sizeof(buf), millis() / 1000);
    zeile(s.z[5], 'k', "Uptime", buf);
    if (!datenDa) strlcpy(buf, "none yet", sizeof(buf));
    else { dauerText(buf, sizeof(buf), (millis() - letzteDaten) / 1000); strlcat(buf, " ago", sizeof(buf)); }
    zeile(s.z[6], 'k', "Data", buf, 0, datenDa);
    zeile(s.z[7], 'k', "Key", cfg.token);   // always: whoever forgot the key reads it here (owner's wish)
  } else {
    strlcpy(s.titel, "Guide", sizeof(s.titel));   // only the QR code: everything else is on "Device"
    for (int i = 0; i < ZEILEN; i++) s.z[i].typ = 'q';   // area of the QR code, not drawn as rows
  }
  strlcpy(eigenerTitel, s.titel, sizeof(eigenerTitel));
}

static void qrZeichnen() {
  QRCode qr;
  uint8_t daten[qrcode_getBufferSize(3)];
  qrcode_initText(&qr, daten, 3, ECC_LOW, ANLEITUNG_URL);
  const int m = 4, rand = 2 * m, groesse = qr.size * m + 2 * rand;   // 29 modules → 132 px
  const int x0 = 4, y0 = ZEILE_Y0 + (ZEILEN * ZEILE_H - groesse) / 2;   // vertically centered below the header
  tft.fillRect(0, ZEILE_Y0, 240, 240 - ZEILE_Y0, C_BG);
  tft.fillRect(x0, y0, groesse, groesse, TFT_WHITE);
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y)) tft.fillRect(x0 + rand + x * m, y0 + rand + y * m, m, m, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  for (int i = 0; i < 4; i++) {
    tft.setFreeFont(i == 0 ? &FreeSansBold9pt7b : &FreeSans9pt7b);
    tft.setTextColor(i == 0 ? C_ACCENT : C_TEXT);
    tft.drawString(ANLEITUNG_TEXT[i], x0 + groesse + 6, y0 + 18 + i * 24);
  }
}

static void anzeigen() {
  // dashboard pages, or the built-in pages if nothing ever arrived or nothing came for DATEN_WEG_MS
  bool standard = seitenAnzahl == 0 || millis() - letzteDaten > DATEN_WEG_MS;
  int n = standard ? 2 : seitenAnzahl;
  int index = 0;
  if (n > 1) {
    time_t jetzt = time(nullptr);
    uint32_t sek = jetzt > 1700000000 ? (uint32_t)jetzt : millis() / 1000;
    index = (sek / max<uint8_t>(rotS, 5)) % n;
  }
  static Seite eigen;
  const Seite *s;
  int neu;
  if (standard) { neu = index == 0 ? SEITE_DEVICE : SEITE_GUIDE; standardSeite(eigen, neu); s = &eigen; }
  else { neu = index; s = &seiten[index]; }
  if (neu != aktSeite) {
    aktSeite = neu;
    memset(zeileGueltig, 0, sizeof(zeileGueltig));
    kopfZeichnen(true);
    if (neu == SEITE_GUIDE) qrZeichnen();
  } else {
    kopfZeichnen(false);
  }
  for (int i = 0; i < ZEILEN; i++) {
    if (zeileGueltig[i] && gezeichnet[i] == s->z[i]) continue;
    if (s->z[i].typ == 'q') { gezeichnet[i] = s->z[i]; zeileGueltig[i] = true; continue; }
    zeileZeichnen(i, s->z[i]);
    gezeichnet[i] = s->z[i];
    zeileGueltig[i] = true;
  }
}

static void vollbildText(const char *const zeilen[], int anzahl) {
  tft.fillScreen(C_BG);
  tft.setTextDatum(TL_DATUM);
  int y = 8;
  for (int i = 0; i < anzahl; i++) {
    tft.setFreeFont(i == 0 ? &FreeSansBold9pt7b : &FreeSans9pt7b);
    tft.setTextColor(i == 0 ? C_ACCENT : C_TEXT);
    tft.drawString(zeilen[i], 6, y);
    y += 26;
  }
}

// ---------------------------------------------------------------- Web: normal operation
static bool tokenOk() {
  if (server.header("X-Token") == cfg.token) return true;
  server.send(403, "application/json", "{\"error\":\"token\"}");
  return false;
}

static void apiSeiten() {
  if (!tokenOk()) return;
  const String &body = server.arg("plain");
  if (body.length() == 0 || body.length() > 4096) {
    server.send(400, "application/json", "{\"error\":\"size\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body);
  if (e) {
    server.send(400, "application/json", String("{\"error\":\"json\",\"detail\":\"") + e.c_str() + "\"}");
    return;
  }
  JsonArray arr = doc["seiten"].as<JsonArray>();
  uint8_t n = 0;
  for (JsonObject so : arr) {
    if (n >= 3) break;
    Seite &s = seiten[n];
    memset(&s, 0, sizeof(s));
    strlcpy(s.titel, so["t"] | "", sizeof(s.titel));
    uint8_t i = 0;
    for (JsonObject zo : so["z"].as<JsonArray>()) {
      if (i >= ZEILEN) break;
      Zeile &z = s.z[i++];
      const char *typ = zo["y"] | "";
      z.typ = typ[0] == '-' ? 0 : typ[0];
      strlcpy(z.l, zo["l"] | "", sizeof(z.l));
      strlcpy(z.v, zo["v"] | "", sizeof(z.v));
      z.p = zo["p"] | 0;
      z.a = zo["a"] | 0;
    }
    n++;
  }
  seitenAnzahl = n;
  if (doc["b"].is<int>()) dashHell = constrain((int)doc["b"], 0, 100);
  if (doc["hell_tag"].is<int>() || doc["hell_nacht"].is<int>() || doc["nacht_von"].is<int>() || doc["nacht_bis"].is<int>())
    zeitplanSetzen(doc["hell_tag"] | (int)cfg.hellTag, doc["hell_nacht"] | (int)cfg.hellNacht,
                   doc["nacht_von"] | (int)cfg.nachtVon, doc["nacht_bis"] | (int)cfg.nachtBis);
  if (doc["rot"].is<int>()) rotS = doc["rot"];
  if (doc["tz"].is<const char *>()) zeitzoneSetzen(doc["tz"]);
  letzteDaten = millis();
  if (n > 0 && cfg.schonDaten != DATEN_MARKE) { cfg.schonDaten = DATEN_MARKE; speichern(); }   // once, real pages only
  datenDa = true;
  server.send(200, "application/json", String("{\"ok\":true,\"seiten\":") + n + "}");
}

// Change the device key with the current one: {"key":"<new>"}, or {} for a new random key (returned once).
static void apiSchluessel() {
  if (!tokenOk()) return;
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  const char *neu = doc["key"] | "";
  if (neu[0]) {
    if (!schluesselGueltig(neu)) { server.send(400, "application/json", "{\"error\":\"key\"}"); return; }
    strlcpy(cfg.token, neu, sizeof(cfg.token));
  } else {
    neuerSchluessel();
  }
  speichern();
  updater.updateCredentials("admin", cfg.token);
  server.send(200, "application/json", String("{\"ok\":true,\"key\":\"") + cfg.token + "\"}");
}

static void apiStatus() {
  char buf[480];
  snprintf(buf, sizeof(buf),
           "{\"name\":\"%s\",\"version\":\"%s\",\"uptime\":%lu,\"ssid\":\"%s\","
           "\"rssi\":%d,\"heap\":%u,\"seiten\":%u,\"seite\":%d,\"daten_alter_s\":%ld,\"zeit\":%ld,\"tz\":\"%s\","
           "\"hell\":%u,\"hell_tag\":%u,\"hell_nacht\":%u,\"nacht_von\":%u,\"nacht_bis\":%u}",
           geraeteName, KAC_VERSION, millis() / 1000, cfg.ssid, WiFi.RSSI(), ESP.getFreeHeap(), seitenAnzahl, aktSeite,
           datenDa ? (long)((millis() - letzteDaten) / 1000) : -1L, (long)time(nullptr), cfg.tz,
           hell, cfg.hellTag, cfg.hellNacht, cfg.nachtVon, cfg.nachtBis);
  server.send(200, "application/json", buf);
}

// ---------------------------------------------------------------- Web: setup
static String htmlEsc(const String &s) {
  String o;
  for (char c : s) {
    if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;"; else o += c;
  }
  return o;
}

// ---------------------------------------------------------------- Web: shared settings fields
// Common time zones as POSIX TZ strings; anything else via "Custom".
static const char *const ZONEN[][2] = {
  {"Europe/Berlin, Paris, Vienna", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Europe/London, Lisbon", "GMT0BST,M3.5.0/1,M10.5.0"},
  {"Europe/Helsinki, Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"}, {"Europe/Moscow", "MSK-3"}, {"UTC", "UTC0"},
  {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"}, {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
  {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"}, {"America/Phoenix", "MST7"}, {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
  {"America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"}, {"Pacific/Honolulu", "HST10"}, {"America/Sao_Paulo", "<-03>3"},
  {"Asia/Dubai", "<+04>-4"}, {"Asia/Kolkata", "IST-5:30"}, {"Asia/Shanghai, Singapore", "CST-8"}, {"Asia/Tokyo", "JST-9"},
  {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"}, {"Australia/Perth", "AWST-8"}, {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};

static String stundenAuswahl(const char *name, uint8_t wert) {
  String h = String("<select name=") + name + " style='width:45%'>";
  for (int i = 0; i < 24; i++) {
    char s[8];
    snprintf(s, sizeof(s), "%02d:00", i);
    h += String("<option value=") + i + (i == wert ? " selected" : "") + ">" + s + "</option>";
  }
  return h + "</select>";
}

static String regler(const char *name, uint8_t wert) {
  return String("<input type=range name=") + name + " min=0 max=100 step=5 value=" + wert +
         " style='width:75%' oninput=\"this.nextElementSibling.textContent=this.value+' %'\"> <b>" + wert + " %</b>";
}

static String einstellFelder() {   // brightness schedule and time zone (setup page and settings page)
  bool nachtAn = cfg.nachtVon != cfg.nachtBis;
  String h = "<fieldset><legend>Brightness</legend><label>Day</label>" + regler("ht", cfg.hellTag) +
             "<label><input type=checkbox name=nachtan value=1 style='width:auto'" + (nachtAn ? " checked" : "") +
             " onchange=\"document.getElementById('nz').style.display=this.checked?'block':'none'\"> Dim at night</label>"
             "<div id=nz style='display:" + (nachtAn ? "block" : "none") + "'><label>Night</label>" + regler("hn", cfg.hellNacht) +
             "<label>from &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; until</label>" +
             stundenAuswahl("nv", nachtAn ? cfg.nachtVon : 23) + " " + stundenAuswahl("nb", nachtAn ? cfg.nachtBis : 7) +
             "</div></fieldset><fieldset><legend>Time zone</legend><select name=tzs style='width:100%' "
             "onchange=\"document.getElementById('tzc').style.display=this.value=='c'?'block':'none'\">";
  bool bekannt = false;
  for (auto &z : ZONEN) {
    bool sel = strcmp(z[1], cfg.tz) == 0;
    bekannt |= sel;
    h += String("<option value=\"") + z[1] + "\"" + (sel ? " selected" : "") + ">" + z[0] + "</option>";
  }
  h += String("<option value=c") + (bekannt ? "" : " selected") + ">Custom (POSIX TZ string)</option></select>"
       "<input id=tzc name=tzc maxlength=47 style='width:100%;display:" + (bekannt ? "none" : "block") +
       "' value=\"" + htmlEsc(cfg.tz) + "\"></fieldset>";
  return h;
}

static void einstellFelderUebernehmen() {   // counterpart of einstellFelder(); writes only what changed
  if (server.hasArg("ht")) {
    int tag = server.arg("ht").toInt(), nacht = server.arg("hn").toInt();
    int von = server.arg("nv").toInt(), bis = server.arg("nb").toInt();
    if (!server.hasArg("nachtan")) { von = 0; bis = 0; }   // equal hours = no night
    zeitplanSetzen(tag, nacht, von, bis);
  }
  if (server.hasArg("tzs")) zeitzoneSetzen(server.arg("tzs") == "c" ? server.arg("tzc").c_str() : server.arg("tzs").c_str());
}

// ---------------------------------------------------------------- Web: settings in normal operation
// http://<device>/ with HTTP Basic auth (admin / device key): status and all settings. The page is sent in
// chunks to keep the RAM use low.
static char wlanNeuSsid[33], wlanNeuPass[65];
static uint32_t wlanWechselAb = 0;   // millis() at which the Wi-Fi change is tried (0 = none)

static bool angemeldet() {
  if (server.authenticate("admin", cfg.token)) return true;
  server.requestAuthentication(BASIC_AUTH, "KAC");
  return false;
}

static void einstellungenSeite(const String &meldung) {
  char alter[24], lauf[24];
  if (!datenDa) strlcpy(alter, "none yet", sizeof(alter));
  else { dauerText(alter, sizeof(alter), (millis() - letzteDaten) / 1000); strlcat(alter, " ago", sizeof(alter)); }
  dauerText(lauf, sizeof(lauf), millis() / 1000);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(F("<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width'>"
    "<title>KAC</title><style>body{font-family:sans-serif;max-width:480px;margin:1em auto;padding:0 1em}"
    "input,select,button{padding:.5em;margin:.3em 0;font-size:1em;box-sizing:border-box}label{display:block;margin-top:.6em}"
    ".w{width:100%}.m{color:#080;font-weight:bold}fieldset{margin:.8em 0;border:1px solid #ccc}td{padding:.1em .6em .1em 0}"
    "</style></head><body><h2>KAC – KleinerAnzeigeComputer</h2>"));
  if (meldung.length()) server.sendContent("<p class=m>" + meldung + "</p>");
  server.sendContent(String("<fieldset><legend>Status</legend><table>") +
    "<tr><td>Device</td><td>" + geraeteName + "</td></tr><tr><td>Firmware</td><td>KAC " KAC_VERSION "</td></tr>" +
    "<tr><td>Address</td><td>" + WiFi.localIP().toString() + "</td></tr><tr><td>Wi-Fi</td><td>" + htmlEsc(cfg.ssid) +
    ", " + WiFi.RSSI() + " dBm</td></tr><tr><td>Uptime</td><td>" + lauf + "</td></tr><tr><td>Data</td><td>" + alter +
    "</td></tr><tr><td>Free RAM</td><td>" + ESP.getFreeHeap() / 1024 + " KB</td></tr></table></fieldset>");
  server.sendContent("<form method=post action=/einstellungen>" + einstellFelder());
  // clock and display
  String h = "<fieldset><legend>Clock and pages</legend><label><input type=checkbox name=uhr12 value=1 style='width:auto'" +
             String(cfg.uhr12 ? " checked" : "") + "> 12-hour clock (am/pm)</label><label>Date format</label><select name=df class=w>";
  for (int i = 0; i < DATUMSFORMATE_N; i++)
    h += String("<option value=") + i + (i == cfg.datumFormat ? " selected" : "") + ">" + DATUMSFORMATE[i][1] + "</option>";
  h += "</select><label>NTP server (empty: pool.ntp.org)</label><input class=w name=ntp maxlength=39 value=\"" + htmlEsc(cfg.ntp) +
       "\"><label>Seconds per page (a dashboard may override it)</label><input name=rot type=number min=5 max=255 value=" +
       String(cfg.rotStandard) + " style='width:40%'></fieldset>";
  server.sendContent(h);
  // Wi-Fi
  h = "<fieldset><legend>Wi-Fi</legend><label>Network</label><input class=w name=ssid list=netze maxlength=32 value=\"" +
      htmlEsc(cfg.ssid) + "\"><datalist id=netze>";
  if (server.hasArg("scan")) {
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) h += "<option value=\"" + htmlEsc(WiFi.SSID(i)) + "\">" + WiFi.RSSI(i) + " dBm</option>";
  }
  h += String("</datalist><a href=/?scan=1>search networks</a><label>Password (empty: keep the stored one for the same network)</label>"
       "<input class=w name=pass type=password maxlength=64><small>A new network is tried first; if the device cannot "
       "connect within 20 s, it goes back to the current one.</small></fieldset>");
  server.sendContent(h);
  server.sendContent("<fieldset><legend>Device key</legend><small>For the dashboard and as the password of this page; up to 32 "
    "characters, no spaces</small><input class=w name=key maxlength=32 value=\"" + htmlEsc(cfg.token) + "\"></fieldset>"
    "<button class=w>Save</button></form>");
  server.sendContent(F("<fieldset><legend>Maintenance</legend>"
    "<form method=post action=/neustart style='display:inline'><button>Restart</button></form> "
    "<form method=post action=/werkseinstellungen style='display:inline' onsubmit=\"return confirm('Erase Wi-Fi, key and all "
    "settings? The device then starts the setup.')\"><button>Factory reset</button></form>"
    "<p><a href=/update>Firmware update</a> &middot; <a href=/api/status>Status (JSON)</a> &middot; "
    "<a href=https://www.bk99.de/en/blog/2026/kac/>Guide</a> &middot; "
    "<a href=https://github.com/GiT-BeDa/KleinerAnzeigeComputer>GitHub</a></p></fieldset></body></html>"));
  server.sendContent("");
}

static void einstellungenZeigen() {
  if (angemeldet()) einstellungenSeite("");
}

static void einstellungenSpeichern() {
  if (!angemeldet()) return;
  einstellFelderUebernehmen();
  // clock and pages: write only if something changed
  uint8_t uhr12 = server.hasArg("uhr12"), df = constrain(server.arg("df").toInt(), 0, DATUMSFORMATE_N - 1);
  uint8_t rot = constrain(server.arg("rot").toInt(), 5, 255);
  String ntp = server.arg("ntp");
  ntp.trim();
  if (ntp.length() && !tzGueltig(ntp.c_str(), sizeof(cfg.ntp))) ntp = cfg.ntp;
  if (uhr12 != cfg.uhr12 || df != cfg.datumFormat || rot != cfg.rotStandard || ntp != cfg.ntp) {
    bool ntpNeu = ntp != cfg.ntp;
    cfg.uhr12 = uhr12; cfg.datumFormat = df; cfg.rotStandard = rot;
    strlcpy(cfg.ntp, ntp.c_str(), sizeof(cfg.ntp));
    rotS = rot;
    speichern();
    if (ntpNeu) configTime(cfg.tz, cfg.ntp[0] ? cfg.ntp : "pool.ntp.org", "time.cloudflare.com");
  }
  String meldung = "Saved.";
  const String &key = server.arg("key");
  if (key.length() && key != cfg.token && schluesselGueltig(key.c_str())) {
    strlcpy(cfg.token, key.c_str(), sizeof(cfg.token));
    speichern();
    updater.updateCredentials("admin", cfg.token);
    meldung += " The device key has changed: log in again with the new key and update your dashboard.";
  }
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  ssid.trim();
  if (ssid.length() && ssid.length() <= 32 && pass.length() <= 64 && (ssid != cfg.ssid || pass.length())) {
    strlcpy(wlanNeuSsid, ssid.c_str(), sizeof(wlanNeuSsid));
    strlcpy(wlanNeuPass, pass.length() || ssid != cfg.ssid ? pass.c_str() : cfg.pass, sizeof(wlanNeuPass));
    wlanWechselAb = millis() + 2000;   // after this answer has been sent
    meldung += " Switching to Wi-Fi " + htmlEsc(ssid) + " now. If it fails, the device returns to " + htmlEsc(cfg.ssid) +
               " within about 30 s. In a new network the address may change: see the Device page on the display.";
  }
  einstellungenSeite(meldung);
}

static void wlanWechselPruefen() {   // from loop(): try the new Wi-Fi, keep it only if it connects
  if (!wlanWechselAb || (int32_t)(millis() - wlanWechselAb) < 0) return;
  wlanWechselAb = 0;
  WiFi.disconnect();
  WiFi.begin(wlanNeuSsid, wlanNeuPass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(200);
  if (WiFi.status() == WL_CONNECTED) {
    strlcpy(cfg.ssid, wlanNeuSsid, sizeof(cfg.ssid));
    strlcpy(cfg.pass, wlanNeuPass, sizeof(cfg.pass));
    speichern();
  } else {
    WiFi.disconnect();
    WiFi.begin(cfg.ssid, cfg.pass);   // back to the old network
  }
  memset(wlanNeuPass, 0, sizeof(wlanNeuPass));
}

static void neustart() {
  if (!angemeldet()) return;
  server.send(200, "text/html", "<!doctype html><meta charset=utf-8><p>Restarting ... <a href=/>back</a> in about 20 s.</p>");
  delay(500);
  ESP.restart();
}

static void werkseinstellungen() {
  if (!angemeldet()) return;
  server.send(200, "text/html", "<!doctype html><meta charset=utf-8><p>Erased. The device restarts and shows the setup screen.</p>");
  delay(500);
  memset(&cfg, 0, sizeof(cfg));   // magic 0: defaults, no Wi-Fi, new key on the next start
  speichern();
  WiFi.persistent(true);
  WiFi.disconnect(true);           // also forget the Wi-Fi in the SDK settings
  delay(200);
  ESP.restart();
}

static void einrichtungSeite() {
  int n = WiFi.scanNetworks();
  String h = F("<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width'>"
               "<title>KAC</title><style>body{font-family:sans-serif;max-width:420px;margin:1em auto;"
               "padding:0 1em}input,select,button{width:100%;padding:.5em;margin:.3em 0;font-size:1em;box-sizing:border-box}"
               "fieldset{margin:.8em 0;border:1px solid #ccc}</style></head><body>"
               "<h2>KAC – KleinerAnzeigeComputer</h2><form method=post action=/speichern><label>Wi-Fi</label><select name=ssid>");
  for (int i = 0; i < n; i++) {
    String s = htmlEsc(WiFi.SSID(i));
    h += "<option value=\"" + s + "\"" + (WiFi.SSID(i) == cfg.ssid ? " selected" : "") + ">" + s + " (" +
         WiFi.RSSI(i) + " dBm)</option>";
  }
  h += F("</select><label>or enter the name</label><input name=ssid2 maxlength=32>"
         "<label>Password (empty: keep the stored one for the same network)</label>"
         "<input name=pass type=password maxlength=64>");
  h += einstellFelder();
  h += "<label>Device key: copy it into your dashboard configuration; also the password for firmware updates "
       "(user admin). Up to 32 characters, no spaces; empty keeps the current key</label><input name=key maxlength=32 value=\"" +
       htmlEsc(cfg.token) + "\">";
  h += F(
         "<button>Save and restart</button></form>"
         "<p><a href=/update>Firmware update</a> (user admin)</p></body></html>");
  server.send(200, "text/html", h);
}

static void einrichtungSpeichern() {
  String ssid = server.arg("ssid2").length() ? server.arg("ssid2") : server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) {
    server.send(400, "text/plain", "Invalid Wi-Fi name or password");
    return;
  }
  bool gleichesNetz = ssid == cfg.ssid;
  strlcpy(cfg.ssid, ssid.c_str(), sizeof(cfg.ssid));
  if (!(gleichesNetz && pass.length() == 0)) strlcpy(cfg.pass, pass.c_str(), sizeof(cfg.pass));   // empty: keep
  einstellFelderUebernehmen();
  if (schluesselGueltig(server.arg("key").c_str())) strlcpy(cfg.token, server.arg("key").c_str(), sizeof(cfg.token));
  speichern();
  server.send(200, "text/html",
              "<!doctype html><meta charset=utf-8><p>Saved. The device restarts and connects to "
              "the Wi-Fi. The access point disappears.</p>");
  delay(1500);
  ESP.restart();
}

static void einrichtungStarten() {
  einrichtung = true;
  einrichtungSeit = millis();
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);   // radio on first: the random number generator draws on RF noise
  const char *zeichen = "abcdefghjkmnpqrstuvwxyz23456789";   // no easily confused characters
  for (int i = 0; i < 10; i++) apPass[i] = zeichen[RANDOM_REG32 % strlen(zeichen)];
  apPass[10] = 0;
  WiFi.softAP(apName, apPass);
  server.on("/", HTTP_GET, einrichtungSeite);
  server.on("/speichern", HTTP_POST, einrichtungSpeichern);
  schluesselSicherstellen();
  updater.setup(&server, "/update", "admin", cfg.token);
  server.begin();
  char z2[48], z3[48];
  snprintf(z2, sizeof(z2), "Password: %s", apPass);
  snprintf(z3, sizeof(z3), "http://%s/", WiFi.softAPIP().toString().c_str());
  char z1[32];
  snprintf(z1, sizeof(z1), "Wi-Fi: %s", apName);
  char z4[48];
  snprintf(z4, sizeof(z4), "Key: %s", cfg.token);
  const char *zeilen[] = {"Setup", z1, z2, z3, z4, "", "Ends after 15 min"};
  vollbildText(zeilen, 7);
  helligkeit(80);
}

// ---------------------------------------------------------------- Start normal operation
static bool verbinden(const char *ssid, const char *pass, uint32_t warteMs) {
  WiFi.begin(ssid, pass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < warteMs) {
    delay(200);
    if (!gesteZurueckgesetzt && millis() > GESTE_FENSTER_MS) {
      cfg.starts = 0; speichern(); gesteZurueckgesetzt = true;
    }
  }
  return WiFi.status() == WL_CONNECTED;
}

static void normalStarten() {
  // Access point permanently off: store "client only" in the SDK settings as well,
  // otherwise the SDK briefly restarts an access point stored by a previous firmware on every boot
  WiFi.persistent(true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.hostname(geraeteName);
  WiFi.setAutoReconnect(true);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  char z2[48];
  snprintf(z2, sizeof(z2), "Wi-Fi %s ...", cfg.ssid);
  const char *zeilen[] = {"KAC", z2};
  vollbildText(zeilen, 2);
  verbinden(cfg.ssid, cfg.pass, 20000);   // after that the SDK keeps trying (auto-reconnect)

  configTime(cfg.tz, cfg.ntp[0] ? cfg.ntp : "pool.ntp.org", "time.cloudflare.com");
  server.collectHeaders("X-Token");
  server.on("/api/seiten", HTTP_POST, apiSeiten);
  server.on("/api/status", HTTP_GET, apiStatus);
  server.on("/api/schluessel", HTTP_POST, apiSchluessel);
  server.on("/", HTTP_GET, einstellungenZeigen);
  server.on("/einstellungen", HTTP_POST, einstellungenSpeichern);
  server.on("/neustart", HTTP_POST, neustart);
  server.on("/werkseinstellungen", HTTP_POST, werkseinstellungen);
  schluesselSicherstellen();
  updater.setup(&server, "/update", "admin", cfg.token);
  server.begin();

  tft.fillScreen(C_BG);
  tft.drawFastHLine(0, KOPF_H + 1, 240, C_LINIE);
}

// ---------------------------------------------------------------- Arduino
void setup() {
  snprintf(geraeteName, sizeof(geraeteName), "KleinerAnzeigeComputer-%06x", ESP.getChipId());
  snprintf(apName, sizeof(apName), "KAC-%06x", ESP.getChipId());
  analogWriteRange(1023);
  analogWriteFreq(1000);
  pinMode(PIN_BL, OUTPUT);
  helligkeit(0);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(C_BG);
  spr.setColorDepth(8);
  spr.createSprite(240, KOPF_H);
  helligkeit(hell);

  // Load settings; boot counter for the power-cycle gesture
  EEPROM.begin(sizeof(Speicher));
  EEPROM.get(0, cfg);
  if (cfg.magic != MAGIC) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = MAGIC;
    // first boot: take over the Wi-Fi stored in the SDK settings (if the previous firmware saved it)
    struct station_config sc;
    if (wifi_station_get_config_default(&sc) && sc.ssid[0]) {
      strlcpy(cfg.ssid, (const char *)sc.ssid, sizeof(cfg.ssid));
      strlcpy(cfg.pass, (const char *)sc.password, sizeof(cfg.pass));
    }
  }
  if (!tzGueltig(cfg.tz, sizeof(cfg.tz))) strlcpy(cfg.tz, TZ_STANDARD, sizeof(cfg.tz));   // also after updating from <0.2.3
  if (cfg.extraMarke != EXTRA_MARKE || cfg.datumFormat >= DATUMSFORMATE_N || cfg.rotStandard < 5 ||
      !tzGueltig(cfg.ntp, sizeof(cfg.ntp)) && cfg.ntp[0]) {
    cfg.extraMarke = EXTRA_MARKE;   // defaults (also after updating from <0.7.0)
    cfg.uhr12 = 0; cfg.datumFormat = 0; cfg.rotStandard = 30; cfg.ntp[0] = 0;
  }
  rotS = cfg.rotStandard;
  if (cfg.planMarke != PLAN_MARKE || !planGueltig(cfg.hellTag, cfg.hellNacht, cfg.nachtVon, cfg.nachtBis)) {
    cfg.planMarke = PLAN_MARKE;   // defaults (also after updating from <0.5.0); saved with the boot counter below
    cfg.hellTag = 90; cfg.hellNacht = 10; cfg.nachtVon = 23; cfg.nachtBis = 7;
  }
  cfg.starts++;
  bool geste = cfg.starts >= GESTE_ANZAHL;
  if (geste) cfg.starts = 0;
  speichern();

  if (geste || cfg.ssid[0] == 0) einrichtungStarten();
  else normalStarten();
}

void loop() {
  server.handleClient();

  if (!gesteZurueckgesetzt && millis() > GESTE_FENSTER_MS) {   // ran long enough: not a gesture
    cfg.starts = 0;
    speichern();
    gesteZurueckgesetzt = true;
  }

  if (einrichtung) {
    if (millis() - einrichtungSeit > EINRICHTUNG_MAX_MS) ESP.restart();
    return;
  }

  wlanWechselPruefen();
  static uint32_t zuletzt = 0;
  if (millis() - zuletzt >= 250) {
    zuletzt = millis();
    uint8_t soll = sollHelligkeit();
    if (soll != hell) { hell = soll; helligkeit(hell); }
    anzeigen();
  }
}
