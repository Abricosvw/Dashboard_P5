# Dashboard P5 (Westgate Dashboard & VAG Diagnostic Scanner)

An advanced automotive intelligence system powered by the **ESP32-P4** (dual-core RISC-V @ 400MHz, 16MB PSRAM). It features high-speed CAN bus parsing, custom variable-geometry wastegate & blow-off control, LUA scripting runtime, and an active VAG Diagnostic Scan Engine.

---

## Technical Features
- **High-Speed CAN Parsing**: Real-time decoding of engine metrics (RPM, Boost, TPS, Oil/Water Temperatures, Gear, Torque, IAT, etc.) at 500 kbit/s.
- **Wastegate & Blow-off Control (Screen 10)**: Real-time custom control algorithms mimicking Bosch Motronic ME7.1 strategies (LDR and LDUVST) with support for 10-point RPM maps and dynamic TPS-drop / overboost triggers.
- **Active VAG Diagnostic Scanner (Screen 11)**: Async diagnostic client using UDS over ISO-TP to scan for trouble codes (DTCs), display detailed descriptions, and clear fault codes across powertrain control modules.
- **DTC Lookup Engine**: Instant lookups from a lightweight SRAM dictionary for critical fault codes, with dynamic line-by-line seek and retrieve from a text file database on the SD card (`/sdcard/SYSTEM/DB/dtc_codes.txt`) for hundreds of thousands of standard VAG decimal/OBD-II alphanumeric codes.

---

## Screen Directory
1. **Screen 1**: Main ECU Dashboard (Gauges for MAP, Wastegate, TPS, RPM, and Boost).
2. **Screen 2**: Secondary Gauges (Oil Pressure, Oil Temp, Coolant Temp, Fuel Pressure, Battery).
3. **Screen 3**: CAN Bus Sniffer & Terminal.
4. **Screen 4**: ECU Advanced Parameters (Pedal, WG Pos, BOV Solenoid, TCU Torque Request/Actual).
5. **Screen 5**: Engine Limits & Advanced Gauges (IAT, Speed, Trans Temp, AFR, EGT, Knock Retard, Boost Act).
6. **Screen 6**: Device Configuration & Live Lua Script Editor.
7. **Screen 7**: Open Claw AI Assistant Terminal.
8. **Screen 8**: Classic Luxury Sport Dashboard.
9. **Screen 9**: Air-to-Water Intercooler Controls (Pump PWM, fans, target temperature).
10. **Screen 10**: Wastegate & Blow-off Strategy Controller (LDR/LDUVST).
11. **Screen 11**: VAG Active Diagnostic Scanner (Auto-Scan, Read Faults, Clear Faults, Terminal Console).

---

## 🚗 How to Run in the Car

To use the active diagnostic scan features and emulated modules in your VAG vehicle, follow these instructions carefully.

### 1. Physical CAN Bus Connections
The active scanner and the emulated components are designed to communicate on the **Powertrain CAN Bus** (operating at **500 kbit/s**).
- Connect the ESP32's transceiver CAN-TX/RX pins to the Powertrain CAN lines.
- **Tapping points**: 
  - **Option A (Direct OBD-II)**: Pins **6 (CAN High)** and **14 (CAN Low)** on the standard OBD-II diagnostic port.
  - **Option B (Gateway Module)**: Tap directly into the Powertrain bus wiring loom at the Gateway control unit (usually located under the steering column).
- *Note*: Do not connect to the Comfort/Infotainment CAN buses as they run at a different speed (100 kbit/s) and cannot route UDS messages to powertrain modules.

### 2. Gateway Installation List Registration (VAG Coding)
The dashboard emulates **Address 13 (ACC / Auto Distance Regulation)** on standard request/response identifiers (`0x757` request, `0x7C1` response). To prevent Gateway routing problems and ensure clean communications:
1. Connect a diagnostic tool (like **VCDS** or **ODIS**) to the car.
2. Open module **19 - CAN Gateway**.
3. Go to **Installation List**.
4. Check/Enable **13 - Auto Dist. Reg / ACC**.
5. Save coding. The gateway will now treat the dashboard as a registered factory module on the Powertrain bus, avoiding route blockage issues.

### 3. Running Diagnostic Scans
1. Turn the ignition to the **ON** position (or start the engine).
2. Swipe or navigate using bottom arrow buttons to **Screen 11 (VAG Diagnostic Scanner)**.
3. Tap **AUTO SCAN** on the screen.
   - The scanner task will run asynchronously in the background.
   - The progress bar and terminal logs will update in real-time.
   - It will sequentially query Engine, Transmission, ABS Brakes, Airbags, Steering Assist, ACC, and Haldex AWD modules.
   - Fault descriptions will be resolved using the SD card database (`/sdcard/SYSTEM/DB/dtc_codes.txt`).
4. To reset faults, tap **CLEAR FAULTS**.
5. Tap **CLEAR LOGS** to reset the console screen.
