# Smart Entry System using Arduino UNO R4 WiFi

An IoT-based entry and occupancy monitoring system designed to count people entering and leaving a space, monitor occupancy, and display live information through an OLED display and a Wi-Fi dashboard.

## 🚀 Features

* Automatic entry and exit detection using two IR sensors.
* Real-time occupancy counting.
* Maximum capacity monitoring.
* Servo-controlled gate mechanism.
* Buzzer alerts for entry, exit, and full-capacity conditions.
* Built-in LED matrix occupancy display.
* OLED display for local status monitoring.
* Wi-Fi connectivity using the Arduino UNO R4 WiFi.
* Web dashboard for live occupancy monitoring.
* Total entry and exit statistics.
* Gate status and capacity availability indicators.

## 🛠️ Hardware Requirements

| Component           | Purpose                                |
| ------------------- | -------------------------------------- |
| Arduino UNO R4 WiFi | Main controller and Wi-Fi connectivity |
| 2 × IR sensors      | Entry and exit detection               |
| Servo motor         | Gate mechanism                         |
| OLED display        | Local status display                   |
| Buzzer              | Audio alerts                           |
| Green LED           | Capacity available indicator           |
| Red LED             | Capacity full indicator                |
| Jumper wires        | Circuit connections                    |
| Breadboard          | Prototype assembly                     |
| USB power supply    | Controller power                       |

## 💻 Software Requirements

* Arduino IDE
* Arduino UNO R4 WiFi board package
* WiFiS3 library
* Arduino_LED_Matrix library
* Servo library
* Adafruit SSD1306 library
* Adafruit GFX Library

## ⚙️ How It Works

1. Two IR sensors detect the sequence in which a person passes.
2. The controller determines whether the person is entering or exiting.
3. The occupancy counter and entry/exit totals are updated.
4. The servo operates the gate mechanism.
5. The buzzer provides an audio alert.
6. The LED matrix and OLED display show occupancy information.
7. The web dashboard displays live system status over the local Wi-Fi network.

## 🌐 Web Dashboard

The dashboard provides live information about:

* Current occupancy and maximum capacity.
* Available spaces.
* Total entries and exits.
* Gate status.
* Wi-Fi connection status.

## 🔌 Getting Started

1. Install Arduino IDE and the required libraries.
2. Open the main `.ino` sketch from the `Code` folder.
3. Configure your Wi-Fi credentials locally.
4. Select **Arduino UNO R4 WiFi** in the Arduino IDE.
5. Connect the hardware according to the circuit diagram.
6. Compile and upload the sketch.
7. Open Serial Monitor to check the Wi-Fi connection and IP address.
8. Open the displayed IP address in a browser connected to the same network.

**Security:** Never commit your actual Wi-Fi password to a public repository.

## 📌 Project Status

Prototype development. Hardware integration, OLED operation, and Wi-Fi dashboard behavior should be verified with the final published firmware.

## 👨‍💻 Author

Created as an electronics and embedded systems project.

## 🔮 Future Improvements

* Improved dashboard analytics.
* Configurable occupancy limits.
* Historical occupancy data.
* Enhanced system diagnostics.
* Improved enclosure and circuit design.
