4/5 — Packet Parsing + Signal Validation

With access to the raw bitstream, I moved on to parsing the incoming data according to the OpenBCI Cyton data format. Each packet begins with a header byte (0xA0) and ends with a footer byte (0xC followed by a counter), with a fixed number of bytes in between representing channel data and metadata.

To verify correct packet reception, I first implemented a simple parser that prints a new line whenever a valid header is detected and a corresponding footer appears at the expected offset. This resulted in consistently formatted packet outputs, with no unexpected bytes or misalignment. This step was important because it confirmed that there was no data corruption or packet loss at the UART level.
<img width="702" height="513" alt="Screenshot 2026-04-07 at 8 01 57 PM" src="https://github.com/user-attachments/assets/bb86e2ad-8833-4226-ba44-28b570faddb6" />
￼￼

After confirming packet integrity, I implemented parsing of the EEG channel data. Each channel value is represented as a 24-bit signed integer, so I reconstructed the values from three bytes and converted them into signed integers. These values were then streamed and plotted over time.

To validate correctness, I compared the reconstructed EEG waveform from the ESP32 with the waveform displayed in the OpenBCI GUI. The signals matched in both shape and timing, confirming that the parsing process was accurate and that the data pipeline preserved signal integrity.
<img width="653" height="507" alt="Screenshot 2026-04-07 at 8 05 31 PM" src="https://github.com/user-attachments/assets/dd83220a-3864-430b-ac93-16a8cb91c607" />

￼

Design Direction Pivot

At this point, I began reconsidering the system architecture for the final demo. While the original plan was to use our custom PCB (ADS1299 + PIC32) for EEG acquisition and processing, there are practical concerns around bring-up time, particularly with soldering fine-pitch components and debugging low-level firmware.

Given that the OpenBCI Cyton board already implements a nearly identical signal acquisition pipeline to our design  , it may be more efficient to use it directly for the demo. This would allow us to focus on the core contribution of the project—real-time SWS detection and closed-loop audio stimulation—without being blocked by hardware integration issues.

Under this approach, the Cyton board would handle EEG acquisition and digitization, while the ESP32 would take over feature extraction, SWS detection, and audio output. Since the ESP32 is already integrated into our breadboard audio subsystem, this creates a clean and practical pipeline that still aligns with our original system goals.

The next step will be to port the feature extraction logic from Python to the ESP32 and implement a rolling buffer for real-time processing, enabling end-to-end closed-loop operation.


4/4 — Hardware Integration (Cyton → ESP32)

After validating the algorithm in Python, I shifted focus toward implementing a fully embedded pipeline. The goal was to move away from a PC-based system and instead stream EEG data directly into the ESP32, where feature extraction and SWS detection could eventually run in real time.

From the system design, the EEG data is sampled at 250 Hz with 24-bit resolution. This results in a raw data rate of approximately 6 kbps for a single channel, which is well within the capabilities of UART communication. This confirmed that it should be feasible to stream the digitized EEG data directly into the ESP32 without compression or downsampling.

Initially, I attempted to connect the TX pin of the Cyton dongle to the RX pin of the ESP32 to capture the data stream. However, this caused the OpenBCI GUI to stop receiving data after a few seconds, triggering a timeout. This suggested that the connection was interfering with the expected communication pathway.

After investigating the Cyton architecture, I realized that the dongle’s TX pin is not used for streaming EEG data outward. Instead, the EEG data is transmitted wirelessly from the headset to the dongle, where it is received on the RX pin and then forwarded via USB to the computer. By tapping into the TX line, I was effectively probing the wrong side of the communication chain and potentially loading the signal in a way that disrupted the system.

I then switched to connecting the RX pin of the dongle to the ESP32. In this configuration, the ESP32 passively observes the incoming data stream without interfering with the communication between the headset and the GUI. This resolved the issue: the GUI continued to function normally, and I was able to observe a continuous stream of hex data on the ESP32 serial monitor. Disconnecting the wire caused the stream to stop immediately, confirming that the data being observed was indeed the live EEG bitstream.

<img width="615" height="324" alt="OBCI  DONGLE" src="https://github.com/user-attachments/assets/8b10787f-f0fa-437b-bb19-c884beb282fc" />


Week of 3/30 — SWS Detection + Real-Time Prototype

This week, I focused on understanding and implementing a single-channel EEG-based slow-wave sleep (SWS) detection algorithm from literature  . The goal was to translate an existing research method into something lightweight enough to eventually run on embedded hardware.

The core idea of the paper is that SWS can be identified using time-domain characteristics of the EEG signal, rather than relying on frequency-domain features or multiple channels. In particular, slow-wave sleep is associated with high-amplitude, low-frequency oscillations, which can be captured by analyzing zero-crossing behavior of the signal.

To prototype this, I used the Sleep-EDF dataset  , which provides labeled EEG recordings segmented into 30-second epochs. I implemented a preprocessing pipeline that mirrors the paper: bandpass filtering (0.3–35 Hz), segmentation into epochs, and further division into 1-second intervals. Within each interval, I subtracted the mean to remove DC offset and computed zero-crossing points (ZCPs), defined as locations where the signal changes sign.

