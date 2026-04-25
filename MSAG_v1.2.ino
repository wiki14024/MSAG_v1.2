#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h> 
#include <LittleFS.h>
#include <SPI.h>
#include <ESPmDNS.h>      
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#include <ElegantOTA.h>
#include <time.h> 
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// NASZE WŁASNE MODUŁY
#include "Config.h"
#include "Globals.h"
#include "Indicators.h"
#include "Meter.h"
#include "SerialLogger.h"

// ====================================================================
// FAKTYCZNE DEKLARACJE ZMIENNYCH GLOBALNYCH
// ====================================================================
float p_total = 0.0;
float ema_p_total = 0.0;
int aktualne_pwm = 0;
bool tryb_auto = true;
bool tryb_awaryjny = false;
bool wifi_connected = false;
float ssr_v = 0.0;
double total_export_kwh = 0.0;
double total_import_kwh = 0.0;

// Zmienne sieci zadeklarowane w Globals.h
float phase_voltage[3] = {0, 0, 0};
float phase_current[3] = {0, 0, 0};
float phase_power[3] = {0, 0, 0};
float phase_angle[3] = {0, 0, 0};
uint16_t diag_sys_status0 = 0, diag_raw_i1 = 0, diag_raw_i2 = 0, diag_raw_i3 = 0;

Preferences nvm;
Adafruit_NeoPixel rgb_led(NUMPIXELS, PIN_RGB, NEO_GRB + NEO_KHZ800);
PendingRecord syncQueue[24];
int queue_size = 0;

// ====================================================================
// OBIEKTY I ZMIENNE TYLKO DLA TEGO PLIKU
// ====================================================================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
SemaphoreHandle_t spiMutex = NULL;
const float EMA_ALPHA = 0.4;
float p_max_heater = 0.0;
double today_export_kwh = 0.0, today_import_kwh = 0.0;
float live_history[60];

unsigned long last_ws_update = 0;
unsigned long last_google_try = 0;
unsigned long last_led_update = 0;
unsigned long awaryjny_timer = 0;
unsigned long last_wifi_attempt = 0;

// ====================================================================
// FUNKCJE POMOCNICZE
// ====================================================================
String getUptime() {
  unsigned long secs = millis() / 1000;
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu:%02d:%02d", secs / 3600, (secs % 3600) / 60, secs % 60);
  return String(buf);
}

void obliczMocGrzalki() {
  p_max_heater = 0.0;
  if (digitalRead(PIN_DIP1) == LOW) p_max_heater += 4000.0;
  if (digitalRead(PIN_DIP2) == LOW) p_max_heater += 2000.0;
  if (digitalRead(PIN_DIP3) == LOW) p_max_heater += 1000.0;
  if (digitalRead(PIN_DIP4) == LOW) p_max_heater += 500.0;
  if (p_max_heater == 0) p_max_heater = 1000.0;
}

void polaczWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifi_connected) {
            wifi_connected = true;
            Serial.println("\n=========================================");
            Serial.println("[WIFI] SUKCES! Polaczono z siecia.");
            Serial.print("[WIFI] Adres IP (strona WWW): ");
            Serial.println(WiFi.localIP());
            Serial.println("=========================================\n");
            
            MDNS.begin(HOSTNAME);
            MDNS.addService("http", "tcp", 80);
            configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
        }
    } else {
        if (wifi_connected) {
            wifi_connected = false;
            Serial.println("\n[WIFI] UWAGA: Rozlaczono z siecia! Uklad dziala offline.");
        }
        if (millis() - last_wifi_attempt > 30000) {
            last_wifi_attempt = millis();
            Serial.print("[WIFI] Proba polaczenia z routerem: ");
            Serial.println(WIFI_SSID);
            WiFi.begin(WIFI_SSID, WIFI_PASS);
        }
    }
}

