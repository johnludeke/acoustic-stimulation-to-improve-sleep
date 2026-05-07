# Bakry's Lab Notebook for ECE445

## Monday, February 23

### Objectives
* Work on the Design Document
* Submit order for components with our TA

### What was done
We wrote an initial draft of our design document, largely based off of our project proposal that we had submitted the week prior. The main changes were restructuring the document and adding tables describing subsystem requirements and verification procedures.

We also finalized several important hardware decisions, including using the ADS1299 analog front end for EEG acquisition and the PIC32 as the main microcontroller in the original system architecture. We submitted a preliminary list of components to order so that we could begin prototyping and PCB development as early as possible.

## Tuesday, February 24

### Objectives
* Continue working on Design Document
* Continue researching EEG acquisition methods

### What was done
We continued brainstorming design decisions related to the project architecture and EEG acquisition pipeline. One major discussion involved reducing the system to a single EEG channel instead of multiple channels because the research paper we were following for slow-wave sleep detection demonstrated that single-channel EEG was sufficient for classification.

We also briefly considered using a simpler ADC instead of the ADS1299, but decided against this because the ADS1299 was specifically designed for low-noise biopotential acquisition and we had already begun planning the PCB around it.

## Friday, February 27

### Objectives
* Meet with TA to discuss Design Document
* Plan goals for the next week
* Continue organizing subsystem tasks

### What was done
We met with Mingrui to discuss our design document and current progress. We received useful feedback about the project schedule and realized that the breadboard demonstration milestone was approaching much sooner than expected.

We also discussed our plan for developing the PCB and agreed that we should begin designing the first hardware revision as soon as possible. The goal became to complete an initial PCB design quickly enough to have hardware available for testing later in the semester.

## Wednesday, February 28

### Objectives
* Finalize Design Document
* Begin learning KiCAD and PCB workflow

### What was done
We began working on an initial PCB schematic in KiCAD and explored different component libraries and routing approaches. We also spent time reading datasheets for the ADS1299 and planned power delivery requirements for both analog and digital portions of the system.

At this point, the intended architecture still involved integrating EEG acquisition, signal processing, audio generation, and power delivery onto a single custom PCB.

## Monday, March 3

### Objectives
* Continue PCB schematic work
* Research acoustic stimulation methods for slow-wave sleep
* Investigate audio generation methods

### What was done
We continued working on the schematic and researching how prior work implemented acoustic stimulation during slow-wave sleep. We read several research papers discussing closed-loop auditory stimulation and how phase-aligned pink noise could strengthen slow-wave oscillations and improve memory consolidation.

We also began discussing how audio playback would eventually be integrated into the embedded system and started evaluating different methods for generating pink noise digitally.

## Thursday, March 5

### Objectives
* Continue schematic development
* Explore audio subsystem implementation
* Investigate waveform generation techniques

### What was done
We continued refining the schematic and discussing the architecture of the audio subsystem. Initially, we planned to generate audio using PWM output directly from the microcontroller because it would simplify the hardware design and reduce the number of required components.

We also researched timing requirements for phase-aligned stimulation and discussed how the system would eventually detect slow-wave troughs and schedule audio playback at predicted peak times.

## Monday, March 9

### Objectives
* Prototype audio subsystem
* Continue PCB debugging
* Test waveform generation

### What was done
We created a small breadboard prototype for the audio subsystem and began testing simple waveform generation and speaker output. This allowed us to experiment with tone generation and evaluate whether PWM-based audio would be sufficient for generating pink noise stimulation.

Although the system successfully generated audio, we observed noticeable noise and distortion that motivated us to later investigate DAC-based approaches.

## Tuesday, March 17

### Objectives
* Continue audio subsystem testing
* Evaluate PWM signal quality
* Research DAC alternatives

### What was done
We continued testing the breadboard audio subsystem and comparing different methods for generating audio. Based on the observed PWM noise and filtering limitations, we began exploring the use of an external DAC for cleaner audio generation.

We researched DAC options compatible with the ESP32 and discussed how the DAC would interface with the rest of the stimulation subsystem through SPI communication.

## Friday, March 20

### Objectives
* Continue PCB work
* Reevaluate overall system architecture
* Discuss implementation priorities

### What was done
As development progressed, we realized that integrating EEG acquisition hardware, embedded processing, power delivery, and audio circuitry into a single PCB would introduce significant hardware complexity within the remaining project timeline.

Because of this, we began discussing a transition toward a more modular architecture based around the OpenBCI Cyton board for EEG acquisition. This would allow us to focus more heavily on the signal processing and stimulation pipeline rather than debugging a fully custom EEG acquisition front end.

## Monday, March 30

### Objectives
* Begin work on simplified audio and power PCB
* Continue audio subsystem development
* Help refine final hardware architecture

### What was done
We officially transitioned away from the original fully integrated PCB architecture and began focusing on a simplified PCB containing the ESP32, DAC, amplifier, and power circuitry.

This new architecture leveraged the OpenBCI Cyton board for EEG acquisition while using our custom hardware for embedded processing and audio generation. The simplified design significantly reduced hardware risk and improved our ability to iterate quickly on the stimulation subsystem.

## Friday, April 3

### Objectives
* Continue developing simplified PCB
* Finalize DAC-based audio architecture
* Continue subsystem verification

### What was done
We finalized the transition from PWM-based audio generation to a DAC-based implementation using the MCP4822 DAC. The DAC produced much cleaner analog output and reduced high-frequency switching noise compared to the PWM approach.

We also continued validating the audio subsystem independently using breadboard testing while reviewing the PCB layout and ensuring that the power and audio routing were appropriate for low-noise operation.

## Friday, April 10

### Objectives
* Continue firmware development
* Test pink noise generation
* Continue debugging real-time processing pipeline

### What was done
We continued developing firmware for the embedded processing pipeline and implemented additional pink noise generation functionality on the ESP32.

We also continued reviewing research papers discussing phase-aligned auditory stimulation and refined our understanding of trough detection and slow-wave peak prediction. At the same time, we monitored manufacturing progress for the final audio and power PCB.

## Thursday, April 17

### Objectives
* Receive and test fabricated PCB
* Prepare for hardware assembly
* Continue firmware debugging

### What was done
Unfortunately, the shipment containing our final audio and power PCB was stolen shortly after delivery. This delayed our planned assembly and hardware verification schedule and required us to immediately place a replacement PCB order.

While waiting for the replacement board, we continued validating firmware functionality and testing portions of the audio subsystem using the breadboard prototype.

## Saturday, April 19

### Objectives
* Resume subsystem integration
* Prepare for PCB assembly and testing
* Continue verification planning

### What was done
The replacement PCB arrived a few days after the original shipment was stolen, allowing us to resume hardware integration and subsystem testing.

We continued validating the audio subsystem and embedded firmware while preparing for final system verification. At this stage, the project architecture had fully transitioned into the final modular implementation consisting of the OpenBCI Cyton board for EEG acquisition and the custom ESP32-based audio and processing PCB for stimulation generation.
