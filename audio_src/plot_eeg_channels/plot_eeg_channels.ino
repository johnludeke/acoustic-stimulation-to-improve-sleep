#include <Arduino.h>

// -----------------------------
// Cyton UART config (J5)
// -----------------------------
static const int RX_PIN = 17;  // IO17 on ESP32-WROOM-32E (J5 RX)
static const int TX_PIN = -1;
static const uint32_t UART_BAUD = 115200;
static const float ADS_GAIN = 24.0f;

static const uint8_t PACKET_SIZE = 33;
uint8_t packet[PACKET_SIZE];
uint8_t packetIndex = 0;
bool syncing = false;

float scale_uV_per_count = 0.0f;

// -----------------------------
// Packet helpers
// -----------------------------
int32_t int24ToInt32(uint8_t b0, uint8_t b1, uint8_t b2) {
  int32_t value = ((int32_t)b0 << 16) | ((int32_t)b1 << 8) | b2;
  if (value & 0x00800000) value |= 0xFF000000;
  return value;
}

bool validFooter(uint8_t b) { return (b & 0xF0) == 0xC0; }

// -----------------------------
// Packet processing — all 8 channels, tab-separated uV
// -----------------------------
void processPacket(const uint8_t *pkt) {
  for (int ch = 0; ch < 8; ch++) {
    int base = 2 + ch * 3;
    int32_t counts = int24ToInt32(pkt[base], pkt[base + 1], pkt[base + 2]);
    float uV = counts * scale_uV_per_count;
    Serial.print(uV, 3);
    if (ch < 7) Serial.print('\t');
  }
  Serial.println();
}

// -----------------------------
// Arduino setup / loop
// -----------------------------
void setup() {
  Serial.begin(230400);
  delay(1000);

  scale_uV_per_count = (4.5f / ADS_GAIN / 8388607.0f) * 1000000.0f;

  Serial1.setRxBufferSize(1024);
  Serial1.begin(UART_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);

  Serial.println("BOOT,PLOT_EEG_CHANNELS");
  Serial.print("BOOT,scale_uV_per_count,");
  Serial.println(scale_uV_per_count, 8);
}

void loop() {
  while (Serial1.available() > 0) {
    uint8_t b = (uint8_t)Serial1.read();

    if (!syncing) {
      if (b == 0xA0) {
        syncing = true;
        packetIndex = 0;
        packet[packetIndex++] = b;
      }
      continue;
    }

    packet[packetIndex++] = b;

    if (packetIndex == PACKET_SIZE) {
      if (packet[0] == 0xA0 && validFooter(packet[32])) {
        processPacket(packet);
      }
      syncing = false;
      packetIndex = 0;
    }
  }
}
