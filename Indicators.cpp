#include "Indicators.h"
#include "Globals.h"
#include "Config.h"

void initIndicators() {
    rgb_led.begin(); 
    rgb_led.setBrightness(50);
    pinMode(PIN_LED1, OUTPUT); 
    pinMode(PIN_LED2, OUTPUT);
}

void aktualizujStanIKolory() {
    if (tryb_awaryjny) {
        if (millis() % 500 < 250) rgb_led.setPixelColor(0, rgb_led.Color(255, 0, 0));
        else rgb_led.setPixelColor(0, rgb_led.Color(0, 0, 0));
        rgb_led.show();
        return;
    }
    if (!wifi_connected) {
        if ((millis() % 500) < 250) rgb_led.setPixelColor(0, rgb_led.Color(0, 128, 128));
        else rgb_led.setPixelColor(0, rgb_led.Color(0, 0, 0));
        rgb_led.show();
        return;
    }
    const float THRESHOLD = 50.0;
    bool heater_on = (aktualne_pwm > 0);
    int color_r = 0, color_g = 0, color_b = 0;
    
    if (p_total > THRESHOLD) { color_r = 255; color_g = 0; color_b = 0; } 
    else if (p_total < -THRESHOLD) { color_r = 0; color_g = 255; color_b = 0; }
    else if (heater_on && fabs(p_total) <= THRESHOLD) { color_r = 255; color_g = 100; color_b = 0; }
    else { rgb_led.setPixelColor(0, rgb_led.Color(0, 0, 255)); rgb_led.show(); return; }
    
    unsigned long ms = millis() % 5000;
    bool led_on = (ms < 500) || (ms >= 2500 && ms < 3000);
    if (led_on) rgb_led.setPixelColor(0, rgb_led.Color(color_r, color_g, color_b));
    else rgb_led.setPixelColor(0, rgb_led.Color(0, 0, 0));
    rgb_led.show();
}

void aktualizujZolteDiody() {
    unsigned long ms = millis();
    unsigned long t1 = ms % 2000;
    if (t1 < 50 || (t1 > 150 && t1 < 200)) digitalWrite(PIN_LED1, HIGH);
    else digitalWrite(PIN_LED1, LOW);

    int moc_procent = (aktualne_pwm * 100) / 1023;
    if (moc_procent <= 1) digitalWrite(PIN_LED2, LOW);
    else if (moc_procent >= 99) digitalWrite(PIN_LED2, HIGH);
    else {
        unsigned long t2 = ms % 2000;
        unsigned long czas_swiecenia = (2000 * moc_procent) / 100;
        if (t2 < czas_swiecenia) digitalWrite(PIN_LED2, HIGH);
        else digitalWrite(PIN_LED2, LOW);
    }
}