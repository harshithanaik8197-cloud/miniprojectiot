# 📱 Smart RFID Inventory Mobile Web App (PWA)

A Progressive Web App (PWA) with **Web Bluetooth API** integration for the **FRDM-MCXW71 Smart Inventory Management System**.

---

## ✨ Features

- **Direct BLE Connection**: Connects to the board (`RFID_INVENTORY`) using the Web Bluetooth API with one tap.
- **Real-Time Stock Cards**: Live inventory counters for Laptop, Keyboard, Mouse, Monitor, Headset, Webcam, and USB-Drive.
- **Instant Scan Feedback**: Animated stock change cards (Green for Stock In, Orange/Red for Stock Out).
- **Sound & Haptics**: Synthesized audio tones and phone vibration feedback upon scanning cards.
- **Live Activity Feed**: Real-time log of scanned products with exact timestamps.
- **Installable PWA**: Can be installed to your phone home screen like a native mobile app.

---

## 🚀 How to Run & Use

### Method 1: On Your PC / Laptop (Quickest Test)

If your laptop has Bluetooth:
1. Run the local server:
   ```powershell
   cd mobile-app
   python serve.py
   ```
2. Open Google Chrome on your PC and go to:
   **`http://localhost:8080`**
3. Click **"Connect BLE"** → select **`RFID_INVENTORY`** from the Bluetooth popup.
4. Tap any RFID card on your RC522 reader to see live updates!

*(You can also use the **"Simulate Scan"** buttons at the bottom of the screen to preview the animations and audio feedback without physical hardware).*

---

### Method 2: On Your Android Phone

1. **Option A (Via Free GitHub Pages — Recommended for phones)**:
   - Push your project repository to GitHub:
     ```powershell
     git add .
     git commit -m "Add smart inventory mobile web app"
     git push origin main
     ```
   - Go to your GitHub repository **Settings → Pages**.
   - Under **Branch**, select `main` and folder `/mobile-app`, then click **Save**.
   - GitHub gives you a free HTTPS URL (e.g., `https://adarshacharya01.github.io/...`).
   - Open that URL in Chrome on your phone, tap **Connect BLE**, and tap **Add to Home screen**!

2. **Option B (Via Local Wi-Fi)**:
   - Run `python serve.py` on your PC.
   - On your phone (connected to same Wi-Fi), open Chrome and go to `http://<YOUR_PC_IP>:8080`.
   - *Note: To enable Web Bluetooth over local HTTP without SSL, visit `chrome://flags/#unsafely-treat-insecure-origin-as-secure` on your phone Chrome and enter your PC IP address.*
