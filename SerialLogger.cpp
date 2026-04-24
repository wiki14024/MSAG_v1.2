#include "SerialLogger.h"
#include "Globals.h"

void wypiszLogiSerial() {
    static unsigned long ostatni_log = 0;
    
    // Loguj tylko raz na sekundę
    if (millis() - ostatni_log >= 1000) {
        ostatni_log = millis();
        
        Serial.printf("LOG L1:%.1fV %.2fA | L2:%.1fV %.2fA | L3:%.1fV %.2fA | P:%.1fW | PWM:%d%%\n", 
            phase_voltage[0], phase_current[0],
            phase_voltage[1], phase_current[1],
            phase_voltage[2], phase_current[2],
            p_total, (aktualne_pwm * 100) / 1023);
            
        Serial.printf(">>> [DIAG] RAW_I1:%u | RAW_I2:%u | RAW_I3:%u | PLL:0x%04X\n", 
            diag_raw_i1, diag_raw_i2, diag_raw_i3, diag_sys_status0);
    }
}