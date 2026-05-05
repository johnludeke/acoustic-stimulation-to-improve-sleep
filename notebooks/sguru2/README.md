


**4/24 - Improving real-time visualization and smoother plot updates**

**Objective:** Improve the live visualization system so the filtered EEG trace updates smoothly in real time while clearly showing trough detections and predicted peaks on time with actual stimulation.

After getting the embedded version working, the next issue was the visualization. The plot technically worked, but it did not look continuously real-time. The EEG trace would only clearly update around predicted peaks or stimulation events, so it looked like the graph was jumping peak-to-peak instead of streaming continuously.
That was a problem because the whole point was to prove to the TA that trough detection and peak prediction are happening live, not just from a static plot. So I worked on making the serial output and Python plotting more consistent.
Serial output rate and autocorrelation decimation
```cpp
// Faster autocorr buffer config
// Full signal is 250 Hz, but autocorr only needs slow-wave timing.
// 250 / 5 = 50 Hz, still enough for 0.5–4 Hz.
static const int AC_DECIM = 5;
static const float AC_FS = FS / AC_DECIM;
static const int AC_SAMPLES = EPOCH_SEC * 50;  // 1500

// Serial output
static const int DATA_PRINT_DECIMATION = 5;  // 50 Hz DATA output
```
This was one of the biggest fixes. Since the Cyton is sampled at 250 Hz, printing every 5 samples gives 50 Hz DATA output. That is dense enough for a smooth plot, but not so much that serial output becomes overwhelming.
The same decimation factor is used for autocorrelation. This keeps the slow-wave frequency estimation cheaper, while still preserving the 0.5 to 4 Hz content.
Using PyQtGraph instead of matplotlib
```python
import sys
import time
import threading
import serial
import numpy as np
from collections import deque

from PyQt5 import QtWidgets, QtCore
import pyqtgraph as pg
```
I switched from matplotlib to PyQtGraph for the serial visualization. Matplotlib was fine for static notebook plots, but it was too laggy for live data. PyQtGraph is much better for this because it can update the plot quickly without freezing the UI.
Threaded serial reading
```python
def serial_thread():
    buffer = ""

    while running:
        try:
            n = ser.in_waiting
            if n > 0:
                chunk = ser.read(n).decode(errors="ignore")
                buffer += chunk

                lines = buffer.split("\n")
                buffer = lines[-1]

                for line in lines[:-1]:
                    line = line.strip()
                    if line:
                        process_line(line)
            else:
                time.sleep(0.001)

        except Exception as e:
            print("Serial thread error:", e)
            time.sleep(0.01)

thread = threading.Thread(target=serial_thread, daemon=True)
thread.start()
```
This separates serial reading from plotting. Before, plotting could block data reading or vice versa. With a background thread, the Python script can keep reading serial data continuously while the Qt timer handles plot updates.
PyQtGraph plot setup
```python
app = QtWidgets.QApplication(sys.argv)

win = pg.GraphicsLayoutWidget(show=True, title="ESP32 Closed-Loop Stimulation")
win.resize(1300, 650)

plot = win.addPlot(title="ESP32 real-time slow-wave stimulation visualization")
plot.setLabel("bottom", "Time relative to newest sample", units="s")
plot.setLabel("left", "Filtered EEG", units="µV")
plot.setXRange(-PLOT_WINDOW_MS / 1000, FUTURE_VIEW_MS / 1000)
plot.setYRange(*YLIM)
plot.showGrid(x=True, y=True)
plot.addLegend()

sig_curve = plot.plot([], [], pen=pg.mkPen(width=2), name="0.5–4 Hz filtered EEG")

trough_scatter = pg.ScatterPlotItem(
    size=13,
    symbol="t",
    brush=pg.mkBrush(80, 160, 255),
    pen=pg.mkPen(None),
    name="Detected trough"
)
plot.addItem(trough_scatter)

pred_scatter = pg.ScatterPlotItem(
    size=13,
    symbol="o",
    brush=pg.mkBrush(255, 170, 0),
    pen=pg.mkPen(None),
    name="Predicted peak target"
)
plot.addItem(pred_scatter)

stim_scatter = pg.ScatterPlotItem(
    size=20,
    symbol="star",
    brush=pg.mkBrush(255, 60, 60),
    pen=pg.mkPen(None),
    name="Actual audio stim"
)
plot.addItem(stim_scatter)
```
This sets up the real-time plot with the filtered EEG trace, detected troughs, predicted peak targets, and actual stimulation events. The x-axis is relative to the newest sample, which makes the plot behave like a scrolling real-time window.
Plot update logic
```python
def make_event_points(ts, ys, current_ms):
    ts = np.array(ts)
    ys = np.array(ys)

    if len(ts) == 0:
        return []

    keep = (
        (ts >= current_ms - PLOT_WINDOW_MS) &
        (ts <= current_ms + FUTURE_VIEW_MS)
    )

    rel = (ts[keep] - current_ms) / 1000.0
    yy = ys[keep]

    return [{"pos": (float(x), float(v))} for x, v in zip(rel, yy)]

def update_plot():
    with lock:
        if len(data_t) < 2:
            return

        t = np.array(data_t)
        y = np.array(data_y)

        tr_t = list(trough_t)
        tr_y = list(trough_y)

        pr_t = list(pred_t)
        pr_y = list(pred_y)

        st_t = list(stim_t)
        st_y = list(stim_y)

        freq = latest_freq
        T = latest_T

    current_ms = t[-1]

    keep = t >= current_ms - PLOT_WINDOW_MS
    rel_t = (t[keep] - current_ms) / 1000.0
    y_view = y[keep]

    sig_curve.setData(rel_t, y_view)

    trough_scatter.setData(make_event_points(tr_t, tr_y, current_ms))
    pred_scatter.setData(make_event_points(pr_t, pr_y, current_ms))
    stim_scatter.setData(make_event_points(st_t, st_y, current_ms))

    freq_text = "estimating..." if freq is None else f"{freq:.2f} Hz"
    T_text = "estimating..." if T is None else f"{T:.0f} ms"

    status.setText(
        f"Freq: {freq_text}\n"
        f"T: {T_text}\n"
        f"Troughs: {len(tr_t)}\n"
        f"Pred peaks: {len(pr_t)}\n"
        f"Audio stims: {len(st_t)}"
    )

timer = QtCore.QTimer()
timer.timeout.connect(update_plot)
timer.start(UPDATE_MS)
```
This fixed the main visualization issue. The plot updates on a timer, not only when a new event appears. The continuous DATA stream drives the EEG trace, and the event buffers just overlay markers on top.
With UPDATE_MS = 25, the plot updates around 40 frames per second, which made the signal look continuous between troughs and stim events.
Overall takeaway
By 4/24, the embedded breadboard version was much closer to a real demo. The ESP32-C6 reads Cyton EEG packets, filters the signal, estimates slow-wave period from a rolling 30-second buffer, detects troughs live, predicts peaks, and triggers pink noise stimulation through the PAM8302.
The Python visualizer now shows the data continuously instead of jumping from event to event. This better demonstrates that the system is actually running in real time and directly addresses the concern from the progress demo.


**4/23 - Embedded closed-loop stimulation on ESP32-C6**

