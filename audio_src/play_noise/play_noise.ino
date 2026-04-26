#include <Arduino.h>
#include <SPI.h>

#define DAC_CS_PIN    5    // IO5  -> DAC CS
#define DAC_SCK_PIN   18   // IO18 -> DAC SCK
#define DAC_MOSI_PIN  23   // IO23 -> DAC SDI

#define DAC_MID       2048
#define NOISE_AMPL    800
#define SAMPLE_RATE   12000

void writeDAC_A(uint16_t value12) {
  value12 &= 0x0FFF;
  uint16_t word = 0x3000 | value12;   // channel A, 1x gain, active

  digitalWrite(DAC_CS_PIN, LOW);
  SPI.transfer16(word);
  digitalWrite(DAC_CS_PIN, HIGH);
}

void setup() {
  pinMode(DAC_CS_PIN, OUTPUT);
  digitalWrite(DAC_CS_PIN, HIGH);

  SPI.begin(DAC_SCK_PIN, -1, DAC_MOSI_PIN, DAC_CS_PIN);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

  writeDAC_A(DAC_MID);
}

void loop() {
  static uint32_t lastUs = 0;
  static const uint32_t intervalUs = 1000000UL / SAMPLE_RATE;

  uint32_t now = micros();
  if ((now - lastUs) < intervalUs) return;
  lastUs += intervalUs;

  int32_t noise = (int32_t)(esp_random() & 0xFFFF) - 32768;
  int32_t sample = DAC_MID + (noise * NOISE_AMPL) / 32768;

  if (sample < 0) sample = 0;
  if (sample > 4095) sample = 4095;

  writeDAC_A((uint16_t)sample);
}