#include <Arduino.h>
#include <SPI.h>

// ── Pin mapping for your board ─────────────────────────────────────────────

#define ADC_PIN                34     // MUST be an ADC-capable pin (not J5 RX as currently wired)
#define DAC_CS_PIN              5     // IO5  -> MCP4822 CS
#define DAC_SCK_PIN            18     // IO18 -> MCP4822 SCK
#define DAC_MOSI_PIN           23     // IO23 -> MCP4822 SDI

// ── Configurable constants ─────────────────────────────────────────────────

#define ADC_SAMPLE_RATE_HZ    500
#define TARGET_FREQ_HZ        1.0f
#define FREQ_TOLERANCE_HZ     0.4f
#define NOISE_DURATION_MS     250
#define BREAK_DURATION_MS    2500
#define PEAK_HYSTERESIS        80
#define AMP_WINDOW_MS        3000
#define PEAK_BUFFER_SIZE        4
#define AMP_BUFFER_SIZE      1500

#define AUDIO_SAMPLE_RATE_HZ  4000
#define DAC_MID              2048    // 12-bit midpoint
#define NOISE_AMPL            600    // adjust for volume

SPIClass *dacSPI = &SPI;

// ── Forward declarations ───────────────────────────────────────────────────

int getAmplitudeRaw();
void resetPeakDetector();
void resetFrequencyEstimator();
void writeDAC_A(uint16_t value12);
void updateAudio();
uint8_t pinkNoiseSample();

// ── State machine ──────────────────────────────────────────────────────────

enum State {
  LISTEN,
  WAIT_PEAK1,
  NOISE1,
  WAIT_PEAK2,
  NOISE2,
  BREAK
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
  return (hi - lo) * 3300.0f / 4095.0f;
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
static int localMax = 0;

bool detectPeak(int sample) {
  float mean = getRollingMean();
  float amp  = getAmplitudeRaw();
  int threshold = (int)(mean + amp * 0.25f);

  if (sample > localMax) {
    localMax = sample;
    risingEdge = true;
    return false;
  }

  if (risingEdge && (localMax - sample) >= PEAK_HYSTERESIS && localMax > threshold) {
    risingEdge = false;
    localMax = sample;
    return true;
  }

  if (!risingEdge && sample < localMax) {
    localMax = sample;
  }

  return false;
}

void resetPeakDetector() {
  risingEdge = false;
  localMax = 0;
}

void resetFrequencyEstimator() {
  peakWrite = 0;
  peakFilled = 0;
}

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

// ── Pink noise ─────────────────────────────────────────────────────────────

static int32_t pinkRows[16] = {0};
static int32_t pinkRunningSum = 0;
static uint32_t pinkIndex = 0;
static volatile bool audioActive = false;
static volatile uint16_t lastNoiseSample = DAC_MID;

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

  int32_t scaled = (raw / 1000);
  scaled = constrain(scaled, -1023, 1023);

  return (uint8_t)(scaled & 0xFF);
}

void writeDAC_A(uint16_t value12) {
  value12 &= 0x0FFF;
  uint16_t word = 0x3000 | value12;   // channel A, 1x gain, active

  digitalWrite(DAC_CS_PIN, LOW);
  dacSPI->transfer16(word);
  digitalWrite(DAC_CS_PIN, HIGH);
}

void startNoise() {
  audioActive = true;
}

void stopNoise() {
  audioActive = false;
  writeDAC_A(DAC_MID);
  lastNoiseSample = DAC_MID;
}

void updateAudio() {
  static uint32_t lastAudioUs = 0;
  static const uint32_t audioIntervalUs = 1000000UL / AUDIO_SAMPLE_RATE_HZ;

  uint32_t now = micros();
  if ((now - lastAudioUs) >= audioIntervalUs) {
    lastAudioUs = now;

    if (audioActive) {
      uint32_t r = esp_random();
      int32_t noise = (int32_t)(r & 0xFFFF) - 32768;
      int32_t sample = DAC_MID + (noise * NOISE_AMPL) / 32768;

      sample = constrain(sample, 0, 4095);
      writeDAC_A((uint16_t)sample);
      lastNoiseSample = (uint16_t)sample;
    } else {
      writeDAC_A(DAC_MID);
      lastNoiseSample = DAC_MID;
    }
  }
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
  Serial.print("DAC:");
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
        resetPeakDetector();
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
  delay(300);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(DAC_CS_PIN, OUTPUT);
  digitalWrite(DAC_CS_PIN, HIGH);

  dacSPI->begin(DAC_SCK_PIN, -1, DAC_MOSI_PIN, DAC_CS_PIN);
  dacSPI->beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

  writeDAC_A(DAC_MID);
}

void loop() {
  updateAudio();

  static uint32_t lastSampleMs = 0;
  static const uint32_t sampleIntervalMs = 1000UL / ADC_SAMPLE_RATE_HZ;

  uint32_t now = millis();
  if ((now - lastSampleMs) < sampleIntervalMs) return;
  lastSampleMs = now;

  int raw = analogRead(ADC_PIN);
  updateAmplitudeBuffer(raw);

  bool peak = detectPeak(raw);
  if (peak && currentState == LISTEN) {
    updateFrequency(now);
  }

  float freq = getFrequency();
  float ampMv = getAmplitude();

  printToPlotter(raw, freq, ampMv);
  updateStateMachine(peak, freq);
}