**Objective:** Implement the closed-loop slow-wave stimulation pipeline on the ESP32-C6 using live Cyton EEG data, including filtering, frequency estimation, trough detection, peak prediction, and pink noise triggering.
Moved the Alg. 2 pipeline from the Python prototype onto the ESP32-C6 running on the breadboard. This version uses live Cyton data over UART, applies the same slow-wave processing logic from the notebook, and then triggers pink noise stimulation through the breadboard audio path.
This is not on the PCB yet. Right now it is using the ESP32-C6 dev kit and PWM audio output directly into the PAM8302 audio amplifier. The PCB version should use similar logic, except the PCB has the regular ESP32 module and an MCP4822 DAC between the microcontroller and audio amplifier.
Serial event format
```cpp
// Event format:
//   DATA,t_ms,filtered_uV
//   FREQ,t_ms,freq_hz,T_ms
//   TROUGH,trough_t_ms,trough_uV,pred_peak_t_ms
//   STIM,t_ms
```
I set up the Arduino output so the Python plotter could parse each event cleanly. DATA gives the filtered EEG trace, FREQ gives the current autocorrelation-derived frequency estimate, TROUGH gives the detected trough and predicted peak time, and STIM confirms when the audio burst actually fires.
Core configuration
```cpp
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
```
This defines the main assumptions for the embedded version. The Cyton samples at 250 Hz, so each sample is 4 ms apart and a 30-second epoch is 7500 samples. The processing is focused on 0.5 to 4 Hz because that is the slow-wave range. Frequency is updated once every second, and troughs have to be at least 0.7 seconds apart and below -25 uV to count.
SWS_Detected is hardcoded true for now because this code is testing Alg. 2 by itself. Later, this should be replaced by the output of Alg. 1.
Biquad bandpass filtering
```cpp
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
```
This is the embedded version of the causal 0.5 to 4 Hz filter from the Python notebook. Instead of using filtfilt or any non-causal filtering, each sample goes through a highpass and lowpass biquad in real time. This is important because the final system cannot use future samples.
Rolling buffers for plotting and autocorrelation
```cpp
// Rolling 30s filtered buffer for full-rate data / plotting
float filtBuf[EPOCH_SAMPLES];
int filtHead = 0;
int filtCount = 0;

void pushFilteredSample(float y) {
  filtBuf[filtHead] = y;
  filtHead = (filtHead + 1) % EPOCH_SAMPLES;
  if (filtCount < EPOCH_SAMPLES) filtCount++;
}


// Decimated 30s buffer for fast autocorrelation
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
```
This keeps the past 30 seconds of filtered data. The full-rate buffer is mainly for keeping the continuous filtered signal available. The autocorrelation buffer is separate and decimated, so the ESP32 does not have to run autocorrelation on all 7500 samples every time.
Autocorrelation frequency estimate
```cpp
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
```
This is the embedded version of the autocorrelation logic from the notebook. It subtracts the mean, checks signal energy, searches lags corresponding to 0.5 to 4 Hz, and chooses the lag with the highest correlation. The best lag becomes the estimated period, and frequency is just 1 over period.
This is what lets the device estimate the current slow-wave rhythm from the last 30 seconds, instead of using a fixed frequency.
Trough detection
```cpp
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
```
This detects troughs causally. It only confirms a trough after the signal starts rising again, so the previous sample has to be lower than the sample before it and the current sample. I also added the minimum distance and amplitude threshold so it does not fire on tiny local minima.
Main processing function
```cpp
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
```
This is the main loop for one EEG sample. It filters the sample, stores it, updates the autocorrelation buffer, prints data for visualization, updates frequency once per second, detects troughs, predicts the next peak using half the estimated period, and schedules stimulation.
This block is basically the full closed-loop logic in one place.
Audio scheduling and stimulation
```cpp
void startNoiseBurst(uint32_t eventSampleMs) {
  audioActive = true;
  audioStopWallMs = millis() + NOISE_DURATION_MS;

  Serial.print("STIM,");
  Serial.println(eventSampleMs);
}

void updateScheduledStim() {
  if (!peakScheduled) return;

  uint32_t nowWallMs = millis();

  if ((int32_t)(nowWallMs - scheduledPeakWallMs) >= 0) {
    peakScheduled = false;
    startNoiseBurst(scheduledPeakSampleMs);
  }
}
```
Once a peak target is scheduled, this waits until the mapped wall-clock time and starts the pink noise burst. The STIM line confirms when the stimulation was triggered.
Python serial visualizer
```python
def process_line(line):
    global latest_freq, latest_T

    parts = line.strip().split(",")
    if len(parts) < 2:
        return

    kind = parts[0]

    try:
        with lock:
            if kind == "DATA":
                # DATA,t_ms,filtered_uV
                t_ms = int(parts[1])
                y = float(parts[2])
                data_t.append(t_ms)
                data_y.append(y)

            elif kind == "FREQ":
                # FREQ,t_ms,freq_hz,T_ms
                latest_freq = float(parts[2])
                latest_T = float(parts[3])
                if PRINT_EVENTS:
                    print(line)

            elif kind == "TROUGH":
                # TROUGH,trough_t_ms,trough_uV,pred_peak_t_ms
                t_ms = int(parts[1])
                y = float(parts[2])
                p_ms = int(parts[3])

                trough_t.append(t_ms)
                trough_y.append(y)

                pred_t.append(p_ms)
                pred_y.append(abs(y))

                if PRINT_EVENTS:
                    print(line)

            elif kind == "STIM":
                # STIM,stim_t_ms
                t_ms = int(parts[1])
                stim_t.append(t_ms)
                stim_y.append(0.0)

                if PRINT_EVENTS:
                    print(line)

            elif kind == "BOOT":
                if PRINT_EVENTS:
                    print(line)

    except Exception:
        pass
```
The Python script reads the ESP32 serial output and stores each event type in its own buffer. DATA becomes the continuous waveform, TROUGH becomes detected trough markers, pred_t becomes predicted peak markers, and STIM becomes the actual audio stimulation marker.
Example serial output:
```markdown
FREQ,30000,4.167,240.0
TROUGH,30208,-167.532,30328
STIM,30328
TROUGH,30908,-53.983,31028
STIM,31028
FREQ,31000,4.167,240.0
FREQ,32000,3.333,300.0
TROUGH,32416,-63.129,32566
STIM,32566
```
This confirmed that the ESP32 was producing frequency updates, trough detections, predicted peaks, and real audio stimulation events.


**4/22 - Real-time trough detection + peak prediction (LSL streaming)**

**Objective:** Convert the phase-aligned stimulation algorithm into a real-time LSL streaming prototype to verify live trough detection and peak prediction.

Got feedback from TA during progress demo on the previous work (4/21). Main question was how do we know this trough detection and peak prediction actually works in real time, since what I showed was a static plot over a full epoch. That pushed me to implement a real-time version of Alg. 2 using live EEG data.
Updated the pipeline to run on an LSL stream from the OpenBCI GUI and visualize everything as the data comes in.
Goals for this version:
* Stream EEG data in real time from OpenBCI (via LSL)
* Apply causal bandpass filtering (0.5–4 Hz) online
* Detect troughs in real time using thresholds
* Maintain a rolling 30-second buffer to estimate dominant frequency
* Predict peaks at T/2 after each detected trough
* Plot everything continuously so timing is visible


1) Connecting to LSL stream
First step was connecting to the OpenBCI LSL stream:
```python
streams = resolve_byprop("name", STREAM_NAME, timeout=10)
inlet = StreamInlet(streams[0])
info = inlet.info()
fs = int(info.nominal_srate()) if info.nominal_srate() > 0 else FS_TARGET
```
This pulls real-time EEG samples from the GUI. I used channel index 0 (first EEG channel), and nominal sampling rate is typically 250 Hz.


3) Causal bandpass filtering in real time
Used a Butterworth bandpass, but applied causally using lfilter (not filtfilt):
```python
def make_bandpass(fs, low=0.5, high=4.0, order=4):
    b, a = butter(order, [low, high], btype="bandpass", fs=fs)
    zi = lfilter_zi(b, a) * 0.0
    return b, a, zi
```
Then applied per chunk:
```python
y_chunk, zi = lfilter(b, a, raw_chunk, zi=zi)
```


3) Rolling 30-second buffer for frequency estimation
Maintained a rolling buffer using deque:
```python
epoch_len = int(EPOCH_SEC * fs)
filt_buf = deque(maxlen=epoch_len)
time_buf = deque(maxlen=epoch_len)
```
This always holds the most recent 30 seconds of filtered EEG.
Every 1 second, I recompute dominant frequency using autocorrelation:
```python
if len(filt_buf) >= epoch_len and (now - last_freq_update) >= FREQ_UPDATE_SEC:
    dom_freq, period = estimate_dominant_freq_autocorr(
        np.asarray(filt_buf),
        fs,
        low=BAND_LOW,
        high=BAND_HIGH
    )
```
So instead of one static SWS epoch, the system continuously updates its estimate based on the last 30 seconds.


4) Real-time trough detection
Implemented streaming trough detection using a 3-point check:
```python
def detect_new_trough(y_prev2, y_prev1, y_curr, sample_idx, fs, last_trough_idx):
    trough_idx = sample_idx - 1

    if not (y_prev1 < y_prev2 and y_prev1 < y_curr):
        return None

    min_dist = int(MIN_TROUGH_DISTANCE_SEC * fs)
    if last_trough_idx is not None and (trough_idx - last_trough_idx) < min_dist:
        return None

    if y_prev1 > -MIN_TROUGH_PROM_UV:
        return None

    return trough_idx, y_prev1
```
Tuned thresholds based on real data:
* Initial: 
    * threshold = -20 µV
    * min distance = 0.3 s → too sensitive, detected too many troughs
* Final:
    * threshold = -25 µV
    * min distance = 0.7 s

This gave much cleaner slow-wave trough detection.
Important detail:
* Trough is only confirmed when signal starts increasing again (local minimum), so detection is causal.


5) Peak prediction using T/2 delay
Once a trough is detected and period is known:
```python
pred_time = trough_time + period / 2.0
```
Stored predictions:
```python
pred_peak_times.append(pred_time)
pred_peak_vals.append(abs(trough_val))
```
This mirrors the exact stimulation logic:
* Detect trough in real time
* Use current 30s frequency estimate
* Schedule stimulation at trough + T/2


