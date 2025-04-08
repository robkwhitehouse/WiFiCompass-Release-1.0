
/*
 - Electronic Boat Compass
 - RK Whitehouse April 2025
 - This version uses FreeRTOS for task scheduling and adds a http server for calibration
 
*/

/* Functionality
* The CMPS14 sensor module provides a tilt compensated
 * compass bearing. This software coverts the CMPS 14 output to NMEA 0183 "HDM" message format
 * and transmits this over WiFi
 * The ESP32 provides a WiFi Access Point. SSID = WiFiCompass (no password)
 * This has a Telnet server On port 23- the HDM messages are transmitted 5 times per second
 * There is also an http server running on port 80 - The default (root) node serves a single page web-app 
 * point your browser at 192.168.4.1/sensorCalibration.html to run the calibration web app
 * that makes callbacks to a REST API to perform calibration and configuration 
 *
 */

/* Software design
 *  
 *  There are two sets of methods 
 *  
 *  1. Foreground tasks - i.e. user interface tasks
 *  2. Background tasks - run at specific intervals
 *
 *  The background tasks are run by RTOS Tasks without any
 *  direct user interaction. The RTOS scheduler is pre-emptive so any shared variables probably need to be protected by semaphores
 *  
 *  The foreground tasks are called from the main loop()
 *  
 *
 * Communication between background and foreground tasks is via a set of global static
 * objects and variables
 * 
 */

/*
 * Hardware design
 * 
 * Runs on an ESP-32 module with a CMPS14 compass sensor module attached via
 * I2C. An RGB LED provides status feedback and is connected on pins 4,5 & 6 
 * 
 * This version has been tested on an ESP-32 C3 Super-mini but it should work with any ESP32 module
 */


/* Imported libraries */
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h> 
#include <WiFi.h>
#include <WiFiAP.h>
#include <FS.h>
#include <LittleFS.h>
#include <WebServer.h>

/* Local libs */
#include "Cmps14.h"
#include "NMEA.hpp"
#include "calibration.h"
#include "webCalibration.h"
#include "TricolourLED.hpp"


#define VERSION "1.0"
#define TELNET_PORT 23       //Compass heading is output on this port
#define CONFIG_PORT 1024
#define WWW_PORT 80
#define MAX_TELNET_CLIENTS 4
#define HOSTNAME "WiFiCompass"
#define FSYS LittleFS   //Or could be SPIFFS


//TriColour LED output pins
#define RED_PIN 4
#define GREEN_PIN 5
#define BLUE_PIN 6

//I2C pins - not the default - you could chnge these if wanted
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9

#define CMPS14_SAMPLERATE_DELAY_MS 100 //Compass chip is sampled 10 times per second

//Pre-Declare background RTOS task methods
void output();
void updateHeading();
void turnOff();

/* Declare Global Singleton Objects */

volatile bool debug = false;

// Non-volatile settings - will be restored on power-up
Preferences settings;

//Handles for the various RTOS tasks. Will be populated later
TaskHandle_t outputTask, updateHeadingTask;


unsigned short  boatHeading = 0; //Heading seen on boat compass, calculated from sensorHeading + boatCompassOffset
unsigned short sensorHeading = 0; //Heading read from CMPS14
byte calibration = 0; //CMPS14 calibration level

//Set up some storage for the NMEA output messages
HSCmessage hsc;
HDMmessage hdm;

//Create WiFi network object pointers
const char *ssid = "WiFiCompass v1.0";  //WiFi network name
WiFiServer *telnetServer = NULL;
WiFiServer configServer(CONFIG_PORT);
WiFiClient **telnetClients = {NULL}; //Array of WiFi Clients
WiFiClient configClient;  //Configuration via Telnet - redundant
WebServer webServer(WWW_PORT);

// Create TriColour LED object
TricolourLED myLED(RED_PIN,GREEN_PIN,BLUE_PIN,TricolourLED::COMMON_CATHODE);

extern int16_t compassCard[]; //declared in calibration.h. has compass card offsets for each degree

