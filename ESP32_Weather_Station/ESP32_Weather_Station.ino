#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "TFT_eSPI_User_Setup.h"
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <time.h>
#include "Config.h"

SPIClass touchBus(VSPI);
TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen touch(TOUCH_PIN_CS, TOUCH_PIN_IRQ);

// The project uses Adafruit-style names below; map them to TFT_eSPI colours.
#define ILI9341_BLACK TFT_BLACK
#define ILI9341_NAVY TFT_NAVY
#define ILI9341_WHITE TFT_WHITE
#define ILI9341_LIGHTGREY TFT_LIGHTGREY
#define ILI9341_YELLOW TFT_YELLOW
#define ILI9341_CYAN TFT_CYAN
#define ILI9341_GREEN TFT_GREEN
#define ILI9341_ORANGE TFT_ORANGE
#define ILI9341_BLUE TFT_BLUE
#define ILI9341_DARKGREY TFT_DARKGREY
#define ILI9341_MAROON TFT_MAROON
WiFiManager wifiManager;

enum Page : uint8_t { HOME, FORECAST, AIR_QUALITY, WIFI_SCAN };
Page page = HOME;
bool portalActive = false;
bool screenDirty = true;
int lastScanResult = -999;
uint32_t lastWeather = 0, lastCrypto = 0, lastAirQuality = 0, lastClockDraw = 0, lastTouch = 0;
uint32_t lastReconnect = 0;
time_t weatherUpdatedAt = 0;

struct WeatherData {
  float temperature = NAN;
  float apparentTemperature = NAN;
  float windSpeed = NAN;
  float windGusts = NAN;
  float pressure = NAN;
  int humidity = -1;
  int code = -1;
  float minTemp[3] = {NAN, NAN, NAN};
  float maxTemp[3] = {NAN, NAN, NAN};
  float uvMax[3] = {NAN, NAN, NAN};
  int dayCode[3] = {-1, -1, -1};
  int rainChance[3] = {-1, -1, -1};
  String day[3];
  String sunrise[3];
  String sunset[3];
  bool valid = false;
} weather;

struct CryptoData { float btc = NAN, eth = NAN, doge = NAN, egld = NAN; bool valid = false; } crypto;
struct AirQualityData { float europeanAqi = NAN, pm25 = NAN; bool valid = false; } airQuality;
String statusLine = "Avvio...";

void loadWeatherCache() {
  Preferences prefs;
  if (!prefs.begin("meteo", true)) return;
  if (prefs.getBool("valid", false)) {
    weather.temperature = prefs.getFloat("temp", NAN);
    weather.apparentTemperature = prefs.getFloat("feels", NAN);
    weather.windSpeed = prefs.getFloat("wind", NAN);
    weather.windGusts = prefs.getFloat("gust", NAN);
    weather.pressure = prefs.getFloat("press", NAN);
    weather.humidity = prefs.getInt("humid", -1);
    weather.code = prefs.getInt("code", -1);
    weatherUpdatedAt = (time_t)prefs.getLong64("updated", 0);
    for (int i = 0; i < 3; ++i) {
      String suffix = String(i);
      weather.minTemp[i] = prefs.getFloat(("min" + suffix).c_str(), NAN);
      weather.maxTemp[i] = prefs.getFloat(("max" + suffix).c_str(), NAN);
      weather.uvMax[i] = prefs.getFloat(("uvmax" + suffix).c_str(), NAN);
      weather.dayCode[i] = prefs.getInt(("code" + suffix).c_str(), -1);
      weather.rainChance[i] = prefs.getInt(("rain" + suffix).c_str(), -1);
      weather.day[i] = prefs.getString(("day" + suffix).c_str(), "");
      weather.sunrise[i] = prefs.getString(("rise" + suffix).c_str(), "");
      weather.sunset[i] = prefs.getString(("set" + suffix).c_str(), "");
    }
    weather.valid = isfinite(weather.temperature) && isfinite(weather.windSpeed) &&
                    isfinite(weather.pressure) && weather.humidity >= 0 && weather.code >= 0;
    for (int i = 0; i < 3; ++i) {
      weather.valid = weather.valid && isfinite(weather.minTemp[i]) && isfinite(weather.maxTemp[i]) &&
                      weather.dayCode[i] >= 0 && weather.day[i].length() > 0;
    }
  }
  prefs.end();
  if (weather.valid) {
    statusLine = "Meteo salvato";
    Serial.println("Meteo precedente caricato dalla memoria");
  }
}

