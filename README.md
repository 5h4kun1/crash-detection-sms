# Crash Detection Alert System with Telegram Notification using ESP32

This project uses an ESP32 microcontroller to detect crashes or sudden movements and immediately send an alert message via Telegram Bot to a predefined user. It leverages a vibration or accelerometer sensor for detection and integrates with the Telegram Bot API for real-time alerts.

## 🚀 Features

- Detects sudden shocks or impacts (indicating a crash).
- Sends real-time messages to Telegram using a bot.
- Connects to Wi-Fi for internet access.
- Minimal power and compact setup.
- Designed to work in vehicles or personal safety systems.

## 🔧 Hardware Requirements

- ESP32 Dev Board
- Vibration/Accelerometer Sensor (like SW-420 or MPU6050)
- Power source (USB or battery)
- Wi-Fi access

## 🧠 How It Works

1. Sensor detects a sudden vibration or impact.
2. ESP32 checks the status via digitalRead.
3. If a crash is detected:
   - Sends a message through a Telegram bot using HTTP POST.
   - Waits before allowing another message (to avoid spamming).

## 📲 Telegram Setup

1. Search `@BotFather` in Telegram and create a new bot.
2. Get the **bot token**.
3. Start the bot using any Telegram account.
4. Use `https://api.telegram.org/bot<your_token>/getUpdates` to find your **chat ID**.
5. Replace the placeholders in the code:
   - `const String BOTtoken = "YOUR_BOT_TOKEN";`
   - `const String chat_id = "YOUR_CHAT_ID";`

## 📡 Network Setup

Update the following with your Wi-Fi credentials:

```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

## 💻 How to Use

1. Flash the code to your ESP32 using Arduino IDE.
2. Connect the sensor to the ESP32 (usually on a digital pin like D14).
3. Power the device and test by shaking or tapping the sensor.
4. Check your Telegram for crash notifications.

## 📂 Code Overview

- `setup()` handles Wi-Fi connection and pin setup.
- `loop()` continuously monitors the sensor.
- `sendTelegramMessage()` handles HTTP communication with Telegram API.

## 🛡️ Safety Note

This is a **DIY prototype** for learning and basic detection. Not meant for certified safety-critical applications.

## 📃 License

 feel free to modify and use as needed.
