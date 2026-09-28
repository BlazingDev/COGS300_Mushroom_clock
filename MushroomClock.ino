/*

  Analog input, analog output, serial output
  Reads an analog input pin, maps the result to a range from 0 to 255 and uses
  the result to set the pulse width modulation (PWM) of an output pin.

  Also prints the results to the Serial Monitor.

  The circuit:

  - potentiometer connected to analog pin 0.
    Center pin of the potentiometer goes to the analog pin.
    side pins of the potentiometer go to +5V and ground

  - LED connected from digital pin 9 to ground

  created 29 Dec. 2008
  modified 9 Apr 2024

  by Tom Igoe

  This example code is in the public domain.

  https://www.arduino.cc/en/Tutorial/AnalogInOutSerial

*/
#include "ArduinoGraphics.h"  // must come before the matrix library
#include "Arduino_LED_Matrix.h"

#include "WiFiS3.h"
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "RTC.h"

// These constants won't change. They're used to give names to the pins used:

// Enter your network credentials here
char ssid[] = "TELUS1211";     // your network name
char pass[] = "kGFU5842TkU5C5kR"; // your network password
int wifiStatus = WL_IDLE_STATUS;


WiFiUDP Udp; // A UDP instance to let us send and receive packets over UDP
NTPClient timeClient(Udp);

const int UTC_OFFSET_HOURS = -7;   // Vancouver: -7 in summer (PDT), -8 in winter (PST)

const int analogInPin = A0;  // Analog input pin that the potentiometer is attached to

const int analogOutPin = 9;  // Analog output pin that the LED is attached to

const int buttonPin = 2; // AM PM switch button


const int STEP = 5;
const int STEPS = 720 / 5;

int sensorValue = 0;  // value read from the pot
int outputValue = 0;  // value output to the PWM (analog out)
int analogRange = 1023;
int ledRange = 255;
int lastPotVal = -100;

// Button parameters
bool isPm = false;
int lastState = HIGH;               // using INPUT_PULLUP, so HIGH = not pressed
unsigned long lastChangeTime = 0;

int alarmMinutes = 0;

const unsigned long BRIGHTEN_TIME_MS = 1UL * 10 * 1000; // 1 minute
bool brightening = false;
unsigned long timeSinceWakeUp = 0; 
char lastShown[9] = "";


ArduinoLEDMatrix matrix;

void syncTimeFromNTP() {
  timeClient.begin();

  // The first request sometimes fails, so keep trying until it works
  while (!timeClient.forceUpdate()) {
    Serial.println("NTP failed, retrying...");
    delay(1000);
  }

  unsigned long localTime = timeClient.getEpochTime() + (UTC_OFFSET_HOURS * 3600L);
  RTCTime t(localTime);
  RTC.setTime(t);
  Serial.println("Clock set from NTP");
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

void connectToWiFi(){
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  // attempt to connect to WiFi network:
  while (wifiStatus != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    wifiStatus = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(10000);
  }

  Serial.println("Connected to WiFi");
  printWifiStatus();
}

// For the display: 12-hour format
int displayHour()   { int h = alarmMinutes / 60; return (h == 0) ? 12 : h; } // 0 -> 12
int displayMinute() { return alarmMinutes % 60; }

// For comparing with the current time: minutes since midnight (0..1439)
int alarmMinutesOfDay() {
  return alarmMinutes + (isPm ? 12 * 60 : 0);
}

void updateAlarmFromPot() {
  int potVal = analogRead(analogInPin);
  

  // Deadband: ignore tiny wobbles so the display doesn't flicker
  if (abs(potVal - lastPotVal) < 4) return;
  lastPotVal = potVal;

  // Use 1024 (not 1023) so every step gets an equal-width slice
  int step = map(potVal, 0, 1024, 0, STEPS);   // 0..143
  alarmMinutes = step * STEP;           // 0..715
}



void writeTextToScreen(const char* text) {
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.textScrollSpeed(200);  // lower = faster
  matrix.textFont(Font_4x6);
  matrix.beginText(1, 1, 0xFFFFFF);
  matrix.println(text);
  matrix.endText(SCROLL_LEFT);  // use endText() with no argument for static text 
  // TO-DO: BLOCKS loop so requires holding the am pm button to switch time will have to fix in future
  matrix.endDraw();
}

void handleBtnPress() {
  int state = digitalRead(buttonPin);
  // Serial.println("btn pressed? = " + state);

  if (state != lastState && millis() - lastChangeTime > 50) {
    lastChangeTime = millis();
    if (state == LOW) {             // just pressed (not released)
      isPm = !isPm;
    }
    lastState = state;
  }
}

bool alarmFiredToday = false;
unsigned long lastTimeCheck = 0;

void checkAlarm() {
  if (millis() - lastTimeCheck < 1000) return;   // only check once per second
  lastTimeCheck = millis();


  RTCTime now;
  RTC.getTime(now);

  int nowMinutes = now.getHour() * 60 + now.getMinutes();
  int alarm = alarmMinutesOfDay();
  Serial.println(now);
  Serial.println(nowMinutes);
  if (nowMinutes == alarm && !alarmFiredToday) {
    alarmFiredToday = true;
    startWakeUp();                 // begin the LED fade
  }

  if (nowMinutes != alarm) {
    alarmFiredToday = false;       // reset once that minute has passed
  }
}

void ledBrighten() {
  if(!brightening) return;

  // Now - initiation
  unsigned long timeElapsed = millis() - timeSinceWakeUp;
  
  if(timeElapsed >= BRIGHTEN_TIME_MS) {
    analogWrite(analogOutPin, ledRange);
    brightening = false;
    return;
  }

  float brightnessPercentage = (float)timeElapsed/BRIGHTEN_TIME_MS;
  int brightnessOut = brightnessPercentage * brightnessPercentage * ledRange;
  analogWrite(analogOutPin, brightnessOut);


}

void startWakeUp() {
  brightening = true;
  timeSinceWakeUp = millis();
}

// ===================== DEMO / OFFLINE FALLBACK =====================

const bool FORCE_MANUAL_TIME = false;  // true = skip WiFi entirely and use the manual time
const int  MAX_WIFI_ATTEMPTS = 2;      // each attempt waits ~10 seconds
const int  MAX_NTP_ATTEMPTS  = 5;

// Manual time used if WiFi/NTP fails (local time, 24-hour clock)
const int       MANUAL_YEAR   = 2026;
const Month     MANUAL_MONTH  = Month::SEPTEMBER;
const int       MANUAL_DAY    = 28;
const DayOfWeek MANUAL_DOW    = DayOfWeek::MONDAY;
const int       MANUAL_HOUR   = 7;
const int       MANUAL_MINUTE = 28;

// Same as connectToWiFi(), but gives up after a few tries instead of looping forever
bool tryConnectToWiFi() {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    return false;
  }

  for (int i = 0; i < MAX_WIFI_ATTEMPTS && wifiStatus != WL_CONNECTED; i++) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    wifiStatus = WiFi.begin(ssid, pass);
    delay(10000);
  }

  if (wifiStatus == WL_CONNECTED) {
    Serial.println("Connected to WiFi");
    printWifiStatus();
    return true;
  }

  Serial.println("WiFi failed, falling back to manual time");
  return false;
}

