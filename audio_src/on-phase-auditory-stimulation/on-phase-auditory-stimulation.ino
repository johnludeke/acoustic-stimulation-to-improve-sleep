// ESP32-C6 + OpenBCI Cyton UART Closed-Loop Slow-Wave Stim
//
// Single timestamp domain:
//   All printed timestamps are sample-time ms:
//     sampleTimeMs = globalSampleIndex * 4
//
// Event format:
//   DATA,t_ms,filtered_uV
//   FREQ,t_ms,freq_hz,T_ms
//   TROUGH,trough_t_ms,trough_uV,pred_peak_t_ms
//   STIM,t_ms

#include <Arduino.h>

// -----------------------------
// Cyton UART config
// -----------------------------
static const int RX_PIN = 17;
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
// Faster autocorr buffer config
// -----------------------------
// Full signal is 250 Hz, but autocorr only needs slow-wave timing.
// 250 / 5 = 50 Hz, still enough for 0.5–4 Hz.
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
#define AUDIO_PIN              4
#define AUDIO_FREQ_HZ      20000
#define AUDIO_RESOLUTION       8
#define AUDIO_SAMPLE_RATE_HZ 4000
#define NOISE_DURATION_MS    250

// -----------------------------
// Serial output
// -----------------------------
static const int DATA_PRINT_DECIMATION = 5;  // 50 Hz DATA output

// -----------------------------
// Global sample clock
// -----------------------------
uint32_t globalSampleIndex = 0;

uint32_t sampleTimeMsFromIndex(uint32_t idx) {
  return idx * SAMPLE_PERIOD_MS;
}

uint32_t currentSampleTimeMs() {
  return sampleTimeMsFromIndex(globalSampleIndex);
}

// -----------------------------
// Packet helpers
// -----------------------------
int32_t int24ToInt32(uint8_t b0, uint8_t b1, uint8_t b2) {
  int32_t value = ((int32_t)b0 << 16) | ((int32_t)b1 << 8) | b2;
  if (value & 0x00800000) value |= 0xFF000000;
  return value;
}

bool validFooter(uint8_t b) {
  return (b & 0xF0) == 0xC0;
}

// -----------------------------
// Simple embedded 0.5–4 Hz filter
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
 -1.9822289f, 0.9823855f,
  0.0f, 0.0f
};

Biquad lp = {
  0.0023333f, 0.0046666f, 0.0023333f,
 -1.8580972f, 0.8674304f,
  0.0f, 0.0f
};

float bandpassProcess(float x) {
  return lp.process(hp.process(x));
}

// -----------------------------
// Rolling 30s filtered buffer for full-rate data / plotting
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
// Decimated 30s buffer for fast autocorrelation
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
  int idx = (start + i) % AC_SAMPLES;
  return acBuf[idx];
}

// -----------------------------
// 30s autocorrelation frequency estimate, using decimated 50 Hz buffer
// -----------------------------
float currentFreqHz = 0.0f;
float currentPeriodMs = 0.0f;
uint32_t lastFreqUpdateSampleMs = 0;

bool estimateDominantFreqAutocorr(float &freqHz, float &periodMs) {
  if (acCount < AC_SAMPLES) return false;

  float mean = 0.0f;
  for (int i = 0; i < acCount; i++) {
    mean += getAcChrono(i);
  }
  mean /= acCount;

  float energy = 0.0f;
  for (int i = 0; i < acCount; i++) {
    float x = getAcChrono(i) - mean;
    energy += x * x;
  }
  if (energy < 1e-6f) return false;

  int minLag = (int)(AC_FS / BAND_HIGH); // 4 Hz => 12 samples at 50 Hz
  int maxLag = (int)(AC_FS / BAND_LOW);  // 0.5 Hz => 100 samples at 50 Hz

  float bestCorr = -1e30f;
  int bestLag = minLag;

  for (int lag = minLag; lag <= maxLag; lag++) {
    float corr = 0.0f;

    for (int i = 0; i < acCount - lag; i++) {
      float x0 = getAcChrono(i) - mean;
      float x1 = getAcChrono(i + lag) - mean;
      corr += x0 * x1;
    }

    if (corr > bestCorr) {
      bestCorr = corr;
      bestLag = lag;
    }
  }

  float periodSec = bestLag / AC_FS;
  freqHz = 1.0f / periodSec;
  periodMs = periodSec * 1000.0f;
  return true;
}

// -----------------------------
// Trough detection
// -----------------------------
float yPrev2 = 0.0f;
float yPrev1 = 0.0f;
bool havePrev2 = false;
bool havePrev1 = false;

int32_t lastTroughSample = -1000000;

bool detectTrough(float yCurr, int32_t &troughSample, float &troughVal) {
  if (!havePrev2 || !havePrev1) return false;

  int32_t candidateSample = (int32_t)globalSampleIndex - 1;

  bool isLocalMin = (yPrev1 < yPrev2) && (yPrev1 < yCurr);
  if (!isLocalMin) return false;

  int minDistSamples = (int)(MIN_TROUGH_DISTANCE_SEC * FS);
  if ((candidateSample - lastTroughSample) < minDistSamples) return false;

  if (yPrev1 > -MIN_TROUGH_PROM_UV) return false;

  troughSample = candidateSample;
  troughVal = yPrev1;
  lastTroughSample = candidateSample;
  return true;
}