// ====================================================================
// TASK STEROWANIA ORAZ ODCZYTU (W pełni zabezpieczony i odchudzony)
// ====================================================================
void ControlTask(void *pvParameters) {
    float p_meter;
    for(;;) {
        // 1. ODCZYTY Z NOWEJ BIBLIOTEKI (Sama blokuje i zwalnia Mutex!)
        p_meter = licznik_atm.getTotalActivePower();
        phase_voltage[0] = licznik_atm.getVoltage(0); phase_voltage[1] = licznik_atm.getVoltage(1); phase_voltage[2] = licznik_atm.getVoltage(2);
        phase_current[0] = licznik_atm.getCurrent(0); phase_current[1] = licznik_atm.getCurrent(1); phase_current[2] = licznik_atm.getCurrent(2);
        phase_power[0] = licznik_atm.getActivePower(0); phase_power[1] = licznik_atm.getActivePower(1); phase_power[2] = licznik_atm.getActivePower(2);
        phase_angle[0] = licznik_atm.getPhaseAngle(0); phase_angle[1] = licznik_atm.getPhaseAngle(1); phase_angle[2] = licznik_atm.getPhaseAngle(2);
        
        diag_sys_status0 = licznik_atm.getSysStatus0();
        diag_raw_i1 = licznik_atm.getRawCurrent(0);
        diag_raw_i2 = licznik_atm.getRawCurrent(1);
        diag_raw_i3 = licznik_atm.getRawCurrent(2);
        
        float p_sum = phase_power[0] + phase_power[1] + phase_power[2];
        p_total = (fabs(p_meter) > fabs(p_sum)) ? p_meter : p_sum;

        // 2. WYWOŁANIE LOGÓW Z OSOBNEGO PLIKU
        wypiszLogiSerial();

        // 3. ALGORYTM AUTOKONSUMPCJI
        ema_p_total = (EMA_ALPHA * p_total) + ((1.0 - EMA_ALPHA) * ema_p_total);
        if (aktualne_pwm > 512 && ssr_v < 1.0) {
            if (!tryb_awaryjny) { tryb_awaryjny = true; awaryjny_timer = millis(); }
        } else if (tryb_awaryjny) {
            if (ssr_v > 1.5 || (aktualne_pwm == 0 && millis() - awaryjny_timer > 10000)) tryb_awaryjny = false;
        }
        
        if (tryb_auto && !tryb_awaryjny) {
            if (ema_p_total > -50) aktualne_pwm -= 40;
            else if (ema_p_total < -100) aktualne_pwm += 20;
            aktualne_pwm = constrain(aktualne_pwm, 0, 1023);
        }
        
        ledcWrite(PIN_PWM_OUT, tryb_awaryjny ? 0 : aktualne_pwm);
        vTaskDelay(pdMS_TO_TICKS(333)); // KRYTYCZNE: Oddaje czas rdzenia
    }
}

// ====================================================================
// WEBSOCKETY I GOOGLE
// ====================================================================
void zapiszKolejkeDoNVM() {
    nvm.putInt("q_size", queue_size);
    if (queue_size > 0) nvm.putBytes("queue", syncQueue, sizeof(PendingRecord) * queue_size);
}

void dodajDoKolejki(uint32_t ts, double e, double i, bool fz) {
    if (queue_size >= 24) {
        for(int j=0; j<23; j++) syncQueue[j] = syncQueue[j+1];
        queue_size = 23;
    }
    syncQueue[queue_size].timestamp = ts; syncQueue[queue_size].exp = e; syncQueue[queue_size].imp = i;
    syncQueue[queue_size].force_zero = fz; syncQueue[queue_size].is_offline = !wifi_connected;
    queue_size++;
    zapiszKolejkeDoNVM();
}

void rozladujKolejkeDoGoogle() {
    if (!wifi_connected || queue_size == 0) return;
    PendingRecord rec = syncQueue[0];
    char timeStr[32]; time_t t = rec.timestamp; struct tm *ti = localtime(&t);
    strftime(timeStr, sizeof(timeStr), "%d.%m.%Y %H:%M", ti);
    String timeParam = String(timeStr); timeParam.replace(" ", "%20"); 
    
    WiFiClientSecure client; client.setInsecure(); HTTPClient http;
    String url = GOOGLE_SCRIPT_URL + "?export=" + (rec.force_zero ? "0" : String(rec.exp, 3)) + 
                 "&import=" + (rec.force_zero ? "0" : String(rec.imp, 3)) + "&time=" + timeParam +
                 "&pv_active=" + String((rec.force_zero || rec.is_offline) ? 0 : 1);
    
    http.begin(client, url); http.setTimeout(5000); 
    int httpCode = http.GET();
    if (httpCode == 200) { 
        for(int i=0; i<queue_size-1; i++) syncQueue[i] = syncQueue[i+1];
        queue_size--; zapiszKolejkeDoNVM();
    }
    http.end();
}

