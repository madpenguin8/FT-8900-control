/*
Copyright (c) 2016 Mike Diehl AI6GS - diehl.mike.a@gmail.com

FT-8900-control.ino is free software: you can redistribute it and/or modify
it under the terms of the GNU LESSER GENERAL PUBLIC LICENSE as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
 
FT-8900-control.ino is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
 
You should have received a copy of the GNU LESSER GENERAL PUBLIC LICENSE
along with  FT-8900-control.ino.  If not, see <http://www.gnu.org/licenses/>.

Control of Yaesu 8900 rigs through its serial interface. Should also work on the 8800 but not tested.
This code strictly sends data packets to the main unit and all aspects can be controlled to your liking.

 The pinout for main unit is as follows
 1 - Buffered AF input, 2k impedence
 2 - Power switch, momentarily pull low through a 1-20k resistor to ground.
 3 - 9V power, only on while unit is powered up as well as during power button press
 4 - Ground
 5 - RX, data sent to the main unit
 6 - TX, data sent to the head unit
 
 If you burn this sketch to an arduino without the boot loader you can operate strictly off of the 9v feed.
 I've done this with an arduino pro mini. With the bootloader the delay is too long and the radio's watchdog
 timer shut it back down. In that case you need an external PS and allow full boot before attempting power up.
 
 The data structure is composed of 13 octets that are stored in a char array. There is no checksum or markers 
 for start and end of the packet. However, the data is carried in the lower 7 bits of each octet. The first octet uses 
 a 1 for the MSB and this syncs the packet. The first octet carries the left encoder data, you can easily set the MSB
 by using an OR, if you prefer you can just hardcode the values like I did in the methods below.
 
 The data structure is explained in setDefaults() so read the comments.
 
 Relevant values for different buttons can be seen in the enumerations below.
 
 There is also an enum that covers all the buttons on the MH-48 mic, this makes it a little bit easier to use the
 included mh48ButtonPress(MH48 button) method. The ADC values can be seen for the rows and columns in
 that method as well.
 
 I've also added some code to control volume and squelch using analog input pins. These simply map the 10bit
 arduino ADC values and map them to the 7 bit values on the head unit. It does seem like the main unit does some
 ramping between values, this makes sense so there aren't any noticeable changes between incremental steps.
 
 
 To change values modify the array at the top of printBuffer() before the for loop that writes it to the port. I added 
 some sample code that turns up the volume a little and then opens the squelch if you pull Digital In 2 HIGH. Zap that 
 out of there and add whatever you wish.
 */

#include <SoftwareSerial.h>

// Left button values
typedef enum{
    LOW_BUTTON_L = 0x00,
    VM_BUTTON_L = 0x20,
    HM_BUTTON_L = 0x40,
    SCN_BUTTON_L = 0x60,
    NO_BUTTON_L = 0x7f
} FrontButtonsLeft;

// Right button values
typedef enum{
    LOW_BUTTON_R = 0x60,
    VM_BUTTON_R = 0x40,
    HM_BUTTON_R = 0x20,
    SCN_BUTTON_R = 0x00,
    NO_BUTTON_R = 0x7f
} FrontButtonsRight;

// PTT
typedef enum{
    PTT_ON = 0x1b,
    PTT_OFF = 0x7f
} PTTButton;

// All other buttons like encoder presses
typedef enum{
    ENC_BUTTON_L = 0x02,
    ENC_BUTTON_R = 0x01,
    SET_BUTTON = 0x04,
    WIRES_BUTTON = 0x08,
    NO_BUTTON_OTHER = 0x00
} OtherButtons;

// MH-48 mic buttons
typedef enum {
    M_BUTTON_NONE = 0,
    M_BUTTON_1,
    M_BUTTON_2,
    M_BUTTON_3,
    M_BUTTON_4,
    M_BUTTON_5,
    M_BUTTON_6,
    M_BUTTON_7,
    M_BUTTON_8,
    M_BUTTON_9,
    M_BUTTON_0,
    M_BUTTON_STAR,
    M_BUTTON_POUND,
    M_BUTTON_A,
    M_BUTTON_B,
    M_BUTTON_C,
    M_BUTTON_D,
    M_BUTTON_UP,
    M_BUTTON_DOWN,
    M_BUTTON_P1,
    M_BUTTON_P2,
    M_BUTTON_P3,
    M_BUTTON_P4
} MH48;

// Global variables
char headData[13];

// For testing
// int idx = 0;

// Store last update
unsigned long previousMillis = 0;

