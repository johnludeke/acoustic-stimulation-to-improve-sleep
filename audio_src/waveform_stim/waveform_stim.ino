// waveform_stim.ino
// ESP32-C6-WROOM-1 — Waveform-Triggered Pink Noise Stimulator
//
// Hardware:
//   GPIO 0   <- waveform generator (0–3.3 V, ADC1 ch0 — only GPIO 0–6 are ADC on ESP32-C6)
//   GPIO 4   -> LM386N-1 amplifier -> AST-03008MR-R speaker
//
// Behavior:
//   - Samples the input waveform at ADC_SAMPLE_RATE_HZ and displays it in
//     the Arduino Serial Plotter (Voltage_mV, Freq_Hz, Amp_mV traces).
//   - When the detected frequency is within FREQ_TOLERANCE_HZ of TARGET_FREQ_HZ:
//       1. Wait for the next peak, then play NOISE_DURATION_MS ms of pink noise.
//       2. Wait for the following peak, then play another burst.
//       3. Enter a BREAK_DURATION_MS ms pause, then repeat from step 1.

#include <Arduino.h>

// ── Configurable constants ─────────────────────────────────────────────────

#define ADC_PIN                0      // GPIO 0 (ADC1 ch0) — waveform input; must be GPIO 0–6 on ESP32-C6
#define AUDIO_PIN              4      // GPIO 4 — PWM audio output
#define AUDIO_FREQ_HZ      20000      // PWM carrier (above hearing range)
#define AUDIO_RESOLUTION       8      // 8-bit resolution (duty 0–255)
#define AUDIO_SAMPLE_RATE_HZ 4000     // Pink noise sample rate (Hz)

#define ADC_SAMPLE_RATE_HZ   500      // ADC sampling rate (Hz)
#define TARGET_FREQ_HZ       1.0f     // Target waveform frequency to match (Hz)
#define FREQ_TOLERANCE_HZ    0.4f     // ± tolerance around target (Hz)
#define NOISE_DURATION_MS    250      // Length of each pink noise burst (ms)
#define BREAK_DURATION_MS   2500      // Pause duration after two bursts (ms)
#define PEAK_HYSTERESIS       80      // ADC counts below localMax to confirm peak
#define AMP_WINDOW_MS       3000      // Rolling window for amplitude tracking (ms)
#define PEAK_BUFFER_SIZE       4      // Number of peak timestamps to keep
#define AMP_BUFFER_SIZE     1500      // ADC_SAMPLE_RATE_HZ * AMP_WINDOW_MS / 1000

// ── Forward declarations ───────────────────────────────────────────────────

int getAmplitudeRaw();
void resetPeakDetector();
void resetFrequencyEstimator();

// ── State machine ──────────────────────────────────────────────────────────

enum State {
  LISTEN,       // Monitoring frequency, waiting for target
  WAIT_PEAK1,   // Target freq confirmed; waiting for first peak
  NOISE1,       // Playing first pink noise burst
  WAIT_PEAK2,   // Waiting for second peak
  NOISE2,       // Playing second pink noise burst
  BREAK         // Resting before next cycle
};

static State currentState = LISTEN;
static uint32_t stateEntryMs = 0;

// ── Amplitude tracking ─────────────────────────────────────────────────────

static int ampBuffer[AMP_BUFFER_SIZE];
static const int AMP_SAMPLES = AMP_BUFFER_SIZE;
static int ampHead = 0;
static int ampCount = 0;

void updateAmplitudeBuffer(int sample) {
  ampBuffer[ampHead] = sample;
  ampHead = (ampHead + 1) % AMP_SAMPLES;
  if (ampCount < AMP_SAMPLES) ampCount++;
}

