# Motion-Triggered Audio Message Player
A motion activated audio player, which plays an 11 KHz 12-bit uncompressed audio over an Arduino audio wave shield using a SPI interface that is triggered by a passive infrared sensor or a push button through a GPIO interface.

## Author
Shaun Astarabadi - shaun.astarabadi@wdc.com

## Required Components
* HiFive1 - A RISC-V, Arduino-Compatible Development Board
  https://www.sifive.com/products/hifive1/
* Audio Wave Shield for Arduino Kit
  https://www.adafruit.com/product/94
* Passive Infrared Motion Sensor
  https://www.adafruit.com/product/189
* Speakers with a 3.5 mm Jack
  https://www.amazon.com/Logitech-S120-2-0-Stereo-Speakers/dp/B000R9AAJA/ref=pd_cp_147_3?_encoding=UTF8&pd_rd_i=B000R9AAJA
  
## Assembly
  
### Arduino Audio Wave Shield
The audio wave shield requires a few modifications to allow the serial peripheral interface (SPI) to work with the HiFive board.  Jumpers must be connected between the points specified in the table below.

| JP13 | GPIO | SPI Signal |
| -----| -----| ---------- |
|   1  | D10  |    CS_     |
|   2  | D13  |    SCK     |
|   3  | D11  |    MOSI    |
|   4  | GND  |    LCS_    |

For more information, see the schematics: https://cdn-learn.adafruit.com/assets/assets/000/010/163/original/wave11schem.png

NOTE: Do not apply the jumper wiring instructions for JP13 in the audio wave shield assembly document (because that is needed for an Arduino, not a HiFive).  These instructions were created for version 1.1 of the wave shield.

### Motion Sensor
The motion sensor has 3 wires that need to be connected to the HiFive board.  
* Ground - Connect to the HiFive's ground, which can be found on header J6 - the pin labeled GND
* Power - Connect to the HiFive's 5v, which can be found on header J6 - the pin labeled 5.0V
* Signal Out - Connect to HiFive GPIO12, which can be found on header J1 - the pin labeled 18
  
### Speakers
The speakers are connected to the wave shield using a 3.5 mm Jack.
