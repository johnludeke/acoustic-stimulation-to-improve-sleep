3/12
skeptical about:

need srb2 routing from input connector?

3/11
notes on pcb - moving power entry to agnd side

3/10
breadboard development: problem - ads1299 components were not breadboardable, so pcb printing (whatever word for putting component on would have to be soldered not breadboard pins). we pivoted and worked on audio subsystem components, perfect since we were implementing in schematic for next pcb order. 

media: picture of breadboard (speaker connected to microcontroller...), code for pink noise generation



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


Monday - Feb 23

Continued working on design document.
I specifically looked into the signal processing subsystem. 
We considered design simplifications from the OpenBCI Cyton Board based on our project, 
such as an algorithm for detecting slow-wave sleep from one EEG channel.
We theorize this would simplify the component connections and power necessary, 
since the Analog Front End (ADS1299) can hold up to 8 input channels.

I studied the datasheet of the ADS1299 - the Analog Front End of the OpenBCI Cyton Board - to better understand
its functionality and pins - electrode inputs, MUX, ADC converters, and programmable gain amplifiers. 
<img width="644" height="508" alt="Screenshot 2026-02-27 at 5 17 36 PM" src="https://github.com/user-attachments/assets/e6181135-d0ff-42d5-b3ae-c6335f58c118" />




Wed - Feb 18

Research into existing openBCI-based projects and planning with team for team contract. Discussed roles, responsibilites, and procedures.
for moving forward, finalized in the team contract. Began iterating on design document based on proposal since TA liked it. 
Focused on the components we initially wrote about (I did signal processing subsystem) as well as looking closer into available components.


Thu - Feb 12

I updated the design section of our proposal - being more specific about the signal processing system's
analog and digital functions. I researched more about the communication protocols used to transmit data
between and within components. This was for both the design section as well as the block diagram
I made below:

<img width="645" height="504" alt="Screenshot 2026-02-27 at 5 09 30 PM" src="https://github.com/user-attachments/assets/7c01d72a-5c21-427e-98d7-4b048ce0466a" />