// Update interval, 20mS is too long and I haven't tried anything longer than 10, may work.
const long interval = 10;

// Some analog inputs for squelch and volume should you choose to use it.
const int leftVolumeInput = A0;
const int rightVolumeInput = A1;
const int leftSquelchInput = A2;
const int rightSquelchInput = A3;

// We need to use soft serial as hardware UART pulls high
// this appears to shutdown the radio, soft solves this.
SoftwareSerial mySerial(10, 11); // RX, TX

void setup()
{
    // Serial data is 19.2k 8n1
    mySerial.begin(19200);
    setDefaults();
}

void loop()
{
    // Non-blocking timer
    unsigned long currentMillis = millis();
    
    if (currentMillis - previousMillis >= interval)
    {
        previousMillis = currentMillis;
        printBuffer();
    }
    
    /* This was used to intercept the head data on pin 10
     // Read in data
     while(mySerial.available())
     {
     char c = mySerial.read();
     // The first octet in the stream has a MSB of 1, all others are 0.
     // Data is represented in the lower 7 bits. They use this as a
     // sync bit and so shall we.
     if(c & B10000000)
     idx = 0;
     
     // Add an octet and increment.
     headData[idx] = c;
     idx++;
     
     // Filled our buffer. Send it out and clear for next round.
     if(idx >= 12)
     {
     printBuffer();
     //clearBuffer();
     idx = 0;
     }
     }
     */
}

// Clear out the buffer, not required but useful to have around.
void clearBuffer()
{
    for(int i = 0; i < 12; i++)
    {
        headData[i] = 0;
    }
}

// Send control data to the radio.
void printBuffer()
{
    // Modify packets here before sending out.
    if(digitalRead(2))
        leftSquelchOpen();
    else
        leftSquelchFull();
    
    // For test, turn up the left volume.
    headData[6] = 0x40;
    
    // Finally write the data packet out to the port after you've modified the array to your liking.
    for(int i = 0; i < 12; i++)
    {
        mySerial.write(headData[i]);
    }
}

void setDefaults()
{
    // Set all parameters to something sane
    // Squelch full close, volume muted, no knobs turned and no buttons pressed
    // All octets only carry data in the lowest 7 bits
    // First octet is synced by a 1 for MSB
    
    // Encoders express number of steps since last update and are two's complement for direction
    // First octet must have MSB as 1 in all cases, it is the sync bit
    // CW dials are negative, CCW are positive. MSB of the lower 7 bits
    // carrys sign.
    headData[0] = 0x80; // No steps B10000000
    
    // Right encoder steps since last update
    headData[1] = 0x00; // No steps B00000000
    
    // PTT control, to key up 0x1b, no key 0x7f;
    headData[2] = PTT_OFF;
    
    // Right squelch, 0x7f is open, value is inverted to volume
    // Set to full squelch or 0
    headData[3] = 0x00;
    
    // Right volume control, 0-127 or the highest value of 7 bits
    // Turn volume all the way down
    headData[4] = 0x00;
    
    // Mic keyboard matrix row, more info in mh48ButtonPress(MH48 button)
    // Inactive is 0x7f, no buttons
    headData[5] = 0x7f;
    
    // Left volume, see above notes
    headData[6] = 0x00;
    
    // Left squelch, see above notes
    headData[7] = 0x00;
    
    // Mic keyboard column, more info in mh48ButtonPress(MH48 button)
    // Inactive is 0x7f, no buttons
    headData[8] = 0x7f;
    
    // Right buttons, see enum
    headData[9] = NO_BUTTON_R;
    
    // Left buttons, see enum
    headData[10] = NO_BUTTON_L;
    
    // Other buttons, see enum
    headData[11] = NO_BUTTON_OTHER;
    
    // Hyper memory key
    // 0x00, no buttons 0x01-0x06 selected button
    headData[12] = 0x00;
}

// Should work, first octet needs MSB set to 1
void leftEncoderOneStepCCW()
{
    headData[0] = 0xc1;
}

void leftEncoderOneStepCW()
{
    headData[0] = 0x81;
}

void leftEncoderNoStep()
{
    headData[0] = 0x80;
}

// Right encoder single step
void rightEncoderOneStepCCW()
{
    headData[1] = 0x7f;
}

void rightEncoderOneStepCW()
{
    headData[1] = 0x01;
}

void rightEncoderNoStep()
{
    headData[1] = 0x00;
}

// PTT control
void unkeyPTT()
{
    headData[2] = 0x7f;
}

void keyPTT()
{
    headData[2] = 0x1b;
}