6) Real-time plotting
Plot updates every ~80 ms:
```python
if (now - last_plot_update) >= PLOT_UPDATE_SEC:
```
What is shown:
* Bandpass filtered EEG (last 30 s)
* Detected troughs (inverted triangle markers)
* Predicted peaks (future markers)
* Future window (~3 s ahead) so predicted peaks are visible
Also added status text:
```python
status_text.set_text(
    f"30s buffer: {fill_pct:.0f}% full\n"
    f"Dominant freq: {freq_text}\n"
    f"Period T: {period_text}\n"
    f"Troughs: {len(trough_times)}"
)
```
<img width="836" height="460" alt="Screenshot 2026-05-04 at 9 36 41 PM" src="https://github.com/user-attachments/assets/fa80c3b1-ce1b-4db0-bd88-720d8bf56304" />
Figure 18. Image of Real-Time plot for Trough Detection and Peak Estimation

Figure 18, which is a static picture but actually updates in real-time, proves the trough detection + peak prediction working as expected. Towards the right end, no troughs are detected since the signal doesn’t dip below -25 µV.

Overall takeaway
This addresses the TA’s concern directly. Instead of showing post-processed results, the system now:
* Streams EEG in real time
* Updates dominant frequency every second using a rolling 30s buffer
* Detects troughs causally as samples arrive
* Predicts peaks immediately after trough detection
So now the full pipeline is actually happening online:
* Continuous EEG stream
* Rolling frequency estimation
* Real-time trough detection
* Immediate peak prediction for stimulation timing
  
**4/21 - Alg. 2 development (frequency estimation + phase-aligned stimulation)**

**Objective:** Develop an embedded-compatible phase-aligned stimulation algorithm using causal filtering, autocorrelation-based frequency estimation, trough detection, and peak prediction.

Took over development of Alg. 2 (phase-aligned stimulation) and updated python_SWS_prediction.ipynb to include signal processing visuals and an embedded-style implementation.
Goal of this stage:
* Given an SWS-detected epoch, estimate dominant slow-wave frequency
* Use that to:
    * Detect troughs in the next epoch
    * Predict peaks (T/2 later), which is where audio stimulation happens


1) Frequency validation via FFT Magnitude Plot
Before implementing the embedded version, I first verified that the SWS epoch actually contains energy in the slow-wave band.
Computed FFT magnitude spectrum of a 30s SWS epoch and saw:
* Large DC component at 0 Hz (expected from baseline offset)
* Clear dominant peak around ~1.2 Hz, which is inside the 0.5-4 Hz SWS range

<img width="747" height="241" alt="Screenshot 2026-05-04 at 9 28 33 PM" src="https://github.com/user-attachments/assets/a86585a3-d24c-41a5-8133-48249c4a8ee1" />
Figure 17. FFT Magnitude Spectrum of SWS-Classified Epoch from Sleep-EDF Dataset


￼
This confirms that:
* The dataset labeling is reasonable
* The signal actually contains slow-wave activity we can track


Why not use FFT for embedded frequency estimation?
FFT works well offline, but not ideal for embedded:
* Complexity:
    * FFT over 30s epoch (~3000 samples) requires ~3000·log₂(3000) ≈ 3.5×10⁴ operations ~ O(N log N)
    * Naive autocorrelation over 0.25-2s lags requires ~3000×175 ≈ 5.25×10⁵ operations but can be reduced via lag constraints and downsampling, giving ~O(N)
* Memory:
    * Requires full buffer and complex-valued arrays
* Latency:
    * Need the full 30s window before computing anything

Because of this, I switched to a time-domain method using autocorrelation:
* Lower overhead in practice with constrained lag search
* Easier to implement on MCU
* Directly gives period, which is what we need for stimulation timing


2) Embedded-style bandpass filtering (0.5-4 Hz)
Instead of using scipy.signal.butter + filtfilt (non-causal), I implemented a causal IIR bandpass using biquads:
```python
def bandpass_0p5_4hz_causal(samples, fs):
    hp = make_highpass(0.5, fs)
    lp = make_lowpass(4.0, fs)
    out = []
    for x in samples:
        y = hp.process(float(x))
        y = lp.process(y)
        out.append(y)
    return out
```
Key points:
* Fully causal, no future samples used
* Matches what we would actually run on MCU
* Avoids filtfilt (scipy) which is not deployable


3) Dominant frequency via autocorrelation (time-domain)
Instead of FFT, estimate frequency by finding the lag that maximizes similarity between the signal and a delayed version of itself.
Search over lags:
* 0.25 to 2.0 seconds, which corresponds to 4 to 0.5 Hz
Core computation:
```python
def autocorr_at_lag(samples, lag_samples):
    numerator = 0.0
    energy_a = 0.0
    energy_b = 0.0
    for i in range(len(samples) - lag_samples):
        a = samples[i]
        b = samples[i + lag_samples]
        numerator += a * b
        energy_a += a * a
        energy_b += b * b
    denom = math.sqrt(energy_a * energy_b)
    if denom <= 1e-12:
        return 0.0
    return numerator / denom
```
Then sweep lags:
```python
dominant_freq, dominant_period, best_corr, lags, corrs = estimate_dominant_frequency_autocorr(
    bp_sws_for_ac,
    fs=FS,
    min_lag_sec=0.25,
    max_lag_sec=2.0
)
```
Result:
```markdown
Estimated dominant frequency: 1.250 Hz
Estimated period: 0.800 s
```
Matches the FFT result (~1.2 Hz), so the method checks out.

4) Trough detection (next epoch)
Once frequency is known, move to the next epoch and detect troughs.
Manual detection (no scipy.find_peaks):
```python
def detect_troughs(samples, fs, min_distance_sec, threshold):
    troughs = []
    last_trough = -10**9
    for i in range(1, len(samples) - 1):
        is_local_min = samples[i] < samples[i - 1] and samples[i] <= samples[i + 1]
        far_enough = (i - last_trough) >= int(min_distance_sec * fs)
        deep_enough = samples[i] < -threshold
        if is_local_min and far_enough and deep_enough:
            troughs.append(i)
            last_trough = i
    return troughs
```
Threshold:
```python
trough_threshold = 0.5 * std_list(bp_next)
```

6) Peak prediction (phase-aligned stimulation)
Key idea: The peak of a sinusoid occurs about T/2 after the trough
```python
half_period_samples = int((dominant_period / 2.0) * FS)

predicted_peak_samples = []
for tr in troughs:
    pred = tr + half_period_samples
    if pred < len(bp_next):
        predicted_peak_samples.append(pred)
```

Results (next epoch)
```markdown
Detected troughs: 35
Predicted stimulation peaks: 35
Half-period delay: 0.400 s
```
Interpretation:
* Slow-wave structure is consistent across the epoch
* Frequency estimate is stable
* Clean 1:1 mapping between troughs and predicted peaks


Overall takeaway
This completes an embedded-compatible version of Alg. 2:
* No scipy (no filtfilt, no find_peaks)
* No FFT dependency for runtime
* Uses:
    * Causal IIR filtering
    * Autocorrelation for frequency
    * Manual trough detection
    * Deterministic peak prediction
Pipeline now:
SWS epoch: bandpass-> autocorrelation-> dominant frequency

Then next following epoch (current data in general): bandpass-> trough detection ->add T/2-> stimulation timing
This directly matches what we want running on the MCU for real-time closed-loop stimulation. One note is for real-time trough detection, since we can’t depend on future data, we’ll have to set some thresholds to determine what can be classified as a trough. 


**4/20 - Full dataset build, model training, comparison, and saving**

**Objective:** Build the full Sleep-EDF Expanded feature dataset, train multiple SWS classifiers, compare performance, and save models for embedded implementation.

