# AI Smart Water Dispenser with Hydration Reminder & Leak Detection

An ESP32-based smart water dispenser concept that automates dispensing, measures dispensed water, provides hydration reminders, detects leaks, and gives local/IoT notifications.

## Features
- Automatic dispensing using IR sensor or push button
- Flow-sensor based water-volume measurement
- Daily intake tracking
- Hydration reminder
- Leak detection with pump lockout
- OLED and buzzer feedback
- Blynk IoT monitoring and notifications
- Timeout and overflow safety logic

## Hardware
- ESP32
- 12V DC water pump
- Relay module
- Flow sensor
- IR sensor or push button
- Water leak sensor
- 0.96-inch I2C OLED
- Buzzer
- LM2596 buck converter
- 12V DC adapter

## Project Flow
Sense -> Control -> Notify -> Record -> Safety

## Pin Configuration
Edit the constants in `src/AI_Smart_Water_Dispenser.ino` to match your actual wiring.

## Important
The flow-sensor calibration constant, Blynk credentials, Wi-Fi credentials, and pin assignments must be configured and tested with the actual hardware before use.

## Suggested Repository Structure
- `src/` - ESP32 Arduino source
- `docs/` - project documentation
- `hardware/` - wiring/pin notes
- `LICENSE` - project license
