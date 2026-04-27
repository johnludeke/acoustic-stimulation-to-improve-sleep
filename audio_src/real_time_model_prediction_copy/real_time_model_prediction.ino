#include <math.h>

static const int RX_PIN = 17;
static const int TX_PIN = -1;
static const uint32_t UART_BAUD = 115200;
static const float ADS_GAIN = 24.0f;

static const uint8_t PACKET_SIZE = 33;

// Raw Cyton stream
static const int SFREQ = 250;

// MLP branch imitates training data sampled at 100 Hz
static const int MODEL_SFREQ = 100;

static const int EPOCH_SECONDS = 30;
static const int MODEL_SAMPLES_PER_EPOCH = MODEL_SFREQ * EPOCH_SECONDS;  // 3000
static const int MODEL_UPDATE_STRIDE = MODEL_SFREQ;                      // every 1 sec
static const float MODEL_SIGNAL_GAIN = 0.01f;

static const int CHAN_IDX = 0;

uint8_t packet[PACKET_SIZE];
uint8_t packetIndex = 0;
bool syncing = false;

float scale_uV_per_count = 0.0f;

// MLP branch ring buffer: 100 Hz, 30 sec
float ringBuffer[MODEL_SAMPLES_PER_EPOCH];
int writeIndex = 0;
int totalModelSamplesSeen = 0;
int modelSamplesSinceUpdate = 0;

// Downsample scheduler: 250 Hz -> 100 Hz
float downsamplePhase = 0.0f;

bool shouldKeepModelSample() {
  downsamplePhase += MODEL_SFREQ;

  if (downsamplePhase >= SFREQ) {
    downsamplePhase -= SFREQ;
    return true;
  }

  return false;
}

// -----------------------------
// Real-time MLP preprocessing filter
// Paper preprocessing: HP 0.3 Hz + LP 35 Hz
// Lightweight causal first-order implementation
// Runs at raw 250 Hz before downsampling
// -----------------------------
static const float HP_CUTOFF_HZ = 0.3f;
static const float LP_CUTOFF_HZ = 35.0f;
static const float DT = 1.0f / SFREQ;

float hp_y_prev = 0.0f;
float hp_x_prev = 0.0f;
float lp_y_prev = 0.0f;

float hp_alpha = 0.0f;
float lp_alpha = 0.0f;

void initPreprocessFilter() {
  float hp_rc = 1.0f / (2.0f * PI * HP_CUTOFF_HZ);
  hp_alpha = hp_rc / (hp_rc + DT);

  float lp_rc = 1.0f / (2.0f * PI * LP_CUTOFF_HZ);
  lp_alpha = DT / (lp_rc + DT);
}

float preprocessForMLP(float x) {
  float hp_y = hp_alpha * (hp_y_prev + x - hp_x_prev);

  hp_x_prev = x;
  hp_y_prev = hp_y;

  float lp_y = lp_y_prev + lp_alpha * (hp_y - lp_y_prev);
  lp_y_prev = lp_y;

  return lp_y;
}

// -----------------------------
// MLP model constants
// -----------------------------
static const int N_IN = 3;
static const int N_HIDDEN = 16;

float scalerMean[N_IN] = {
  0.07064507810063761f,
  0.08072200939008493f,
  5.5694790684588643e-05f
};

float scalerScale[N_IN] = {
  0.029943648719958926f,
  0.030413848077910952f,
  4.308577877021656e-05f
};

float W1[N_IN][N_HIDDEN] = {
  {-1.091888451569748f, 0.4123534857351705f, 0.18718707641961618f, -0.01781582426646287f, -0.7406020656161469f, -0.11904475208578898f, -0.9406098566286352f, 0.975978707614867f, 0.13694283538876634f, 0.13785881985356593f, -0.5046950539947084f, 0.41802251150654823f, 0.08443015703804664f, -1.4656754299771844f, -0.2937668453772877f, -1.3382891588917656f},
  {0.3388960681504829f, 0.5858053227427477f, -0.46903442437241727f, -0.10837531343008097f, -0.26084412642202925f, -0.8614448947557747f, 0.823935034439481f, -0.7041306708928553f, -0.4670856194489173f, 0.940785686532555f, -0.25598639011239077f, 0.5619875418570031f, 0.6141209667339418f, 0.22698968257239807f, -0.16091233498565283f, 0.5247708426216438f},
  {-0.8721246477247135f, 0.24955895686424706f, 0.4552067929505497f, 0.20084731956250948f, -0.2865694506471235f, 1.1465594241677868f, 0.10103868066169068f, -0.5450750103667227f, -0.3901465265115041f, -0.14230363985719507f, -0.04397279208789674f, 0.22389959445177518f, -0.6542013429248628f, 0.9629185938298982f, -0.3770583076362349f, -0.8239932988995569f}
};