float getAmplitude() {
  if (ampCount == 0) return 0.0f;
  int lo = 4095, hi = 0;
  for (int i = 0; i < ampCount; i++) {
    int v = ampBuffer[i];
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  return (hi - lo) * 3300.0f / 4095.0f;  // mV peak-to-peak
}

float getRollingMean() {
  if (ampCount == 0) return 2048.0f;
  long sum = 0;
  for (int i = 0; i < ampCount; i++) sum += ampBuffer[i];
  return (float)sum / ampCount;
}

// ── Frequency estimation ───────────────────────────────────────────────────

static uint32_t peakTimes[PEAK_BUFFER_SIZE];
static int peakWrite = 0;
static int peakFilled = 0;

void updateFrequency(uint32_t peakTimeMs) {
  peakTimes[peakWrite] = peakTimeMs;
  peakWrite = (peakWrite + 1) % PEAK_BUFFER_SIZE;
  if (peakFilled < PEAK_BUFFER_SIZE) peakFilled++;
}

float getFrequency() {
  if (peakFilled < 2) return 0.0f;

  // Average the last two inter-peak intervals
  int n = (peakFilled < PEAK_BUFFER_SIZE) ? peakFilled : PEAK_BUFFER_SIZE;
  int intervals = (n >= 3) ? 2 : 1;
  float totalPeriodMs = 0.0f;

  for (int i = 0; i < intervals; i++) {
    int idx1 = (peakWrite - 1 - i + PEAK_BUFFER_SIZE) % PEAK_BUFFER_SIZE;
    int idx0 = (peakWrite - 2 - i + PEAK_BUFFER_SIZE) % PEAK_BUFFER_SIZE;
    uint32_t period = peakTimes[idx1] - peakTimes[idx0];
    if (period == 0) return 0.0f;
    totalPeriodMs += period;
  }

  float avgPeriodMs = totalPeriodMs / intervals;
  return 1000.0f / avgPeriodMs;
}

bool frequencyInRange(float freq) {
  return (freq >= (TARGET_FREQ_HZ - FREQ_TOLERANCE_HZ)) &&
         (freq <= (TARGET_FREQ_HZ + FREQ_TOLERANCE_HZ));
}

// ── Peak detection ─────────────────────────────────────────────────────────

static bool risingEdge = false;
static int  localMax   = 0;

bool detectPeak(int sample) {
  float mean = getRollingMean();
  float amp  = getAmplitudeRaw();
  int   threshold = (int)(mean + amp * 0.25f);  // 25% above mean

  if (sample > localMax) {
    localMax    = sample;
    risingEdge  = true;
    return false;
  }

  if (risingEdge && (localMax - sample) >= PEAK_HYSTERESIS && localMax > threshold) {
    risingEdge = false;
    localMax   = sample;
    return true;
  }

  if (!risingEdge && sample < localMax) {
    localMax = sample;
  }

  return false;
}

// Resets peak detector state — call when entering a new WAIT_PEAK window
void resetPeakDetector() {
  risingEdge = false;
  localMax   = 0;
}

// Clears the frequency buffer — call when entering LISTEN so stale timestamps
// from a previous cycle don't make any frequency appear valid immediately.
void resetFrequencyEstimator() {
  peakWrite  = 0;
  peakFilled = 0;
}

// Raw amplitude in ADC counts (used internally by detectPeak threshold)
int getAmplitudeRaw() {
  if (ampCount == 0) return 0;
  int lo = 4095, hi = 0;
  for (int i = 0; i < ampCount; i++) {
    int v = ampBuffer[i];
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  return hi - lo;
}

// ── Pink noise (Voss-McCartney) ────────────────────────────────────────────

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
      // Map esp_random() to signed [-32768, 32767]
      pinkRows[i] = (int32_t)(esp_random() >> 16) - 32768;
      pinkRunningSum += pinkRows[i];
    }
  }

  // White noise contribution
  int32_t white = (int32_t)(esp_random() >> 16) - 32768;
  int32_t raw   = pinkRunningSum + white;

  // Divisor ~1000 targets ~92% PWM range utilization (intentionally louder than exact)
  int32_t scaled = (raw / 1000) + 128;
  if (scaled < 0)   scaled = 0;
  if (scaled > 255) scaled = 255;
  return (uint8_t)scaled;
}

// ── Audio control ──────────────────────────────────────────────────────────

static volatile bool    audioActive      = false;
static volatile uint8_t lastNoiseSample  = 128;  // 128 = midpoint (silence)

void startNoise() {
  audioActive = true;
}

void stopNoise() {
  audioActive = false;
  ledcWrite(AUDIO_PIN, 0);
}

// Called from loop() at AUDIO_SAMPLE_RATE_HZ via micros() polling
void updateAudio() {
  static uint32_t lastAudioUs = 0;
  static const uint32_t audioIntervalUs = 1000000UL / AUDIO_SAMPLE_RATE_HZ;

  uint32_t now = micros();
  if ((now - lastAudioUs) >= audioIntervalUs) {
    lastAudioUs = now;
    if (audioActive) {
      lastNoiseSample = pinkNoiseSample();
      ledcWrite(AUDIO_PIN, lastNoiseSample);
    } else {
      lastNoiseSample = 128;
    }
  }
}