void saveWeatherCache() {
  Preferences prefs;
  if (!prefs.begin("meteo", false)) { Serial.println("Impossibile aprire memoria meteo"); return; }
  prefs.putBool("valid", weather.valid);
  prefs.putFloat("temp", weather.temperature);
  prefs.putFloat("feels", weather.apparentTemperature);
  prefs.putFloat("wind", weather.windSpeed);
  prefs.putFloat("gust", weather.windGusts);
  prefs.putFloat("press", weather.pressure);
  prefs.putInt("humid", weather.humidity);
  prefs.putInt("code", weather.code);
  prefs.putLong64("updated", (int64_t)weatherUpdatedAt);
  for (int i = 0; i < 3; ++i) {
    String suffix = String(i);
    prefs.putFloat(("min" + suffix).c_str(), weather.minTemp[i]);
    prefs.putFloat(("max" + suffix).c_str(), weather.maxTemp[i]);
    prefs.putFloat(("uvmax" + suffix).c_str(), weather.uvMax[i]);
    prefs.putInt(("code" + suffix).c_str(), weather.dayCode[i]);
    prefs.putInt(("rain" + suffix).c_str(), weather.rainChance[i]);
    prefs.putString(("day" + suffix).c_str(), weather.day[i]);
    prefs.putString(("rise" + suffix).c_str(), weather.sunrise[i]);
    prefs.putString(("set" + suffix).c_str(), weather.sunset[i]);
  }
  prefs.end();
}

String weatherUpdateLabel() {
  if (weatherUpdatedAt < 1700000000) return "AGGIORNAMENTO --:--";
  struct tm updateTm;
  if (!localtime_r(&weatherUpdatedAt, &updateTm)) return "AGGIORNAMENTO --:--";
  char label[24];
  strftime(label, sizeof(label), "AGGIORNATO %H:%M", &updateTm);
  return String(label);
}

int textPixelWidth(const String &text, uint8_t size) {
  // Built-in Adafruit GFX font is 6 pixels per character at size 1.
  return text.length() * 6 * size;
}

bool readTouch(int &x, int &y) {
  if (!touch.touched()) return false;
  TS_Point point = touch.getPoint();
  if (point.z < 50) return false;
  x = constrain(lroundf(TOUCH_AX * point.x + TOUCH_BX * point.y + TOUCH_CX), 0, 319);
  y = constrain(lroundf(TOUCH_AY * point.x + TOUCH_BY * point.y + TOUCH_CY), 0, 239);
  return true;
}

void printClipped(const String &text, int x, int y, int maxWidth, uint16_t color, uint8_t size = 1) {
  tft.setTextSize(size); tft.setTextColor(color);
  String out = text;
  while (out.length() && textPixelWidth(out, size) > maxWidth) out.remove(out.length() - 1);
  tft.setCursor(x, y); tft.print(out);
}

String weatherLabel(int code) {
  if (code < 0 || code > 99) return "Dato non disponibile";
  if (code == 0) return "Sereno";
  if (code <= 2) return "Poco nuvoloso";
  if (code == 3) return "Coperto";
  if (code <= 48) return "Nebbia";
  if (code <= 57) return "Pioviggine";
  if (code <= 67) return "Pioggia";
  if (code <= 77) return "Neve";
  if (code <= 82) return "Rovesci";
  if (code == 85 || code == 86) return "Neve intensa";
  if (code >= 95) return "Temporale";
  return "Variabile";
}

void drawWeatherIcon(int x, int y, int code, uint8_t scale = 1) {
  const uint16_t sun = ILI9341_YELLOW, cloud = ILI9341_LIGHTGREY, rain = ILI9341_CYAN;
  bool wet = (code >= 51 && code <= 82) || code == 85 || code == 86;
  bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
  bool storm = code >= 95;
  bool cloudy = code >= 1 && code <= 3;
  if (code == 0 || cloudy) {
    tft.fillCircle(x + 12 * scale, y + 12 * scale, 8 * scale, sun);
    for (int i = 0; i < 8; ++i) {
      float a = i * PI / 4; int x1 = x + 12 * scale + cos(a) * 12 * scale;
      int y1 = y + 12 * scale + sin(a) * 12 * scale;
      int x2 = x + 12 * scale + cos(a) * 17 * scale;
      int y2 = y + 12 * scale + sin(a) * 17 * scale;
      tft.drawLine(x1, y1, x2, y2, sun);
    }
  }
  if (cloudy || wet || storm || (code >= 45 && code <= 48)) {
    tft.fillCircle(x + 17 * scale, y + 19 * scale, 8 * scale, cloud);
    tft.fillCircle(x + 27 * scale, y + 17 * scale, 10 * scale, cloud);
    tft.fillRoundRect(x + 9 * scale, y + 19 * scale, 29 * scale, 11 * scale, 5 * scale, cloud);
  }
  if (wet) for (int i = 0; i < 3; ++i) tft.drawLine(x + (15 + i * 8) * scale, y + 33 * scale, x + (12 + i * 8) * scale, y + 40 * scale, snow ? ILI9341_WHITE : rain);
  if (storm) { tft.fillTriangle(x + 25 * scale, y + 30 * scale, x + 17 * scale, y + 46 * scale, x + 25 * scale, y + 43 * scale, ILI9341_YELLOW); }
}