void wyslijDaneWebsocket(bool sendLive) {
  StaticJsonDocument<4096> doc;
  doc["grid_p"] = (int)p_total; doc["heater_pwm"] = (aktualne_pwm * 100) / 1023;
  doc["heater_active"] = (aktualne_pwm > 0); doc["ssr_v"] = ssr_v;
  doc["mode"] = tryb_auto ? "auto" : "manual"; doc["uptime"] = getUptime();
  doc["fw_version"] = FW_VERSION; doc["cpu_temp"] = temperatureRead();
  doc["export_kwh"] = total_export_kwh; doc["import_kwh"] = total_import_kwh;
  doc["cloud_url"] = GOOGLE_SCRIPT_URL;
  
  JsonArray phases = doc.createNestedArray("phases");
  for(int i = 0; i < 3; i++) {
    JsonObject phase = phases.createNestedObject();
    phase["v"] = phase_voltage[i]; phase["a"] = phase_current[i]; phase["p"] = (int)phase_power[i];
  }
  
  if (tryb_awaryjny) doc["sys_color"] = "red";
  else if (ema_p_total > 100) doc["sys_color"] = "red";
  else if (ema_p_total < -100) doc["sys_color"] = "green";
  else if (aktualne_pwm > 102) doc["sys_color"] = "orange";
  else doc["sys_color"] = "blue";
  
  struct tm ti;
  if (getLocalTime(&ti)) { char ts[10]; strftime(ts, sizeof(ts), "%H:%M:%S", &ti); doc["clock"] = String(ts); }
  if (sendLive) { JsonArray live = doc.createNestedArray("live_data"); for(int i=0; i<60; i++) live.add((int)live_history[i]); }
  String js; serializeJson(doc, js); ws.textAll(js);
}

void onWsEvent(AsyncWebSocket *s, AsyncWebSocketClient *c, AwsEventType t, void *a, uint8_t *d, size_t l) {
  if (t == WS_EVT_DATA) { 
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, d, l)) return;
    if (doc.containsKey("mode")) tryb_auto = (doc["mode"] == "auto");
    if (doc.containsKey("pwm") && !tryb_auto) {
        int pwm_val = doc["pwm"]; aktualne_pwm = (constrain(pwm_val, 0, 100) * 1023) / 100;
    }
    if (doc.containsKey("cmd")) { 
      String cmd = doc["cmd"];
      if (cmd == "reboot") ESP.restart();
      else if (cmd == "reset_kwh") {
            total_export_kwh = 0; total_import_kwh = 0; today_export_kwh = 0; today_import_kwh = 0;
            nvm.putDouble("exp_kwh", 0); nvm.putDouble("imp_kwh", 0);
      }
    }
  }
}

