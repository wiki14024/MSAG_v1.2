
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class EnergyMeter {
public:
    // Zwraca TRUE jeśli SPI i układ odpowiedzą poprawnie, FALSE w przypadku awarii
    bool init(int csPin, SemaphoreHandle_t mutex);
    
    // Funkcje odczytu (0 = L1, 1 = L2, 2 = L3)
    float getTotalActivePower();
    float getVoltage(uint8_t phase); 
    float getCurrent(uint8_t phase);
    float getActivePower(uint8_t phase);
    float getPhaseAngle(uint8_t phase);
    
    double getExportEnergy();
    double getImportEnergy();
    
    uint16_t getSysStatus0();
    uint16_t getRawCurrent(uint8_t phase); 
    
private:
    int _cs;
    SemaphoreHandle_t _spiMutex;
    
    uint16_t read16(uint16_t addr);
    int32_t read32(uint16_t addrHigh, uint16_t addrLow);
    void write16(uint16_t addr, uint16_t val);
};

extern EnergyMeter licznik_atm;