Built the full dataset across all matched Sleep-EDF Expanded records. Each record is loaded, converted into 30-second labeled epochs, converted into x = [x1, x2, x3], and stored with its record key.
```python
def build_dataset(root_dir):
    pairs = match_sleep_edf_pairs(root_dir)

    X_all = []
    y_all = []
    groups_all = []

    for psg_path, hyp_path, record_key in pairs:
        try:
            X_rec, y_rec, ch_name, sfreq = read_sleep_edf_record(psg_path, hyp_path)

            X_all.append(X_rec)
            y_all.append(y_rec)
            groups_all.extend([record_key] * len(y_rec))

            print(f"{record_key}: {len(y_rec)} epochs | ch={ch_name} | sfreq={sfreq}")

        except Exception as e:
            print(f"Skipping {record_key}: {e}")

    X_all = np.vstack(X_all)
    y_all = np.concatenate(y_all)
    groups_all = np.array(groups_all)

    return X_all, y_all, groups_all
```
Dataset sanity checks:
```python
X, y, groups = build_dataset(data_dir)

print("Dataset shape:", X.shape)
print("Label shape:", y.shape)
print("Num records represented:", len(np.unique(groups)))
print("Positive class fraction (SWS):", y.mean())
print("Feature mins:", X.min(axis=0))
print("Feature maxs:", X.max(axis=0))
```
Output:
```markdown
Dataset shape: (42615, 3)
Label shape: (42615,)
Num records represented: 44
Positive class fraction: 0.1505
```
This confirmed that SWS is the minority class, so balanced metrics are more useful than raw accuracy.
Next, I split by record instead of by epoch to avoid data leakage:
```python
gss = GroupShuffleSplit(n_splits=1, test_size=0.2, random_state=42)
train_idx, test_idx = next(gss.split(X, y, groups=groups))

X_train, X_test = X[train_idx], X[test_idx]
y_train, y_test = y[train_idx], y[test_idx]
```
Output:
```markdown
Train size: 34116
Test size: 8499
Train positive fraction: 0.1598
Test positive fraction: 0.1133
```
Then I trained logistic regression, MLP, and random forest models:
```python
models = {
    "logreg": Pipeline([
        ("scaler", StandardScaler()),
        ("clf", LogisticRegression(
            class_weight="balanced",
            max_iter=2000,
            random_state=42
        )),
    ]),

    "mlp": Pipeline([
        ("scaler", StandardScaler()),
        ("clf", MLPClassifier(
            hidden_layer_sizes=(16,),
            activation="relu",
            max_iter=1000,
            random_state=42
        )),
    ]),

    "rf": RandomForestClassifier(
        n_estimators=300,
        max_depth=6,
        class_weight="balanced",
        random_state=42
    ),
}
```
Training/evaluation loop:
```python
for name, model in models.items():
    model.fit(X_train, y_train)
    y_pred = model.predict(X_test)

    bal_acc = balanced_accuracy_score(y_test, y_pred)
    cm = confusion_matrix(y_test, y_pred)
    report = classification_report(y_test, y_pred, digits=4)

    print("Balanced accuracy:", bal_acc)
    print("Confusion matrix:\n", cm)
    print(report)
```
Main results:
```markdown
Logistic Regression balanced accuracy: 0.9371
MLP balanced accuracy: 0.9282
```
Logistic regression caught almost all SWS epochs but produced more false positives. The MLP had slightly lower balanced accuracy but better precision for SWS, which may be better for audio stimulation since false positives could trigger stimulation during non-SWS.
Finally, I compared models and saved them:
```python
best_name = max(results, key=lambda k: results[k]["balanced_accuracy"])
best_model = results[best_name]["model"]

joblib.dump(results["logreg"]["model"], "sws_logreg.joblib")
joblib.dump(results["mlp"]["model"], "sws_mlp.joblib")
joblib.dump(results["rf"]["model"], "sws_rf.joblib")
joblib.dump(best_model, "sws_x_model_best.joblib")
```
This gives me multiple model options for embedded implementation. Logistic regression is simpler, while MLP may be better for balancing precision/recall depending on how conservative we want stimulation triggering to be.


**4/19 - Feature extraction + single-record validation**

**Objective:** Implement and validate single-record x feature extraction from 30-second labeled EEG epochs for binary SWS classification.

Implemented the feature extraction portion of python-SWS-prediction.ipynb. First, I set up imports for EDF handling, signal processing, model training, and model saving:
```python
from pathlib import Path
from collections import Counter

import joblib
import mne
import numpy as np

from sklearn.model_selection import GroupShuffleSplit
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import balanced_accuracy_score, classification_report, confusion_matrix
from sklearn.linear_model import LogisticRegression
from sklearn.ensemble import RandomForestClassifier
from sklearn.neural_network import MLPClassifier
```
Then I wrote helper functions for the zero-crossing point features. The 30-second epoch is split into 1-second intervals, each interval is zero-meaned, and zero-crossing segments are extracted.
```python
def zero_mean_interval(sig_1s):
    return sig_1s - np.mean(sig_1s)

def get_zero_crossing_indices(sig):
    signs = np.sign(sig).astype(float)

    for i in range(len(signs)):
        if signs[i] == 0:
            signs[i] = 1 if i == 0 else signs[i - 1]

    return np.where(np.diff(signs) != 0)[0]
```
Main feature extraction:
```python
def compute_x_features(epoch, sfreq):
    samples_per_sec = int(sfreq)
    assert len(epoch) == 30 * samples_per_sec, "Expected a 30-second epoch"

    all_segment_lengths = []
    x3_total = 0.0

    for sec in range(30):
        start = sec * samples_per_sec
        end = (sec + 1) * samples_per_sec
        seg_1s = zero_mean_interval(epoch[start:end])

        zc = get_zero_crossing_indices(seg_1s)

        if len(zc) < 2:
            continue

        for i in range(len(zc) - 1):
            e_i = zc[i]
            e_ip1 = zc[i + 1]

            seg_len_sec = (e_ip1 - e_i) / sfreq
            all_segment_lengths.append(seg_len_sec)

            area = np.sum(np.abs(seg_1s[e_i:e_ip1])) / sfreq
            x3_total += seg_len_sec * area

    if len(all_segment_lengths) == 0:
        return np.array([np.nan, np.nan, np.nan], dtype=float)

    return np.array([
        np.mean(all_segment_lengths),
        np.std(all_segment_lengths),
        x3_total
    ], dtype=float)
```
I mapped the labels into the binary classification problem:
```python
VALID_LABEL_MAP = {
    "Sleep stage W": 0,
    "Sleep stage 1": 0,
    "Sleep stage 2": 0,
    "Sleep stage 3": 1,
    "Sleep stage 4": 1,
    "Sleep stage R": 0,
}
```
Then I wrote utilities to match PSG files with hypnogram files and choose the EEG channel:
```python
def pairing_key(filename: str) -> str:
    return filename[:7]

def match_sleep_edf_pairs(root_dir):
    root_dir = Path(root_dir)
    psg_files = sorted(root_dir.glob("*-PSG.edf"))
    hyp_files = sorted(root_dir.glob("*-Hypnogram.edf"))

    hyp_by_key = {pairing_key(hyp.name): hyp for hyp in hyp_files}

    pairs = []
    for psg in psg_files:
        key = pairing_key(psg.name)
        hyp = hyp_by_key.get(key)
        if hyp is not None:
            pairs.append((psg, hyp, key))

    return pairs

def pick_eeg_channel(raw):
    preferred = ["EEG Fpz-Cz", "EEG Pz-Oz", "Fpz-Cz", "Pz-Oz"]
    for want in preferred:
        if want in raw.ch_names:
            return want
    raise ValueError("No EEG channel found")
```
The main record-reading function attaches annotations, expands them into 30-second events, extracts EEG epochs, computes features, and assigns labels:
```python
events, event_id = mne.events_from_annotations(
    raw,
    event_id=ANNOTATION_DESC_2_EVENT_ID,
    chunk_duration=30.0,
    verbose=False,
)

epochs = mne.Epochs(
    raw.copy().pick([chosen]),
    events=events,
    event_id=event_id,
    tmin=0.0,
    tmax=30.0 - 1.0 / sfreq,
    baseline=None,
    preload=True,
    verbose=False,
)
```
Single-record validation confirmed the pipeline worked:
```markdown
Chosen EEG channel: EEG Fpz-Cz
Sampling rate: 100
X_one shape: (1092, 3)
y_one shape: (1092,)
Class counts: NSWS=956, SWS=136
```
One hiccup was that MNE gave warnings about channels having different highpass/lowpass filters. 
Since I explicitly selected one EEG channel before feature extraction, this did not block the model pipeline.

**4/18 - Epoch / label alignment checks**

**Objective:** Verify that Sleep-EDF Expanded hypnogram annotations correctly align with PSG EEG data before building the supervised training pipeline.

Started by verifying that the Sleep-EDF Expanded hypnogram labels actually aligned with the PSG EEG data before building the model. Since the classifier operates on 30-second epochs, I first checked whether the annotations were stored as individual 30 s labels or longer segments.
```python
print("First 20 annotation descriptions:")
print(list(annot.description[:20]))
print("First 20 annotation durations:")
print(list(annot.duration[:20]))
'''
The output showed labels like Wake, Stage 1, Stage 2, Stage 3, Stage 4, and REM, with durations such as 630 s, 270 s, 90 s, etc. This confirmed that one annotation label can span multiple adjacent 30-second epochs.

I then expanded each annotation duration into 30-second epoch counts:
```python
from collections import Counter
import numpy as np
epoch_counts = Counter()
for desc, dur in zip(annot.description, annot.duration):
    epoch_counts[desc] += int(round(dur / 30.0))
