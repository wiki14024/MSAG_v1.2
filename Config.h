#pragma once

// ====================================================================
// KONFIGURACJA WIFI I CHMURY
// ====================================================================
const char* WIFI_SSID = "SmartHome";
const char* WIFI_PASS = "123456789";
const String GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbwhYfMLe6PS3PxiIYSjvXA-fwvaT6TM6Ta97vp9TzwSErPxCwhNvsFWI8d1dCimNf-w/exec";
const String FW_VERSION = "v1.2 PRO-C6-HYBRID";
const char* HOSTNAME = "msag";

// ====================================================================
// PINY
// ====================================================================
#define PIN_DIP1 20 
#define PIN_DIP2 21 
#define PIN_DIP3 22 
#define PIN_DIP4 23 
#define PIN_LED1 19
#define PIN_LED2 18
#define PIN_PWM_OUT 14
#define PIN_SSR_SENSE 0
#define PIN_SPI_MOSI 2
#define PIN_SPI_MISO 3
#define PIN_SPI_SCK  4
#define PIN_SPI_CS   5
#define PIN_RGB 8
#define PIN_RST_OUT 6
#define PIN_RST_IN 7

const int pwmFreq = 5000;
const int pwmResolution = 10;
#define NUMPIXELS 1