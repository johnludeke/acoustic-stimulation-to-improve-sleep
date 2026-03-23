#include <Arduino.h>

const int audioPin = 4;
uint32_t rng = 1;

uint32_t xorshift() {
  uint32_t x = rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng = x;
  return x;
}

void setup() {
  bool ok = ledcAttach(audioPin, 40000, 8);
  if (!ok) {
    while (true) delay(1000);
  }
}

void loop() {
  uint8_t sample = (uint8_t)(xorshift() & 0xFF);
  ledcWrite(audioPin, sample);
  delayMicroseconds(80);
}
