# WiFiCompass-Release-1.0

Transmit a compass heading over WiFi in NMEA 0183 format. 

An Arduino C++ project.
Code runs on an ESP32 with the arduino core. Hooked up to a CMPS-14 compass module. 

Uses RTOS tasks 

This is the first production version. Released April 2025.
Repository also includes webserver code for calibration of the CMPS-14. Including a single page web app (HTML and javasctipt)

**Installation**
Connect ESP32 to USB port. Build project in Arduino IDE and upload. Connect PC to WiFi acces point SSID "WiFi Compass v1.0".
In a web browser enter "192.168.4.1/$upload.htm". Drag and drop file "sensorCalibration.html". This will store the file on the ESP32 Flash partition (SPIFFS). Enter "192.168.4.1/sensorCalibration.html" to check that the upload has worked. 
