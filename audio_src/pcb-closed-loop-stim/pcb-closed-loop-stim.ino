// ESP32-WROOM-32E + OpenBCI Cyton — Closed-Loop Slow-Wave Stim
// PCB Rev 1 (Team 18)
//
// Hardware differences from breadboard prototype:
//   Audio: MCP4822 12-bit DAC via SPI → PAM8302 amp → speaker
//          (replaces ledcWrite PWM on old dev board)
//   UART:  Serial1 RX on J5 connector (IO17), TX unused
//   Flash: via J6
//
// Serial event format (230400 baud on USB):
//   DATA,t_ms,filtered_uV
//   FREQ,t_ms,freq_hz,T_ms
//   TROUGH,trough_t_ms,trough_uV,pred_peak_t_ms
//   STIM,t_ms

#include <Arduino.h>
#include <SPI.h>

// -----------------------------
// DAC (MCP4822) pin mapping
// -----------------------------
#define DAC_CS_PIN    5    // IO5  -> MCP4822 CS
#define DAC_SCK_PIN   18   // IO18 -> MCP4822 SCK
#define DAC_MOSI_PIN  23   // IO23 -> MCP4822 SDI
#define DAC_MID       2048 // 12-bit midpoint (silence)
#define NOISE_AMPL    500  // amplitude in DAC counts (0–2048 max)

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
// Sampling / signal config
// -----------------------------
static const float FS = 250.0f;
static const uint32_t SAMPLE_PERIOD_MS = 4;

static const int EPOCH_SEC = 30;
static const int EPOCH_SAMPLES = 7500;

static const float BAND_LOW = 0.5f;
static const float BAND_HIGH = 4.0f;

static const uint32_t FREQ_UPDATE_MS = 1000;
static const float MIN_TROUGH_DISTANCE_SEC = 0.7f;
static const float MIN_TROUGH_PROM_UV = 25.0f;

bool SWS_Detected = true;

// -----------------------------
// Decimated autocorr buffer
// 250 Hz / 5 = 50 Hz, sufficient for 0.5–4 Hz
// -----------------------------
static const int AC_DECIM = 5;
static const float AC_FS = FS / AC_DECIM;
static const int AC_SAMPLES = EPOCH_SEC * 50;  // 1500

// -----------------------------
// Trough detection smoothing
// -----------------------------
static const float SMOOTH_ALPHA = 0.25f;
float smoothY = 0.0f;
bool smoothInitialized = false;

// -----------------------------
// Audio config
// -----------------------------
#define AUDIO_SAMPLE_RATE_HZ 4000
#define NOISE_DURATION_MS    250

// -----------------------------
// Serial output decimation
// -----------------------------
static const int DATA_PRINT_DECIMATION = 5;  // 50 Hz DATA output

// -----------------------------
// Global sample clock
// -----------------------------
uint32_t globalSampleIndex = 0;

uint32_t sampleTimeMsFromIndex(uint32_t idx) { return idx * SAMPLE_PERIOD_MS; }
uint32_t currentSampleTimeMs()               { return sampleTimeMsFromIndex(globalSampleIndex); }

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
// 0.5–4 Hz biquad bandpass filter
// -----------------------------
struct Biquad {
  float b0, b1, b2;
  float a1, a2;
  float z1, z2;

