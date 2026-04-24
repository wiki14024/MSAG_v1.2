#include "Meter.h"

// -------------------------------------------------------------
// ADRESY REJESTRÓW ATM90E32
// -------------------------------------------------------------
#define ATM_METER_EN    0x00
#define ATM_CH_MAP_I    0x01
#define ATM_CH_MAP_U    0x02
#define ATM_SAG_PEAK    0x05
#define ATM_OV_TH       0x06
#define ATM_ZX_CONFIG   0x07
#define ATM_SAG_TH      0x08
#define ATM_PHASE_LOSS  0x09
#define ATM_FREQ_LO     0x0C
#define ATM_FREQ_HI     0x0D

#define ATM_PL_CONST_H  0x31
#define ATM_PL_CONST_L  0x32
#define ATM_MMODE0      0x33
#define ATM_MMODE1      0x34
#define ATM_PSTART_TH   0x35
#define ATM_QSTART_TH   0x36
#define ATM_SSTART_TH   0x37
#define ATM_PPHASE_TH   0x38
#define ATM_QPHASE_TH   0x39
#define ATM_SPHASE_TH   0x3A

#define ATM_UGAIN_A     0x61
#define ATM_IGAIN_A     0x62
#define ATM_CFG_REG     0x7F
#define ATM_SOFT_RESET  0x70

// Wartości kalibracyjne 
#define CAL_UGAIN       33308
#define CAL_IGAIN_A     17128
#define CAL_IGAIN_B     17016
#define CAL_IGAIN_C     17062

EnergyMeter licznik_atm;

bool EnergyMeter::init(int csPin, SemaphoreHandle_t mutex) {
    __cs = csPin;
    _spiMutex = mutex;
    
    if (_spiMutex == NULL) {
        Serial.println("[UWAGA] Miernik zainicjowany bez Mutexa! Ryzyko kolizji na szynie SPI.");
    }
    
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);
    
    write16(ATM_SOFT_RESET, 0x789A); 
    delay(50);
    
    // 1. WERYFIKACJA MAGISTRALI SPI
    write16(ATM_CFG_REG, 0x55AA);  
    if (read16(ATM_CFG_REG) != 0x55AA) {
        return false; 
    }
    
    // 2. JAWNE MAPOWANIE KANAŁÓW (3P4W)
    write16(ATM_CH_MAP_I, 0x0210); // IA=I0, IB=I1, IC=I2
    write16(ATM_CH_MAP_U, 0x0654); // UA=U0, UB=U1, UC=U2
    
    // 3. DYNAMICZNE WYLICZANIE PROGÓW NAPIĘCIOWYCH
    float divider = (2.0f * CAL_UGAIN) / 32768.0f;
    uint16_t sagTh = 0, ovTh = 0, phaseLossTh = 0;
    
    if (divider > 0.0f) {
        sagTh       = (uint16_t)((230.0f * 0.78f * 100.0f * 1.41421356f) / divider); // 179V
        ovTh        = (uint16_t)((230.0f * 1.22f * 100.0f * 1.41421356f) / divider); // 280V
        phaseLossTh = (uint16_t)((23.0f * 100.0f * 1.41421356f) / divider);          // 23V (10% Un)
    }
    
    write16(ATM_METER_EN, 0x0001); 
    
    // Zmiana PeakDet_period na 40ms dla 50Hz zgodnie z sugestią
    write16(ATM_SAG_PEAK, 0x283F); 
    
    write16(ATM_SAG_TH,     sagTh);  
    write16(ATM_OV_TH,      ovTh);   
    write16(ATM_PHASE_LOSS, phaseLossTh); // Dodany rejestr detekcji utraty fazy
    
    write16(ATM_FREQ_HI,  5300);   
    write16(ATM_FREQ_LO,  4700);   
    write16(ATM_ZX_CONFIG, 0xD654);   
    
    // 4. KONFIGURACJA METROLOGICZNA I FILTRY
    write16(ATM_PL_CONST_H, 0x0861);
    write16(ATM_PL_CONST_L, 0xC468);
    
    // MMODE0: 0x0087 jest prawidłowe dla przekładników CT. (Zmienić na 0x0487 TYLKO jeśli masz Rogowskiego)
    write16(ATM_MMODE0, 0x0087);   
    write16(ATM_MMODE1, 0x0000);   
    
    // Bardzo czułe filtry szumów ADC - startują już od ok. ~0.032W (Wartość 100 * 0.00032)
    write16(ATM_PSTART_TH, 0x0064);
    write16(ATM_QSTART_TH, 0x0064);
    write16(ATM_SSTART_TH, 0x0064);
    write16(ATM_PPHASE_TH, 0x0064);
    write16(ATM_QPHASE_TH, 0x0064);
    write16(ATM_SPHASE_TH, 0x0064);
    
    // 5. WPISANIE KALIBRACJI Z TWOICH ZMIENNYCH
    write16(ATM_UGAIN_A, CAL_UGAIN); write16(0x65, CAL_UGAIN); write16(0x69, CAL_UGAIN);
    write16(ATM_IGAIN_A, CAL_IGAIN_A); write16(0x66, CAL_IGAIN_B); write16(0x6A, CAL_IGAIN_C);

    // Czyszczenie Offsetów Pomiary + Fazy + Neutralny (0x63 do 0x6E)
    for(uint16_t reg = 0x63; reg <= 0x6E; reg++) write16(reg, 0x0000);
    // Kasowanie starych offsetów mocy (0x41-0x4C)
    for(uint16_t reg = 0x41; reg <= 0x4C; reg++) write16(reg, 0x0000);
    // Kasowanie offsetów harmonicznych (0x51-0x56)
    for(uint16_t reg = 0x51; reg <= 0x56; reg++) write16(reg, 0x0000);
    
    write16(ATM_CFG_REG, 0x0000); 
    return true; 
}