// Same as syncTimeFromNTP(), but gives up after a few tries
bool trySyncTimeFromNTP() {
  timeClient.begin();

  for (int i = 0; i < MAX_NTP_ATTEMPTS; i++) {
    if (timeClient.forceUpdate()) {
      unsigned long localTime = timeClient.getEpochTime() + (UTC_OFFSET_HOURS * 3600L);
      RTCTime t(localTime);
      RTC.setTime(t);
      Serial.println("Clock set from NTP");
      return true;
    }
    Serial.println("NTP failed, retrying...");
    delay(1000);
  }

  Serial.println("NTP failed, falling back to manual time");
  return false;
}

void setManualTime() {
  RTCTime t(MANUAL_DAY, MANUAL_MONTH, MANUAL_YEAR,
            MANUAL_HOUR, MANUAL_MINUTE, 0,
            MANUAL_DOW, SaveLight::SAVING_TIME_INACTIVE);
  RTC.setTime(t);
  Serial.println("Clock set MANUALLY (demo mode)");
}

// Tries WiFi + NTP first; uses the manual time if anything fails
void setupClock() {
  RTC.begin();

  bool timeSet = false;
  if (!FORCE_MANUAL_TIME && tryConnectToWiFi()) {
    timeSet = trySyncTimeFromNTP();
  }

  if (!timeSet) {
    setManualTime();
  }
}

void setup() {
  // initialize serial communications at 9600 bps:
  // Timezone from wifi connection?
  Serial.begin(9600);
  while (!Serial) { ; } // wait for serial port to connect

  // // Invert the way the button is read so that constant power is considered LOW and a break is considered HIGH per convention
  pinMode(buttonPin, INPUT_PULLUP);

  setupClock();
  
  matrix.begin();

}

void loop() {

 
  checkAlarm();
  updateAlarmFromPot();
  handleBtnPress();
  ledBrighten();


  // Write wakeup time onto led screen
  char buf[9];
  sprintf(buf, "%2d:%02d%s", displayHour(), displayMinute(), isPm ? "PM" : "AM");

// in loop(), replace writeTextToScreen(buf); with:
  if (strcmp(buf, lastShown) != 0) {
    strcpy(lastShown, buf);
    writeTextToScreen(buf);
  }

  // read the analog in value:
  sensorValue = analogRead(analogInPin);


  // map it to the range of the analog out:
  outputValue = map(sensorValue, 0, analogRange, 0, ledRange);

}