// Volume and squelch convenience methods.
void rightSquelchOpen()
{
    headData[3] = 0x7f;
}

void rightSquelchFull()
{
    headData[3] = 0x00;
}

void rightVolumeMute()
{
    headData[4] = 0x00;
}

void leftSquelchOpen()
{
    headData[7] = 0x7f;
}

void leftSquelchFull()
{
    headData[7] = 0x00;
}

void leftVolumeMute()
{
    headData[6] = 0x00;
}

// Volumes 0-127 (0x00 - 0x7f)
void adjustRightVolume()
{
    int outputValue = map(analogRead(rightVolumeInput), 0, 1023, 0, 127);
    headData[6] = outputValue;
}

void adjustLeftVolume()
{
    int outputValue = map(analogRead(leftVolumeInput), 0, 1023, 0, 127);
    headData[4] = outputValue;
}

// Squelch values appear to be inverted 127 (0x7f) is fully open.
void adjustRightSquelch()
{
    int outputValue = 127 - map(analogRead(rightSquelchInput), 0, 1023, 0, 127);
    headData[3] = outputValue;
}

void adjustLeftSquelch()
{
    int outputValue = 127 - map(analogRead(leftSquelchInput), 0, 1023, 0, 127);
    headData[7] = outputValue;
}

// Button presses for the MH-48 mic
void mh48ButtonPress(MH48 button)
{
    switch (button) {
        case M_BUTTON_NONE:
        {
            headData[5] = 0x7f;
            headData[8] = 0x7f;
        }
            break;
            
        case M_BUTTON_1:
        {
            headData[5] = 0x04;
            headData[8] = 0x1c;
        }
            break;
            
        case M_BUTTON_2:
        {
            headData[5] = 0x04;
            headData[8] = 0x34;
        }
            break;
            
        case M_BUTTON_3:
        {
            headData[5] = 0x04;
            headData[8] = 0x4c;
        }
            break;
            
        case M_BUTTON_4:
        {
            headData[5] = 0x1a;
            headData[8] = 0x1c;
        }
            break;
            
        case M_BUTTON_5:
        {
            headData[5] = 0x1a;
            headData[8] = 0x34;
        }
            break;
            
        case M_BUTTON_6:
        {
            headData[5] = 0x1a;
            headData[8] = 0x4d;
        }
            break;
            
        case M_BUTTON_7:
        {
            headData[5] = 0x32;
            headData[8] = 0x1c;
        }
            break;
            
        case M_BUTTON_8:
        {
            headData[5] = 0x32;
            headData[8] = 0x34;
        }
            break;
            
        case M_BUTTON_9:
        {
            headData[5] = 0x32;
            headData[8] = 0x4d;
        }
            break;
            
        case M_BUTTON_0:
        {
            headData[5] = 0x4c;
            headData[8] = 0x34;
        }
            break;
            
        case M_BUTTON_STAR:
        {
            headData[5] = 0x4c;
            headData[8] = 0x1c;
        }
            break;
            
        case M_BUTTON_POUND:
        {
            headData[5] = 0x4c;
            headData[8] = 0x4d;
        }
            break;
            
        case M_BUTTON_A:
        {
            headData[5] = 0x04;
            headData[8] = 0x67;
        }
            break;
            
        case M_BUTTON_B:
        {
            headData[5] = 0x1a;
            headData[8] = 0x67;
        }
            break;
            
        case M_BUTTON_C:
        {
            headData[5] = 0x32;
            headData[8] = 0x67;
        }
            break;
            
        case M_BUTTON_D:
        {
            headData[5] = 0x4c;
            headData[8] = 0x67;
        }
            break;
            
        case M_BUTTON_UP:
        {
            headData[5] = 0x1e;
            headData[8] = 0x06;
        }
            break;
            
        case M_BUTTON_DOWN:
        {
            headData[5] = 0x35;
            headData[8] = 0x06;
        }
            break;
            
        case M_BUTTON_P1:
        {
            headData[5] = 0x65;
            headData[8] = 0x1c;
        }
            break;
            
        case M_BUTTON_P2:
        {
            headData[5] = 0x65;
            headData[8] = 0x34;
        }
            break;
            
        case M_BUTTON_P3:
        {
            headData[5] = 0x65;
            headData[8] = 0x4d;
        }
            break;
            
        case M_BUTTON_P4:
        {
            headData[5] = 0x65;
            headData[8] = 0x67;
        }
            break;
            
            // Defualt to no buttons
        default:
        {
            headData[5] = 0x7f;
            headData[8] = 0x7f;
        }
            break;
    }
}

