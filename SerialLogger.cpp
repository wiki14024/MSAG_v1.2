#include "SerialLogger.h"
#include "Globals.h"
#include <WiFi.h>

void wypiszLogiSerial() {
    static unsigned long ostatni_log = 0;
    
    // Loguj tylko raz na sekundę
    if (millis() - ostatni_log >= 1000) {
        ostatni_log = millis();
        
        // Pobranie adresu IP jeśli połączono
        String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "OFFLINE";
        
        Serial.printf("[IP: %s] L1:%.1fV %.2fA | L2:%.1fV %.2fA | L3:%.1fV %.2fA | P:%.1fW | PWM:%d%%\n", 
            ip.c_str(),
            phase_voltage[0], phase_current[0],
            phase_voltage[1], phase_current[1],
            phase_voltage[2], phase_current[2],
            p_total, (aktualne_pwm * 100) / 1023);
            
        // RAW usunięte. Ewentualne błędy sprzętowe ATM układ wypisze tylko gdy wystąpią (wartość inna niż 0)
        if (diag_sys_status0 != 0x0000) {
             Serial.printf(">>> [DIAG] UWAGA, Flaga Bledu ATM: 0x%04X\n", diag_sys_status0);
        }
    }
}