float b1[N_HIDDEN] = {
  0.1843987261569534f, -0.3882017868950004f, 0.7823484737726821f, 0.1865002864680781f,
  0.7989341807856405f, 0.4637759773878096f, -0.3557938704920554f, 0.8193649515214547f,
  0.5531406599021441f, -0.3380557573991156f, -0.6529430446380955f, -0.31344130588756763f,
  -0.3948793416584471f, -0.6883185203807853f, 0.9544846484194657f, 0.02263266573338654f
};

float W2[N_HIDDEN] = {
  -0.8104262310451913f,
  0.12000253337004058f,
  -0.9237007172300167f,
  -0.015509424708776844f,
  -0.8082594998046373f,
  1.8130139862926167f,
  -2.05980355245413f,
  -1.3883793519856142f,
  -1.4184032757504006f,
  0.8791664549596984f,
  0.3611349562509353f,
  0.32479645427835135f,
  0.8870102953139535f,
  -2.155739439904034f,
  -0.5735532721948244f,
  -1.709511437679677f
};

float b2 = 0.2241235594210636f;

float relu(float x) {
  return x > 0.0f ? x : 0.0f;
}

float sigmoid(float x) {
  if (x >= 0.0f) {
    float z = expf(-x);
    return 1.0f / (1.0f + z);
  } else {
    float z = expf(x);
    return z / (1.0f + z);
  }
}

float predictSWSProbability(float x1, float x2, float x3) {
  float x[N_IN] = {x1, x2, x3};
  float xs[N_IN];

  for (int i = 0; i < N_IN; i++) {
    xs[i] = (x[i] - scalerMean[i]) / scalerScale[i];
  }

  float h[N_HIDDEN];

  for (int j = 0; j < N_HIDDEN; j++) {
    float sum = b1[j];

    for (int i = 0; i < N_IN; i++) {
      sum += xs[i] * W1[i][j];
    }

    h[j] = relu(sum);
  }

  float logit = b2;

  for (int j = 0; j < N_HIDDEN; j++) {
    logit += h[j] * W2[j];
  }

  return sigmoid(logit);
}

// -----------------------------
// Cyton packet parsing
// -----------------------------
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

void getContiguousEpoch(float *epochOut) {
  for (int i = 0; i < MODEL_SAMPLES_PER_EPOCH; i++) {
    int idx = (writeIndex + i) % MODEL_SAMPLES_PER_EPOCH;
    epochOut[i] = ringBuffer[idx];
  }
}

void computeXFeatures(const float *epoch, int sfreq, float &x1, float &x2, float &x3, bool &valid) {
  const int samplesPerSec = sfreq;

  double segLenSum = 0.0;
  double segLenSqSum = 0.0;
  double x3Total = 0.0;
  int segCount = 0;

  for (int sec = 0; sec < EPOCH_SECONDS; sec++) {
    int start = sec * samplesPerSec;
    int end = start + samplesPerSec;

    double mean = 0.0;
    for (int i = start; i < end; i++) {
      mean += epoch[i];
    }
    mean /= samplesPerSec;

    float seg[MODEL_SFREQ];

    for (int i = 0; i < samplesPerSec; i++) {
      seg[i] = epoch[start + i] - (float)mean;
    }

    int processedSigns[MODEL_SFREQ];

    for (int i = 0; i < samplesPerSec; i++) {
      int s = 0;

      if (seg[i] > 0) s = 1;
      else if (seg[i] < 0) s = -1;
      else s = 0;

      if (s == 0) {
        s = (i == 0) ? 1 : processedSigns[i - 1];
      }

      processedSigns[i] = s;
    }

    int zc[MODEL_SFREQ];
    int zcCount = 0;

    for (int i = 0; i < samplesPerSec - 1; i++) {
      if (processedSigns[i + 1] != processedSigns[i]) {
        zc[zcCount++] = i;
      }
    }

    if (zcCount < 2) {
      continue;
    }

    for (int i = 0; i < zcCount - 1; i++) {
      int e_i = zc[i];
      int e_ip1 = zc[i + 1];

      float segLenSec = float(e_ip1 - e_i) / float(sfreq);

      segLenSum += segLenSec;
      segLenSqSum += double(segLenSec) * double(segLenSec);
      segCount++;

      double area = 0.0;

      for (int k = e_i; k < e_ip1; k++) {
        area += fabs(seg[k]);
      }

      area /= sfreq;
      x3Total += double(segLenSec) * area;
    }
  }

  if (segCount == 0) {
    x1 = NAN;
    x2 = NAN;
    x3 = NAN;
    valid = false;
    return;
  }

  x1 = float(segLenSum / segCount);

  double meanSq = segLenSqSum / segCount;
  double meanVal = segLenSum / segCount;
  double variance = meanSq - meanVal * meanVal;

  if (variance < 0.0) variance = 0.0;

  x2 = float(sqrt(variance));
  x3 = float(x3Total);
  valid = true;
}