void drawHeader(const String &title) {
  tft.fillRect(0, 0, 320, 27, tft.color565(8, 16, 27));
  tft.drawFastHLine(0, 26, 320, tft.color565(31, 47, 65));
  tft.setTextSize(2); tft.setTextColor(ILI9341_WHITE); tft.setCursor(8, 6); tft.print(title);
  tft.fillCircle(267, 13, 3, WiFi.status() == WL_CONNECTED ? ILI9341_GREEN : ILI9341_ORANGE);
  tft.setTextSize(1); tft.setTextColor(ILI9341_LIGHTGREY); tft.setCursor(274, 9); tft.print(WiFi.status() == WL_CONNECTED ? "ONLINE" : "OFFLINE");
}

void drawNavigation() {
  const char *labels[] = {"METEO", "3 GIORNI", "ARIA", "WIFI"};
  for (int i = 0; i < 4; ++i) {
    int x = i * 80;
    tft.fillRect(x, 216, 79, 24, i == page ? ILI9341_BLUE : ILI9341_DARKGREY);
    tft.drawRect(x, 216, 79, 24, ILI9341_LIGHTGREY);
    tft.setTextColor(ILI9341_WHITE); tft.setTextSize(1);
    int cx = x + (80 - textPixelWidth(labels[i], 1)) / 2;
    tft.setCursor(cx, 224); tft.print(labels[i]);
  }
}

