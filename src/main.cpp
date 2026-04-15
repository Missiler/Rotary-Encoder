/*
            This is a personal project that is based on the Haptic Feedback door knob.
        Currently, the used microcontroller is an Xiao Seeed Studio ESP32-C3 with Bluetooth compabilites.
              The Microcontroller is chosen as it can charge a LiPo-battery and work wirelessly.
    The code will use the BLEMouse library as it is very simple to use will send scroll ticks instead of continous scrolls.
  
              To get the scroll wheel to work the best, you can change the scroll distance in Windows 11.
              Settings > Bluetooth & devices > Mouse > Scroll wheel > Choose how many lines to scroll each time.


                      It uses an AS5600 Magnetic Rotary Encoder to detect the rotation of a knob.


                      The following code is mostly AI-generated using ChatGPT 5.4
              It contains a filter to stop jittering, adaptive polling to save power and acceleration of the scroll.
              
     Unfortunately, I wanted to write this code all on my own, but I really wanted to get this done and start using it on my own.
*/
#include <Arduino.h>
#include <BleMouse.h>
#include <Wire.h>
#include <AS5600.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET -1  // Reset pin # (or -1 if sharing Arduino reset pin)


struct{
  const char* DEVICE_NAME = "Tassadar";
  const char* MANUFACTURER = "Seeed";
}config_Variables;

BleMouse bleMouse(config_Variables.DEVICE_NAME, config_Variables.MANUFACTURER, 100);
AS5600 encoder;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ===================== STATE MACHINE =====================
enum State {
  ADVERTISING,
  CONNECTED_ACTIVE,
  CONNECTED_IDLE
};

State currentState = ADVERTISING;

// ===================== VARIABLES =====================
int32_t prevRaw = 0;
float filteredDelta = 0.0f;
float scrollAccum = 0.0f;
uint32_t lastMotionTime = 0;

// ===================== TUNING =====================

// Movement filtering
const int RAW_DEADZONE = 2;              // ignore tiny AS5600 noise
const float DELTA_FILTER_ALPHA = 0.25f;  // 0..1, lower = smoother
const float STOP_THRESHOLD = 1.2f;       // below this, treat as no real motion

//Channge this to increase/decrease scroll sensitivity, speed and acceleration.
const float BASE_COUNTS_PER_SCROLL = 700.0f;
const float FAST_COUNTS_PER_SCROLL = 220.0f;
const float FAST_DELTA_THRESHOLD = 8.0f;
const float MAX_ACCEL_MULT = 2.0f;

// Timing
const uint32_t IDLE_TIMEOUT_MS = 1500;

// Adaptive polling
const int POLL_MS_ADVERTISING = 80;
const int POLL_MS_IDLE = 25;
const int POLL_MS_ACTIVE_SLOW = 8;
const int POLL_MS_ACTIVE_FAST = 2;

// ===================== HELPERS =====================
int32_t getWrappedDelta(int32_t current, int32_t previous) {
  int32_t delta = current - previous;

  if (delta > 2048) delta -= 4096;
  if (delta < -2048) delta += 4096;

  return delta;
}