From these zero-crossing points, the signal is divided into segments. These segments encode important temporal information about the waveform. Faster signals produce many short segments, while slower oscillations produce fewer, longer segments. This becomes particularly useful for SWS detection, since slow waves (~1 Hz) naturally lead to longer segment durations.

From this representation, I computed the three primary features:
	•	x1: mean length of zero-crossing segments
	•	x2: standard deviation of segment lengths
	•	x3: weighted area under the signal between zero crossings
￼<img width="730" height="652" alt="inport natplotlib pyplot as plt" src="https://github.com/user-attachments/assets/9c378b71-64b4-49de-bf29-1de81037d5ee" />

The third feature is especially important because it jointly captures amplitude and temporal behavior. Larger amplitudes increase the area under the curve, while slower oscillations increase the spacing between crossings, so x3 effectively encodes the defining characteristics of slow-wave activity.

After validating feature extraction offline, I moved to a real-time prototype using the OpenBCI Cyton board. I configured the OpenBCI GUI to stream EEG data through Lab Streaming Layer (LSL), and wrote a Python script to subscribe to this stream. The script maintains a rolling 30-second buffer (based on the 250 Hz sampling rate), continuously recomputing the feature vector for each epoch window. This allowed me to replicate the offline pipeline in real time, confirming that the approach is viable for embedded implementation later.

<img width="636" height="166" alt="-0 00021" src="https://github.com/user-attachments/assets/43c4bca1-d8b2-4d8f-a63a-f0a9203f2ae4" />

￼

3/24 - fixing dgnd pin connections
3/23 - fixing agnd pin connections

3/12
skeptical about:

need srb2 routing from input connector?

general pcb routing

3/11
pcb organizing and routing. pins were divided by 16 on each side of the ads, put capacitors compactly along each relevant side corresponding to *what each set of 16 pins on side of ads corresponds to *
notes on pcb - considering moving power entry to agnd side, but its actually ok 

3/10
breadboard development: problem - ads1299 components were not breadboardable, so pcb printing (whatever word for putting component on would have to be soldered not breadboard pins). we pivoted and worked on audio subsystem components, perfect since we were implementing in schematic for next pcb order. 


media: picture of breadboard (speaker connected to microcontroller...), code for pink noise generation
<img width="277" height="587" alt="Screenshot 2026-03-23 at 3 01 41 PM" src="https://github.com/user-attachments/assets/9825c349-c282-4628-9cb7-45ee67294c08" />




initially made random white noise (2 lines) to confirm working, it made a faint noise we didn't hear till we increased amplitude. then made pink noise (refer to code, how to make pink noise)

next steps (maybe next entry) - looking into how to make that pink noise phase aligned however the sws algorithm does it. 

3/9
picking up components - we got the ads1299, *whatever else part we picked up* our goal for the breadboard demo is to get an visualize an eeg signal. still waiting on pic32 microcontroller.

finalized signal processing subsystem in schematic by adding input connector, bias protection (whatever the compoenents between electrode and ads are). these use the reference and bias channels (in our case, m2 i think) to support the input channel c3 sent to the ads. we plan on using one channel as it is supported by paper (cite) and would minimize latency in data processing and sws dection (provide support for this). also minimizes size of signal processing subsystem on pcb, better for over all comfort. 
<img width="853" height="494" alt="Screenshot 2026-03-23 at 2 45 01 PM" src="https://github.com/user-attachments/assets/3002cdad-c230-4e04-899f-226ab506b2b9" />


media: pictures of those parts on schematic

For PCB design, I assigned footprints to all our parts in each subsystem, looking into capacitor/resister threshold-size constraints,

*talk about some design decisions we made in the process*

3/3
pcb implementation. doing schematic. ads1299 power, input output, capacitor and bias connections (and their purpose, design decisions)
discuss digital and analog rails for power, and how those route to the ads - our decision to make the +-2.5V analog rails to 0-5V analog rails (so avss=agnd)(talk about why we did this and what this does)

talk about the key pins/regions of the ads1299, their purpose, and what we did to implement those in our shcematic

2/25

considered dongle development for *whatever comm protocal cyton +ble module use, some kind of hgiehr bitrate*, decided we could make our device more accessible by communicating over *regular bluetooth, something about 1 channel info needing lower bitratei think*

include math justification for ble decision from design document

finalizing specific components, including ads with 8 channels matching our cap if we want to expand. *just talk about stuff from component list in design doc idk*

Tuesday - Feb 24

Overview -

Updated design document - we looked over parts, ensuring their current support and delivery, and planned subsystem development.


**2/23 – Signal processing subsystem focus + ADS1299 study**

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


**2/18 – Initial research + team planning**

**Objective:** Establish project direction and begin refining design from proposal.

We reviewed existing OpenBCI-based projects and discussed how they structure EEG acquisition systems. At the same time, we finalized team roles, responsibilities, and workflow through the team contract.

I began iterating on the design document based on TA feedback, focusing on the signal processing subsystem. This involved looking more closely at the components we initially proposed and verifying that they were appropriate and available.

This session set the foundation for moving from a conceptual proposal to a concrete system design.


**2/12 – Proposal refinement + system architecture**
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





