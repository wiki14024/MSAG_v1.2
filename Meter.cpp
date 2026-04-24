#include "Meter.h"

// -------------------------------------------------------------
// ADRESY REJESTRÓW ATM90E32
// -------------------------------------------------------------
#define ATM_METER_EN    0x00
#define ATM_SAG_PEAK    0x05
#define ATM_OV_TH       0x06
#define ATM_ZX_CONFIG   0x07
#define ATM_SAG_TH      0x08
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
#define ATM_UOFFSET_A   0x63
#define ATM_IOFFSET_A   0x64
#define ATM_UGAIN_B     0x65
#define ATM_IGAIN_B     0x66
#define ATM_UOFFSET_B   0x67
#define ATM_IOFFSET_B   0x68
#define ATM_UGAIN_C     0x69
#define ATM_IGAIN_C     0x6A
#define ATM_UOFFSET_C   0x6B
#define ATM_IOFFSET_C   0x6C

#define ATM_SOFT_RESET  0x70
#define ATM_EMM_INT_ST0 0x73
#define ATM_EMM_INT_ST1 0x74
#define ATM_EMM_INT_EN0 0x75
#define ATM_EMM_INT_EN1 0x76
#define ATM_CFG_REG     0x7F

// Wartości kalibracyjne wyciągnięte ze starej biblioteki
#define CAL_UGAIN       33308
#define CAL_IGAIN_A     17128
#define CAL_IGAIN_B     17016
#define CAL_IGAIN_C     17062

EnergyMeter licznik_atm;

void EnergyMeter::init(int csPin, SemaphoreHandle_t mutex) {
    _cs = csPin;
    _spiMutex = mutex;
    
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);
    
    // ==========================================
    // 0. RESET UKŁADU
    // ==========================================
    write16(ATM_SOFT_RESET, 0x789A); 
    delay(50);
    
    // Odblokowanie rejestrów
    write16(ATM_CFG_REG, 0x55AA);  
    
    // ==========================================
    // 1. STATUSY I ZDARZENIA (Z BeginInternal)
    // ==========================================
    write16(ATM_METER_EN, 0x0001); // Włączenie pomiaru
    write16(ATM_SAG_PEAK, 0x143F); // Konfiguracja detektora Sag/Peak
    write16(ATM_SAG_TH,   0x33A1); // Wyliczony próg zapadu napięcia (190V)
    write16(ATM_OV_TH,    0x48EF); // Wyliczony próg przepięcia (268V)
    write16(ATM_FREQ_HI,  5300);   // Próg wysokiej częstotliwości (53 Hz)
    write16(ATM_FREQ_LO,  4700);   // Próg niskiej częstotliwości (47 Hz)
    write16(ATM_EMM_INT_EN0, 0xB76F); // Maski przerwań cz. 1
    write16(ATM_EMM_INT_EN1, 0xDDFD); // Maski przerwań cz. 2
    write16(ATM_EMM_INT_ST0, 0x0001); // Reset flag statusu
    write16(ATM_EMM_INT_ST1, 0x0001); // Reset flag statusu
    write16(ATM_ZX_CONFIG, 0xD654);   // Konfiguracja Zero-Crossing
    
    // ==========================================
    // 2. KONFIGURACJA METROLOGICZNA
    // ==========================================
    write16(ATM_PL_CONST_H, 0x0861);
    write16(ATM_PL_CONST_L, 0xC468);
    write16(ATM_MMODE0, 0x00E6);   // 50Hz, 3-fazy 4-przewody (Twoja stabilna konfiguracja)
    write16(ATM_MMODE1, 0x0000);   // PGA Gain na 1x
    
    // Progi startowe - CELOWO 0x0000 zamiast 0x1D4C (brak martwej strefy pomiaru)
    write16(ATM_PSTART_TH, 0x0000);
    write16(ATM_QSTART_TH, 0x0000);
    write16(ATM_SSTART_TH, 0x0000);
    write16(ATM_PPHASE_TH, 0x0000);
    write16(ATM_QPHASE_TH, 0x0000);
    write16(ATM_SPHASE_TH, 0x0000);
    
    // ==========================================
    // 3. KALIBRACJA Z TWOICH PARAMETRÓW
    // ==========================================
    write16(ATM_UGAIN_A, CAL_UGAIN);
    write16(ATM_UGAIN_B, CAL_UGAIN);
    write16(ATM_UGAIN_C, CAL_UGAIN);
    
    write16(ATM_IGAIN_A, CAL_IGAIN_A);
    write16(ATM_IGAIN_B, CAL_IGAIN_B);
    write16(ATM_IGAIN_C, CAL_IGAIN_C);

    // Zerowanie offsetów dla napięcia i prądu (wymagane by pozbyć się śmieci)
    write16(ATM_UOFFSET_A, 0x0000); write16(ATM_IOFFSET_A, 0x0000);
    write16(ATM_UOFFSET_B, 0x0000); write16(ATM_IOFFSET_B, 0x0000);
    write16(ATM_UOFFSET_C, 0x0000); write16(ATM_IOFFSET_C, 0x0000);

    // Kasowanie starych offsetów mocy (0x41-0x4C)
    for(uint16_t reg = 0x41; reg <= 0x4C; reg++) write16(reg, 0x0000);
    // Kasowanie offsetów harmonicznych (0x51-0x56)
    for(uint16_t reg = 0x51; reg <= 0x56; reg++) write16(reg, 0x0000);
    
    // ==========================================
    // 4. ZABLOKOWANIE REJESTRÓW
    // ==========================================
    write16(ATM_CFG_REG, 0x0000); 
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