# IoT-Based Smart Inventory Management System

**FRDM-MCXW71 + RC522 RFID Reader + BLE 5.3 Wireless Telemetry + Web Bluetooth Mobile App**

An automated, real-time inventory tracking and management system powered by the **NXP FRDM-MCXW71** development board (Arm Cortex-M33, BLE 5.3), **MFRC522 RFID reader** (13.56 MHz, SPI), active **buzzer** audio feedback, and dual telemetry interfaces:
1. **Web Bluetooth Mobile PWA** (Runs directly in Chrome on Android / Mac / Windows with zero app install)
2. **Python BLE-to-MQTT Bridge + Node-RED Dashboard**

---

## 🔌 Hardware Connections

### RC522 RFID Reader → FRDM-MCXW71

| RC522 Pin | FRDM-MCXW71 Pin | Header / Arduino Pin | Function / Description |
|-----------|-----------------|----------------------|------------------------|
| **3.3V**  | 3V3             | J3-4                 | Power (**3.3V ONLY**)  |
| **GND**   | GND             | J2-7 / J3-7          | Ground reference       |
| **SDA (CS)** | **PTA18**    | J2-3 / Arduino **D10** | Software Chip Select (Active Low) |
| **SCK**   | **PTB2**        | J2-6 / Arduino **D13** | LPSPI1 Clock           |
| **MOSI**  | **PTB3**        | J2-4 / Arduino **D11** | LPSPI1 Master Out Slave In |
| **MISO**  | **PTB1**        | J2-5 / Arduino **D12** | LPSPI1 Master In Slave Out |
| **RST**   | **PTC4**        | J1-6                 | Hard Reset (Active Low) |
| **IRQ**   | Not connected   | —                    | Optional interrupt     |

### Buzzer & Onboard Protection

| Component | FRDM-MCXW71 Pin | Note |
|-----------|-----------------|------|
| **Buzzer (+)** | **PTC5** via 100Ω | Active buzzer: 1 beep on success, 3 beeps on alert/error |
| **Buzzer (−)** | GND             | Ground |
| **Onboard Flash CS** | **PTB0** & **PTB4** | Deselected (held HIGH) in `MFRC522_Init()` to prevent SPI MISO line contention with onboard MX25R6435F flash |

> ⚠️ **CRITICAL:** Use **3.3V only** for the RC522. Supplying 5V to the RC522 will damage the MCXW71 MCU GPIO inputs.

---

## 🏗️ System Architecture

```
 RFID Cards / Tags (13.56 MHz MIFARE)
                 │ 
                 ▼ SPI (LPSPI1, 1 MHz, Mode 0)
     RC522 Driver (source/mfrc522.c)
                 │ MFRC522_CheckCard() / MFRC522_ReadCardSerial()
                 ▼
     Inventory Engine (source/rfid_inventory.c)
      • Add / Remove auto-toggle on consecutive scans
      • 7 Pre-registered products & stock counters
      • Unknown tag detection & alert
      • Buzzer audio feedback (success: 1 beep, alert: 3 beeps)
                 │ BleApp_SendUartStream()
                 ▼
     NXP BLE 5.3 Radio (bluetooth/profiles/wireless_uart/)
      • Primary Adv: Flags + 128-bit UUID + Shortened Name "RFID" (27B <= 31B)
      • Scan Response: Full Name "RFID_INVENTORY" (16B)
                 │
      ┌──────────┴────────────────────────────────────────┐
      ▼                                                   ▼
[Option 1: Mobile Web App]                     [Option 2: PC / Node-RED]
Chrome on Android / PC                         Python BLE Bridge (`ble_mqtt_bridge.py`)
Web Bluetooth API                              Local MQTT Broker (Mosquitto)
`mobile-app/index.html`                        Node-RED Dashboard (`:1880/ui`)
Real-time Add/Remove logs & stock              Live gauges, graphs, and audit table
```

---

## 🏷️ Pre-Registered Products

The system comes pre-configured with 7 RFID cards/fobs in `source/rfid_inventory.c`:

| Product ID | Product Name | RFID UID (Hex) | Initial Stock | Scan Behavior |
|:---:|:---|:---:|:---:|:---|
| `1001` | **Laptop**     | `2E 6D 9E 04` | 10 | Toggle Add ↔ Remove |
| `1002` | **Keyboard**   | `FA 76 26 02` | 20 | Toggle Add ↔ Remove |
| `1003` | **Mouse**      | `23 8A 9C 04` | 15 | Toggle Add ↔ Remove |
| `1004` | **Monitor**    | `69 84 A0 04` | 8  | Toggle Add ↔ Remove |
| `1005` | **Headset**    | `DA 17 9F 04` | 12 | Toggle Add ↔ Remove |
| `1006` | **Webcam**     | `94 21 4C 06` | 14 | Toggle Add ↔ Remove |
| `1007` | **USB-Drive**   | `93 3E 02 07` | 25 | Toggle Add ↔ Remove |
| — | *Unknown Tag*  | *Any other UID* | — | Alert buzzer (3 beeps), red warning telemetry |