uint16_t EnergyMeter::read16(uint16_t addr) {
    uint16_t res = 0;
    if (_spiMutex != NULL) xSemaphoreTake(_spiMutex, portMAX_DELAY);
    SPI.beginTransaction(SPISettings(200000, MSBFIRST, SPI_MODE3));
    digitalWrite(_cs, LOW);
    delayMicroseconds(10); 
    SPI.transfer16(addr | 0x8000); 
    res = SPI.transfer16(0xFFFF);
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
    if (_spiMutex != NULL) xSemaphoreGive(_spiMutex);
    return res;
}

int32_t EnergyMeter::read32(uint16_t addrHigh, uint16_t addrLow) {
    uint16_t high = read16(addrHigh);
    uint16_t low = read16(addrLow);
    return (int32_t((int16_t)high) << 16) | low;
}

void EnergyMeter::write16(uint16_t addr, uint16_t val) {
    if (_spiMutex != NULL) xSemaphoreTake(_spiMutex, portMAX_DELAY);
    SPI.beginTransaction(SPISettings(200000, MSBFIRST, SPI_MODE3));
    digitalWrite(_cs, LOW);
    delayMicroseconds(10);
    SPI.transfer16(addr & 0x7FFF); 
    SPI.transfer16(val);
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
    if (_spiMutex != NULL) xSemaphoreGive(_spiMutex);
}

// -------------------------------------------------------------
// GETTERY UŻYTKOWE
// -------------------------------------------------------------
float EnergyMeter::getTotalActivePower() { return (float)read32(0xB0, 0xC0) * 0.00032f; }
float EnergyMeter::getVoltage(uint8_t phase) { return (float)read16(0xD9 + phase) / 100.0f; }
float EnergyMeter::getCurrent(uint8_t phase) { return (float)read16(0xDD + phase) / 1000.0f; }
float EnergyMeter::getActivePower(uint8_t phase) { return (float)read32(0xB1 + phase, 0xC1 + phase) * 0.00032f; }
float EnergyMeter::getPhaseAngle(uint8_t phase) { 
    float angle = (float)read16(0xF9 + phase) / 10.0f; 
    return (angle > 180.0f) ? (angle - 360.0f) : angle; 
}
double EnergyMeter::getExportEnergy() { return (double)read16(0x84) * 0.000003125; }
double EnergyMeter::getImportEnergy() { return (double)read16(0x80) * 0.000003125; }
uint16_t EnergyMeter::getSysStatus0() { return read16(0x73); }
uint16_t EnergyMeter::getRawCurrent(uint8_t phase) { return read16(0xDD + phase); }