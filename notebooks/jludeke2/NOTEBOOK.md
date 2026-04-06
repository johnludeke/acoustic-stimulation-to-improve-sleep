# John Ludeke's Lab Notebook for ECE 445
## Monday, April 6
### Overview
Using our audio subsystem, add an input for the waveform generator. Create software that listens for the incoming wave, and determines its frequency and amplitude. Then, based on a certain frequency threshold, output pink noise audio. Create a Jupyter notebook that outputs this audio as well.

### Pink Noise Algorithm
```
// Pink noise (Voss-McCartney)

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
```
This code works by creating 16 columns. Each column updates its frequency based on its column number. First columns update their frequencies very frequently, and later columns less so. This approximates the 1/f graph of typical pink noise. Additionally, we add a generic white noise to the end signal to smooth it out.

### Detect Waves State Machine
This is how our system will work in the final product. It is simple and effective, and follows the research papers provided to us that are supposed to prolong slow-wave sleep.

1. Listen for the desired frequency. If not found, loop back to 1.
2. Play audio at the first peak.
3. Play audio at the second peak.
4. Wait for the desired time.
5. Back to 1.


## Monday, March 30 - Friday, April 3
### Overview
Create a new PCB that is dedicated to the audio processor. The reason for this is that there is a lot that can go wrong with the more complicated board previously and we want a proof of concept that puts our priority at performing the audio processing and generation. This new board has the audio subsystem, a simpler power subsystem, and a microcontroller that allows us to take in signals and output audio. We leave the signal processing to the Cyton board.

This PCB is also easier to edit, meaning we can make revisions fast if needed. The parts used are simpler and quicker to order as well, and uses parts from our original order where possible. We also will place this order ourselves to get it faster.

### Schematic
<img width="892" height="704" alt="image" src="https://github.com/user-attachments/assets/92dd1e8b-656e-4647-8f01-2fe38c2eb1ab" />

### PCB
<img width="601" height="682" alt="image" src="https://github.com/user-attachments/assets/0966bcc1-3711-4692-9c89-6c75cb7bd884" />



## Thursday, March 26
### Overview
Cleaned up schematic for audio subsystem and wired it in. After these updates, the entire PCB is complete. In addition, all parts will be available, and there are no errors or warnings in the ERC and DRC.

### Schematic update
I just organized the schematic so that it is clean and readable with no components too close together.
<img width="1240" height="721" alt="image" src="https://github.com/user-attachments/assets/abf8b020-8013-4cbd-a130-07e3a6b3a74f" />

