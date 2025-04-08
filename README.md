# WiFiCompass-Release-1.0

Transmit a compass heading over WiFi in NMEA 0183 format. 

An Arduino C++ project.
Code runs on an ESP32 with the arduino core. Hooked up to a CMPS-14 compass module over I2C. 

Uses RTOS tasks 

There are many comments in the C++ code explaining how it works

This is the first production version. Released April 2025.
Repository also includes webserver code for calibration of the CMPS-14. Including a single page web app (HTML and javasctipt)

**Installation**
Connect ESP32 to USB port. Build project in Arduino IDE and upload. Connect PC to WiFi acces point SSID "WiFi Compass v1.0".
In a web browser enter "192.168.4.1/$upload.htm". Drag and drop file "sensorCalibration.html". This will store the file on the ESP32 Flash partition (SPIFFS). Enter "192.168.4.1/sensorCalibration.html" to check that the upload has worked. 

**Calibration**
Before use, you WILL need to calibrate the CMPS14 to get accurate headings. Use the calibration web app (see above) this will guide you through the process.

**Usage**
1. Connect to WiFi Access Point SSID "WiFi Compass 1.0"
2. Use a Telnet client e.g. putty or juiceSSH to connect to 192.168.4.1 (port 23)
3. You should see a stream of NMEA 0183 compass heading messages (5 per second)
