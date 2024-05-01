# openlithium
Reverse engineering Sony's obsolete InfoLithium protocol for a 23 year old Camcorder


The protocol describes the communcation on a propietary 1-wire
interface Between camera and Battery is a 1-wire line. 3V HIGH is the
default state, 0.2V logic level LOW.  Any Partner who wants to
communicates, pulls the line to LOW.

The Camcorder initiates the communication by sending a preample with 2
big LOW pulses with width of 16ms-20ms. I have conflicting
measurements.  Another LOW pulse with a similar width follows. This is
believed to wake up the micro controller in the battery.

Then a never ending stream of communication starts: The Camera keeps
sending 4 Data-Words to the battery controller. The first two words,
contain binary data, probably command words. The next two bytes that
are sent only, their pulled down start bit. The battery controller is
supposed to sync up to the start bit and pull down the 1-wire bus, and
"fill-up" the byte, in order to send out its response in an orderly
manner to the camera


| COMMAND1 | COMMAND2 | RESP1 | RESP2 |

![1wire_frame](1wire_frame.png)

On my battery, an atmega 48pa, sends his synced respons over
Pin 25. An unknown SOP23 transistor marked with Q1 on the silk screen,
takes this digital signal and uses it to pull down the 1-wire line,
hence inverting the logic levels. The 1-wire line is conected over a
?buffered? input to PIN26 of the ATMEGA, where the mcu may read the signals.




This could be us:
![this_could_be_us.png](this_could_be_us.png)

Soon I'll have an idea how this works...