void drawHome() {
  // Blu notte sufficientemente distinto dal nero anche sul TFT CYD.
  const uint16_t background = tft.color565(8, 24, 54);
  const uint16_t card = tft.color565(17, 39, 68);
  const uint16_t muted = tft.color565(151, 173, 198);
  const uint16_t accent = tft.color565(91, 190, 255);
  const uint16_t edge = tft.color565(36, 62, 88);
  tft.fillScreen(background);

  tft.fillRoundRect(12, 4, 30, 25, 7, accent);
  tft.setTextSize(2); tft.setTextColor(background); tft.setCursor(21, 8); tft.print("M");
  tft.setTextColor(ILI9341_WHITE); tft.setCursor(50, 5); tft.print("METEO MILANO");
  tft.setTextSize(1); tft.setTextColor(muted); tft.setCursor(51, 20); tft.print("STAZIONE LOCALE · LOMBARDIA");
  struct tm tm; char clockText[8] = "--:--";
  if (getLocalTime(&tm, 10)) strftime(clockText, sizeof(clockText), "%H:%M", &tm);
  tft.setTextSize(2); tft.setTextColor(ILI9341_WHITE); tft.setCursor(250, 7); tft.print(clockText);

  time_t currentEpoch = time(nullptr);
  bool weatherStale = weatherUpdatedAt < 1700000000 ||
      (currentEpoch >= 1700000000 && currentEpoch - weatherUpdatedAt > 20 * 60);
  bool usingCache = weather.valid && (WiFi.status() != WL_CONNECTED || weatherStale);
  tft.fillRoundRect(12, 34, 296, 22, 7, card);
  tft.drawRoundRect(12, 34, 296, 22, 7, edge);
  uint16_t updateColor = usingCache ? ILI9341_ORANGE : ILI9341_GREEN;
  tft.fillRoundRect(17, 37, 63, 16, 6, updateColor);
  tft.setTextColor(background);
  if (usingCache) { tft.setTextSize(1); tft.setCursor(28, 41); tft.print("CACHE"); }
  else { tft.setTextSize(2); tft.setCursor(25, 38); tft.print("LIVE"); }
  tft.setTextColor(updateColor); tft.setCursor(91, 41); tft.print(weatherUpdateLabel());

  tft.fillRoundRect(12, 63, 177, 91, 10, card);
  tft.drawRoundRect(12, 63, 177, 91, 10, edge);
  tft.fillRoundRect(196, 63, 112, 91, 10, card);
  tft.drawRoundRect(196, 63, 112, 91, 10, edge);
  tft.setTextSize(1); tft.setTextColor(muted); tft.setCursor(22, 70); tft.print("TEMPERATURA ATTUALE");
  if (weather.valid) {
    tft.setTextSize(4); tft.setTextColor(ILI9341_WHITE); tft.setCursor(22, 87); tft.print(weather.temperature, 1);
    tft.setTextSize(1); tft.setTextColor(accent); tft.setCursor(151, 105); tft.print("°C");
    tft.setTextColor(muted); tft.setCursor(22, 137); tft.print("Percepita ");
    if (isfinite(weather.apparentTemperature)) { tft.print(weather.apparentTemperature, 1); tft.print(" C"); }
    else tft.print("-- C");
  } else {
    tft.setTextSize(2); tft.setTextColor(muted); tft.setCursor(22, 98); tft.print("--.- °C");
  }
  tft.setTextSize(1); tft.setTextColor(muted); tft.setCursor(205, 72); tft.print("CONDIZIONI");
  if (weather.valid) {
    drawWeatherIcon(231, 80, weather.code, 1);
    printClipped(weatherLabel(weather.code), 204, 128, 96, ILI9341_WHITE, 1);
    tft.setTextColor(ILI9341_YELLOW); tft.setCursor(204, 145); tft.print("UV max ");
    if (isfinite(weather.uvMax[0])) tft.print(weather.uvMax[0], 1); else tft.print("--");
  } else {
    tft.setTextColor(muted); tft.setCursor(211, 108); tft.print("In attesa...");
  }

  const int panelX[3] = {12, 113, 214};
  const char *panelLabel[3] = {"UMIDITA", "VENTO", "PRESSIONE"};
  for (int i = 0; i < 3; ++i) {
    tft.fillRoundRect(panelX[i], 161, 94, 43, 8, card);
    tft.drawRoundRect(panelX[i], 161, 94, 43, 8, edge);
    tft.setTextSize(1); tft.setTextColor(muted); tft.setCursor(panelX[i] + 9, 168); tft.print(i == 1 ? "VENTO/RAFF." : panelLabel[i]);
  }
  tft.setTextSize(2); tft.setTextColor(accent); tft.setCursor(21, 182);
  if (weather.valid) { tft.print(weather.humidity); tft.print("%"); } else tft.print("--%");
  tft.setTextColor(ILI9341_CYAN); tft.setTextSize(2); tft.setCursor(120, 181);
  if (weather.valid && isfinite(weather.windSpeed)) tft.print(weather.windSpeed, 0); else tft.print("--");
  tft.setTextSize(1); tft.setCursor(120, 196); tft.print("raff. ");
  if (weather.valid && isfinite(weather.windGusts)) tft.print(weather.windGusts, 0); else tft.print("--");
  tft.print(" km/h");
  tft.setTextColor(ILI9341_WHITE); tft.setTextSize(2); tft.setCursor(220, 182);
  if (weather.valid && isfinite(weather.pressure)) tft.print(weather.pressure, 0); else tft.print("----");
  tft.setTextSize(1); tft.setCursor(269, 188); tft.print("hPa");

  String prices = crypto.valid ? "BTC " + String(crypto.btc, 0) + "  ETH " + String(crypto.eth, 0) + "  DOGE " + String(crypto.doge, 3) + "  EGLD " + String(crypto.egld, 2) : "Mercati: aggiornamento in attesa";
  printClipped(prices, 12, 207, 296, ILI9341_GREEN, 1);
  drawNavigation();
}