void setup() {
  
  //setup I2C Bus
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
//  Wire.begin();
  //Setup RGB LED and associated blinker
  myLED.begin();
  myLED.setColour(TricolourLED::AMBER);
  myLED.setState(TricolourLED::ON);

  disableCalibration();  //Stop the CMPS14 from automatic recalibrating
  Serial.begin(115200);
  delay(1000);

  
  // Setup filesystem
//  if (!SPIFFS.begin(true)) 
  if (!FSYS.begin(true)) // "true" - format FSYS if necessary"
    Serial.println("Mounting File System failed");

  Serial.print("WiFiCompass. Version ");
  Serial.println(VERSION);

  calibrationBegin();

  settings.begin("compass",false); //Open (or create) settings namespace "compass" in read-write mode
  if ( settings.isKey("compassCard") ) {//We have an existing compassCard in NVRAM
    Serial.println("Loading settings from flash memory");
    settings.getBytes("compassCard",&compassCard,sizeof(compassCard));
  } else Serial.println("No settings found in flash");
  
  
  //Startup the Wifi access point
  WiFi.softAP(ssid);
  WiFi.setHostname(HOSTNAME);
  telnetClients = new WiFiClient*[4];
  for(int i = 0; i < 4; i++)
  {
    telnetClients[i] = NULL;
  }
  //This server outputs the NMEA messages
  telnetServer = new WiFiServer(TELNET_PORT);
  telnetServer->begin();

  //This server is used for config, calibration  & debug
  
 // configServer.begin();

  webServerSetup(); //Setup the webserver -used for calibration
 
  IPAddress myAddr = WiFi.softAPIP();
  Serial.print("IP Address =");
  Serial.println(myAddr);

  delay(1000);

  //Start the RTOS background tasks
  xTaskCreate(output, "Output", 4000, NULL, 1, &outputTask);
  xTaskCreate(updateHeading, "updateHDG", 4000, NULL, 1, &updateHeadingTask);

  myLED.setColour(TricolourLED::GREEN);

}

unsigned loopCounter;
long loopStart, totalLoopTime=0;

void loop() {
  struct CMPS14_calibration cal;
  
  loopStart = micros();

  WiFiClient tempClient = telnetServer->available();   // listen for incoming clients

  if (tempClient) {                             // if you get a client,
    Serial.println("New NMEA client.");           // print a message out the serial port
    for (int i=0; i<MAX_TELNET_CLIENTS; i++ ) {   //Add it to the Client array
       if ( telnetClients[i] == NULL ) {
          WiFiClient* client = new WiFiClient(tempClient);
          telnetClients[i] = client;
          break;
       }
    }
  }
  //Configuration can also be done via Telnet (different port)
  //But this method is now deprecated - please use a web browser
  //configClient = configServer.available();
  //if (configClient) {
  //  if (configClient.connected()) {
  //    Serial.println("Config Client detected.");
  //    calibrationMenu();
  //  }
  //}

  webServer.handleClient(); //handle incoming http requests (calibration etc.)
  
  
  //Reflect sensor status in the LED
  
   //Check if sensor is OK
  if (sensorHeading == 0 ) { //probably a fault
      myLED.setColour(TricolourLED::RED);
      myLED.setState(TricolourLED::BLINKING);
  } else {  
  //Check if sensor calibration is OK
    cal = (CMPS14_calibration)calibration;
      if (cal.mag < 2) {
        myLED.setColour(TricolourLED::AMBER);
        myLED.setState(TricolourLED::BLINKING);
      } 
      else {
     //Assume all OK - set LED to GREEN
        myLED.setColour(TricolourLED::GREEN);
        myLED.setState(TricolourLED::ON);
      }
  } 
 
//Check if debug mode required
  if(Serial.available()) {
    int c = Serial.read();
    if ( c == 'd') {
      debug = !debug;
      Serial.println("Debugging toggled");
    }
    else debug = false;
  }

  totalLoopTime += (micros() - loopStart);
  loopCounter++;
}

//Definition of background RTOS tasks

//Output the heading as an NMEA message over WiFi (RTOS Task)
void output(void * pvParameters) {
  char buff[128];
  TickType_t xLastWakeTime;
  const TickType_t xPeriod = 200; //Run every 200ms

  // Initialise the xLastWakeTime variable with the current time.
  xLastWakeTime = xTaskGetTickCount ();
  
  for (;;) {
     if (Serial && debug) {
       sprintf(buff, "Sensor: %03d deg. Boat: %03d deg.\n", sensorHeading, boatHeading);
       Serial.print(buff);  
     }
  
     //Update the NMEA message with the current boat heading 
     hdm.update(boatHeading);
     //This is the main business - transmit the heading as an NMEA message over Telnet (WiFi) to anybody that is interested   
     for ( int i=0; i<MAX_TELNET_CLIENTS; i++ ) {
       if ( telnetClients[i] != NULL ) 
         telnetClients[i]->println(hdm.msgString);
     }
     vTaskDelayUntil( &xLastWakeTime, xPeriod );
  }
}


//Update the heading from the CMPS14 (RTOS task)
void updateHeading(void * pvParameters) {         
  TickType_t xLastWakeTime;
  const TickType_t xPeriod = 100; //Run every 100ms

  // Initialise the xLastWakeTime variable with the current time.
  xLastWakeTime = xTaskGetTickCount ();
  
  for (;;) {
    //get the raw CMPS14 output
    sensorHeading = getBearing();

    //Apply compass card offset
    boatHeading = MOD360(sensorHeading - compassCard[sensorHeading]);

    calibration = getCalibration();

    vTaskDelayUntil( &xLastWakeTime, xPeriod );
  }
}  