void processPacket(const uint8_t *pkt) {
  int base = 2 + CHAN_IDX * 3;
  int32_t counts = int24ToInt32(pkt[base], pkt[base + 1], pkt[base + 2]);

  float uV = counts * scale_uV_per_count;
  float rawVolts = uV / 1000000.0f;

  // MLP branch preprocessing at raw 250 Hz
  // Then amplitude-calibrate to better match Sleep-EDF training feature scale
  float filteredForMLP = preprocessForMLP(rawVolts) * MODEL_SIGNAL_GAIN;

  // Downsample MLP branch from 250 Hz -> 100 Hz
  if (!shouldKeepModelSample()) {
    return;
  }

  ringBuffer[writeIndex] = filteredForMLP;
  writeIndex = (writeIndex + 1) % MODEL_SAMPLES_PER_EPOCH;

  totalModelSamplesSeen++;
  modelSamplesSinceUpdate++;

  if (totalModelSamplesSeen < MODEL_SAMPLES_PER_EPOCH) {
    if (totalModelSamplesSeen % MODEL_SFREQ == 0) {
      Serial.print("Filling model buffer: ");
      Serial.print(totalModelSamplesSeen / MODEL_SFREQ);
      Serial.println(" / 30 sec");
    }
    return;
  }

  if (modelSamplesSinceUpdate >= MODEL_UPDATE_STRIDE) {
    static float epoch[MODEL_SAMPLES_PER_EPOCH];
    getContiguousEpoch(epoch);

    float x1, x2, x3;
    bool valid = false;

    computeXFeatures(epoch, MODEL_SFREQ, x1, x2, x3, valid);

    Serial.print("model_samples=");
    Serial.print(totalModelSamplesSeen);
    Serial.print(" | raw_equiv_samples~");
    Serial.print((long)((float)totalModelSamplesSeen * ((float)SFREQ / MODEL_SFREQ)));
    Serial.print(" | Ch ");
    Serial.print(CHAN_IDX + 1);
    Serial.print(" | x=[");

    if (valid) {
      float x1Scaled = (x1 - scalerMean[0]) / scalerScale[0];
      float x2Scaled = (x2 - scalerMean[1]) / scalerScale[1];
      float x3Scaled = (x3 - scalerMean[2]) / scalerScale[2];

      float pSWS = predictSWSProbability(x1, x2, x3);
      bool isSWS = pSWS >= 0.5f;

      Serial.print(x1, 6);
      Serial.print(", ");
      Serial.print(x2, 6);
      Serial.print(", ");
      Serial.print(x3, 8);
      Serial.print("] | x_scaled=[");
      Serial.print(x1Scaled, 2);
      Serial.print(", ");
      Serial.print(x2Scaled, 2);
      Serial.print(", ");
      Serial.print(x3Scaled, 2);
      Serial.print("] | pSWS=");
      Serial.print(pSWS, 8);
      Serial.print(" | pred=");
      Serial.print(isSWS ? "SWS" : "NSWS");
    } else {
      Serial.print("nan, nan, nan] | x_scaled=[nan, nan, nan] | pSWS=nan | pred=INVALID");
    }

    Serial.println();
    modelSamplesSinceUpdate = 0;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  scale_uV_per_count = (4.5f / ADS_GAIN / 8388607.0f) * 1000000.0f;

  initPreprocessFilter();

  Serial1.begin(UART_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);

  Serial.println("ESP32 Cyton parser + 100Hz MLP SWS inference starting...");
  Serial.print("Raw sampling rate: ");
  Serial.print(SFREQ);
  Serial.println(" Hz");
  Serial.print("Model branch sampling rate: ");
  Serial.print(MODEL_SFREQ);
  Serial.println(" Hz");
  Serial.print("Scale (uV/count): ");
  Serial.println(scale_uV_per_count, 8);
  Serial.print("Model window length: ");
  Serial.print(EPOCH_SECONDS);
  Serial.println(" sec");
  Serial.print("MLP preprocessing filter: HP=");
  Serial.print(HP_CUTOFF_HZ);
  Serial.print(" Hz, LP=");
  Serial.print(LP_CUTOFF_HZ);
  Serial.println(" Hz");
  Serial.println("Waiting for initial 30-second model buffer fill...");
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