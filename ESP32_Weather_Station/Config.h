#pragma once

// ESP32-2432S028 / Cheap Yellow Display: ILI9341 on the HSPI bus.
constexpr int DISPLAY_SCLK = 14;
constexpr int DISPLAY_MOSI = 13;
constexpr int DISPLAY_CS = 15;
constexpr int DISPLAY_DC = 2;
constexpr int DISPLAY_BACKLIGHT = 21;

// XPT2046-compatible resistive touch controller. It is wired on a separate bus.
// Prefisso PIN per evitare collisioni con le macro TOUCH_* di TFT_eSPI.
constexpr int TOUCH_PIN_CLK = 25;
constexpr int TOUCH_PIN_CS = 33;
constexpr int TOUCH_PIN_MOSI = 32;
constexpr int TOUCH_PIN_MISO = 39;
constexpr int TOUCH_PIN_IRQ = 36;

// Rete usata automaticamente a ogni avvio. Il portale Wi-Fi resta disponibile
// dall'ultima pagina qualora questa rete non fosse raggiungibile.
constexpr char DEFAULT_WIFI_SSID[] = "M1";
constexpr char DEFAULT_WIFI_PASSWORD[] = "M1testM1";
constexpr char WIFI_PORTAL_PASSWORD[] = "12345678";

constexpr float MILAN_LAT = 45.4642;
constexpr float MILAN_LON = 9.1900;
constexpr char TIME_ZONE[] = "CET-1CEST,M3.5.0,M10.5.0/3";

constexpr uint32_t WEATHER_INTERVAL_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t CRYPTO_INTERVAL_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t AIR_QUALITY_INTERVAL_MS = 30UL * 60UL * 1000UL;

// Calibrazione affine dell'XPT2046 per ESP32-2432S028 in landscape.
constexpr float TOUCH_AX = 0.00003874f;
constexpr float TOUCH_BX = 0.08726453f;
constexpr float TOUCH_CX = -13.03474426f;
constexpr float TOUCH_AY = -0.06659155f;
constexpr float TOUCH_BY = 0.00032632f;
constexpr float TOUCH_CY = 256.75961304f;
