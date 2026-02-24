# John Ludeke's Lab Notebook for ECE 445
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



