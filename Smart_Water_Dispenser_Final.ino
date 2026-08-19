/*
  SMART WATER DISPENSER - FINAL PROGRAM
  ESP32 DOIT DEVKIT V1 + Blynk IoT

  PIN MAP
  -------
  IR bottle sensor OUT  -> GPIO27
  YF-S201 flow OUT      -> GPIO26
  Rain sensor D0        -> GPIO25
  Leak sensor D0        -> GPIO33
  Relay IN              -> GPIO14
  Buzzer I/O             -> GPIO13
  Push button            -> GPIO12
  OLED SDA               -> GPIO21
  OLED SCL               -> GPIO22

  BLYNK DATastreams
  V0 = Water Dispensed (mL)
  V1 = Flow Rate (L/min)
  V2 = Water Leakage (0/1)
  V3 = Rain Sensor (0/1)
  V4 = Pump Status (0/1)
  V5 = Daily Water (mL)
  V7 = Reset Daily Water
  V8 = Manual Pump

  IMPORTANT:
  - Replace the Blynk/Wi-Fi placeholders below.
  - Keep the pump OFF during initial sensor tests.
  - Pump is 3-6 V: NEVER connect the 12 V adapter directly to it.
  - Use a suitable 5 V supply/buck converter for the pump.
  - If YF-S201 is powered at 5 V, protect its signal before GPIO26
    with a suitable level shifter/voltage divider.
*/

#define BLYNK_TEMPLATE_ID "TMPL30CRe-xhK"
#define BLYNK_TEMPLATE_NAME "Smart Water Dispenser "
#define BLYNK_AUTH_TOKEN    "W19wdW5evNQDmQJqs0-65SdKOxVCBnQO"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

char ssid[] = "Nord_5";
char pass[] = "dmfu7356";

// ---------- Pins ----------
const uint8_t IR_PIN       = 27;
const uint8_t FLOW_PIN     = 26;
const uint8_t RAIN_PIN     = 25;
const uint8_t LEAK_PIN     = 33;
const uint8_t RELAY_PIN    = 14;
const uint8_t BUZZER_PIN   = 13;
const uint8_t BUTTON_PIN   = 12;
const uint8_t OLED_SDA     = 21;
const uint8_t OLED_SCL     = 22;

// ---------- Logic settings ----------
// Change these only if your individual sensor tests showed opposite logic.
const bool IR_ACTIVE_LOW    = true;
const bool RAIN_ACTIVE_LOW  = true;
const bool LEAK_ACTIVE_LOW  = true;

// Most relay modules like yours are LOW-trigger.
const bool RELAY_ACTIVE_LOW = true;

// Common YF-S201 nominal calibration.
// Calibrate later with a measured volume.
const float PULSES_PER_LITRE = 450.0;

// Dispense target for one physical-button request.
const float TARGET_ML = 250.0;

// Stop pump if it runs without receiving flow pulses.
const unsigned long FLOW_TIMEOUT_MS = 4000;

// ---------- OLED ----------
Adafruit_SSD1306 display(128, 64, &Wire, -1);
bool oledOK = false;

// ---------- Blynk ----------
BlynkTimer timer;

// ---------- Flow ----------
volatile unsigned long flowPulses = 0;
unsigned long previousPulses = 0;
unsigned long lastFlowCalculation = 0;
unsigned long lastPulseMillis = 0;

float flowRateLMin = 0.0;
float sessionML = 0.0;
float dailyML = 0.0;

// ---------- Pump ----------
bool pumpRunning = false;
bool manualPump = false;

// ---------- Button ----------
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
unsigned long lastButtonChange = 0;

// ---------- Status ----------
bool reminderActive = false;
unsigned long lastReminder = 0;
const unsigned long REMINDER_INTERVAL_MS = 60UL * 60UL * 1000UL;

// ============================================================
// INTERRUPT
// ============================================================
void IRAM_ATTR flowISR() {
  flowPulses++;
}

// ============================================================
// SENSOR FUNCTIONS
// ============================================================
bool bottleDetected() {
  int s = digitalRead(IR_PIN);
  return IR_ACTIVE_LOW ? (s == LOW) : (s == HIGH);
}

bool rainDetected() {
  int s = digitalRead(RAIN_PIN);
  return RAIN_ACTIVE_LOW ? (s == LOW) : (s == HIGH);
}

bool leakDetected() {
  int s = digitalRead(LEAK_PIN);
  return LEAK_ACTIVE_LOW ? (s == LOW) : (s == HIGH);
}

