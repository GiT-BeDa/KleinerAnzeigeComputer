// KleinerAnzeigeComputer (KAC) – custom firmware for the GeekMagic SmallTV Ultra (ESP8266, ST7789 240x240)
//
// Normal operation: Wi-Fi client only (no access point). A program on the network sends pages as JSON
// (POST /api/seiten, header X-Token). The device shows them in rotation and redraws only rows that
// changed – each row is drawn completely in memory (sprite) and then transferred in one piece, so
// nothing flickers. The device sets the clock in the header itself via NTP.
//
// Setup: cut the power 5 times in a row (each run < 10 s) or no Wi-Fi stored → access point
// "KAC-<chip ID>" with a random password shown only on the display; setup page at
// http://192.168.4.1/. Restarts after 15 min without saving.
//
// Firmware update: http://<ip>/update, user admin, password = KAC_TOKEN (set at build time).
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
#error "KAC_TOKEN missing: set it in the environment when building"
#endif
static_assert(sizeof(KAC_TOKEN) > 16, "KAC_TOKEN too short or empty: KAC_TOKEN=<secret> pio run");

// ---------------------------------------------------------------- Settings
// Host name "KleinerAnzeigeComputer-" + chip ID (last 3 bytes of the MAC); the setup Wi-Fi uses the short
// form "KAC-" + chip ID so that it fits on one line of the setup screen
static char geraeteName[33];
static char apName[16];
static const char *TZ_STANDARD = "CET-1CEST,M3.5.0,M10.5.0/3";   // POSIX TZ, default Europe/Berlin
static const uint8_t PIN_BL = 5;                       // backlight, inverted
static const uint8_t GESTE_ANZAHL = 5;                  // boots in a row that start setup mode
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

struct Speicher {          // EEPROM (one flash sector), survives restarts and firmware updates
  uint8_t magic;
  uint8_t starts;
  char ssid[33];
  char pass[65];
  char tz[48];             // POSIX time zone; appended later, so older settings keep their Wi-Fi data
};
static Speicher cfg;
static const uint8_t MAGIC = 0x4B;

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

// Set the time zone; write to flash only if it changed (the dashboard sends it with every update)
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
  char uhr[8] = "--:--", datum[12] = "", titel[16] = "";
  time_t jetzt = time(nullptr);
  if (jetzt > 1700000000) {
    struct tm t;
    localtime_r(&jetzt, &t);
    strftime(uhr, sizeof(uhr), "%H:%M", &t);
    strftime(datum, sizeof(datum), "%d/%m/%Y", &t);
  }
  if (aktSeite >= 0 && aktSeite < seitenAnzahl) strlcpy(titel, seiten[aktSeite].titel, sizeof(titel));
  else strlcpy(titel, "Info", sizeof(titel));
  bool alt = !datenDa || millis() - letzteDaten > DATEN_ALT_MS;
  bool offline = WiFi.status() != WL_CONNECTED;
  char sig[64];
  snprintf(sig, sizeof(sig), "%s|%s|%s|%d|%d", uhr, datum, titel, alt, offline);
  if (!erzwingen && !strcmp(sig, kopfAlt)) return;
  strlcpy(kopfAlt, sig, sizeof(kopfAlt));

  spr.fillSprite(C_BG);
  spr.setFreeFont(&FreeSansBold18pt7b);
  spr.setTextColor(C_TEXT);
  spr.setTextDatum(ML_DATUM);
  spr.drawString(uhr, 4, KOPF_H / 2 + 1);
  if (offline || alt) spr.fillCircle(spr.textWidth(uhr) + 12, KOPF_H / 2, 4, C_ALERT);
  spr.setFreeFont(&FreeSans9pt7b);
  textRechts(datum, RAND_R, 9, C_TEXT);
  textRechts(titel, RAND_R, 26, C_ACCENT);
  spr.pushSprite(0, 0, 0, 0, 240, KOPF_H);
}

// Info page until the first data arrives: status in rows 0-1, below it a QR code to the build guide
static void infoSeite(Seite &s) {
  memset(&s, 0, sizeof(s));
  strlcpy(s.titel, "no data yet", sizeof(s.titel));
  bool online = WiFi.status() == WL_CONNECTED;
  s.z[0].typ = 'h';
  strlcpy(s.z[0].l, "KAC " KAC_VERSION, sizeof(s.z[0].l));
  strlcpy(s.z[0].v, online ? WiFi.localIP().toString().c_str() : "-", sizeof(s.z[0].v));
  s.z[1].typ = 'k';
  strlcpy(s.z[1].l, online ? cfg.ssid : "connecting ...", sizeof(s.z[1].l));
  if (online) snprintf(s.z[1].v, sizeof(s.z[1].v), "%d dBm", (WiFi.RSSI() / 3) * 3);   // 3-dB steps
  for (int i = 2; i < ZEILEN; i++) s.z[i].typ = 'q';   // area of the QR code, not drawn as rows
}

