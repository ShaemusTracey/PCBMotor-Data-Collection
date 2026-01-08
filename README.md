# PCBMotor TWUM Position Data Collector

High-speed encoder data acquisition system for the 30mm PCBMotor Traveling Wave Ultrasonic Motor (TWUM), implemented on ESP32 with dual-core processing and queue-based buffering.

## Overview

This project collects positional data from the PCBMotor TWUM Driver's encoder sensor at microsecond intervals. The ESP32's dual-core architecture separates motor control and data collection (Core 1) from data transmission (Core 0) for reliable real-time sampling. Supports both interactive serial commands and autonomous PWM operation modes.

### Features

- Dual-core architecture for control and data transmission separation
- Configurable sample period (default: 1000μs)
- Automatic motor alignment to sensor pulse edge
- Queue-based buffering prevents data loss
- Two operation modes: Serial command or PWM hardware control

## Pin Configuration

| Function | GPIO Pin |
|----------|----------|
| Sensor Input | 4 |
| Serial RX / PWM Output | 16 |
| Serial TX / Direction | 17 |

## Operation Modes

### Serial Mode
Interactive control through Serial Monitor. Motor responds to commands sent at 19200 baud, collects position data during operation, and returns to ready state after motion stops.

**To enable**: Set `serialCom = true`

### PWM Mode
Autonomous operation using hardware PWM. Motor runs for a specified duration at a set duty cycle, then halts. Requires reset to run again.

**To enable**: Set `serialCom = false`

## Data Collection

Position data is sampled at the configured rate and transmitted via Serial Monitor:

- Samples collected at `Ts` microsecond intervals
- 32 sensor readings buffered before transmission
- Collection stops when readings stabilize (4 identical buffers)
- Binary data output for efficient transmission