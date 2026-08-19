/*
  AI Smart Water Dispenser
  Hydration Reminder + Leak Detection

  ESP32 Arduino project.
  Configure Wi-Fi/Blynk credentials and sensor calibration before deployment.
*/

#define BLYNK_TEMPLATE_ID "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "Smart Water Dispenser"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_TOKEN"

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BlynkSimpleEsp32.h>

char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";

#define FLOW_PIN       27
#define LEAK_PIN       26
#define TRIGGER_PIN    25
#define RELAY_PIN      33
#define BUZZER_PIN     32

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
BlynkTimer timer;

volatile unsigned long flowPulses = 0;
unsigned long lastDispense = 0;
unsigned long dailyVolumeMl = 0;
bool leakDetected = false;
bool dispensing = false;

const float PULSES_PER_LITER = 450.0; // Calibrate for your flow sensor
const unsigned long MAX_DISPENSE_MS = 15000;
const unsigned long REMINDER_INTERVAL_MS = 2UL * 60UL * 60UL * 1000UL;

void IRAM_ATTR flowISR() {
  flowPulses++;
}

void showOLED(const String &line1, const String &line2, const String &line3 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  display.println(line2);
  display.println(line3);
  display.display();
}

float pulsesToMl(unsigned long pulses) {
  return (pulses * 1000.0) / PULSES_PER_LITER;
}

void stopPump() {
  digitalWrite(RELAY_PIN, LOW);
  dispensing = false;
}

void startDispense() {
  if (leakDetected) {
    showOLED("SAFETY LOCKOUT", "Leak detected!");
    tone(BUZZER_PIN, 1800, 700);
    Blynk.virtualWrite(V3, "LEAK LOCKOUT");
    return;
  }

  flowPulses = 0;
  dispensing = true;
  lastDispense = millis();
  digitalWrite(RELAY_PIN, HIGH);
  showOLED("Dispensing...", "Volume: 0 ml");
}

void finishDispense() {
  stopPump();
  float volume = pulsesToMl(flowPulses);

  // Basic pulse-sanity check.
  if (volume >= 5.0 && volume <= 2000.0) {
    dailyVolumeMl += (unsigned long)volume;
  }

  showOLED("Dispense complete", "Volume: " + String((int)volume) + " ml",
           "Daily: " + String(dailyVolumeMl) + " ml");

  Blynk.virtualWrite(V0, dailyVolumeMl);
  Blynk.virtualWrite(V1, volume);
  Blynk.virtualWrite(V2, millis() / 1000);
}

void checkLeak() {
  // Change HIGH/LOW if your leak sensor is active-low.
  bool currentLeak = digitalRead(LEAK_PIN) == HIGH;

  if (currentLeak && !leakDetected) {
    leakDetected = true;
    stopPump();
    showOLED("WARNING!", "Water leak detected");
    tone(BUZZER_PIN, 2000, 1000);
    Blynk.logEvent("water_leak", "Water leak detected. Pump locked.");
    Blynk.virtualWrite(V3, "LEAK DETECTED");
  }

  if (!currentLeak && leakDetected) {
    leakDetected = false;
    showOLED("Leak cleared", "System ready");
    Blynk.virtualWrite(V3, "NORMAL");
  }
}

void hydrationReminder() {
  if (millis() - lastDispense >= REMINDER_INTERVAL_MS && !leakDetected) {
    tone(BUZZER_PIN, 1200, 500);
    showOLED("Hydration reminder", "Please drink water");
    Blynk.logEvent("hydration_reminder", "Hydration reminder");
    lastDispense = millis();
  }
}

void updateOLED() {
  if (dispensing) {
    float currentMl = pulsesToMl(flowPulses);
    showOLED("Dispensing...", "Now: " + String((int)currentMl) + " ml",
             "Daily: " + String(dailyVolumeMl) + " ml");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(FLOW_PIN, INPUT_PULLUP);
  pinMode(LEAK_PIN, INPUT);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  stopPump();

  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), flowISR, RISING);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED initialization failed.");
  }

  showOLED("AI Smart Water", "Dispenser Starting...");

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  timer.setInterval(1000L, checkLeak);
  timer.setInterval(1000L, updateOLED);
  timer.setInterval(60000L, hydrationReminder);

  lastDispense = millis();
}

void loop() {
  Blynk.run();
  timer.run();

  if (digitalRead(TRIGGER_PIN) == LOW && !dispensing) {
    delay(30); // simple debounce
    if (digitalRead(TRIGGER_PIN) == LOW) {
      startDispense();
      while (digitalRead(TRIGGER_PIN) == LOW) {
        Blynk.run();
        timer.run();
      }
    }
  }

  if (dispensing) {
    if (leakDetected || millis() - lastDispense >= MAX_DISPENSE_MS) {
      finishDispense();
    }
  }
}
