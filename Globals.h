#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>

// Deklaracje zmiennych współdzielonych
extern volatile float p_total;
extern volatile float ema_p_total;
extern volatile int aktualne_pwm;
extern bool tryb_auto;
extern bool tryb_awaryjny;
extern bool wifi_connected;
extern float ssr_v;

extern double total_export_kwh;
extern double total_import_kwh;

// Dane sieci (Fazy i diagnostyka)
extern float phase_voltage[3];
extern float phase_current[3];
extern float phase_power[3];
extern float phase_angle[3];
extern uint16_t diag_sys_status0;
extern uint16_t diag_raw_i1;
extern uint16_t diag_raw_i2;
extern uint16_t diag_raw_i3;

extern Preferences nvm;
extern Adafruit_NeoPixel rgb_led;

// Struktura i kolejka
struct PendingRecord {
    uint32_t timestamp;
    double exp;
    double imp;
    bool force_zero;
    bool is_offline;
};

extern PendingRecord syncQueue[24];
extern int queue_size;