  float process(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

Biquad hp = {
  0.9911536f, -1.9823072f, 0.9911536f,
 -1.9822289f,  0.9823855f,
  0.0f, 0.0f
};

Biquad lp = {
  0.0023333f, 0.0046666f, 0.0023333f,
 -1.8580972f, 0.8674304f,
  0.0f, 0.0f
};

float bandpassProcess(float x) { return lp.process(hp.process(x)); }

// -----------------------------
// Rolling 30s filtered buffer
// -----------------------------
float filtBuf[EPOCH_SAMPLES];
int filtHead = 0;
int filtCount = 0;

void pushFilteredSample(float y) {
  filtBuf[filtHead] = y;
  filtHead = (filtHead + 1) % EPOCH_SAMPLES;
  if (filtCount < EPOCH_SAMPLES) filtCount++;
}

// -----------------------------
// Decimated 30s buffer for autocorrelation
// -----------------------------
float acBuf[AC_SAMPLES];
int acHead = 0;
int acCount = 0;

void pushAutocorrSample(float y) {
  acBuf[acHead] = y;
  acHead = (acHead + 1) % AC_SAMPLES;
  if (acCount < AC_SAMPLES) acCount++;
}

float getAcChrono(int i) {
  int start = (acHead - acCount + AC_SAMPLES) % AC_SAMPLES;
  return acBuf[(start + i) % AC_SAMPLES];
}

// -----------------------------
// 30s autocorrelation frequency estimate
// -----------------------------
float currentFreqHz = 0.0f;
float currentPeriodMs = 0.0f;
uint32_t lastFreqUpdateSampleMs = 0;

bool estimateDominantFreqAutocorr(float &freqHz, float &periodMs) {
  if (acCount < AC_SAMPLES) return false;

  float mean = 0.0f;
  for (int i = 0; i < acCount; i++) mean += getAcChrono(i);
  mean /= acCount;

  float energy = 0.0f;
  for (int i = 0; i < acCount; i++) {
    float x = getAcChrono(i) - mean;
    energy += x * x;
  }
  if (energy < 1e-6f) return false;

  int minLag = (int)(AC_FS / BAND_HIGH); // ~12 samples at 50 Hz for 4 Hz
  int maxLag = (int)(AC_FS / BAND_LOW);  // ~100 samples at 50 Hz for 0.5 Hz

  float bestCorr = -1e30f;
  int bestLag = minLag;

  for (int lag = minLag; lag <= maxLag; lag++) {
    float corr = 0.0f;
    for (int i = 0; i < acCount - lag; i++) {
      corr += (getAcChrono(i) - mean) * (getAcChrono(i + lag) - mean);
    }
    if (corr > bestCorr) {
      bestCorr = corr;
      bestLag = lag;
    }
  }

  float periodSec = bestLag / AC_FS;
  freqHz  = 1.0f / periodSec;
  periodMs = periodSec * 1000.0f;
  return true;
}

// -----------------------------
// Trough detection
// -----------------------------
float yPrev2 = 0.0f, yPrev1 = 0.0f;
bool havePrev2 = false, havePrev1 = false;
int32_t lastTroughSample = -1000000;

bool detectTrough(float yCurr, int32_t &troughSample, float &troughVal) {
  if (!havePrev2 || !havePrev1) return false;

  int32_t candidateSample = (int32_t)globalSampleIndex - 1;

  if (!((yPrev1 < yPrev2) && (yPrev1 < yCurr))) return false;

  int minDistSamples = (int)(MIN_TROUGH_DISTANCE_SEC * FS);
  if ((candidateSample - lastTroughSample) < minDistSamples) return false;

  if (yPrev1 > -MIN_TROUGH_PROM_UV) return false;

  troughSample = candidateSample;
  troughVal = yPrev1;
  lastTroughSample = candidateSample;
  return true;
}

// -----------------------------
// MCP4822 DAC write
// -----------------------------
void writeDAC_A(uint16_t value12) {
  value12 &= 0x0FFF;
  uint16_t word = 0x3000 | value12;  // channel A, 1x gain, active
  digitalWrite(DAC_CS_PIN, LOW);
  SPI.transfer16(word);
  digitalWrite(DAC_CS_PIN, HIGH);
}

// -----------------------------
// Pink noise generator — 12-bit DAC output
// -----------------------------
static int32_t pinkRows[16] = {0};
static int32_t pinkRunningSum = 0;
static uint32_t pinkIndex = 0;

uint16_t pinkNoiseSample12() {
  uint32_t lastIdx = pinkIndex++;
  uint32_t diff = lastIdx ^ pinkIndex;

  for (int i = 0; i < 16; i++) {
    if (diff & (1u << i)) {
      pinkRunningSum -= pinkRows[i];
      pinkRows[i] = (int32_t)(esp_random() >> 16) - 32768;
      pinkRunningSum += pinkRows[i];
    }
  }

  int32_t white = (int32_t)(esp_random() >> 16) - 32768;
  int32_t raw   = pinkRunningSum + white;

  // Scale to 12-bit range centered at DAC_MID
  int32_t sample = DAC_MID + (int32_t)((int64_t)raw * NOISE_AMPL / 524288LL);
  if (sample < 0)    sample = 0;
  if (sample > 4095) sample = 4095;
  return (uint16_t)sample;
}

// -----------------------------
// Audio state
// -----------------------------
static volatile bool audioActive = false;
static uint32_t audioStopWallMs = 0;

void startNoiseBurst(uint32_t eventSampleMs) {
  audioActive = true;
  audioStopWallMs = millis() + NOISE_DURATION_MS;

  Serial.print("STIM,");
  Serial.println(eventSampleMs);
}

void stopNoise() {
  audioActive = false;
  writeDAC_A(DAC_MID);
}

void updateAudio() {
  static uint32_t lastAudioUs = 0;
  static const uint32_t intervalUs = 1000000UL / AUDIO_SAMPLE_RATE_HZ;

  uint32_t nowUs     = micros();
  uint32_t nowWallMs = millis();

  if (audioActive && (int32_t)(nowWallMs - audioStopWallMs) >= 0) {
    stopNoise();
    return;
  }

  if ((nowUs - lastAudioUs) >= intervalUs) {
    lastAudioUs = nowUs;
    writeDAC_A(audioActive ? pinkNoiseSample12() : DAC_MID);
  }
}

// -----------------------------
// Sample-time → wall-time mapping
// -----------------------------
bool clockMapped = false;
uint32_t sampleClockStartWallMs = 0;

void updateClockMappingIfNeeded() {
  if (!clockMapped && globalSampleIndex == 0) {
    sampleClockStartWallMs = millis();
    clockMapped = true;
  }
}

uint32_t sampleMsToWallMs(uint32_t sampleMs) {
  return sampleClockStartWallMs + sampleMs;
}

// -----------------------------
// Peak scheduling
// -----------------------------
bool peakScheduled = false;
uint32_t scheduledPeakSampleMs = 0;
uint32_t scheduledPeakWallMs = 0;

void schedulePeakStim(uint32_t predPeakSampleMs) {
  scheduledPeakSampleMs = predPeakSampleMs;
  scheduledPeakWallMs   = sampleMsToWallMs(predPeakSampleMs);
  peakScheduled = true;
}

void updateScheduledStim() {
  if (!peakScheduled) return;
  if ((int32_t)(millis() - scheduledPeakWallMs) >= 0) {
    peakScheduled = false;
    startNoiseBurst(scheduledPeakSampleMs);
  }
}

// -----------------------------
// Main sample processing
// -----------------------------
void processChannel0Sample(float ch0_uV) {
  updateClockMappingIfNeeded();

  uint32_t sampleMs = currentSampleTimeMs();

  float y = bandpassProcess(ch0_uV);
  pushFilteredSample(y);

  if ((globalSampleIndex % AC_DECIM) == 0) pushAutocorrSample(y);

  if (!smoothInitialized) { smoothY = y; smoothInitialized = true; }
  else                      smoothY = SMOOTH_ALPHA * y + (1.0f - SMOOTH_ALPHA) * smoothY;

  if ((globalSampleIndex % DATA_PRINT_DECIMATION) == 0) {
    Serial.print("DATA,");
    Serial.print(sampleMs);
    Serial.print(",");
    Serial.println(y, 3);
  }

  if ((sampleMs - lastFreqUpdateSampleMs) >= FREQ_UPDATE_MS) {
    float fHz, tMs;
    if (estimateDominantFreqAutocorr(fHz, tMs)) {
      currentFreqHz = fHz;
      currentPeriodMs = tMs;

      Serial.print("FREQ,");
      Serial.print(sampleMs);
      Serial.print(",");
      Serial.print(currentFreqHz, 3);
      Serial.print(",");
      Serial.println(currentPeriodMs, 1);
    }
    lastFreqUpdateSampleMs = sampleMs;
  }

  if (SWS_Detected && currentPeriodMs > 0.0f) {
    int32_t troughSample;
    float troughVal;

    if (detectTrough(smoothY, troughSample, troughVal)) {
      uint32_t troughSampleMs  = sampleTimeMsFromIndex((uint32_t)troughSample);
      uint32_t predPeakSampleMs = troughSampleMs + (uint32_t)(currentPeriodMs / 2.0f);

      Serial.print("TROUGH,");
      Serial.print(troughSampleMs);
      Serial.print(",");
      Serial.print(troughVal, 3);
      Serial.print(",");
      Serial.println(predPeakSampleMs);

      schedulePeakStim(predPeakSampleMs);
    }
  }

  yPrev2 = yPrev1;
  yPrev1 = smoothY;
  if (!havePrev1)      havePrev1 = true;
  else if (!havePrev2) havePrev2 = true;

  globalSampleIndex++;
}

// -----------------------------
// Cyton packet processing
// -----------------------------
void processPacket(const uint8_t *pkt) {
  int base = 2;  // channel 0 / OpenBCI channel 1
  int32_t count0 = int24ToInt32(pkt[base], pkt[base + 1], pkt[base + 2]);
  processChannel0Sample(count0 * scale_uV_per_count);
}

// -----------------------------
// Arduino setup / loop
// -----------------------------
void setup() {
  Serial.begin(230400);

  scale_uV_per_count = (4.5f / ADS_GAIN / 8388607.0f) * 1000000.0f;

  // Cyton UART on J5 — increase buffer before begin() to survive boot delay
  Serial1.setRxBufferSize(1024);
  Serial1.begin(UART_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);

  delay(500);

  // MCP4822 DAC via SPI
  pinMode(DAC_CS_PIN, OUTPUT);
  digitalWrite(DAC_CS_PIN, HIGH);
  SPI.begin(DAC_SCK_PIN, -1, DAC_MOSI_PIN, DAC_CS_PIN);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  writeDAC_A(DAC_MID);

  // Warm up pink noise state
  for (int i = 0; i < 100; i++) pinkNoiseSample12();

  Serial.println("BOOT,PCB_ESP32_CYTON_CLOSED_LOOP_STIM");
  Serial.print("BOOT,scale_uV_per_count,");
  Serial.println(scale_uV_per_count, 8);
}

void loop() {
  updateAudio();
  updateScheduledStim();

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