---

## 📡 BLE Protocol & GATT Profile

- **Device Name:** `RFID_INVENTORY` (Shortened: `RFID`)
- **Custom Service UUID:** `01ff0100-ba5e-f4ee-5ca1-eb1e5e4b1ce0`
- **UART Characteristic UUID:** `01ff0101-ba5e-f4ee-5ca1-eb1e5e4b1ce0` (Properties: `NOTIFY`, `WRITE_WITHOUT_RESPONSE`)

### Real-Time Telemetry Messages

| Message Frame | Description | Action Trigger |
|:---|:---|:---|
| `ADD:<Name>:<Stock>\n` | Item added back to inventory | Consecutive scan #1 |
| `REMOVE:<Name>:<Stock>\n` | Item checked out / removed | Consecutive scan #2 |
| `ALERT:UNKNOWN_TAG:<UID>\n` | Unregistered RFID scanned | Unknown card detected |
| `STATUS:<Name>:<Stock>,...\n` | Full inventory table dump | Board Button SW2 or client request |

*Example Live Notification:* `REMOVE:Webcam:14\n`

---

## 📱 Mobile App (Web Bluetooth PWA)

The project includes a ready-to-run mobile app located in [`mobile-app/`](file:///c:/Users/Harshitha/OneDrive/Desktop/IoT-BASED-SMART-INVENTORY-MANAGEMENT-SYSTEM/mobile-app):

### Features:
- **Zero Install:** Runs directly in Google Chrome on Android, Windows, Mac, or Linux.
- **One-Click Connect:** Uses the Web Bluetooth API to connect directly to `RFID_INVENTORY`.
- **Live Inventory Cards:** Shows real-time stock counts with color status indicators.
- **Activity Log Stream:** Displays timestamped transactions (`ADD`, `REMOVE`, `ALERT`).
- **Inventory Refresh:** Pulls full stock status on demand.

### Running Locally:
```powershell
cd mobile-app
python serve.py
```
Open **`http://localhost:8000`** in Google Chrome.

### Hosting on the Web:
- **GitHub Pages:**
  1. Go to repository **Settings** → **Pages**.
  2. Set Source to `Deploy from a branch`, choose branch `main` and folder `/mobile-app` (or configure via GitHub Actions).
- **Render / Netlify / Vercel:**
  Deploy as a **Static Site** with publish directory set to `mobile-app`. (HTTPS is required for Web Bluetooth in remote environments).

---

## 📦 Project Structure

```
miniprojectiot/
├── source/
│   ├── rfid_inventory.c       ← Main inventory logic & BLE streaming
│   ├── rfid_inventory.h
│   ├── mfrc522.c / mfrc522.h  ← Low-level MFRC522 driver with Flash CS fix
│   ├── rc522_drv.c / rc522_drv.h ← Adapter layer for MFRC522
│   ├── app_config.c           ← BLE advertising payload & scan response config
│   ├── gatt_db.h              ← GATT database definition
│   └── ...
├── board/
│   ├── pin_mux.c / pin_mux.h  ← Pin multiplexing and GPIO configuration
│   └── ...
├── mobile-app/
│   ├── index.html             ← Responsive Web Bluetooth Mobile App
│   ├── manifest.json          ← PWA Manifest
│   ├── serve.py               ← Local Python HTTPS/HTTP development server
│   └── README.md              ← Mobile app usage and hosting guide
├── dashboard/
│   ├── ble_mqtt_bridge.py     ← Python: BLE → MQTT bridge
│   ├── node_red_flow.json     ← Node-RED dashboard flow
│   └── DASHBOARD_SETUP.md     ← Desktop dashboard setup guide
└── CMakeLists.txt
```

---

## 🔨 Building & Flashing

### Prerequisites
- **MCUXpresso for VS Code** or MCUXpresso IDE (NXP toolchain)
- **Arm GNU Toolchain** (`arm-none-eabi-gcc` 14.2+)
- **CMake 3.30+** and **Ninja**

### Build Commands:
```powershell
# 1. Configure Debug Preset
cmake --preset Debug

# 2. Compile Firmware
cmake --build --preset Debug

# Target binary created:
# Debug/frdmmcxw71_wireless_uart_bm_light_sensor.elf
```

Flash the board using the MCUXpresso VS Code extension debugger or NXP LinkServer CLI.

---

## 📲 Testing with nRF Connect

1. Install **nRF Connect for Mobile** (Android / iOS).
2. Scan for Bluetooth devices and connect to **`RFID_INVENTORY`**.
3. Open custom service `01ff0100-ba5e-f4ee-5ca1-eb1e5e4b1ce0`.
4. Tap the **triple-arrow icon** on characteristic `01ff0101-...` to **Enable Notifications**.
5. Switch display format to **Text (UTF-8)**.
6. Scan cards on the RC522 reader to observe live `ADD` and `REMOVE` records.

---

*SPDX-License-Identifier: BSD-3-Clause*  
*Copyright 2024 NXP — Modified for IoT Smart Inventory Management System*