// ====================================================================
// SETUP
// ====================================================================
void setup() {
  Serial.begin(115200); delay(1000);
  Serial.println("\n\n========================================");
  Serial.println("MSAG PRO-C6-HYBRID - MULTIFILE (CLEAN)");
  Serial.println("========================================\n");
  
  spiMutex = xSemaphoreCreateMutex();
  if (spiMutex == NULL) { Serial.println("[ERROR] Brak mutexa!"); while(1) { delay(1000); } }
  
  initIndicators(); // Z pliku Indicators.cpp
  
  pinMode(PIN_RST_OUT, OUTPUT); digitalWrite(PIN_RST_OUT, HIGH); delay(10); 
  digitalWrite(PIN_RST_OUT, LOW); delay(50); digitalWrite(PIN_RST_OUT, HIGH); delay(100);
  
  pinMode(PIN_RST_IN, INPUT_PULLUP); pinMode(PIN_SSR_SENSE, INPUT);
  pinMode(PIN_DIP1, INPUT_PULLUP); pinMode(PIN_DIP2, INPUT_PULLUP);
  pinMode(PIN_DIP3, INPUT_PULLUP); pinMode(PIN_DIP4, INPUT_PULLUP);
  
  ledcAttach(PIN_PWM_OUT, pwmFreq, pwmResolution);
  obliczMocGrzalki();
  
  nvm.begin("msag", false);
  total_export_kwh = nvm.getDouble("exp_kwh", 0.0); total_import_kwh = nvm.getDouble("imp_kwh", 0.0);
  queue_size = nvm.getInt("q_size", 0);
  if (queue_size > 0 && queue_size <= 24) nvm.getBytes("queue", syncQueue, sizeof(PendingRecord) * queue_size);
  
  // URUCHOMIENIE NOWEJ BIBLIOTEKI
 SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CS);
  if (!licznik_atm.init(PIN_SPI_CS, spiMutex)) {
      Serial.println("[CRITICAL ERROR] Komunikacja SPI z ATM90E32 nie powiodla sie!");
      // Opcjonalnie: while(1) { delay(1000); } // zablokowanie programu
  } else {
      Serial.println("[ATM] Inicjalizacja zakonczona sukcesem.");
  }

  LittleFS.begin(true);
  for(int i = 0; i < 60; i++) live_history[i] = 0.0;

  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS); last_wifi_attempt = millis();
  
  ws.onEvent(onWsEvent); server.addHandler(&ws);
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html"); 
  ElegantOTA.begin(&server); server.begin(); 
  
  xTaskCreate(ControlTask, "Control_Task", 8192, NULL, 2, NULL);
}

// ====================================================================
// LOOP (Zadania niekrytyczne czasowo)
// ====================================================================
void loop() {
  static unsigned long rst_start = 0;
  
  polaczWiFi(); ws.cleanupClients(); ElegantOTA.loop();
  
  if (millis() - last_led_update >= 50) { 
      last_led_update = millis(); 
      aktualizujStanIKolory(); 
  }
  aktualizujZolteDiody();
  
  if (digitalRead(PIN_RST_IN) == LOW) {
      if (rst_start == 0) rst_start = millis();
      if (millis() - rst_start > 5000) ESP.restart();
  } else { rst_start = 0; }

  ssr_v = (analogRead(PIN_SSR_SENSE) * 3.3 / 4095.0) * 3.0;
  
  if (millis() - last_ws_update >= 1000) { 
      last_ws_update = millis(); obliczMocGrzalki();
      
      // Odczyt liczników energii co 10s (Brak konieczności używania Mutexa tutaj)
      static unsigned long last_energy_read = 0;
      if (millis() - last_energy_read >= 10000) {
          last_energy_read = millis();
          
          double re = licznik_atm.getExportEnergy();
          double ri = licznik_atm.getImportEnergy();
          
          if (re > 0 || ri > 0) {
              total_export_kwh += re; today_export_kwh += re; 
              total_import_kwh += ri; today_import_kwh += ri; 
          }
      }
      
      struct tm ti;
      if (getLocalTime(&ti) && ti.tm_year > 123) {
          static int last_sync_min = -1;
          if (ti.tm_min != last_sync_min) {
              last_sync_min = ti.tm_min;
              if (ti.tm_hour == 0 && ti.tm_min == 0) { today_export_kwh = 0; today_import_kwh = 0; }
              bool add_to_q = (ti.tm_min == 0 || ti.tm_min == 30 || (ti.tm_hour == 23 && ti.tm_min == 59));
              if (add_to_q) {
                  time_t now; time(&now);
                  dodajDoKolejki((uint32_t)now, today_export_kwh, today_import_kwh, (ti.tm_hour == 0 && ti.tm_min == 5));
              }
              nvm.putDouble("exp_kwh", total_export_kwh); nvm.putDouble("imp_kwh", total_import_kwh);
          }
      }

      for(int i=0; i<59; i++) live_history[i] = live_history[i+1];
      live_history[59] = ema_p_total; wyslijDaneWebsocket(true);
  }

  if (queue_size > 0 && millis() - last_google_try >= 15000) {
    last_google_try = millis(); rozladujKolejkeDoGoogle();
  }
}