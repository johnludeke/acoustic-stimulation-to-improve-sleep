

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





