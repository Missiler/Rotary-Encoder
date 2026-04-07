/*
            This is a personal project that is based on the Haptic Feedback door knob.
        Currently, the used microcontroller is an Xiao Seeed Studio ESP32-C3 with Bluetooth compabilites.
              The Microcontroller is chosen as it can charge a LiPo-battery and work wirelessly.
    The code will use the BLEMouse library as it is very simple to use will send scroll ticks instead of continous scrolls.
  
              To get the scroll wheel to work the best, you can change the scroll distance in Windows 11.
              Settings > Bluetooth & devices > Mouse > Scroll wheel > Choose how many lines to scroll each time.


                      It uses an AS5600 Magnetic Rotary Encoder to detect the rotation of a knob.

*/

#include <Arduino.h>
#include <BleMouse.h>


//I will rename it to something more fun.
BleMouse bleMouse("XIAO Scroll", "Seeed", 100);

void setup() {
  Serial.begin(115200);
  bleMouse.begin();
}

void loop() {
  delay(100);  // small delay = smooth continuous scroll
}

//Next step: Create a 3D-printed case for the rotary encoder.
//Then: Coding :)