void drawForecast() {
  drawHeader("PREVISIONI MILANO");
  tft.fillRect(0, 27, 320, 189, tft.color565(7, 20, 38));
  for (int i = 0; i < 3; ++i) {
    int x = 5 + i * 105; tft.fillRoundRect(x, 38, 100, 174, 8, tft.color565(12, 24, 39)); tft.drawRoundRect(x, 38, 100, 174, 8, tft.color565(30, 48, 67));
    printClipped(weather.day[i].length() ? weather.day[i] : String("Giorno ") + (i + 1), x + 8, 49, 85, ILI9341_WHITE, 1);
    drawWeatherIcon(x + 25, 66, weather.dayCode[i], 1);
    tft.setTextColor(ILI9341_ORANGE); tft.setTextSize(2); tft.setCursor(x + 18, 120);
    if (weather.valid) { tft.print(weather.maxTemp[i], 0); tft.print("C"); } else tft.print("--C");
    tft.setTextColor(ILI9341_CYAN); tft.setCursor(x + 18, 145);
    if (weather.valid) { tft.print(weather.minTemp[i], 0); tft.print("C"); } else tft.print("--C");
    tft.setTextSize(1); tft.setCursor(x + 8, 166); tft.setTextColor(ILI9341_LIGHTGREY); tft.print("Pioggia "); tft.print(weather.rainChance[i] >= 0 ? String(weather.rainChance[i]) : "--"); tft.print("%");
    tft.setTextColor(ILI9341_YELLOW); tft.setCursor(x + 8, 178); tft.print("ALBA ");
    tft.print(weather.sunrise[i].length() >= 16 ? weather.sunrise[i].substring(11, 16) : "--:--");
    tft.setTextColor(ILI9341_ORANGE); tft.setCursor(x + 8, 190); tft.print("TRAM ");
    tft.print(weather.sunset[i].length() >= 16 ? weather.sunset[i].substring(11, 16) : "--:--");
    tft.setTextColor(ILI9341_YELLOW); tft.setCursor(x + 8, 202); tft.print("UV max ");
    if (isfinite(weather.uvMax[i])) tft.print(weather.uvMax[i], 1); else tft.print("--");
  }
  drawNavigation();
}

uint16_t airQualityColor(float aqi) {
  if (aqi <= 20) return ILI9341_GREEN;
  if (aqi <= 40) return ILI9341_CYAN;
  if (aqi <= 60) return ILI9341_YELLOW;
  if (aqi <= 80) return ILI9341_ORANGE;
  return ILI9341_MAROON;
}
String airQualityLabel(float aqi) {
  if (aqi <= 20) return "Buona";
  if (aqi <= 40) return "Discreta";
  if (aqi <= 60) return "Moderata";
  if (aqi <= 80) return "Scarsa";
  if (aqi <= 100) return "Molto scarsa";
  return "Estremamente scarsa";
}
void drawAirQuality() {
  drawHeader("QUALITA' ARIA");
  tft.fillRect(0, 27, 320, 189, tft.color565(7, 20, 38));
  tft.setTextSize(1); tft.setTextColor(ILI9341_LIGHTGREY); tft.setCursor(12, 44); tft.print("INDICE EUROPEO (AQI)");
  if (airQuality.valid) {
    tft.setTextSize(5); tft.setTextColor(airQualityColor(airQuality.europeanAqi)); tft.setCursor(20, 64); tft.print(airQuality.europeanAqi, 0);
    printClipped(airQualityLabel(airQuality.europeanAqi), 155, 82, 155, airQualityColor(airQuality.europeanAqi), 1);
    tft.setTextSize(1); tft.setTextColor(ILI9341_LIGHTGREY); tft.setCursor(20, 132); tft.print("Particolato fine PM2.5");
    tft.setTextSize(3); tft.setTextColor(ILI9341_WHITE); tft.setCursor(20, 151); tft.print(airQuality.pm25, 1);
    tft.setTextSize(1); tft.setCursor(103, 164); tft.print("ug/m3");
    tft.setTextColor(ILI9341_DARKGREY); tft.setCursor(12, 181); tft.print("Fonte: Open-Meteo / CAMS ENSEMBLE");
    tft.setCursor(12, 194); tft.print("Stima modellistica per l'area di Milano");
  } else {
    tft.setTextSize(2); tft.setTextColor(ILI9341_LIGHTGREY); tft.setCursor(22, 92); tft.print("Dati aria in attesa...");
  }
  drawNavigation();
}