float clampFloat(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

float absf(float x) {
  return (x < 0.0f) ? -x : x;
}

int getAdaptivePollDelay(State state, float motionMagnitude) {
  if (state == ADVERTISING) return POLL_MS_ADVERTISING;
  if (state == CONNECTED_IDLE) return POLL_MS_IDLE;

  if (motionMagnitude > FAST_DELTA_THRESHOLD) {
    return POLL_MS_ACTIVE_FAST;
  }
  return POLL_MS_ACTIVE_SLOW;
}

void handleScrolling(float delta) {
  float speed = absf(delta);
  float t = clampFloat(speed / FAST_DELTA_THRESHOLD, 0.0f, 1.0f);

  float accel = 1.0f + t * (MAX_ACCEL_MULT - 1.0f);
  scrollAccum += delta * accel;

  float countsPerScroll =
      BASE_COUNTS_PER_SCROLL +
      t * (FAST_COUNTS_PER_SCROLL - BASE_COUNTS_PER_SCROLL);

  while (scrollAccum >= countsPerScroll) {
    bleMouse.move(0, 0, -1);
    scrollAccum -= countsPerScroll;
  }

  while (scrollAccum <= -countsPerScroll) {
    bleMouse.move(0, 0, 1);
    scrollAccum += countsPerScroll;
  }
}


void screen_update(uint32_t raw) {
  // 1. Keep track of the smoothed angle across function calls
  static float smoothedAngle = -1.0f;
  
  // Initialize it on the very first run
  if (smoothedAngle < 0) {
    smoothedAngle = raw;
  }

  // 2. Calculate the shortest distance between current smoothed and new raw
  // This safely handles the jump between 4095 and 0
  int32_t diff = raw - (int32_t)smoothedAngle;
  if (diff > 2048) diff -= 4096;
  if (diff < -2048) diff += 4096;

  // 3. Apply the Low-Pass Filter
  // Change 0.1f to adjust smoothing (0.05 = very smooth/slower, 0.5 = fast/jittery)(EMA Filter)
  const float DISPLAY_FILTER_ALPHA = 0.1f; 
  smoothedAngle += diff * DISPLAY_FILTER_ALPHA;

  // 4. Wrap it back nicely within 0-4095 bounds
  if (smoothedAngle >= 4096.0f) smoothedAngle -= 4096.0f;
  if (smoothedAngle < 0.0f) smoothedAngle += 4096.0f;

  // 5. Convert 0-4095 to 0-360 Degrees for human readability
  int degrees = (int)((smoothedAngle / 4096.0f) * 360.0f);

  // ===================== DRAWING =====================
  
  display.clearDisplay();

  // Print the Text
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 12); // Center-ish vertically on the left
  display.print("Angle: ");
  display.print(degrees);
  display.print((char)247); // Prints a little degree symbol '°'

  // Draw a visual "Knob" on the right side of the screen
  int cx = SCREEN_WIDTH - 25; // X Center of the circle
  int cy = SCREEN_HEIGHT / 2; // Y Center of the circle
  int r = 12;                 // Radius

  // Calculate where the line should point (subtract 90 deg so 0 is facing up)
  float rad = (degrees - 90) * PI / 180.0;
  int lx = cx + r * cos(rad);
  int ly = cy + r * sin(rad);

  // Draw the outer circle and the indicator line
  display.drawCircle(cx, cy, r, SSD1306_WHITE);
  display.drawLine(cx, cy, lx, ly, SSD1306_WHITE);

  display.display(); // Push it to the screen
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Wire.begin();
  encoder.begin();
  bleMouse.begin();
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();

  delay(200);

  if (encoder.magnetDetected()) {
    Serial.println("Magnet detected!");
  } else {
    Serial.println("No magnet detected!");
  }

  prevRaw = encoder.rawAngle();
  lastMotionTime = millis();

  Serial.println("Ready.");
}

// ===================== LOOP =====================
void loop() {


  // ---------- DISCONNECTED ----------
  if (!bleMouse.isConnected()) {
    if (currentState != ADVERTISING) {
      currentState = ADVERTISING;
      scrollAccum = 0.0f;
      filteredDelta = 0.0f;
      Serial.println("State -> ADVERTISING");
    }

    delay(POLL_MS_ADVERTISING);
    return;
  }



  // ---------- READ SENSOR ----------
  int32_t currRaw = encoder.rawAngle();
  int32_t rawDelta = getWrappedDelta(currRaw, prevRaw);
  prevRaw = currRaw;

  // Hard jitter gate
  if (abs(rawDelta) <= RAW_DEADZONE) {
    rawDelta = 0;
  }

  // Low-pass filter for smoother motion
  filteredDelta =
      DELTA_FILTER_ALPHA * rawDelta +
      (1.0f - DELTA_FILTER_ALPHA) * filteredDelta;

  float motionMag = absf(filteredDelta);
  bool moving = motionMag >= STOP_THRESHOLD;




  // ---------- STATE TRANSITIONS ----------
  switch (currentState) {
    case ADVERTISING:
      currentState = CONNECTED_IDLE;
      lastMotionTime = millis();
      Serial.println("State -> CONNECTED_IDLE");
      break;

      //Active meaning it will refresh constantly
    case CONNECTED_ACTIVE:
      if (moving) {
        lastMotionTime = millis();
      } else if (millis() - lastMotionTime > IDLE_TIMEOUT_MS) {
        display.clearDisplay();
        display.setCursor(SCREEN_WIDTH/2 - 3, SCREEN_HEIGHT/2);
        display.print("Idle...");
        display.display();
        currentState = CONNECTED_IDLE;
        
      }
      break;
    case CONNECTED_IDLE:
      if (moving) {
        currentState = CONNECTED_ACTIVE;
        lastMotionTime = millis();
        
      }
      break;
  }




  // ---------- STATE BEHAVIOR ----------
  switch (currentState) {
    case CONNECTED_ACTIVE:
      handleScrolling(filteredDelta);
      screen_update(encoder.rawAngle());
      break;

    case CONNECTED_IDLE:
      // bleed off residual filtered motion slowly
      filteredDelta *= 0.85f;
      break;

    case ADVERTISING:
    default:
      break;
  }

  delay(getAdaptivePollDelay(currentState, motionMag));
}