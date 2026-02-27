# Bakry's Lab Notebook for ECE445
## Friday, February 27
### Objectives
* Meet with TA to discuss Design document and to submit it
* Plan goals for next week including designing the first PCB and ordering final components from e-shop
* Add notebook entries

### What was done
We met with Mingrui who helped make sure we were on track, reassuring us that it was fine that we had not yet designed a PCB. We got good feedback about our design document, specifically about our schedule since we had not realized how close the breadboard demo was, which we then submitted.
Our plan now is to have a first PCB design completed and ordered by next Monday so that we have something to present for the breadboard demo.

## Wednesday, February 28

### Objectives
* Finalize Design Document

### What was done
We briefly met to start working on a initial PCB design, exploring KiCAD and finding the components we need in the first iteration of our PCB and attempting to wire them together. We also did some more research about the type of power we would need to provide to the ADS1299 and the PIC32 through reading their corresponding datasheets.

## Tuesday, February 24

### Objectives
* Continue working on Design document

### What was done
We continued brainstroming things related to the design of our project. The main point brought up was that we should use only one channel of EEG signals from the brain since the paper that describes generating pink noise based off of EEG signals only considered one channel, and because this will be simpler. Also, the idea of using a simpler ADS620 chip instead of the ADS1299 chip was brought up but since we had already ordered the ADS1299 and because we would have to design more circuits with it, we decided not to change the design of our project.

## Monday, February 23

## Objectives
* Work on the design Document
* Submit order for components with our TA

## What was done
We wrote an initial draft of our design document, largely based off of our project Proposal that we had submitted the week prior. The main changes were the structure of the document was updated and we added tables to describe the requirements and verification procedures for each subsystem. Also, we started including more detail like the inclusion of the ads1299 chip for intercepting and processing signals direclty from the electrodes, and using the pic32 chip as our microcontroller, as well as other components that we finalized and ordered in preperation for out first prototype.
