static const int RX_PIN = 17;
static const int TX_PIN = -1;
static const uint32_t UART_BAUD = 115200;
static const float ADS_GAIN = 24.0f;

static const uint8_t PACKET_SIZE = 33;
uint8_t packet[PACKET_SIZE];
uint8_t packetIndex = 0;
bool syncing = false;

float scale_uV_per_count = 0.0f;

int32_t int24ToInt32(uint8_t b0, uint8_t b1, uint8_t b2) {
  int32_t value = ((int32_t)b0 << 16) | ((int32_t)b1 << 8) | b2;
  if (value & 0x00800000) {
    value |= 0xFF000000;
  }
  return value;
}

bool validFooter(uint8_t b) {
  return (b & 0xF0) == 0xC0;
}

// void processPacket(const uint8_t *pkt) {
//   static int packetCount = 0;
//   packetCount++;

//   if (packetCount % 50 != 0) return;

//   int32_t counts[8];
//   float uV[8];

//   for (int ch = 0; ch < 8; ch++) {
//     int base = 2 + ch * 3;
//     counts[ch] = int24ToInt32(pkt[base], pkt[base + 1], pkt[base + 2]);
//     uV[ch] = counts[ch] * scale_uV_per_count;
//   }

//   Serial.print("sample=");
//   Serial.print(pkt[1]);
//   Serial.print(" | ");

//   for (int ch = 0; ch < 8; ch++) {
//     Serial.print(uV[ch], 2);
//     if (ch < 7) Serial.print(", ");
//   }
//   Serial.println();
// }

//debug proc packet func to ensure header and footer bits are right
// void processPacket(const uint8_t *pkt) {
//   static int packetCount = 0;
//   packetCount++;

//   if (packetCount % 20 != 0) return;  // slow it down

//   Serial.print("Header=0x");
//   Serial.print(pkt[0], HEX);

//   Serial.print(" Sample=");
//   Serial.print(pkt[1]);

//   Serial.print(" Footer=0x");
//   Serial.print(pkt[32], HEX);

//   Serial.print(" | ");

//   for (int ch = 0; ch < 8; ch++) {
//     int base = 2 + ch * 3;
//     int32_t val = int24ToInt32(pkt[base], pkt[base + 1], pkt[base + 2]);
//     Serial.print(val);
//     if (ch < 7) Serial.print(", ");
//   }

//   Serial.println();
// }

void processPacket(const uint8_t *pkt) {
  int32_t counts[8];
  float uV[8];

  for (int ch = 0; ch < 8; ch++) {
    int base = 2 + ch * 3;
    counts[ch] = int24ToInt32(pkt[base], pkt[base + 1], pkt[base + 2]);
    uV[ch] = counts[ch] * scale_uV_per_count;
  }

  for (int ch = 0; ch < 8; ch++) {
    Serial.print(uV[ch], 3);
    if (ch < 7) Serial.print('\t');
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  scale_uV_per_count = (4.5f / ADS_GAIN / 8388607.0f) * 1000000.0f;

  Serial1.begin(UART_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);

  Serial.println("ESP32 parser starting...");
  Serial.print("Scale (uV/count): ");
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
// void loop() {
//   static int count = 0;

//   while (Serial1.available() > 0) {
//     uint8_t b = Serial1.read();

//     if (b < 0x10) Serial.print('0');
//     Serial.print(b, HEX);
//     Serial.print(' ');

//     count++;
//     if (count == 33) {
//       Serial.println();
//       count = 0;
//     }
//   }
// }
// void loop() {
//   while (Serial1.available() > 0) {
//     uint8_t b = Serial1.read();

//     // Start new line on header
//     if (b == 0xA0) {
//       Serial.println();   // new packet line
//       Serial.print("A0 ");
//       continue;
//     }

//     // Print byte (zero-padded)
//     if (b < 0x10) Serial.print('0');
//     Serial.print(b, HEX);
//     Serial.print(' ');
//   }
// }
// void loop() {
//   while (Serial1.available() > 0) {
//     uint8_t b = (uint8_t)Serial1.read();
//     Serial.print(b, HEX);
//     Serial.print(" ");
//   }
// }