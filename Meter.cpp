#include "Meter.h"

// Rejestry ATM90E32
#define ATM_METER_EN    0x00
#define ATM_MMODE0      0x33
#define ATM_UGAIN_A     0x61
#define ATM_IGAIN_A     0x62
#define ATM_UGAIN_B     0x65
#define ATM_IGAIN_B     0x66
#define ATM_UGAIN_C     0x69
#define ATM_IGAIN_C     0x6A
#define ATM_CFG_REG     0x7F
#define ATM_SOFT_RESET  0x70

// Wartości kalibracyjne wyciągnięte ze starej biblioteki
#define CAL_UGAIN       33308
#define CAL_IGAIN_A     17128
#define CAL_IGAIN_B     17016
#define CAL_IGAIN_C     17062

EnergyMeter licznik_atm; // Globalny obiekt

void EnergyMeter::init(int csPin, SemaphoreHandle_t mutex) {
    _cs = csPin;
    _spiMutex = mutex;
    
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);
    
    // Pełny reset i konfiguracja
    write16(ATM_SOFT_RESET, 0x789A); 
    delay(50);
    
    write16(ATM_CFG_REG, 0x55AA);  // Odblokowanie rejestrów
    write16(ATM_METER_EN, 0x0001); // Włączenie pomiaru
    
    write16(ATM_MMODE0, 0x00E6);   // 50Hz, 3-fazy 4-przewody (Twoje ustawienie na lock)
    
    // Wpisanie kalibracji
    write16(ATM_UGAIN_A, CAL_UGAIN);
    write16(ATM_UGAIN_B, CAL_UGAIN);
    write16(ATM_UGAIN_C, CAL_UGAIN);
    
    write16(ATM_IGAIN_A, CAL_IGAIN_A);
    write16(ATM_IGAIN_B, CAL_IGAIN_B);
    write16(ATM_IGAIN_C, CAL_IGAIN_C);
    
    // Kasowanie szumów w stanie jałowym
    write16(0x29, 0x0000); // IStartTh 
    write16(0x27, 0x0000); // PStartTh 
    write16(0x3E, 0x0000); // UlowTh 
    
    write16(ATM_CFG_REG, 0x0000); // Zablokowanie rejestrów przed przypadkowym zapisem
}

uint16_t EnergyMeter::read16(uint16_t addr) {
    uint16_t res = 0;
    if (_spiMutex != NULL) xSemaphoreTake(_spiMutex, portMAX_DELAY);
    SPI.beginTransaction(SPISettings(200000, MSBFIRST, SPI_MODE3));
    digitalWrite(_cs, LOW);
    delayMicroseconds(10); 
    SPI.transfer16(addr | 0x8000); // Odczyt wymaga 1 na 15. bicie
    res = SPI.transfer16(0xFFFF);
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
    if (_spiMutex != NULL) xSemaphoreGive(_spiMutex);
    return res;
}

int32_t EnergyMeter::read32(uint16_t addrHigh, uint16_t addrLow) {
    uint16_t high = read16(addrHigh);
    uint16_t low = read16(addrLow);
    // Zrzutowanie int16_t na int32_t na High Byte zachowuje znak minusa dla mocy eksportowanej!
    return (int32_t((int16_t)high) << 16) | low;
}

void EnergyMeter::write16(uint16_t addr, uint16_t val) {
    if (_spiMutex != NULL) xSemaphoreTake(_spiMutex, portMAX_DELAY);
    SPI.beginTransaction(SPISettings(200000, MSBFIRST, SPI_MODE3));
    digitalWrite(_cs, LOW);
    delayMicroseconds(10);
    SPI.transfer16(addr & 0x7FFF); // Zapis wymaga 0 na 15. bicie
    SPI.transfer16(val);
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
    if (_spiMutex != NULL) xSemaphoreGive(_spiMutex);
}

// -------------------------------------------------------------
// GETTERY UŻYTKOWE
// -------------------------------------------------------------
float EnergyMeter::getTotalActivePower() {
    return (float)read32(0xB0, 0xC0) * 0.00032f;
}

float EnergyMeter::getVoltage(uint8_t phase) {
    return (float)read16(0xD9 + phase) / 100.0f;
}

float EnergyMeter::getCurrent(uint8_t phase) {
    return (float)read16(0xDD + phase) / 1000.0f;
}

float EnergyMeter::getActivePower(uint8_t phase) {
    return (float)read32(0xB1 + phase, 0xC1 + phase) * 0.00032f;
}

float EnergyMeter::getPhaseAngle(uint8_t phase) {
    float angle = (float)read16(0xF9 + phase) / 10.0f;
    return (angle > 180.0f) ? (angle - 360.0f) : angle;
}

double EnergyMeter::getExportEnergy() {
    return (double)read16(0x84) * 0.000003125; // 1.0 / 100.0 / 3200.0
}

double EnergyMeter::getImportEnergy() {
    return (double)read16(0x80) * 0.000003125;
}

uint16_t EnergyMeter::getSysStatus0() {
    return read16(0x73);
}

uint16_t EnergyMeter::getRawCurrent(uint8_t phase) {
    return read16(0xDD + phase);
}