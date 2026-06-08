# Cowboy Hat OS – ESP32‑C3 Wiring Map (Final + Transistor Instructions)

This file defines the exact wiring for all components used in Cowboy Hat OS (Go Time Edition)
when running on an ESP32‑C3 microcontroller.

It includes:
- OLED wiring
- IMU wiring
- Radar wiring
- Motor wiring
- EXACT transistor + resistor instructions
- EXACT parts list
- ASCII diagrams

------------------------------------------------------------
1. OLED Display (SSD1306 I2C)
------------------------------------------------------------

Address: 0x3C
Voltage: 3.3V

OLED Pin   → ESP32‑C3 Pin
--------------------------------
VCC        → 3.3V
GND        → GND
SDA        → GPIO 8
SCL        → GPIO 9

OLED and IMU share the same I2C bus.

------------------------------------------------------------
2. IMU (MPU6050 / MPU6886 / BNO055 / etc.)
------------------------------------------------------------

IMU Pin    → ESP32‑C3 Pin
--------------------------------
VCC        → 3.3V
GND        → GND
SDA        → GPIO 8
SCL        → GPIO 9

------------------------------------------------------------
3. Radar Sensors
------------------------------------------------------------

Option A — RCWL‑0516 (simple motion radar)
RCWL Pin   → ESP32‑C3 Pin
--------------------------------
VIN        → 5V
GND        → GND
OUT        → GPIO 10

Option B — LD2410 (mmWave radar)
LD2410 Pin → ESP32‑C3 Pin
--------------------------------
VIN        → 5V
GND        → GND
TX         → GPIO 20 (ESP RX)
RX         → GPIO 21 (ESP TX)

------------------------------------------------------------
4. Vibration Motors (4x)
------------------------------------------------------------

IMPORTANT:
Motors MUST use a transistor + diode.
Never connect motors directly to GPIO.

Motor Pin Assignments:
Motor        → ESP32‑C3 Pin
--------------------------------
Front        → GPIO 4
Back         → GPIO 5
Left         → GPIO 6
Right        → GPIO 7

------------------------------------------------------------
5. EXACT TRANSISTOR + RESISTOR WIRING
------------------------------------------------------------

Use these parts:

Transistor: NPN 2N2222 or S8050  
Resistor: 100Ω (base resistor)  
Diode: 1N4148 or 1N4007 (flyback diode)

Each motor uses:
- 1 × NPN transistor
- 1 × 100Ω resistor
- 1 × diode

------------------------------------------------------------
6. ASCII Wiring Diagram (Single Motor)
------------------------------------------------------------

                +5V (motor power)
                   |
                   |
                Motor +
                   |
                   |
                Motor -
                   |
                   |
                Collector (C)
                   |
                   |
ESP32‑C3 GPIO --100Ω-- Base (B)
                   |
                   |
                Emitter (E)
                   |
                   |
                  GND

------------------------------------------------------------
7. ASCII Wiring Diagram WITH REQUIRED DIODE
------------------------------------------------------------

                +5V
                 |
                 |
              Motor +
                 |
                 |
              Motor -
                 |--------------------|
                 |                    |
              Collector (C)        | /  Diode (1N4148 / 1N4007)
                 |                |/  
                 |                |\  (reverse polarity)
ESP GPIO --100Ω-- Base (B)        | \
                 |                    |
                 |--------------------|
              Emitter (E)
                 |
                 |
                GND

------------------------------------------------------------
8. What Each Transistor Pin Does
------------------------------------------------------------

Base (B)     = control signal from ESP32‑C3 (through 100Ω resistor)
Collector (C)= motor negative
Emitter (E)  = ground

------------------------------------------------------------
9. Power System
------------------------------------------------------------

- LiPo battery → 5V boost → ESP32‑C3 5V pin
- Motors powered from 5V
- ESP32‑C3 onboard regulator provides 3.3V logic
- ALL grounds must be connected together

------------------------------------------------------------
10. Full Wiring Summary Table
------------------------------------------------------------

Component       Pin     → ESP32‑C3
--------------------------------------------
OLED SDA        SDA     → GPIO 8
OLED SCL        SCL     → GPIO 9
IMU SDA         SDA     → GPIO 8
IMU SCL         SCL     → GPIO 9
Motor Front     SIG     → GPIO 4
Motor Back      SIG     → GPIO 5
Motor Left      SIG     → GPIO 6
Motor Right     SIG     → GPIO 7
Radar OUT       OUT     → GPIO 10
Radar TX        TX      → GPIO 20
Radar RX        RX      → GPIO 21
Power           VCC     → 3.3V / 5V
Ground          GND     → GND

------------------------------------------------------------
END OF FILE
------------------------------------------------------------
