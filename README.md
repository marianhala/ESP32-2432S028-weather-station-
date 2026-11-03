<img width="3072" height="2210" alt="Meteo" src="https://github.com/user-attachments/assets/7cc4443d-7d4e-4fb4-9d64-e73325d3eaa5" />

#################################

ESP32-2432S028-weather-station-
ESP32 weather station HOME, FORECAST, AIR_QUALITY, WIFI_SCAN

Stazione meteo Milano — ESP32-2432S028 Sketch Arduino per display touch da 2,8 pollici con quattro pagine: meteo attuale, previsione a tre giorni, qualità dell'aria e scanner Wi-Fi.

Prima compilazione

01 - In Arduino IDE installa il pacchetto esp32 by Espressif Systems e seleziona ESP32 Dev Module.

02 - Installa da Gestione librerie: TFT_eSPI (di Bodmer), ArduinoJson, WiFiManager (di tzapu) e XPT2046_Touchscreen (di Paul Stoffregen).

03 - Apri Documenti/Arduino/libraries/TFT_eSPI/User_Setup.h e sostituisci tutto il contenuto con quello di TFT_eSPI_User_Setup.h. Non basta includere il file dal programma: anche i sorgenti interni di TFT_eSPI leggono User_Setup.h durante la compilazione. Il messaggio stray '#' in program significa che le direttive #define nel file installato sono commentate o precedute da *; devono essere righe attive che iniziano con #define. Questa scheda richiede il driver ILI9341_2.

04 - Apri ESP32_Weather_Station.ino, collega la scheda con un cavo dati USB-C e carica.

05 - Prima di compilare, sostituisci i valori segnaposto YOUR_WIFI_SSID, YOUR_WIFI_PASSWORD e CHANGE_ME in Config.h con le tue credenziali locali. Non committare né condividere le credenziali reali. Se la rete predefinita non è disponibile entro 12 secondi, la scheda espone l'access point Meteo-Milano; collegati da telefono/PC e apri http://192.168.4.1 per configurare la rete. Le credenziali scelte nel portale vengono memorizzate dalla ESP32.

Fonti e aggiornamenti Open-Meteo: condizioni attuali e previsione di Milano, ogni 10 minuti. CoinGecko: BTC, ETH, DOGE ed EGLD in euro, ogni 5 minuti. Open-Meteo Air Quality: indice europeo AQI e PM2.5, aggiornati ogni 30 minuti. NTP: data, ora e secondi in fuso orario italiano.

L'ultimo meteo valido e la previsione a tre giorni vengono salvati nella memoria NVS della ESP32. Se la rete non è disponibile all'avvio, restano visibili i dati salvati e la schermata principale indica l'ora dell'ultimo aggiornamento e quando sta usando la cache. La schermata principale mostra temperatura percepita, vento e raffiche a 10 m, pressione al livello del mare e UV massimo giornaliero; la pagina previsioni mostra alba, tramonto e UV massimo per ciascun giorno. La pagina qualità dell'aria mostra AQI europeo e PM2.5 stimati per l'area di Milano. La schermata principale usa card scure ad alto contrasto e aggiorna l'orologio una volta al minuto.

Le connessioni HTTPS sono abilitate con certificato non verificato (setInsecure) per non dover gestire certificati sulla microcontrollore. Per un prodotto esposto a internet va sostituito con la verifica di un certificato CA.

Touch e Wi-Fi La pagina Wi‑Fi mostra SSID, canale e potenza del segnale. SCANSIONA aggiorna l'elenco; PORTALE WIFI avvia nuovamente la scelta della rete. La selezione completa (SSID e password) avviene nel portale web, scelta più affidabile della tastiera su un display 320×240.

Nel portale web WiFiManager le pagine interne mostrano il pulsante fisso Home in basso a destra; riporta alla pagina principale del portale.

Il touch ora usa il driver specifico XPT2046_Touchscreen sul suo bus SPI dedicato e una calibrazione affine per questa scheda. Se un lotto diverso produce tocchi spostati, modifica i sei coefficienti TOUCH_A*, TOUCH_B* e TOUCH_C* in Config.h.

Pin della scheda Il display usa SCLK 14, MOSI 13, CS 15, DC 2; la retroilluminazione è su GPIO 21. Il touch usa GPIO 25, 33, 32, 39 e 36. Lo slot microSD non viene inizializzato, per lasciare spazio a un'estensione con icone o cache persistente.

############################################