print("Expanded 30-second epoch counts by label:")
for k, v in sorted(epoch_counts.items()):
    print(f"{k}: {v}")
psg_duration_sec = raw.n_times / raw.info["sfreq"]
psg_epochs_30s = int(np.floor(psg_duration_sec / 30.0))
labeled_epochs_30s = sum(epoch_counts.values())
print("PSG full 30s epochs:", psg_epochs_30s)
print("Labeled 30s epochs:", labeled_epochs_30s)
print("Difference:", labeled_epochs_30s - psg_epochs_30s)
```
This showed that the labeled annotations covered fewer epochs than the raw PSG file:
```markdown
PSG full 30s epochs: 1025
Labeled 30s epochs: 944
Difference: -81
```
This was important because it means I should not blindly cut the full EEG recording into 30-second windows. Only annotation-backed epochs should be used for supervised training.

**4/17 - SWS Prediction + Phase-aligned Stimulation Algorithms Clarification**

**Objective:** Clarify the full two-algorithm pipeline for SWS prediction and phase-aligned stimulation, and decide how to connect feature-based SWS classification with trough-based peak prediction.

John messaged this morning saying the audio PCB we ordered out of pocket was stolen from the package room. We will wait a few days and put notices up in his apartment complex - if that’s to no avail, we’ll have no choice but to reorder. 

Worked with Bakry to clarify the full pipeline for feature extraction, SWS prediction, and phase-aligned audio stimulation. We converged on the architecture shown in the figure.

<img width="735" height="581" alt="Screenshot 2026-05-04 at 9 00 44 PM" src="https://github.com/user-attachments/assets/1321fa72-04ad-43ec-b64c-94c8ab581157" />
Figure 16. Diagram of Clarified Pipeline for Feature Extraction, SWS Detection, and Phase-Aligned Audio Stimulation
￼
The key idea builds directly off our progress demo. Previously, the audio subsystem took a known waveform generator (e.g., 0.5–2 Hz) and output phase-aligned pink noise. Here, we replace that synthetic input with a data-driven waveform derived from EEG, specifically from a rolling 30 s epoch updated every second.
Pipeline:
* Continue current feature extraction + SWS classification (Alg. 1) on 30 s windows
* If classified as SWS → trigger Alg. 2
* Alg. 2:
    * Bandpass EEG (0.5–4 Hz)
    * Compute dominant slow-wave frequency via autocorrelation (best lag)
    * Detect troughs in real time (threshold-based)
    * Predict next peak at T/2 after trough
    * Trigger audio at predicted peak
This keeps the system identical to demo behavior, just replacing the input source with real EEG instead of a synthetic oscillator.
Division of work:
* I will continue refining feature extraction + SWS prediction (based on single-channel approach  )
* Bakry will focus on Alg. 2, including whether simply verifying dominant frequency ∈ [0.5, 4] Hz is sufficient for SWS gating vs. full classifier dependency


Dataset issues + switch to Sleep-EDF Expanded:
Before moving to model development, I tested for epoch-label on the original Sleep-EDF dataset and ran into a major issue: sleep stage labels were not being parsed correctly. The original dataset used .rec files for the EEG and .hyp files for the corresponding sleep stage labels, while the expanded dataset used more modern .edf files for both. Also, the original dataset only had 7 patients overnight EEG data, while the expanded dataset had 44 patients’ overnight EEG data and corresponding labels, providing significantly more training data. 

Using:
```python
raw = mne.io.read_raw_edf(psg_path, preload=False, verbose=False)
annot = mne.read_annotations(hyp_path)
```

Original dataset (PSG + .hyp):
* Channels: ['Fpz-Cz', 'Pz-Oz', 'horizontal', 'oro-nasal', 'Submental', 'body', 'Event marker']
* sfreq: 100 Hz
* Annotations: 0 detected
* Unique descriptions: []
→ .hyp files were not properly attaching annotations to EEG → unusable for supervised training
Sleep-EDF Expanded:
* Channels: ['EEG Fpz-Cz', 'EEG Pz-Oz', 'EOG horizontal', 'EMG submental', 'Marker']
* sfreq: 100 Hz
* Annotations: 148 segments detected
* Includes valid labels:
    * Wake (W), Stage 1, Stage 2, Stage 3, Stage 4
The expanded dataset is thus fully compatible with MNE annotation pipeline, usable for epoching + labeling.
Basically, today I switched to Sleep-EDF Expanded because it provides properly formatted EDF + annotation pairs. This is critical since the entire SWS pipeline depends on 30 s labeled epochs for supervised learning


**4/14 - Real-time x feature extraction on ESP32 + design decision on PCB direction**

**Objective:** Implement real-time x feature extraction from Cyton EEG data on ESP32 and evaluate whether to proceed with full PCB bring-up or current hybrid setup.
On the software side, I extended the x feature extraction pipeline to run directly on the ESP32 using raw Cyton data streamed over UART. Unlike the earlier Python version, this required working with packetized data and integrating feature extraction into a continuous embedded loop.
The pipeline starts with parsing Cyton packets and converting raw ADC counts into a usable signal:

int32_t counts = int24ToInt32(pkt[base], pkt[base + 1], pkt[base + 2]);

float uV = counts * scale_uV_per_count;

float rawVolts = uV / 1000000.0f;

This converts the 24-bit ADC output into microvolts and then into a voltage signal. Samples are pushed into a rolling buffer:

ringBuffer[writeIndex] = filteredForMLP;

writeIndex = (writeIndex + 1) % MODEL_SAMPLES_PER_EPOCH;

This buffer represents a continuous 30-second window (updating every second). Once the buffer is filled, I reconstruct a contiguous epoch and compute the x features:

getContiguousEpoch(epoch);

computeXFeatures(epoch, MODEL_SFREQ, x1, x2, x3, valid);

Inside computeXFeatures, the logic mirrors the Python implementation. The 30-second window is split into 1-second segments, each segment is zero-meaned, and zero-crossings are detected:

for (int sec = 0; sec < EPOCH_SECONDS; sec++) {

  ...
  
  seg[i] = epoch[start + i] - (float)mean;
  
  ...
  
  if (processedSigns[i + 1] != processedSigns[i]) {
  
    zc[zcCount++] = i;
	
  }
}
From these zero-crossings, segment lengths and areas are computed and accumulated:

float segLenSec = float(e_ip1 - e_i) / float(sfreq);

segLenSum += segLenSec;

...
area += fabs(seg[k]);

x3Total += double(segLenSec) * area;

Finally, the three features are computed:

x1 = float(segLenSum / segCount);

x2 = float(sqrt(variance));

x3 = float(x3Total);

This produces the same feature vector as before with the python prototype (interfacing with the LSL stream generated from OpenBCI GUI), but now entirely in real time on the ESP32 using live EEG data directly from the intercepted bluetooth transmission. 

Separately, we ran into an unexpected situation with hardware. We had not received confirmation from our TA about our round 3/4 PCBs being delivered, but when we checked the lab storage, we found that they had actually already arrived. This forced us to decide whether to pivot to assembling the full PCB or continue with our current Cyton + audio PCB setup.

We evaluated both options:

Option 1: Assemble full PCB (EEG + audio)

* Pros:
    * Fully integrated system
    * Matches original design intent
    * Good for demonstrating complete pipeline
* Cons:
    * Risky due to time constraints
    * Complex soldering (ADS1299 especially)
    * Debugging analog front end could take too long

Option 2: Use Cyton + audio PCB (current approach)

* Pros:
    * Already validated EEG acquisition
    * Faster to integrate with existing pipeline
    * Lower risk for demo and verification
    * Still meets core requirement of closed-loop stimulation
* Cons:
    * Not fully custom EEG hardware
    * Less impressive from a PCB integration standpoint

Given the timeline and course requirements (working system > fully custom hardware), we decided to move forward with the Cyton board for EEG acquisition and use our custom audio PCB for stimulation. This keeps the system reliable while still demonstrating all key functionality: real-time EEG digitization and phase-aligned audio stimulation.

**4/10 - PCB ordered**

**Objective:** Submit final PCB design for order.

We finalized the design and placed the PCB order out of pocket to avoid delays. We chose to order with no stencil since we were confident in our soldering and it would reduce the cost. At this point, all schematic and layout checks (ERC/DRC) had been completed, and we were confident in the design. Ordering marked the transition from design to implementation, and the focus moving forward would shift toward bring-up and testing.


**4/9 - Audio PCB design review + ordering logistics**

**Objective:** Finalize audio subsystem PCB design and prepare for fabrication.

I reviewed the audio subsystem portion of the PCB in detail, focusing on both the signal path and power design. Compared to earlier iterations, the design was simplified by using a single 3.3V rail instead of multiple voltage domains. This reduces complexity in routing and avoids unnecessary regulation stages, which is fine since both the DAC (MCP4822) and amplifier (PAM8302) can operate cleanly within this supply range.

<img width="491" height="380" alt="Screenshot 2026-05-04 at 8 41 05 PM" src="https://github.com/user-attachments/assets/e29b4162-5a05-40d6-9622-3f837a702d00" />
Figure 15. Schematic for PCB with Audio and Power Subsystems
￼
The signal chain is now clearly structured as:

* MCU (SPI) -> DAC -> AC coupling capacitor -> audio amplifier -> speaker

The addition of the coupling capacitor ensures the amplifier input is properly centered and prevents DC offset from propagating. Decoupling capacitors were also placed close to both the DAC and amplifier to stabilize the supply and reduce noise.

After reviewing the layout, I worked with John to slightly spread out components, mainly to make soldering easier and reduce the chance of error during assembly. 

We also verified that all parts were in stock and would arrive on time. At this point, we finalized PCB ordering options. John compiled pricing for different vendors:

* With stencil:
    * JLCPCB: $76.50
    * PCBWay: $113.57
* Without stencil:
    * JLCPCB: $44.76
    * PCBWay: $80.30

This gave us flexibility depending on whether we wanted easier assembly (stencil) or lower cost.

**4/6 - Audio subsystem iteration + timing behavior**

**Objective:** Improve audio subsystem prototype and begin aligning stimulation timing with incoming signal behavior.

I extended the breadboard setup for the audio subsystem to better visualize and debug timing behavior. I added an LED indicator that toggles with the waveform generator signal so we could directly see the frequency and timing of the incoming waveform. This made it much easier to reason about whether our stimulation logic was aligned with the signal or not.

On the software side, I updated the audio stimulation logic so that the pink noise output is triggered in phase with the waveform. Specifically, the system now plays pink noise at a detected peak, then again at the next peak, followed by a delay of around 0.5 seconds before repeating. This helped us move closer to the intended behavior of phase-locked stimulation, where the audio is aligned with the oscillatory structure of the signal rather than just playing periodically.

This was a good step toward validating the timing pipeline before integrating real EEG signals, since it let us test phase alignment in a controlled setting using a known waveform.

**4/5 - Packet Parsing + Signal Validation**

**Objective:** Parse Cyton data packets on the ESP32, reconstruct EEG signals, and validate signal integrity against the OpenBCI GUI to confirm a correct embedded data pipeline.

With access to the raw bitstream, I moved on to parsing the incoming data according to the OpenBCI Cyton data format. Each packet begins with a header byte (0xA0) and ends with a footer byte (0xC followed by a counter), with a fixed number of bytes in between representing channel data and metadata.

To verify correct packet reception, I first implemented a simple parser that prints a new line whenever a valid header is detected and a corresponding footer appears at the expected offset. This resulted in consistently formatted packet outputs, with no unexpected bytes or misalignment. This step was important because it confirmed that there was no data corruption or packet loss at the UART level.

<img width="460" height="115" alt="Screenshot 2026-05-04 at 8 33 10 PM" src="https://github.com/user-attachments/assets/23d5682c-920a-4ec6-9afd-846d4517d4b6" />
Figure 13. Serial Monitor Confirming Proper Header & Footer Bits per Cyton Data Format, and Values per Channel per Sample (packet)
￼
After confirming packet integrity, I implemented parsing of the EEG channel data. Each channel value is represented as a 24-bit signed integer, so I reconstructed the values from three bytes and converted them into signed integers. These values were then streamed and plotted over time.

To validate correctness, I compared the reconstructed EEG waveform from the ESP32 with the waveform displayed in the OpenBCI GUI. The signals matched in both shape and timing, confirming that the parsing process was accurate and that the data pipeline preserved signal integrity.

￼<img width="619" height="408" alt="Screenshot 2026-05-04 at 8 34 55 PM" src="https://github.com/user-attachments/assets/f5f466be-b804-4800-afe6-87cc888c1959" />
Figure 14. Serial Plotter Alongside OpenBCI GUI During Ongoing Data Stream

Design Direction Pivot

At this point, I began reconsidering the system architecture for the final demo. While the original plan was to use our custom PCB (ADS1299 + PIC32) for EEG acquisition and processing, there are practical concerns around bring-up time, particularly with soldering smaller components and debugging low-level firmware.

Given that the OpenBCI Cyton board already implements a nearly identical signal acquisition pipeline to our design  , it may be more efficient to use it directly for the demo. This would allow us to focus on the core contribution of the project—real-time SWS detection and closed-loop audio stimulation—without being blocked by hardware integration issues.

Under this approach, the Cyton board would handle EEG acquisition and digitization, while the ESP32 would take over feature extraction, SWS detection, and audio output. Since the ESP32 is already integrated into our breadboard audio subsystem, this creates a clean and practical pipeline that still aligns with our original system goals.

The next step will be to port the feature extraction logic from Python to the ESP32 and implement a rolling buffer for real-time processing, enabling end-to-end closed-loop operation.




**4/4 - Hardware Integration (Cyton, ESP32)**

**Objective:** Establish a reliable hardware interface between the OpenBCI Cyton and ESP32 by correctly tapping into the EEG data stream without disrupting system communication.

After validating the algorithm in Python, I shifted focus toward implementing a fully embedded pipeline. The goal was to move away from a PC-based system and instead stream EEG data directly into the ESP32, where feature extraction and SWS detection could eventually run in real time.

From the system design, the EEG data is sampled at 250 Hz with 24-bit resolution. This results in a raw data rate of approximately 6 kbps for a single channel, which is well within the capabilities of UART communication. This confirmed that it should be feasible to stream the digitized EEG data directly into the ESP32 without compression or downsampling.

Initially, I attempted to connect the TX pin of the Cyton dongle to the RX pin of the ESP32 to capture the data stream. However, this caused the OpenBCI GUI to stop receiving data after a few seconds, triggering a timeout. This suggested that the connection was interfering with the expected communication pathway.

After investigating the Cyton architecture, I realized that the dongle’s TX pin is not used for streaming EEG data outward. Instead, the EEG data is transmitted wirelessly from the headset to the dongle, where it is received on the RX pin and then forwarded via USB to the computer. By tapping into the TX line, I was effectively probing the wrong side of the communication chain and potentially loading the signal in a way that disrupted the system.

I then switched to connecting the RX pin of the dongle to the ESP32. In this configuration, the ESP32 intercepts the incoming data stream without interfering with the communication between the headset and the GUI. This resolved the issue: the GUI continued to function normally, and I was able to observe a continuous stream of hex data on the ESP32 serial monitor. Disconnecting the wire caused the stream to stop immediately, confirming that the data being observed was indeed the live EEG bitstream.

<img width="600" height="310" alt="Screenshot 2026-05-04 at 8 23 12 PM" src="https://github.com/user-attachments/assets/1acd5cbf-2cef-44ba-8061-0f47921ef1cf" />
Figure 12. OpenBCI Cyton BLE Dongle Module


**3/31 - Real-Time x Feature Extraction from LSL Stream**

**Objective:** Extend the 3/30 offline SWS feature extraction work into a real-time streaming prototype using OpenBCI data.

Building off the 3/30 entry, I adapted the x feature extraction code so it could run continuously on live EEG data instead of only on saved Sleep-EDF recordings. The goal was to confirm that the zero-crossing feature pipeline can operate on a rolling 30-second window, matching the epoch length used in the original SWS detection method.
I connected to the OpenBCI GUI stream through Lab Streaming Layer (LSL) using the stream name obci_eeg1. The script opens an LSL inlet, reads the stream sampling rate, and defaults to 250 Hz if needed. I used channel 0 as the EEG channel while testing, since we’ll only care about one channel and I can wire the headset so that channel 0 corresponds to C3-M2.
The main real-time structure is a rolling buffer:
buffer = deque(maxlen=30 * sfreq)
samples_since_update = 0
update_stride = sfreq
This keeps the most recent 30 seconds of EEG data and recomputes the feature vector every 1 second. This means the system does not wait for separate non-overlapping epochs, but instead updates continuously using a sliding window, which is closer to how the final system needs to behave.
For each 30-second buffer, the code divides the signal into 1-second intervals, subtracts the mean from each interval, detects zero-crossing points, and computes the three x features:
* x1: mean zero-crossing segment length
* x2: standard deviation of segment lengths
* x3: weighted area under the signal between zero crossings
This real-time version helped verify that the feature extraction algorithm is computationally simple enough to run repeatedly and that the output can update live from the OpenBCI stream. The next step is to connect this feature vector to the trained SWS classifier so that each rolling 30-second window can be classified as SWS or non-SWS in real time.

1. Helper functions for zero-crossings
   
def zero_mean_interval(sig_1s):

    return sig_1s - np.mean(sig_1s)

def get_zero_crossing_indices(sig):

    signs = np.sign(sig).copy()
	
    for i in range(len(signs)):
	
        if signs[i] == 0:
		
            signs[i] = 1 if i == 0 else signs[i - 1]
			
    return np.where(np.diff(signs) != 0)[0]
	
This part prepares the signal for zero-crossing analysis. zero_mean_interval() subtracts the mean from each 1-second segment so the signal is centered around zero. Then get_zero_crossing_indices() finds where the signal changes sign. The small loop handles exact zeros so they do not get counted as extra crossings.

2. Computing the x features from a 30-second epoch
def compute_x_features(epoch, sfreq):
    samples_per_sec = int(sfreq)
    assert len(epoch) == 30 * samples_per_sec

    all_segment_lengths = []
    x3_total = 0.0

    for sec in range(30):
        start = sec * samples_per_sec
        end = (sec + 1) * samples_per_sec
        seg_1s = zero_mean_interval(epoch[start:end])

        zc = get_zero_crossing_indices(seg_1s)
        if len(zc) < 2:
            continue

        for i in range(len(zc) - 1):
            e_i = zc[i]
            e_ip1 = zc[i + 1]

            seg_len_sec = (e_ip1 - e_i) / sfreq
            all_segment_lengths.append(seg_len_sec)

            area = np.sum(np.abs(seg_1s[e_i:e_ip1])) / sfreq
            x3_total += seg_len_sec * area
This block takes a full 30-second window and splits it into 1-second chunks. For each chunk, it zero-means the signal, finds zero-crossings, and then looks at each pair of consecutive crossings. The distance between crossings gives a segment length, and the area under that segment is computed using the absolute value of the signal.
Across the whole 30 seconds, the code builds up a list of segment lengths for x1 and x2, and accumulates x3 using segment length times area. This is where the time-domain behavior of the signal gets converted into features.

**3/30 - SWS Detection + Real-Time Prototype**

**Objective:** Understand and implement the single-channel zero-crossing-point-based (x) feature vector

I focused on understanding and implementing a single-channel EEG-based slow-wave sleep (SWS) detection algorithm from literature(cite). The goal was to translate an existing research method into something lightweight enough to eventually run on embedded hardware.

The core idea of the paper is that SWS can be identified using time-domain characteristics of the EEG signal, rather than relying on frequency-domain features or multiple channels. In particular, slow-wave sleep is associated with high-amplitude, low-frequency oscillations, which can be captured by analyzing zero-crossing behavior of the signal stemming from the central/frontal brain region.

To prototype this, I used the Sleep-EDF dataset(cite), which provides sleep stage labeled EEG recordings for each adjacent 30-second epoch. I implemented a preprocessing pipeline that mirrors the paper: bandpass filtering (0.3–35 Hz), segmentation into epochs, subtracting the mean of each epoch to remove DC offset and computing zero-crossing points (ZCPs), defined as locations where the signal changes sign.

From these zero-crossing points, the signal is divided into segments. These segments encode important temporal information about the waveform. Faster signals produce many short segments, while slower oscillations produce fewer, longer segments. This becomes particularly useful for SWS detection, since slow waves naturally lead to longer segment durations (slower oscillations).

From this representation, I computed the three primary features:
	•	x1: mean length of zero-crossing segments
	•	x2: standard deviation of segment lengths
	•	x3: weighted area under the signal between zero crossings
￼<img width="190" height="302" alt="Screenshot 2026-05-04 at 8 19 07 PM" src="https://github.com/user-attachments/assets/e5471348-14c1-48e1-af71-83cbcad8c15f" />
Figure 10. Expressions for Each Term in ZCP Feature Vector

<img width="721" height="642" alt="Screenshot 2026-05-04 at 8 20 13 PM" src="https://github.com/user-attachments/assets/75d810ff-db0a-4f71-85cd-f3d4240795f5" />
Figure 11. Code Excerpt Visualizing Epoch and ZCP Feature Vector Calculated for that and Following Four Epochs
￼
The third feature is especially important because it jointly captures amplitude and temporal behavior. Larger amplitudes increase the area under the curve, while slower oscillations increase the spacing between crossings, so x3 effectively encodes the defining characteristics of slow-wave activity.

After validating feature extraction offline, I moved to a real-time prototype using the OpenBCI Cyton board. I configured the OpenBCI GUI to stream EEG data through Lab Streaming Layer (LSL), and wrote a Python script to subscribe to this stream. The script maintains a rolling 30-second buffer (based on the 250 Hz sampling rate), continuously recomputing the feature vector for each epoch window. This allowed me to replicate the offline pipeline in real time, confirming that the approach is viable for embedded implementation later.


￼
￼
**3/24 – Fixing DGND pin connections**

**Objective:** Ensure a continuous digital ground plane and eliminate DRC errors caused by DGND islands.

Same issue as the AGND problem from 3/23, but now on the DGND side (right side of the board). DRC flagged multiple disconnected copper “islands,” which break return current paths and can introduce instability in digital signaling. This is especially important since DGND carries switching noise from the microcontroller, SPI lines, and audio components.

I fixed this by adding vias to tie top-layer DGND regions to a continuous bottom DGND plane and rerouting traces that were unintentionally isolating copper regions. I also made sure all DGND pins (MCU, DAC, amplifier) had a clean, low-impedance path to the same ground reference. This keeps the digital return paths well-defined and prevents noise from coupling into the analog side, consistent with our AGND/DGND separation strategy in the design.
￼
<img width="390" height="99" alt="Screenshot 2026-05-04 at 8 01 40 PM" src="https://github.com/user-attachments/assets/e829923e-9516-45cb-8f3c-b9466a30d35e" />
Figure 8. Example of Fixing DGND connections from PIC32 pins

Once these fixes were made, which John helped with some that I didn’t know how to address, we submitted this PCB for the fourth round pass.

<img width="857" height="779" alt="Screenshot 2026-05-04 at 8 02 11 PM" src="https://github.com/user-attachments/assets/49d586a0-b6f6-49e1-a7f9-dbb8f62e895f" />
Figure 9. PCB Submitted for Fourth Round Pass

**3/23 - Fixing AGND pin connections**

**Objective:** Ensure a continuous analog ground plane and eliminate DRC errors caused by AGND islands.

AGND is defined as a copper zone on the left side of the board across both layers. When running DRC, I saw many errors related to unconnected “islands,” which happen when copper regions are unintentionally isolated due to routing or component placement. This is especially problematic for the analog front end, since the ADS1299 relies on a clean, stable ground reference for microvolt-level EEG signals.

To fix this, I used the bottom layer as a continuous AGND plane and added vias from all AGND pins (ADS1299, input caps, protection circuitry) to that plane. I also adjusted traces that were cutting through zones and breaking connectivity. After this, the AGND region became electrically continuous, which is critical for reducing noise and ensuring accurate signal acquisition.
￼
￼<img width="293" height="545" alt="Screenshot 2026-05-04 at 8 00 26 PM" src="https://github.com/user-attachments/assets/1564f5de-5e46-47b8-8939-bd87bbb19d49" />
Figure 7. Examples of Fixing AGND connections from ADS1299 pins using Vias


**3/12 - Routing concerns / design check**

**Objective:** Re-evaluate routing decisions before locking in PCB layout.

At this point, I stepped back and questioned whether some of our routing decisions were actually necessary, specifically around SRB2 and general EEG input routing. After revisiting the ADS1299 architecture and our use case, I realized that since we are only using a single differential channel (C3–M2), we can simplify a lot of the routing.

SRB2 is useful for multi-channel referencing, but for our single-channel design it is not strictly required in the same way, so over-routing it just adds complexity and potential noise pickup. This reinforced the idea that we should keep routing minimal and directly aligned with the single-channel SWS detection approach, which also helps reduce latency and PCB complexity.

With our current PCB in order, I confirmed that it passed the audit on PCBway and submitted for the third round pass. However, I soon realized there were many DRC errors that we had not addressed. We couldn’t address this in time for the third round, so we planned to clear these up during/after spring break to submit on the fourth round pass.

<img width="871" height="507" alt="Screenshot 2026-05-04 at 7 54 25 PM" src="https://github.com/user-attachments/assets/59126a6d-350f-4c90-a732-1681a614b46a" />
Figure 6. Full PCB Schematic with Signal Processing, Audio, and Power Subsystems Implemented with no ERC Errors
￼

**3/11 - PCB organization and routing**

**Objective:** Organize component placement and routing around ADS1299 for clean layout and low-noise operation.

I structured the layout based on the ADS1299 pin distribution, which is split across sides (~16 pins per side). I grouped components accordingly and placed decoupling capacitors as close as possible to their respective pins, which is important for stabilizing supply voltages and reducing noise.

I also separated the board into functional regions:

* Analog front end (ADS1299 + input circuitry)
* Digital/control (MCU, SPI)
* Power

Initially considered moving the power entry closer to the AGND side, but decided against it because it would bring switching noise too close to the analog front end. Instead, I kept power and digital components on the DGND side and maintained physical separation from the analog region. 


**3/10 - Breadboard development (audio subsystem pivot)**

**Objective:** Prototype audio output path and verify ability to generate stimulation signals.

Since the ADS1299 is not breadboardable, we pivoted to developing the audio subsystem, which we also needed for the PCB anyway. We set up a breadboard with the ESP32, amplifier, and speaker to test signal generation.
<img width="252" height="564" alt="Screenshot 2026-05-04 at 7 50 08 PM" src="https://github.com/user-attachments/assets/0e0acdaa-210e-4632-8557-7219f768c13d" />
Figure 5. Audio Subsystem on Breadboard

I first generated simple white noise using a basic random signal to confirm the system worked. Initially, the output was very faint, but after increasing amplitude, it became audible. After confirming functionality, I implemented pink noise generation, which is more relevant for sleep stimulation since it emphasizes lower frequencies.

This validated that the audio chain works end-to-end and gave us a working prototype for stimulation output. It also highlighted the importance of amplitude scaling and clean signal generation. We did not have access to a DAC yet, but plan to include one for the PCB.


**3/9 - Component pickup + schematic finalization + audio subsystem**

**Objective:** Prepare schematic for PCB and refine audio subsystem design. Finalize signal processing schematic and begin hardware setup.

We picked up key components, including the ADS1299, and started finalizing the signal processing subsystem. The goal for the breadboard demo was initially to visualize EEG signals, but since we were still waiting on the PIC32, progress focused more on schematic development.

I completed the input and protection circuitry, which includes:

* Diode clamping network for voltage spikes
* 2.2 kΩ series resistors for current limiting
* 100 pF capacitors for high-frequency noise filtering

These components ensure that signals entering the ADS1299 are safe and clean. I also finalized the use of a single EEG channel (C3 referenced to M2), which is supported by literature for SWS detection and helps reduce latency, power, and PCB size. This simplification is important for making the system more practical as a wearable device.

<img width="749" height="430" alt="Screenshot 2026-05-04 at 7 49 02 PM" src="https://github.com/user-attachments/assets/bd86f71e-5a6b-4d01-bebd-031f9d900865" />
Figure 3. Channel Input Protection Schematic for Signal Processing Subsystem ￼

I assigned footprints to all components, mostly choosing 0603 packages for passives to balance compactness and manufacturability. While doing this, I also considered how component values relate to footprint size (e.g., larger capacitance often requires physically larger packages due to voltage rating and dielectric constraints).

For the audio subsystem, I initially planned to use PWM from the microcontroller directly into the amplifier with an RC filter. However, I realized this would introduce high-frequency switching noise, which is problematic in a system that also includes a sensitive analog front end.

To address this, I added a DAC (MCP4822) between the MCU and amplifier. This converts the digital signal into a true analog output, reducing noise and improving signal quality. I also added a coupling capacitor (C40) between the DAC and amplifier input to block DC offset, since the amplifier expects an AC-coupled signal.
￼<img width="418" height="441" alt="Screenshot 2026-05-04 at 7 48 24 PM" src="https://github.com/user-attachments/assets/17343de8-e896-46b6-98f5-e79620f609fc" />
Figure 4. Audio Subsystem Schematic

**3/3 - PCB implementation (schematic development)**

**Objective:** Build complete ADS1299-based signal processing schematic.

I worked on the full schematic for the signal processing subsystem, focusing on correct implementation of power, inputs, and interfaces. One key design decision was to use a single-supply configuration (0–5V) instead of ±2.5V, effectively setting AVSS = AGND. This simplifies the power design and avoids needing a negative rail.

I implemented all key ADS1299 connections:

* Differential inputs (IN1P/IN1N)
* Reference pins with decoupling capacitors
* Bias network for stabilizing common-mode signal
* SPI interface to MCU
* DRDY interrupt for timing (250 Hz sampling -> ~4 ms period)

I also added proper decoupling (0.1 µF + 1 µF) on supply rails to stabilize voltage and reduce noise. Overall, the goal was to create a low-noise environment suitable for microvolt EEG signals.


**2/25 - Communication + component decisions**

**Objective:** Decide communication method and finalize major system components.

We considered different communication approaches, including a custom dongle for higher bitrate transmission, but ultimately decided to use standard Bluetooth (BLE). Since we are only using a single EEG channel, the data rate is relatively low (~6 kbps at 250 Hz sampling), so BLE is more than sufficient.

This decision prioritizes accessibility and ease of use, especially for integration with a mobile app, while still meeting performance requirements.

We also finalized key components across subsystems, including:

* ADS1299 for signal acquisition (with room to scale up to 8 channels)
* PIC32 microcontroller
* BLE module
* Audio chain (DAC, amplifier, speaker)

**2/24 - Design document update + testing plan**

**Objective:** Finalize component selection and define verification strategy for the system.

We updated the design document by reviewing all components, ensuring availability, compatibility, and delivery timelines. This led to finalizing the full subsystem/component breakdown (headband, ADS1299, PIC32, BLE module, DAC, amplifier, etc.), which establishes a clear hardware architecture for the project.

Table 1. Components Ordered
<img width="561" height="415" alt="Screenshot 2026-05-04 at 7 46 09 PM" src="https://github.com/user-attachments/assets/ac7ac9c7-6da4-4c8f-a34f-71ec74fe6f3b" />


I also worked on defining testing procedures, focusing on the signal processing subsystem. One key requirement from the Sound Asleep reference is that artifact-contaminated data must be less than 10% over long recordings. OpenBCI identifies artifacts using ADC saturation, which occurs when the analog signal exceeds the ADC’s measurable range and clips.

Based on this, I proposed measuring the fraction of time where samples are saturated:

* artifact fraction = (time in saturation) / (total recording time)

Additionally, I added a requirement to verify the accuracy of our SWS detection model against labeled data. This ensures not only that the hardware works, but that the system meets performance expectations in detecting slow-wave sleep.

**2/23 - Signal processing subsystem focus + ADS1299 study**

**Objective:**
 Understand ADS1299 operation and evaluate simplifying system to single-channel EEG.

I continued working on the design document, focusing specifically on the signal processing subsystem. One key idea we explored was simplifying the system to use only a single EEG channel for SWS detection. Since ADS1299 supports up to 8 channels, this significantly reduces complexity in routing, power consumption, and processing requirements.

I also researched existing DIY EEG projects and found one that uses a single-channel design, which reinforced that this approach is practical for our use case.

To better understand implementation, I studied the ADS1299 datasheet in detail, focusing on:

* Electrode inputs and differential measurement
* Internal MUX and ADC structure
* Programmable gain amplifiers
* Reference and biasing requirements
￼
This helped clarify how signals flow through the chip and informed later schematic decisions.

<img width="605" height="502" alt="Screenshot 2026-05-04 at 7 40 36 PM" src="https://github.com/user-attachments/assets/e18fdf6b-f6a5-4bf8-b2c7-024b5a8d09c7" />
Figure 2. ADS1299 Analog Front-End Functional Block Diagram


**2/18 - Initial research + team planning**

**Objective:** Establish project direction and begin refining design from proposal.

We reviewed existing OpenBCI-based projects and discussed how they structure EEG acquisition systems. At the same time, we finalized team roles, responsibilities, and workflow through the team contract.

I began iterating on the design document based on TA feedback, focusing on the signal processing subsystem. This involved looking more closely at the components we initially proposed and verifying that they were appropriate and available.

This session set the foundation for moving from a conceptual proposal to a concrete system design.


**2/12 - Proposal refinement + system architecture**

**Objective:** Refine design section of proposal and define system-level architecture.

I updated the design section of the proposal to be more specific about the signal processing system, particularly how analog EEG signals are acquired, processed, and converted into digital data.

I also researched communication protocols used between components, including SPI (for ADS1299 to MCU) and wireless communication (BLE). This informed both the written design and the system block diagram.
￼<img width="573" height="499" alt="Screenshot 2026-05-04 at 7 38 29 PM" src="https://github.com/user-attachments/assets/2d54790e-96fd-4a6c-80cb-7f3fb7e3171d" />
Figure 1. Block Diagram

At this stage, I was focused on clearly defining:

* Analog front end (EEG acquisition)
* Digital processing (microcontroller)
* Communication (Bluetooth)
* Output (audio stimulation)

This work established the overall system pipeline that later guided both schematic and PCB development.