### PCB update
The PCB had to have its BLE system moved to be closer to the ADS so that the audio subsystem could be as far from it as possible (since the audio subsystem is noisy and we don't want to disrupt the ADS). Additionally, updates were made to the schematic by teammates to remove a resistor and diode array. I traced in those changes as well.
<img width="836" height="740" alt="image" src="https://github.com/user-attachments/assets/0adb9873-ba31-4f6b-8b5a-313cfca098e7" />


## Wednesday, March 25
### Overview
Added bluetooth chip to schematic and PCB.

### Schematic update
Included in this update are the addition of test pins. This allow us to connect to the chip with a programmer to flash firmware (as well as test if things aren't working as expected). The BLE chip connects the RXD and TXD pinouts of the PIC32 to itself which it can then transmit wirelessly.
<img width="1053" height="609" alt="image" src="https://github.com/user-attachments/assets/a9a7255c-4cb2-4d73-9df1-8cd7407312c4" />

### PCB update
The PCB was a little more complicated to update. I had to edit the footprint of the BLE chip to include the correct areas on the chip that are required to not have any copper in front of them (to make the bluetooth transmission possible). This involved making a custom symbol library for version control so that other members could also pull it.
<img width="727" height="653" alt="image" src="https://github.com/user-attachments/assets/d18aa953-0f5a-47b3-9dab-49f943b805c6" />


## Tuesday, March 24
### Quick update
Sid worked on a majority of the DRC errors in the PCB, but there were a few left to clean up. The task was somewhat laborious, but not too bad. Many errors had to do with bad grounding of AGND or DGND, along with some connections that were not complete. After today, there are no errors with the PCB design, which means any error should be either oversight in the architecture, or bad connections when soldering.


## Monday, March 8 - Thursday, March 11
### Overview
Completed wiring for the schematic. Involved some bug bashing and design changes.

### Schematic update
Note the addition of mounting holes, as well as slight differences in connection. A main difference is the power subsystem being digital grounded rather than analog grounded.
<img width="957" height="718" alt="image" src="https://github.com/user-attachments/assets/7f83633b-6a37-4f8b-af1d-612d34cdd85e" />

### PCB diagram
This was difficult to wire, and took a good amount of time. Main considerations were where to have our analog and digital ground, and how to organize the pieces so that traces didn't have to go too far. Note here that the ADS should be far from our power subsystem to reduce noise. The ADS also contains both AGND and DGND, but sits on AGND. The PIC32 sits on the DGND.
<img width="936" height="677" alt="image" src="https://github.com/user-attachments/assets/dcdf3431-0089-4c96-af1b-31ef7403ba23" />


## Friday, March 6
### Quick update
Worked on goign through the errors from the schematic so that it was cleanly wired. Can continue work on the PCB next update.

## Wednesday, March 4
### Overview
Continued work on PCB schematic. Finished PIC32, adjusted regulators, and added input components. I also cleaned up the schematic to be more organized and optimal.

### Picture of progress
You can see how the new schematic compared to before is more filled out and more optimized. It can be seen how packed the board is getting. Still missing is the Bluetooth chip for communication out.
<img width="975" height="733" alt="image" src="https://github.com/user-attachments/assets/2b10cfdb-d6e6-4d06-9716-b6e6e823780a" />


## Monday, March 2
### Quick update
Continued work on power schematic, adjusted a few things. Needed to add capacitors in parallel to add more denoising. Also started work on inputs to our microcontroller, the PIC32.

## Thursday, Feburary 26
### Overview
Began work on schematic for power system and connecting to the ADS1299.

### What was done
Working through the power system cleared up a lot of things about power through our whole system. We have continued with using 4 AA batteries to meet all power requirements of our parts. This involves a 3.3V digital rail, and a 0-5V analog rail (this is purely used for the ADS1299 chip). 

I also included as much denoising to the power systems as possible to reduce the possible interruptions to the EEG signals. I've included a picture of the diagram.
<img width="1021" height="701" alt="image" src="https://github.com/user-attachments/assets/8d2160c3-b937-4aef-96b5-feceaf132840" />


## Tuesday, February 24
### Overview
Continued working on the design document, with slight pivots to the project on a broader view.

### Thinking about small pivots.
We are thinking of using just a single signal (rather than four) for a couple of reasons. The students leading our project provided us with a paper that talks about detecting slow-wave sleep with just one electrode. We would like to follow this, which reduces complexity. This still requires 3 electrodes, but means that our microcontroller just has to juggle one "combined" signal.

### What was changed?
I added /changed a couple things:
- Clarified the mobile applications goals (purely visualization of slow-wave sleep).
- Clarified the microcontroller UART communication with the BLE.
- Clarified the BLE communication with the mobile app.

We also did some talking about how to do initial testing. We decided that capturing the signals and confirming we have them before moving to the PIC32 signal processor would be smart, then we can worry about the other subsystems.

### What's next?
- Decide on a direction so that we can start KiCad
  - Main concern here is compatibility!

## Monday, February 23
### Overview
Get the spreadsheet of parts to order done. Start (and finish?) on the design document to be able to translate it into a KiCad PCB design. Our group also did some generak organization.

### Basic organization tasks.
As a group, we set up a Google Drive folder, as well as finished setting up the GitHub where our eventual code will live (including signal processing and the mobile application).

### Creating the parts list.
This was somewhat difficult. It involved deprecating a lot of the pieces we initially thought we needed, simplifying the design to just include what was necessary for the sleep tracking, and ensuring parts were to be delivered on time. 

Some updates were removing pieces like the accelerometer, as well as replacing the regulator with a light-weight alternative that is both cheaper and better suited to the AAA batteries we opted for in our power delivery subsystem.

The parts list was wrapped up in a Google Sheet and sent off to our TA for ordering. We also have a considerable amount of ordinary parts we can get from the ECEB (e.g., resistors).

### Design document
The design document followed closely to our Project Proposal, with key differences being:
- Fleshed out our subsystems with key figures when needed.
- Made appropriate adjustments where planned parts that are at end-of-life were, or where parts that were deemed unnecessary were removed.

### What's next?
- Need to start on the PCB design in KiCad so that we can get a PCB ordered by Thursday.
  - We might start with the power subsytem as a separate PCB while we finish up the signal processor.
- Tweak the design document as necessary if new problems come up (likely related to ordering parts).

## Friday, February 20
### Overview
Create team contract.

### What was done?
- Collaborated with teammates on our team contract (met at CIF).

### What's next?
- Put parts into a ready-to-order spreadsheet.
- Create design document.
- Get ready for our design doc presentation.

## Friday, February 13
### Overview
Finished up our project proposal.

### What was done?
- Went through preliminary parts and found modern-day equivalents (where necessary).
- Found active retailers that sold the parts along with estimated costs.
- Drafted goals for the project.
- Came up with general schema / tech stack for the mobile application portion of the project.

### What's next?
- Put parts into a ready-to-order spreadsheet.
- Create design document.