// ── ADC sampling ───────────────────────────────────────────────────────────

int sampleADC() {
  return analogRead(ADC_PIN);
}

// ── Serial Plotter output ──────────────────────────────────────────────────

void printToPlotter(int adcRaw, float freqHz, float ampMv) {
  float voltageMv = adcRaw * 3300.0f / 4095.0f;
  Serial.print("Voltage_mV:");
  Serial.print(voltageMv, 1);
  Serial.print('\t');
  Serial.print("Freq_Hz:");
  Serial.print(freqHz, 2);
  Serial.print('\t');
  Serial.print("Amp_mV:");
  Serial.print(ampMv, 1);
  Serial.print('\t');
  Serial.print("PinkNoise:");
  Serial.println(lastNoiseSample);
}

// ── State machine ──────────────────────────────────────────────────────────

void enterState(State s) {
  if (s == LISTEN) {
    resetFrequencyEstimator();
    resetPeakDetector();
  }
  currentState = s;
  stateEntryMs = millis();
}

void updateStateMachine(bool peakDetected, float freq) {
  uint32_t now = millis();
  uint32_t elapsed = now - stateEntryMs;

  switch (currentState) {

    case LISTEN:
      if (frequencyInRange(freq)) {
        resetPeakDetector();  // clear stale state before hunting for peak 1
        enterState(WAIT_PEAK1);
      }
      break;

    case WAIT_PEAK1:
      if (!frequencyInRange(freq)) {
        enterState(LISTEN);
        break;
      }
      if (peakDetected) {
        startNoise();
        enterState(NOISE1);
      }
      break;

    case NOISE1:
      if (elapsed >= NOISE_DURATION_MS) {
        stopNoise();
        resetPeakDetector();
        enterState(WAIT_PEAK2);
      }
      break;

    case WAIT_PEAK2:
      if (!frequencyInRange(freq)) {
        enterState(LISTEN);
        break;
      }
      if (peakDetected) {
        startNoise();
        enterState(NOISE2);
      }
      break;

    case NOISE2:
      if (elapsed >= NOISE_DURATION_MS) {
        stopNoise();
        enterState(BREAK);
      }
      break;

    case BREAK:
      if (elapsed >= BREAK_DURATION_MS) {
        enterState(LISTEN);
      }
      break;
  }
}

// ── Arduino setup / loop ───────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);

  analogReadResolution(12);
  // ADC_11db = full 0–3.3 V range. On ESP-IDF 5.x cores this may be
  // ADC_ATTEN_DB_12 — change if the compiler reports ADC_11db undefined.
  analogSetAttenuation(ADC_11db);

  // Core 3.x API: ledcAttach(pin, freq, resolution) replaces ledcSetup+ledcAttachPin
  ledcAttach(AUDIO_PIN, AUDIO_FREQ_HZ, AUDIO_RESOLUTION);
  ledcWrite(AUDIO_PIN, 0);

  // Warm up the pink noise RNG state
  for (int i = 0; i < 100; i++) pinkNoiseSample();

  // NOTE: Do NOT print any text here — Serial Plotter will misparse it as data labels.
}

void loop() {
  // 1. Audio — highest priority, polled every ~250 µs
  updateAudio();

  // 2. ADC sample — polled every 2 ms
  static uint32_t lastSampleMs = 0;
  static const uint32_t sampleIntervalMs = 1000UL / ADC_SAMPLE_RATE_HZ;

  uint32_t now = millis();
  if ((now - lastSampleMs) < sampleIntervalMs) return;
  lastSampleMs = now;

  int raw = sampleADC();
  updateAmplitudeBuffer(raw);

  bool peak = detectPeak(raw);
  // Only update frequency estimate from peaks seen during LISTEN.
  // Peaks in WAIT_PEAK states trigger noise bursts but must not corrupt
  // the frequency buffer (a spurious peak from resetPeakDetector() would
  // produce a ~500 Hz reading and immediately kick us back to LISTEN).
  if (peak && currentState == LISTEN) {
    updateFrequency(now);
  }

  float freq  = getFrequency();
  float ampMv = getAmplitude();

  printToPlotter(raw, freq, ampMv);
  updateStateMachine(peak, freq);
}