void drawWifi() {
  drawHeader("SCANNER WIFI"); tft.fillRect(0, 27, 320, 189, tft.color565(7, 20, 38));
  tft.setTextColor(WiFi.status() == WL_CONNECTED ? ILI9341_GREEN : ILI9341_ORANGE);
  tft.setTextSize(1); tft.setCursor(7, 34);
  if (WiFi.status() == WL_CONNECTED) {
    tft.print("Connesso: "); printClipped(WiFi.SSID(), 65, 34, 245, ILI9341_GREEN, 1);
    tft.setTextColor(ILI9341_LIGHTGREY); tft.setCursor(7, 47); tft.print("IP: "); tft.print(WiFi.localIP());
  } else {
    tft.print("WiFi non connesso. Rete richiesta: M1 (2,4 GHz)");
    tft.setTextColor(ILI9341_LIGHTGREY); tft.setCursor(7, 47); tft.print("Portale: Meteo-Milano");
  }
  int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) { tft.setCursor(8, 64); tft.print("Scansione in corso..."); }
  else if (count < 0) { tft.setCursor(8, 64); tft.print("Tocca SCANSIONA per cercare reti."); }
  else {
    for (int i = 0; i < min(count, 4); ++i) {
      int y = 65 + i * 27; String ssid = WiFi.SSID(i); if (!ssid.length()) ssid = "<rete nascosta>";
      printClipped(ssid, 8, y, 154, ILI9341_WHITE, 1);
      tft.setTextColor(ILI9341_CYAN); tft.setCursor(168, y); tft.printf("CH %d", WiFi.channel(i));
      tft.setTextColor(WiFi.RSSI(i) > -67 ? ILI9341_GREEN : ILI9341_ORANGE); tft.setCursor(230, y); tft.printf("%d dBm", WiFi.RSSI(i));
      tft.drawFastHLine(5, y + 17, 310, ILI9341_DARKGREY);
    }
  }
  tft.fillRoundRect(8, 183, 142, 25, 5, ILI9341_BLUE); tft.fillRoundRect(168, 183, 142, 25, 5, ILI9341_MAROON);
  tft.setTextColor(ILI9341_WHITE); tft.setTextSize(1); tft.setCursor(41, 192); tft.print("SCANSIONA"); tft.setCursor(190, 192); tft.print("PORTALE WIFI");
  drawNavigation();
}
void drawPage() { if (page == HOME) drawHome(); else if (page == FORECAST) drawForecast(); else if (page == AIR_QUALITY) drawAirQuality(); else drawWifi(); screenDirty = false; }

bool getJson(const String &url, JsonDocument &doc) {
  WiFiClientSecure client; client.setInsecure(); HTTPClient http; http.setTimeout(12000);
  if (!http.begin(client, url)) { Serial.println("HTTPS: impossibile iniziare la richiesta"); return false; }
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  int result = http.GET();
  if (result != HTTP_CODE_OK) { Serial.printf("HTTPS GET fallita: %d (%s)\n", result, http.errorToString(result).c_str()); http.end(); return false; }
  String payload = http.getString();
  http.end();
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    String preview = payload.substring(0, 180);
    preview.replace("\r", " "); preview.replace("\n", " ");
    Serial.printf("JSON non valido: %s; risposta ricevuta (%u byte): %s\n",
                  err.c_str(), (unsigned)payload.length(), preview.c_str());
  }
  return !err;
}

void updateWeather() {
  if (WiFi.status() != WL_CONNECTED) return;
  DynamicJsonDocument doc(8192);
  String url = "https://api.open-meteo.com/v1/forecast?latitude=45.4642&longitude=9.1900&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,wind_gusts_10m,pressure_msl&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset,uv_index_max&timezone=Europe%2FRome&forecast_days=3";
  if (!getJson(url, doc)) { statusLine = "Meteo non raggiungibile"; Serial.println(statusLine); return; }
  float temperature = doc["current"]["temperature_2m"] | NAN;
  float apparentTemperature = doc["current"]["apparent_temperature"] | NAN;
  float windSpeed = doc["current"]["wind_speed_10m"] | NAN;
  float windGusts = doc["current"]["wind_gusts_10m"] | NAN;
  float pressure = doc["current"]["pressure_msl"] | NAN;
  int humidity = doc["current"]["relative_humidity_2m"] | -1;
  int code = doc["current"]["weather_code"] | -1;
  if (!isfinite(temperature) || !isfinite(apparentTemperature) || !isfinite(windSpeed) || !isfinite(windGusts) || !isfinite(pressure) || humidity < 0 || code < 0 || code > 99) {
    statusLine = "Risposta meteo incompleta"; Serial.println(statusLine); return;
  }
  float minTemp[3], maxTemp[3], uvMax[3]; int dayCode[3], rainChance[3]; String days[3], sunrises[3], sunsets[3];
  for (int i = 0; i < 3; ++i) {
    minTemp[i] = doc["daily"]["temperature_2m_min"][i] | NAN;
    maxTemp[i] = doc["daily"]["temperature_2m_max"][i] | NAN;
    uvMax[i] = doc["daily"]["uv_index_max"][i] | NAN;
    dayCode[i] = doc["daily"]["weather_code"][i] | -1;
    rainChance[i] = doc["daily"]["precipitation_probability_max"][i] | -1;
    String iso = doc["daily"]["time"][i] | "";
    sunrises[i] = doc["daily"]["sunrise"][i] | "";
    sunsets[i] = doc["daily"]["sunset"][i] | "";
    if (!isfinite(minTemp[i]) || !isfinite(maxTemp[i]) || dayCode[i] < 0 || dayCode[i] > 99 || iso.length() < 10 || sunrises[i].length() < 16 || sunsets[i].length() < 16) {
      statusLine = "Previsione meteo incompleta"; Serial.println(statusLine); return;
    }
    days[i] = i == 0 ? "Oggi" : iso.substring(8, 10) + "/" + iso.substring(5, 7);
  }
  weather.temperature = temperature; weather.apparentTemperature = apparentTemperature;
  weather.windSpeed = windSpeed; weather.windGusts = windGusts; weather.pressure = pressure;
  weather.humidity = humidity; weather.code = code;
  for (int i = 0; i < 3; ++i) {
    weather.minTemp[i] = minTemp[i]; weather.maxTemp[i] = maxTemp[i];
    weather.uvMax[i] = uvMax[i];
    weather.dayCode[i] = dayCode[i]; weather.rainChance[i] = rainChance[i]; weather.day[i] = days[i];
    weather.sunrise[i] = sunrises[i]; weather.sunset[i] = sunsets[i];
  }
  weather.valid = true;
  time_t now = time(nullptr);
  weatherUpdatedAt = now >= 1700000000 ? now : 0;
  saveWeatherCache();
  statusLine = "Meteo aggiornato"; Serial.println(statusLine); screenDirty = true;
}

