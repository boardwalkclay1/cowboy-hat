# Cowboy Hat OS – ESP32‑C3 Wiring Map (Final)

This file defines the exact wiring for all components used in Cowboy Hat OS (Go Time Edition)
when running on an ESP32‑C3 microcontroller.

---

## 1. OLED Display (SSD1306 I2C)
Address: 0x3C  
Voltage: 3.3V

| OLED Pin | ESP32‑C3 Pin |
|----------|--------------|
| VCC      | 3.3V         |
| GND      | GND          |
| SDA      | GPIO 8       |
| SCL      | GPIO 9       |

OLED and IMU share the same I2C bus.

---

## 2. Vibration Motors (4x)
Motors MUST use a transistor + diode.  
Never connect motors directly to GPIO.

### Motor Driver Wiring
ESP32‑C3 GPIO → 100Ω resistor → NPN base  
NPN emitter → GND  
NPN collector → Motor –  
Motor + → 5V (or 3.3V motor)  
Diode across motor (reverse polarity)

### Motor Pin Assignments
| Motor | ESP32‑C3 Pin |
|--------|--------------|
| Front  | GPIO 4       |
| Back   | GPIO 5       |
| Left   | GPIO 6       |
| Right  | GPIO 7       |

---

## 3. IMU (MPU6050 / MPU6886 / BNO055 / etc.)
I2C device — shares bus with OLED.

| IMU Pin | ESP32‑C3 Pin |
|---------|--------------|
| VCC     | 3.3V         |
| GND     | GND          |
| SDA     | GPIO 8       |
| SCL     | GPIO 9       |

---

## 4. Radar Sensors

### Option A — RCWL‑0516 (Simple Motion Radar)
| RCWL Pin | ESP32‑C3 Pin |
|----------|--------------|
| VIN      | 5V           |
| GND      | GND          |
| OUT      | GPIO 10      |

### Option B — LD2410 (mmWave Radar)
| LD2410 Pin | ESP32‑C3 Pin |
|------------|--------------|
| VIN        | 5V           |
| GND        | GND          |
| TX → ESP RX| GPIO 20      |
| RX → ESP TX| GPIO 21      |

---

## 5. Power System
- LiPo battery → 5V boost → ESP32‑C3 5V pin  
- Motors powered from 5V  
- ESP32‑C3 onboard regulator provides 3.3V logic  
- ALL grounds must be connected together

---

## 6. Full Wiring Summary Table

| Component     | Pin | ESP32‑C3 |
|---------------|-----|----------|
| OLED SDA      | SDA | GPIO 8   |
| OLED SCL      | SCL | GPIO 9   |
| IMU SDA       | SDA | GPIO 8   |
| IMU SCL       | SCL | GPIO 9   |
| Motor Front   | SIG | GPIO 4   |
| Motor Back    | SIG | GPIO 5   |
| Motor Left    | SIG | GPIO 6   |
| Motor Right   | SIG | GPIO 7   |
| Radar OUT     | OUT | GPIO 10  |
| Radar TX      | TX  | GPIO 20  |
| Radar RX      | RX  | GPIO 21  |
| Power         | VCC | 3.3V/5V  |
| Ground        | GND | GND      |

---