// ============================================================
// OUTPUT FUNCTIONS
// ============================================================
void setRelay(bool on) {
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(RELAY_PIN, on ? LOW : HIGH);
  } else {
    digitalWrite(RELAY_PIN, on ? HIGH : LOW);
  }
}

void setBuzzer(bool on) {
  digitalWrite(BUZZER_PIN, on ? HIGH : LOW);
}

void beep(unsigned int durationMs = 120) {
  setBuzzer(true);
  delay(durationMs);
  setBuzzer(false);
}

// ============================================================
// OLED
// ============================================================
void showOLED(String l1, String l2 = "", String l3 = "", String l4 = "") {
  if (!oledOK) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println(l1);

  display.setCursor(0, 16);
  display.println(l2);

  display.setCursor(0, 32);
  display.println(l3);

  display.setCursor(0, 48);
  display.println(l4);

  display.display();
}

// ============================================================
// PUMP CONTROL
// ============================================================
void stopPump() {
  setRelay(false);
  pumpRunning = false;
}

bool safetyOK() {
  if (leakDetected()) return false;
  if (rainDetected()) return false;
  if (!bottleDetected()) return false;
  return true;
}

bool startPump() {
  if (leakDetected()) {
    stopPump();
    showOLED("WATER LEAK!", "PUMP LOCKED OFF");
    beep(250);
    return false;
  }

  if (rainDetected()) {
    stopPump();
    showOLED("RAIN/WATER", "SAFETY LOCK", "Pump OFF");
    beep(180);
    return false;
  }

  if (!bottleDetected()) {
    stopPump();
    showOLED("NO BOTTLE", "Place bottle");
    beep(120);
    return false;
  }

  setRelay(true);
  pumpRunning = true;
  lastPulseMillis = millis();

  return true;
}

// ============================================================
// FLOW CALCULATION
// ============================================================
void calculateFlow() {
  unsigned long now = millis();

  if (now - lastFlowCalculation < 1000) return;

  noInterrupts();
  unsigned long currentPulses = flowPulses;
  interrupts();

  unsigned long delta = currentPulses - previousPulses;
  float elapsedSeconds = (now - lastFlowCalculation) / 1000.0;

  if (elapsedSeconds > 0.0) {
    flowRateLMin =
      (delta / PULSES_PER_LITRE) * (60.0 / elapsedSeconds);
  }

  if (pumpRunning && delta > 0) {
    float addedML =
      (delta / PULSES_PER_LITRE) * 1000.0;

    sessionML += addedML;
    dailyML += addedML;
    lastPulseMillis = now;
  }

  previousPulses = currentPulses;
  lastFlowCalculation = now;
}

// ============================================================
// AUTOMATIC PUMP SAFETY
// ============================================================
void controlPump() {
  if (!pumpRunning) return;

  if (leakDetected()) {
    stopPump();
    manualPump = false;
    showOLED("!!! LEAK !!!", "PUMP OFF");
    beep(300);

    if (Blynk.connected()) {
      Blynk.logEvent("water_leak", "Water leakage detected. Pump stopped.");
    }
    return;
  }

  if (rainDetected()) {
    stopPump();
    manualPump = false;
    showOLED("RAIN/WATER", "PUMP OFF");
    beep(200);
    return;
  }

  if (!bottleDetected()) {
    stopPump();
    manualPump = false;
    showOLED("BOTTLE REMOVED", "PUMP OFF");
    beep(120);
    return;
  }

  // Physical-button dispensing stops at the target volume.
  if (!manualPump && sessionML >= TARGET_ML) {
    stopPump();
    showOLED("DISPENSE COMPLETE",
             "250 mL target reached",
             "Daily: " + String(dailyML, 0) + " mL");
    beep(100);
    return;
  }

  // Manual pump is still protected by the no-flow timeout.
  if (millis() - lastPulseMillis > FLOW_TIMEOUT_MS) {
    stopPump();
    manualPump = false;
    showOLED("NO WATER FLOW", "PUMP STOPPED", "Check tank/pipe");
    beep(250);
  }
}

