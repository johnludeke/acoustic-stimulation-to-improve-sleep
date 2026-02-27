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

Research into existing openBCI-based projects and planning with team for team contract. Discussed roles, responsibilites, and procedures
for moving forward, finalized in the team contract. Began iterating on design document based on proposal since TA liked it. 
Focused on the components we initially wrote about (I did signal processing subsystem) as well as looking closer into available components.


Thu - Feb 12

I updated the design section of our proposal - being more specific about the signal processing system's
analog and digital functions. I researched more about the communication protocols used to transmit data
between and within components. This was for both the design section as well as the block diagram
I made below:

<img width="645" height="504" alt="Screenshot 2026-02-27 at 5 09 30 PM" src="https://github.com/user-attachments/assets/7c01d72a-5c21-427e-98d7-4b048ce0466a" />

