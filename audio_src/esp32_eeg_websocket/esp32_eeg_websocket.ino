// ESP32-C6 + OpenBCI Cyton UART Closed-Loop Slow-Wave Stim
// WebSocket Edition — streams EEG data to a browser dashboard over WiFi
//
// Setup:
//   1. Set WIFI_SSID and WIFI_PASSWORD below
//   2. Flash to ESP32-C6
//   3. Open Serial Monitor to find the ESP32's IP address
//   4. Open the web dashboard in a browser on the same network
//
// WebSocket messages (same format as previous Serial output):
//   DATA,t_ms,filtered_uV
//   FREQ,t_ms,freq_hz,T_ms
//   TROUGH,trough_t_ms,trough_uV,pred_peak_t_ms
//   STIM,t_ms
//
// WebSocket server runs on ws://<ESP32_IP>:81

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsServer.h>  // Install: "WebSockets" by Markus Sattler via Library Manager

// -----------------------------------------------
// *** SET YOUR WIFI CREDENTIALS HERE ***
// -----------------------------------------------
static const char* WIFI_SSID     = "esp32";
static const char* WIFI_PASSWORD = "ilovesleep";
// -----------------------------------------------

WebSocketsServer webSocket(81);

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
// Serial output decimation
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
// WebSocket broadcast helper
// Sends a string to all connected clients.
// Also mirrors to Serial for debugging.
// -----------------------------
void wsBroadcast(String msg) {
  webSocket.broadcastTXT(msg);
  Serial.println(msg);
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
  int idx = (start + i) % AC_SAMPLES;
  return acBuf[idx];
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

  int minLag = (int)(AC_FS / BAND_HIGH);
  int maxLag = (int)(AC_FS / BAND_LOW);

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

  String msg = "STIM," + String(eventSampleMs);
  wsBroadcast(msg);
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
// Sample-time to wall-time mapping
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
// WebSocket event handler
// Allows the web app to send commands back to the ESP32
// e.g. "SWS_ON", "SWS_OFF"
// -----------------------------
void onWebSocketEvent(uint8_t clientId, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_TEXT) {
    String msg = String((char*)payload);
    msg.trim();

    if (msg == "SWS_ON") {
      SWS_Detected = true;
      Serial.println("CMD: SWS_ON");
    } else if (msg == "SWS_OFF") {
      SWS_Detected = false;
      Serial.println("CMD: SWS_OFF");
    }

    // Acknowledge to all clients
    String ack = "STATUS,SWS_Detected," + String(SWS_Detected ? "1" : "0");
    webSocket.broadcastTXT(ack);
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

  // 2. Store full-rate filtered signal
  pushFilteredSample(y);

  // 3. Store decimated filtered signal for autocorrelation
  if ((globalSampleIndex % AC_DECIM) == 0) {
    pushAutocorrSample(y);
  }

  // 4. Smooth for trough detection only
  if (!smoothInitialized) {
    smoothY = y;
    smoothInitialized = true;
  } else {
    smoothY = SMOOTH_ALPHA * y + (1.0f - SMOOTH_ALPHA) * smoothY;
  }

  // 5. Send bandpassed signal at 50 Hz over WebSocket
  if ((globalSampleIndex % DATA_PRINT_DECIMATION) == 0) {
    char buf[48];
    snprintf(buf, sizeof(buf), "DATA,%lu,%.3f", (unsigned long)sampleMs, y);
    wsBroadcast(String(buf));
  }

  // 6. Update dominant frequency every 1000 ms
  if ((sampleMs - lastFreqUpdateSampleMs) >= FREQ_UPDATE_MS) {
    float fHz, tMs;
    if (estimateDominantFreqAutocorr(fHz, tMs)) {
      currentFreqHz = fHz;
      currentPeriodMs = tMs;

      char buf[64];
      snprintf(buf, sizeof(buf), "FREQ,%lu,%.3f,%.1f",
               (unsigned long)sampleMs, currentFreqHz, currentPeriodMs);
      wsBroadcast(String(buf));
    }

    lastFreqUpdateSampleMs = sampleMs;
  }

  // 7. Trough detection + peak prediction + audio scheduling
  if (SWS_Detected && currentPeriodMs > 0.0f) {
    int32_t troughSample;
    float troughVal;

    if (detectTrough(smoothY, troughSample, troughVal)) {
      uint32_t troughSampleMs = sampleTimeMsFromIndex((uint32_t)troughSample);
      uint32_t predPeakSampleMs = troughSampleMs + (uint32_t)(currentPeriodMs / 2.0f);

      char buf[80];
      snprintf(buf, sizeof(buf), "TROUGH,%lu,%.3f,%lu",
               (unsigned long)troughSampleMs, troughVal,
               (unsigned long)predPeakSampleMs);
      wsBroadcast(String(buf));

      schedulePeakStim(predPeakSampleMs);
    }
  }

  // Update trough detector history
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
  int base = 2;
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

  // --- WiFi ---
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  // --- WiFi Access Point (ESP32 creates its own network) ---
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32_EEG", "12345678");

  Serial.println();
  Serial.println("ESP32 Access Point started");
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Connect your laptop/phone to WiFi: ESP32_EEG");
  Serial.println("Password: 12345678");
  Serial.println("WebSocket URL: ws://192.168.4.1:81");
  Serial.println();
  Serial.print("WiFi connected. ESP32 IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("Open your browser to the dashboard and set the IP above.");
  Serial.print("WebSocket URL: ws://");
  Serial.print(WiFi.localIP());
  Serial.println(":81");

  // --- WebSocket server ---
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  // --- Cyton UART ---
  Serial1.begin(UART_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);

  // --- Audio ---
  ledcAttach(AUDIO_PIN, AUDIO_FREQ_HZ, AUDIO_RESOLUTION);
  ledcWrite(AUDIO_PIN, 0);
  for (int i = 0; i < 100; i++) pinkNoiseSample();

  Serial.println("BOOT,ESP32_CYTON_WEBSOCKET_STIM");
}

void loop() {
  // Keep WebSocket server alive
  webSocket.loop();

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