// -----------------------------
// Pink noise generator
// -----------------------------
static int32_t pinkRows[16] = {0};
static int32_t pinkRunningSum = 0;
static uint32_t pinkIndex = 0;

uint8_t pinkNoiseSample() {
  uint32_t lastIndex = pinkIndex;
  pinkIndex++;

  uint32_t diff = lastIndex ^ pinkIndex;
  for (int i = 0; i < 16; i++) {
    if (diff & (1u << i)) {
      pinkRunningSum -= pinkRows[i];
      pinkRows[i] = (int32_t)(esp_random() >> 16) - 32768;
      pinkRunningSum += pinkRows[i];
    }
  }

  int32_t white = (int32_t)(esp_random() >> 16) - 32768;
  int32_t raw = pinkRunningSum + white;

  int32_t scaled = (raw / 1000) + 128;
  if (scaled < 0) scaled = 0;
  if (scaled > 255) scaled = 255;
  return (uint8_t)scaled;
}

static volatile bool audioActive = false;
static uint32_t audioStopWallMs = 0;
static uint8_t lastNoiseSample = 128;

void startNoiseBurst(uint32_t eventSampleMs) {
  audioActive = true;
  audioStopWallMs = millis() + NOISE_DURATION_MS;

  Serial.print("STIM,");
  Serial.println(eventSampleMs);
}

void stopNoise() {
  audioActive = false;
  ledcWrite(AUDIO_PIN, 0);
}

void updateAudio() {
  static uint32_t lastAudioUs = 0;
  static const uint32_t audioIntervalUs = 1000000UL / AUDIO_SAMPLE_RATE_HZ;

  uint32_t nowUs = micros();
  uint32_t nowWallMs = millis();

  if (audioActive && (int32_t)(nowWallMs - audioStopWallMs) >= 0) {
    stopNoise();
  }

  if ((nowUs - lastAudioUs) >= audioIntervalUs) {
    lastAudioUs = nowUs;

    if (audioActive) {
      lastNoiseSample = pinkNoiseSample();
      ledcWrite(AUDIO_PIN, lastNoiseSample);
    }
  }
}

// -----------------------------
// Sample-time to wall-time mapping for scheduled stim
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
  scheduledPeakWallMs = sampleMsToWallMs(predPeakSampleMs);
  peakScheduled = true;
}

void updateScheduledStim() {
  if (!peakScheduled) return;

  uint32_t nowWallMs = millis();

  if ((int32_t)(nowWallMs - scheduledPeakWallMs) >= 0) {
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

  // 1. Full-rate causal 0.5–4 Hz filtering
  float y = bandpassProcess(ch0_uV);

  // 2. Store full-rate filtered signal for DATA plotting / full 30s context
  pushFilteredSample(y);

  // 3. Store decimated filtered signal for cheaper 30s autocorrelation
  if ((globalSampleIndex % AC_DECIM) == 0) {
    pushAutocorrSample(y);
  }

  // 4. Smooth only for trough detection, not for plotting
  if (!smoothInitialized) {
    smoothY = y;
    smoothInitialized = true;
  } else {
    smoothY = SMOOTH_ALPHA * y + (1.0f - SMOOTH_ALPHA) * smoothY;
  }

  // 5. Print bandpassed signal for Python visualization
  if ((globalSampleIndex % DATA_PRINT_DECIMATION) == 0) {
    Serial.print("DATA,");
    Serial.print(sampleMs);
    Serial.print(",");
    Serial.println(y, 3);
  }

  // 6. Update dominant frequency every 1000 ms in sample-time domain
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

  // 7. Trough detection + peak prediction + audio scheduling
  if (SWS_Detected && currentPeriodMs > 0.0f) {
    int32_t troughSample;
    float troughVal;

    // Detect troughs on smoothed filtered signal
    if (detectTrough(smoothY, troughSample, troughVal)) {
      uint32_t troughSampleMs = sampleTimeMsFromIndex((uint32_t)troughSample);
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

  // Update trough detector history using smoothed signal
  yPrev2 = yPrev1;
  yPrev1 = smoothY;

  if (!havePrev1) havePrev1 = true;
  else if (!havePrev2) havePrev2 = true;

  globalSampleIndex++;
}

// -----------------------------
// Cyton packet processing
// -----------------------------
void processPacket(const uint8_t *pkt) {
  int base = 2;  // channel 0 / OpenBCI channel 1
  int32_t count0 = int24ToInt32(pkt[base], pkt[base + 1], pkt[base + 2]);
  float ch0_uV = count0 * scale_uV_per_count;

  processChannel0Sample(ch0_uV);
}

// -----------------------------
// Arduino setup / loop
// -----------------------------
void setup() {
  Serial.begin(230400);
  delay(1000);

  scale_uV_per_count = (4.5f / ADS_GAIN / 8388607.0f) * 1000000.0f;

  Serial1.begin(UART_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);

  ledcAttach(AUDIO_PIN, AUDIO_FREQ_HZ, AUDIO_RESOLUTION);
  ledcWrite(AUDIO_PIN, 0);

  for (int i = 0; i < 100; i++) pinkNoiseSample();

  Serial.println("BOOT,ESP32_CYTON_CLOSED_LOOP_STIM_SAMPLE_CLOCK_FAST_AC");
  Serial.print("BOOT,scale_uV_per_count,");
  Serial.println(scale_uV_per_count, 8);
  Serial.println("BOOT,DATA=0.5-4Hz bandpassed y, TROUGH=smoothed detection y");
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