// ============================================================
// PHYSICAL BUTTON
// ============================================================
void readButton() {
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastButtonChange = millis();
  }

  if (millis() - lastButtonChange > 40) {
    if (reading != stableButtonState) {
      stableButtonState = reading;

      if (stableButtonState == LOW) {
        reminderActive = false;

        // Do not start if safety conditions are not satisfied.
        if (!pumpRunning) {
          sessionML = 0.0;

          if (startPump()) {
            manualPump = false;
            showOLED("DISPENSING...",
                     "Target: 250 mL",
                     "Flow: 0.00 L/min");
          }
        } else {
          stopPump();
          manualPump = false;
          beep(100);
        }
      }
    }
  }

  lastButtonReading = reading;
}

// ============================================================
// BLYNK
// ============================================================

// V8 = Manual Pump switch
BLYNK_WRITE(V8) {
  int value = param.asInt();

  if (value == 1) {
    sessionML = 0.0;

    if (startPump()) {
      manualPump = true;
    } else {
      manualPump = false;
      Blynk.virtualWrite(V8, 0);
    }
  } else {
    manualPump = false;
    stopPump();
  }
}

// V7 = Reset daily water
BLYNK_WRITE(V7) {
  if (param.asInt() == 1) {
    dailyML = 0.0;
    sessionML = 0.0;

    Blynk.virtualWrite(V7, 0);
    beep(100);
  }
}

// ============================================================
// SEND DATA TO BLYNK
// ============================================================
void updateBlynkAndOLED() {
  bool bottle = bottleDetected();
  bool rain = rainDetected();
  bool leak = leakDetected();

  if (leak) {
    showOLED("!!! WATER LEAK !!!",
             "Pump: OFF",
             "Leak sensor active");
  }
  else if (rain) {
    showOLED("RAIN/WATER DETECTED",
             "Pump: OFF",
             "Safety lock active");
  }
  else if (pumpRunning) {
    showOLED(manualPump ? "MANUAL PUMP" : "DISPENSING",
             "Flow: " + String(flowRateLMin, 2) + " L/min",
             "Water: " + String(sessionML, 0) + " mL",
             "Target: 250 mL");
  }
  else {
    showOLED("SMART WATER",
             "Bottle: " + String(bottle ? "YES" : "NO"),
             "Daily: " + String(dailyML, 0) + " mL",
             "Pump: OFF");
  }

  if (Blynk.connected()) {
    Blynk.virtualWrite(V0, sessionML);
    Blynk.virtualWrite(V1, flowRateLMin);
    Blynk.virtualWrite(V2, leak ? 1 : 0);
    Blynk.virtualWrite(V3, rain ? 1 : 0);
    Blynk.virtualWrite(V4, pumpRunning ? 1 : 0);
    Blynk.virtualWrite(V5, dailyML);
  }
}

// ============================================================
// HYDRATION REMINDER
// ============================================================
void checkReminder() {
  if (millis() - lastReminder >= REMINDER_INTERVAL_MS) {
    lastReminder = millis();
    reminderActive = true;

    if (!pumpRunning && !leakDetected() && !rainDetected()) {
      beep(200);
      delay(100);
      beep(200);
    }

    if (Blynk.connected()) {
      Blynk.logEvent("hydration_reminder",
                     "Time to drink water!");
    }
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Sensor inputs
  pinMode(IR_PIN, INPUT);
  pinMode(FLOW_PIN, INPUT_PULLUP);
  pinMode(RAIN_PIN, INPUT);
  pinMode(LEAK_PIN, INPUT);

  // Outputs
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Button
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Pump OFF at startup
  setRelay(false);
  setBuzzer(false);

  // OLED
  Wire.begin(OLED_SDA, OLED_SCL);

  oledOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  if (oledOK) {
    showOLED("SMART WATER",
             "Starting...",
             "Pump: OFF");
  }

  // Flow interrupt
  attachInterrupt(
    digitalPinToInterrupt(FLOW_PIN),
    flowISR,
    RISING
  );

  lastFlowCalculation = millis();
  lastReminder = millis();

  Serial.println();
  Serial.println("================================");
  Serial.println("SMART WATER DISPENSER");
  Serial.println("Starting Wi-Fi + Blynk...");
  Serial.println("================================");

  // Connect Wi-Fi and Blynk
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  Serial.println("Blynk connection started.");

  // Timers
  timer.setInterval(1000L, calculateFlow);
  timer.setInterval(500L, controlPump);
  timer.setInterval(2000L, updateBlynkAndOLED);
  timer.setInterval(10000L, checkReminder);
  timer.setInterval(50L, readButton);

  Serial.println("SYSTEM READY");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  Blynk.run();
  timer.run();
}
