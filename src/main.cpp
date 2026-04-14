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

BleMouse bleMouse("Tassadar", "Seeed", 100);
AS5600 encoder;

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

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Wire.begin();
  encoder.begin();
  bleMouse.begin();

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

    case CONNECTED_ACTIVE:
      if (moving) {
        lastMotionTime = millis();
      } else if (millis() - lastMotionTime > IDLE_TIMEOUT_MS) {
        currentState = CONNECTED_IDLE;
        Serial.println("State -> CONNECTED_IDLE");
      }
      break;

    case CONNECTED_IDLE:
      if (moving) {
        currentState = CONNECTED_ACTIVE;
        lastMotionTime = millis();
        Serial.println("State -> CONNECTED_ACTIVE");
      }
      break;
  }

  // ---------- STATE BEHAVIOR ----------
  switch (currentState) {
    case CONNECTED_ACTIVE:
      handleScrolling(filteredDelta);
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