static void qrZeichnen() {
  QRCode qr;
  uint8_t daten[qrcode_getBufferSize(3)];
  qrcode_initText(&qr, daten, 3, ECC_LOW, ANLEITUNG_URL);
  const int m = 4, rand = 2 * m, groesse = qr.size * m + 2 * rand;   // 29 modules → 132 px
  const int x0 = 4, y0 = ZEILE_Y0 + 2 * ZEILE_H + 4;
  tft.fillRect(0, ZEILE_Y0 + 2 * ZEILE_H, 240, 240 - ZEILE_Y0 - 2 * ZEILE_H, C_BG);
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
  int n = seitenAnzahl;
  int neu = 0;
  if (n > 1) {
    time_t jetzt = time(nullptr);
    uint32_t sek = jetzt > 1700000000 ? (uint32_t)jetzt : millis() / 1000;
    neu = (sek / max<uint8_t>(rotS, 5)) % n;
  }
  static Seite info;
  const Seite *s;
  if (n == 0) { infoSeite(info); s = &info; neu = -1; }
  else s = &seiten[neu];
  if (neu != aktSeite) {
    aktSeite = neu;
    memset(zeileGueltig, 0, sizeof(zeileGueltig));
    kopfZeichnen(true);
    if (neu == -1) qrZeichnen();
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
  if (server.header("X-Token") == KAC_TOKEN) return true;
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
  if (doc["b"].is<int>()) { hell = doc["b"]; helligkeit(hell); }
  if (doc["rot"].is<int>()) rotS = doc["rot"];
  if (doc["tz"].is<const char *>()) zeitzoneSetzen(doc["tz"]);
  letzteDaten = millis();
  datenDa = true;
  server.send(200, "application/json", String("{\"ok\":true,\"seiten\":") + n + "}");
}

static void apiStatus() {
  char buf[400];
  snprintf(buf, sizeof(buf),
           "{\"name\":\"%s\",\"version\":\"%s\",\"uptime\":%lu,\"ssid\":\"%s\","
           "\"rssi\":%d,\"heap\":%u,\"seiten\":%u,\"seite\":%d,\"daten_alter_s\":%ld,\"zeit\":%ld,\"tz\":\"%s\"}",
           geraeteName, KAC_VERSION, millis() / 1000, cfg.ssid, WiFi.RSSI(), ESP.getFreeHeap(), seitenAnzahl, aktSeite,
           datenDa ? (long)((millis() - letzteDaten) / 1000) : -1L, (long)time(nullptr), cfg.tz);
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

static void einrichtungSeite() {
  int n = WiFi.scanNetworks();
  String h = F("<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width'>"
               "<title>KAC</title><style>body{font-family:sans-serif;max-width:420px;margin:1em auto;"
               "padding:0 1em}input,select,button{width:100%;padding:.5em;margin:.3em 0;font-size:1em}</style></head><body>"
               "<h2>KAC – KleinerAnzeigeComputer</h2><form method=post action=/speichern><label>Wi-Fi</label><select name=ssid>");
  for (int i = 0; i < n; i++) {
    String s = htmlEsc(WiFi.SSID(i));
    h += "<option value=\"" + s + "\"" + (WiFi.SSID(i) == cfg.ssid ? " selected" : "") + ">" + s + " (" +
         WiFi.RSSI(i) + " dBm)</option>";
  }
  h += F("</select><label>or enter the name</label><input name=ssid2 maxlength=32>"
         "<label>Password (empty: keep the stored one for the same network)</label>"
         "<input name=pass type=password maxlength=64>");
  h += "<label>Time zone (POSIX TZ, e.g. CET-1CEST,M3.5.0,M10.5.0/3)</label><input name=tz maxlength=47 value=\"" +
       htmlEsc(cfg.tz) + "\">";
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
  if (tzGueltig(server.arg("tz").c_str(), sizeof(cfg.tz))) strlcpy(cfg.tz, server.arg("tz").c_str(), sizeof(cfg.tz));
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
  const char *zeichen = "abcdefghjkmnpqrstuvwxyz23456789";   // no easily confused characters
  for (int i = 0; i < 10; i++) apPass[i] = zeichen[RANDOM_REG32 % strlen(zeichen)];
  apPass[10] = 0;
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName, apPass);
  server.on("/", HTTP_GET, einrichtungSeite);
  server.on("/speichern", HTTP_POST, einrichtungSpeichern);
  updater.setup(&server, "/update", "admin", KAC_TOKEN);
  server.begin();
  char z2[48], z3[48];
  snprintf(z2, sizeof(z2), "Password: %s", apPass);
  snprintf(z3, sizeof(z3), "http://%s/", WiFi.softAPIP().toString().c_str());
  char z1[32];
  snprintf(z1, sizeof(z1), "Wi-Fi: %s", apName);
  const char *zeilen[] = {"Setup", z1, z2, z3, "", "Ends after 15 min"};
  vollbildText(zeilen, 6);
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

  configTime(cfg.tz, "pool.ntp.org", "time.cloudflare.com");
  server.collectHeaders("X-Token");
  server.on("/api/seiten", HTTP_POST, apiSeiten);
  server.on("/api/status", HTTP_GET, apiStatus);
  updater.setup(&server, "/update", "admin", KAC_TOKEN);
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

  static uint32_t zuletzt = 0;
  if (millis() - zuletzt >= 250) {
    zuletzt = millis();
    anzeigen();
  }
}