void updateCrypto() {
  if (WiFi.status() != WL_CONNECTED) return;
  DynamicJsonDocument doc(2048);
  if (!getJson("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,ethereum,dogecoin,elrond-erd-2&vs_currencies=eur", doc)) { statusLine = "Crypto non raggiungibile"; Serial.println(statusLine); return; }
  float btc = doc["bitcoin"]["eur"] | NAN;
  float eth = doc["ethereum"]["eur"] | NAN;
  float doge = doc["dogecoin"]["eur"] | NAN;
  float egld = doc["elrond-erd-2"]["eur"] | NAN;
  if (!isfinite(btc) || !isfinite(eth) || !isfinite(doge) || !isfinite(egld) || btc <= 0 || eth <= 0 || doge <= 0 || egld <= 0) {
    statusLine = "Risposta crypto incompleta"; Serial.println(statusLine); return;
  }
  crypto.btc = btc; crypto.eth = eth; crypto.doge = doge; crypto.egld = egld; crypto.valid = true;
  statusLine = "Mercati aggiornati";
  Serial.printf("Mercati aggiornati: BTC %.2f EUR, ETH %.2f EUR, DOGE %.5f EUR, EGLD %.2f EUR\n", btc, eth, doge, egld);
  screenDirty = true;
}

void updateAirQuality() {
  if (WiFi.status() != WL_CONNECTED) return;
  DynamicJsonDocument doc(2048);
  String url = "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=45.4642&longitude=9.1900&current=european_aqi,pm2_5&timezone=Europe%2FRome";
  if (!getJson(url, doc)) { statusLine = "Qualita' aria non raggiungibile"; Serial.println(statusLine); return; }
  float aqi = doc["current"]["european_aqi"] | NAN;
  float pm25 = doc["current"]["pm2_5"] | NAN;
  if (!isfinite(aqi) || !isfinite(pm25) || aqi < 0 || pm25 < 0) {
    statusLine = "Risposta qualita' aria incompleta"; Serial.println(statusLine); return;
  }
  airQuality.europeanAqi = aqi; airQuality.pm25 = pm25; airQuality.valid = true;
  statusLine = "Qualita' aria aggiornata"; Serial.println(statusLine); screenDirty = true;
}

void handleTouch() {
  if (millis() - lastTouch < 300) return;
  int x, y; if (!readTouch(x, y)) return; lastTouch = millis();
  if (y >= 216) { page = (Page)constrain(x / 80, 0, 3); screenDirty = true; return; }
  if (page == WIFI_SCAN && y >= 175 && y <= 215) {
    if (x < 158) { WiFi.scanDelete(); WiFi.scanNetworks(true); screenDirty = true; }
    else { portalActive = true; wifiManager.startConfigPortal("Meteo-Milano", WIFI_PORTAL_PASSWORD); screenDirty = true; }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("UI NIGHT BLUE v2 - firmware aggiornato");
  loadWeatherCache();
  pinMode(DISPLAY_BACKLIGHT, OUTPUT); digitalWrite(DISPLAY_BACKLIGHT, HIGH);
  tft.init();
  // Su alcune ESP32-2432S028 USB-C il pannello ILI9341_2 richiede l'inversione
  // hardware dei colori: senza, il blu viene reso giallo.
  tft.invertDisplay(true);
  tft.setRotation(1);
  // Test colore all'accensione: se resta bianco, il firmware/config TFT caricato
  // non corrisponde a questo sketch o il driver non sta ricevendo i comandi colore.
  tft.fillScreen(TFT_BLUE);
  tft.setTextColor(TFT_WHITE); tft.setTextSize(2); tft.setCursor(30, 100); tft.print("TEST TFT BLU");
  Serial.println("TEST TFT: schermata blu per 1 secondo");
  delay(1000);
  tft.fillScreen(tft.color565(8, 24, 54));
  touchBus.begin(TOUCH_PIN_CLK, TOUCH_PIN_MISO, TOUCH_PIN_MOSI, TOUCH_PIN_CS); touch.begin(touchBus); touch.setRotation(0);
  tft.setTextColor(ILI9341_WHITE); tft.setTextSize(2); tft.setCursor(30, 100); tft.print("Meteo Milano"); tft.setTextSize(1); tft.setCursor(30, 130); tft.print("Connessione WiFi...");
  WiFi.mode(WIFI_STA); WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASSWORD);
  Serial.printf("Connessione alla rete WiFi '%s' (solo 2,4 GHz)\n", DEFAULT_WIFI_SSID);
  uint32_t connectStarted = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - connectStarted < 12000) delay(100);
  wifiManager.setCustomHeadElement(
    "<style>.portal-home{position:fixed;right:16px;bottom:16px;z-index:9999;"
    "background:#1565c0;color:#fff!important;padding:12px 20px;border-radius:8px;"
    "text-decoration:none;font-weight:bold;box-shadow:0 2px 8px #555}</style>"
    "<script>document.addEventListener('DOMContentLoaded',function(){"
    "if(location.pathname!=='/'&&!document.querySelector('.portal-home')){"
    "var a=document.createElement('a');a.className='portal-home';a.href='/';"
    "a.textContent='Home';a.setAttribute('aria-label','Torna alla pagina principale');"
    "document.body.appendChild(a);}});</script>");
  wifiManager.setConfigPortalBlocking(false); wifiManager.setConnectTimeout(20);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connessione fallita: avvio portale di configurazione Wi-Fi");
    portalActive = true;
    wifiManager.autoConnect("Meteo-Milano", WIFI_PORTAL_PASSWORD);
  } else {
    Serial.printf("WiFi connesso, IP %s\n", WiFi.localIP().toString().c_str());
  }
  configTzTime(TIME_ZONE, "pool.ntp.org", "time.nist.gov");
  WiFi.scanNetworks(true);
  uint32_t startupTime = millis();
  lastWeather = startupTime - WEATHER_INTERVAL_MS;
  lastCrypto = startupTime - CRYPTO_INTERVAL_MS;
  lastAirQuality = startupTime - AIR_QUALITY_INTERVAL_MS;
}

void loop() {
  wifiManager.process(); handleTouch();
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (portalActive) { portalActive = false; Serial.printf("WiFi connesso, IP %s\n", WiFi.localIP().toString().c_str()); screenDirty = true; }
  } else if (!portalActive && now - lastReconnect >= 15000UL) {
    lastReconnect = now;
    Serial.println("WiFi disconnesso: nuovo tentativo di connessione");
    WiFi.reconnect();
    screenDirty = true;
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (now - lastWeather >= WEATHER_INTERVAL_MS) { lastWeather = now; updateWeather(); }
    if (now - lastCrypto >= CRYPTO_INTERVAL_MS) { lastCrypto = now; updateCrypto(); }
    if (now - lastAirQuality >= AIR_QUALITY_INTERVAL_MS) { lastAirQuality = now; updateAirQuality(); }
  }
  if (page == HOME && now - lastClockDraw >= 60000UL) { lastClockDraw = now; screenDirty = true; }
  int scanResult = WiFi.scanComplete();
  if (page == WIFI_SCAN && scanResult != lastScanResult) { lastScanResult = scanResult; screenDirty = true; }
  if (screenDirty) drawPage();
}
