

#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>

#include <math.h>

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

#include "homepage.h"
#include "configureLora.h"
#include "configureWifi.h"

#include "statusPage.h"
#include "rebootPage.h"
#include "resetRequestPage.h"
#include "resetResultPage.h"

// General def for adafruit sensor
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Includes for BME680
#include <bme680.h>
#include <Adafruit_BME680.h>
#include <bme680_defs.h>

// Includes for BME280
#include <Adafruit_BME280.h>

// Includes for SPS Sensirion
#include <sps30.h>

#define SEALEVELPRESSURE_HPA (1013.25)


#define LW_DEVADDR 0x260B146F
#define LW_NWKSKEY { 0xD0, 0x26, 0xBF, 0xEF, 0x88, 0x4F, 0x7D, 0xCA, 0x9F, 0x71, 0x28, 0xF1, 0x84, 0x2A, 0x16, 0xF6 }
#define LW_APPSKEY { 0x5F, 0xA5, 0x6B, 0x7C, 0xD6, 0xC1, 0x1F, 0xCF, 0x6D, 0xAE, 0x60, 0xD7, 0x65, 0xC9, 0x56, 0x7C }


#define LW_DATARATE DR_SF12

#define WL_MAC_ADDR_LENGTH 6

#define debugSerial Serial

//Development / production profiles
//#define NO_CONNECTION_PROFILE 1
//#define DEBUG_PROFILE 1
#ifdef DEBUG_PROFILE
  #define NUM_MEASURE_SESSIONS 10
  #define CYCLE_DELAY 2000
  #define SH_DEBUG_PRINTLN(a) debugSerial.println(a)
  #define SH_DEBUG_PRINT(a) debugSerial.print(a)
  #define SH_DEBUG_PRINT_DEC(a,b) debugSerial.print(a,b)
  #define SH_DEBUG_PRINTLN_DEC(a,b) debugSerial.println(a,b)
#else
  #define NUM_MEASURE_SESSIONS 30
  #define CYCLE_DELAY 26000
  #define SH_DEBUG_PRINTLN(a) 
  #define SH_DEBUG_PRINT(a) 
  #define SH_DEBUG_PRINT_DEC(a,b) 
  #define SH_DEBUG_PRINTLN_DEC(a,b) 
#endif

#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RST 16
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define BME_SDA 4
#define BME_SCL 15

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

//Noise sensor pins
#define NOISE_MEASURE_PIN 34
#define NUM_NOISE_SAMPLES 1200

//Init global objects
TwoWire I2CBME = TwoWire(1);

Adafruit_BME680 bme680; // I2C
Adafruit_BME280 bme280; // I2C
WebServer server(80);
WiFiClientSecure client;

//EEPROM Data
const int EEPROM_SIZE = 256;

// Common fields
String ssid = "";
String password = "";

// WiFi config
String deviceName = "";

// LoRaWAN config
String operationMode = "";
String r_devaddr = "";
String r_nwksKey = "";
String r_appsKey = "";

static uint8_t mydata[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA,};
byte packet[13];

//Flags
int status = -1;
bool hasBME680 = false;
bool hasBME280 = false;
bool pm10SensorOK = true;
boolean isOkSetup = false;
bool inSending = false;
bool hasScreen = true;

// TCP + TLS
IPAddress apIP(192, 168, 4, 1);
const char *ssidDefault = "PulseEcoSensor";

byte valuesMask = 0;

// Uncomment this line if you want to use stronger host verification
// It adds a tad more security, but you'll need to reflash your device more often
//#define WITH_HOST_VERIFICATION 1

u1_t NWKSKEY[16];
u1_t APPSKEY[16];
u4_t DEVADDR;
//u1_t NWKSKEY[16] = LW_NWKSKEY;
//u1_t APPSKEY[16] = LW_APPSKEY;
//u4_t DEVADDR = LW_DEVADDR; 

// These callbacks are only used in over-the-air activation, so they are
// left empty here (we cannot leave them out completely unless
// DISABLE_JOIN is set in arduino-lmic/project_config/lmic_project_config.h,
// otherwise the linker will complain).
void os_getArtEui (u1_t* buf) { }
void os_getDevEui (u1_t* buf) { }
void os_getDevKey (u1_t* buf) { }

static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 120;

// Pin mapping
// Allegedly compatible with TTGO LoRa32 V1
const lmic_pinmap lmic_pins = {
  .nss = 18,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 14,
  .dio = {26, 33, 32},
};

// Constants
const char* host = "pulse.eco";
const char* fingerprint = "4E 9F 97 B8 6C 8F 70 C0 2A C9 6A 83 6D 5F 3B C7 81 C5 D6 3D";

// Stores the result of various function calls for sps sensor to check success or failure
int16_t operationResult;

void discoverAndSetStatus() {
  String data = "";
  bool validData = false;

  char readValue = (char)EEPROM.read(0);
  if ((char)readValue == '[') {
    //we're good
    //read till you get to the end
    SH_DEBUG_PRINTLN("Found data in EEPROM");
    int nextIndex = 1;
    while (nextIndex < EEPROM_SIZE && (readValue = (char) EEPROM.read(nextIndex++)) != ']') {
      data += readValue;
    }
  }

  if ((char)readValue == ']') {
    validData = true;
    #ifdef DEBUG_PROFILE
      SH_DEBUG_PRINTLN("Read data:");
      SH_DEBUG_PRINTLN(data);
    #endif
  } else {
    SH_DEBUG_PRINTLN("No data found in EEPROM");
  }

  String readFields[6]; // Increase the size to accommodate LoRaWAN fields
  if (validData) {
    //Try to properly split the string
    //String format: SSID:password:mode

    int count = splitCommand(&data, ':', readFields, 6);
    if (count != 3 && count != 6) { // Check for both WiFi and LoRaWAN modes
      validData = false;
      SH_DEBUG_PRINTLN("Incorrect data format.");
    } else {
      if (count == 3) {
        #ifdef DEBUG_PROFILE
          SH_DEBUG_PRINTLN("Read data parts for WiFi:");
          SH_DEBUG_PRINTLN(readFields[0]);
          SH_DEBUG_PRINTLN(readFields[1]);
          SH_DEBUG_PRINTLN(readFields[2]);
        #endif
        deviceName = readFields[0];
        ssid = readFields[1];
        password = readFields[2];
        operationMode = "wifi";
      } else if (count == 6) {
        #ifdef DEBUG_PROFILE
          SH_DEBUG_PRINTLN("Read data parts for LoRaWAN:");
          SH_DEBUG_PRINTLN(readFields[0]); // WiFi mode (ap or client)
          SH_DEBUG_PRINTLN(readFields[1]); // SSID
          SH_DEBUG_PRINTLN(readFields[2]); // Password
          SH_DEBUG_PRINTLN(readFields[3]); // DevAddr
          SH_DEBUG_PRINTLN(readFields[4]); // NwkSKey
          SH_DEBUG_PRINTLN(readFields[5]); // AppSKey
        #endif
        operationMode = readFields[0];
        ssid = readFields[1];
        password = readFields[2];
        r_devaddr = readFields[3];
        r_nwksKey = readFields[4];
        r_appsKey = readFields[5];
      }
      validData = true;
    }
  }

  if (ssid == NULL || ssid.equals("")) {
    SH_DEBUG_PRINTLN("No WiFi settings found.");
    //no network set yet
    validData = false;
  }

  if (!validData) {
    //It's still not connected to anything
    //broadcast net and display form
    SH_DEBUG_PRINTLN("Setting status code to 0: dipslay config options.");
    status = 0;

  } else {

    if (operationMode.equals("wifi")) {
      status = 1; // WiFi mode in STA
      SH_DEBUG_PRINTLN("Initially setting status to 1: try to connect to the network.");
    } else if (operationMode.equals("lora_ap")) {
      status = 2; // LoRaWAN in AP mode
      SH_DEBUG_PRINTLN("Initially setting status to 2: LoRaWAN in AP mode.");
    } else if (operationMode.equals("lora_sta")) {
      status = 3; // LoRaWAN in client mode
      SH_DEBUG_PRINTLN("Initially setting status to 3: LoRaWAN in client mode.");
    }
  }
}

// the setup routine runs once when you press reset:
void setup() {

  adcAttachPin(NOISE_MEASURE_PIN);
  analogSetPinAttenuation(NOISE_MEASURE_PIN, ADC_0db);
  
  while (!debugSerial && millis() < 10000); // wait for Serial to be initialized
  debugSerial.begin(57600);
  delay(100);     // per sample code on RF_95 test
  SH_DEBUG_PRINTLN(F("Starting"));

  #ifdef VCC_ENABLE
    // For Pinoccio Scout boards
    pinMode(VCC_ENABLE, OUTPUT);
    digitalWrite(VCC_ENABLE, HIGH);
    SH_DEBUG_PRINTLN(F("VCC_ENABLE ON"));
    delay(1000);
  #endif

  //reset OLED display via software
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  //initialize OLED
  SH_DEBUG_PRINTLN(F("Initializing Wire"));
  if (!Wire.begin(OLED_SDA, OLED_SCL)) {
    SH_DEBUG_PRINTLN(F("Wire allocation failed"));
    hasScreen = false;
  } else {
    SH_DEBUG_PRINTLN(F("Initializing Display"));
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) { // Address 0x3C for 128x32
      SH_DEBUG_PRINTLN(F("SSD1306 allocation failed"));
      hasScreen = false;
    }
  }

  displayInitScreen(false);

  //Init the temp/hum sensor
  // Set up oversampling and filter initialization
  SH_DEBUG_PRINTLN("Init BME sensor.");
  I2CBME.begin(BME_SDA, BME_SCL, 100000);

  hasBME680 = true;
  if (!bme680.begin(0x76)) {
    if (!bme680.begin(0x77)) {
      SH_DEBUG_PRINTLN("Could not find a valid BME680 sensor, check wiring!");
      hasBME680 = false;
    }
  }
  if (hasBME680) {
    bme680.setTemperatureOversampling(BME680_OS_8X);
    bme680.setHumidityOversampling(BME680_OS_2X);
    bme680.setPressureOversampling(BME680_OS_4X);
    bme680.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme680.setGasHeater(320, 150); // 320*C for 150 ms
  }

  if (!hasBME680) {
    //No BME680 found, trying out BME280 instead
    hasBME280 = true;
    if (!bme280.begin(0x76, &I2CBME)) {
      if (!bme280.begin(0x77, &I2CBME)) {
        SH_DEBUG_PRINTLN("No BME280 found! Check wiring!");
        hasBME280 = false;
      }
    }
  }

  if (hasBME680) {
    SH_DEBUG_PRINTLN("Found a BME680 sensor attached");
  } else if (hasBME280) {
    SH_DEBUG_PRINTLN("Found a BME280 sensor attached");
  }

  delay(2000);

  // Init the ЅРЅ sensor
  SH_DEBUG_PRINTLN("Init SPS sensor.");

  uint8_t auto_clean_days = 4;

  sensirion_i2c_init();

  SH_DEBUG_PRINTLN("Waiting SPS sensor to boot.");
  while (sps30_probe() != 0) {
    SH_DEBUG_PRINTLN("SPS sensor probing failed\n");
    delay(500);
  }

  operationResult = sps30_set_fan_auto_cleaning_interval_days(auto_clean_days);

  if (operationResult) {
    SH_DEBUG_PRINTLN("error setting the auto-clean interval: ");
    SH_DEBUG_PRINTLN(operationResult);
  }

  operationResult = sps30_start_measurement();
  if (operationResult < 0) {
    SH_DEBUG_PRINTLN("error starting measurement\n");
  }

  delay(2000);
  sps30_sleep();

  EEPROM.begin(EEPROM_SIZE);

  #ifndef NO_CONNECTION_PROFILE
    discoverAndSetStatus();
    // statuses: 0 -> initial AP; 1-> active mode client; 2 -> active mode LoRaWAN AP; 3 -> active mode LoRaWAN client;

    SH_DEBUG_PRINT("STATUS: ");
    SH_DEBUG_PRINTLN(status);

    if (status == 1) {
      setupWifiInSTAMode();
    }

    if (status == 2) {

      //Input params
      //Start up the web server
      SH_DEBUG_PRINTLN("Setting up configuration web server");
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);

      uint8_t mac[WL_MAC_ADDR_LENGTH];
      WiFi.softAPmacAddress(mac);
      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      WiFi.softAP(ssid.c_str(), password.c_str());
      delay(500);

      server.on("/", HTTP_GET, handleStatusGet);
      server.on("/check", HTTP_GET, handleStatusCheck);
      server.on("/values", HTTP_GET, handleStatusValues);
      server.on("/valuesJson", HTTP_GET, handleStatusValuesJSON);
      server.on("/reboot", HTTP_POST, handleReboot);
      server.on("/reset", HTTP_GET, handleResetRequest);
      server.on("/reset", HTTP_POST, handleResetResult);
      server.onNotFound(handleStatusGet);
      server.begin();

      SH_DEBUG_PRINTLN("HTTP server started");
      SH_DEBUG_PRINT("AP IP address: ");
      SH_DEBUG_PRINTLN(apIP);

      doLoRaWAN();
      isOkSetup = true;
    }

    if (status == 3) {
      setupWifiInSTAMode();
      doLoRaWAN();
    }

    if (status == 0) {
      //Input params
      //Start up the web server
      SH_DEBUG_PRINTLN("Setting up configuration web server");
      displayConfigScreen();
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);

      uint8_t mac[WL_MAC_ADDR_LENGTH];
      WiFi.softAPmacAddress(mac);
      String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                     String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
      macID.toUpperCase();
      String AP_NameString = "PulseEcoSensor-" + macID;

      char AP_NameChar[AP_NameString.length() + 1];
      memset(AP_NameChar, 0, AP_NameString.length() + 1);

      for (int i = 0; i < AP_NameString.length(); i++)
        AP_NameChar[i] = AP_NameString.charAt(i);

      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      WiFi.softAP(AP_NameChar);
      delay(500);

      server.on("/", HTTP_GET, handleGetHomepage);
      server.on("/wifi", HTTP_GET, handleGetWifi);
      server.on("/lorawan", HTTP_GET, handleGetLorawan);
      server.on("/wifiConfig", HTTP_POST, handlePostWifi);
      server.on("/loraWanConfig", HTTP_POST, handlePostLorawan);
      server.onNotFound(handleGetHomepage);

      server.begin();
      SH_DEBUG_PRINTLN("HTTP server started");
      SH_DEBUG_PRINT("AP IP address: ");
      SH_DEBUG_PRINTLN(apIP);
    }
  #else
    status = 1;
  #endif

  //wait a bit before your start
  delayWithDecency(2000);
  if (status > 0) {
    displayInitScreen(true);
  }
}

// Counters
int dataPacketsSentCount = 0;
int loopCycleCount = 0;
int noConnectionLoopCount = 0;

// Measurements
int noiseTotal = 0;
int pm10 = 0;
int pm25 = 0;
int pm1 = 0;
int temp = 0;
int humidity = 0;
int pressure = 0;
int altitude = 0;
int gasResistance = 0;
int noise = 0;
int noiseDba = 0;

bool rebootOnNextLoop = false;
bool resetOnNextLoop = false;

char hexbuffer[3];

void loop() {
  

  if (!inSending) {
    //increase counter
    loopCycleCount++;
  
    if (rebootOnNextLoop) {
      rebootOnNextLoop = false;
      ESP.restart();
    }
  
    if (resetOnNextLoop) {
      resetOnNextLoop = false;
      wipeSettings();
      ESP.restart();
    }
  
    if (status == 0) {
      server.handleClient();
      delay(50);
      noConnectionLoopCount++;
      // second 20 cycles
      // 1 minute 60 * 20 = 1200 cycles
      if (noConnectionLoopCount % 20 == 0) {
        displayConfigScreen();
      }
      // 10 minutes 10 * 1200 = 12000  cycles
      if (noConnectionLoopCount >= 12000) {
        //Reboot after 10 minutes in setup mode. Might be a temp failure in the network
        ESP.restart();
      }
  
    } else {
  
      //wait
      server.handleClient();
      delayWithDecency(CYCLE_DELAY);
  
      measureNoise();
  
      if (loopCycleCount >= NUM_MEASURE_SESSIONS) {
        //done measuring
        //measure dust, temp, hum and send data.
  
        SH_DEBUG_PRINTLN("Starting with the wrapping session.");
  
        readEnvironmentSensors();
        noise = ((int)noiseTotal / loopCycleCount) / 4; //mapped to 0-255
        noiseDba = round(37.08 * log10(noise) - 14.7);
        measurePM();
  
        if (pm10SensorOK) {
          SH_DEBUG_PRINT("pm25: "); SH_DEBUG_PRINT_DEC(pm25, DEC);
          SH_DEBUG_PRINT(", pm10: "); SH_DEBUG_PRINT_DEC(pm10, DEC);
          SH_DEBUG_PRINT(", pm1: "); SH_DEBUG_PRINT_DEC(pm1, DEC);
        }
        if (noise > 10) {
          SH_DEBUG_PRINT(", noise: "); SH_DEBUG_PRINT_DEC(noise, DEC);
          SH_DEBUG_PRINT(", noiseDba: "); SH_DEBUG_PRINT_DEC(noiseDba, DEC);
        }
        if (hasBME280 || hasBME680) {
          SH_DEBUG_PRINT(", temp: "); SH_DEBUG_PRINT_DEC(temp, DEC);
          SH_DEBUG_PRINT(", hum: "); SH_DEBUG_PRINT_DEC(humidity, DEC);
          SH_DEBUG_PRINT(", pres: "); SH_DEBUG_PRINT_DEC(pressure, DEC);
          SH_DEBUG_PRINT(", alt: "); SH_DEBUG_PRINT_DEC(altitude, DEC);
        }
        if (hasBME680) {
          SH_DEBUG_PRINT(", gasresistance: "); SH_DEBUG_PRINT_DEC(gasResistance, DEC);
        }
        SH_DEBUG_PRINTLN("");
  
        displayValuesOnScreen();
  
        //do the send here based on the status
  
        if (status == 1) {
  
          
  
          String url = "/wifipoint/store";
          url += "?devAddr=" + deviceName;
          url += "&version=6";
          if (pm10SensorOK) {
            url += "&pm10=" + String(pm10);
            url += "&pm25=" + String(pm25);
            url += "&pm1=" + String(pm1);
          }
          if (noise > 10) {
            url += "&noise=" + String(noise);
          }
          if (hasBME280 || hasBME680) {
            url += "&temperature=" + String(temp);
            url += "&humidity=" + String(humidity);
            url += "&pressure=" + String(pressure);
            url += "&altitude=" + String(altitude);
          }
          if (hasBME680) {
            url += "&gasresistance=" + String(gasResistance);
          }
          #ifdef DEBUG_PROFILE
            SH_DEBUG_PRINT("Invoking: ");
            SH_DEBUG_PRINTLN(url);
          #endif
  
          #ifndef NO_CONNECTION_PROFILE
            SH_DEBUG_PRINT("connecting to ");
            SH_DEBUG_PRINTLN(host);
            client.setInsecure();//skip verification
            if (!client.connect(host, 443)) {
              SH_DEBUG_PRINTLN("Connection failed. Restarting");
              ESP.restart();
              return;
            }
            String userAgent = "WIFI_SENSOR_V6_1";
            #ifdef WITH_HOST_VERIFICATION
              userAgent = userAgent + "_V";
              if (client.verify(fingerprint, host)) {
                SH_DEBUG_PRINTLN("certificate matches");
              } else {
                SH_DEBUG_PRINTLN("certificate doesn't match! Restarting");
                ESP.restart();
              }
            #else
              userAgent = userAgent + "_U";
            #endif
            client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                        "Host: " + host + "\r\n" +
                        "User-Agent: " + userAgent + "\r\n" +
                        "Connection: close\r\n\r\n");
  
            SH_DEBUG_PRINTLN("HTTPS request sent");
            while (client.connected()) {
              String line = client.readStringUntil('\n');
              if (line == "\r") {
                SH_DEBUG_PRINTLN("HTTP Headers received");
                break;
              }
            }
            String line = client.readStringUntil('\n');
            if (line.startsWith("OK")) {
              SH_DEBUG_PRINTLN("Transmission successfull!");
              dataPacketsSentCount++;
            } else {
              SH_DEBUG_PRINTLN("Transmission failed!");
            }
        #endif
        }
  
        if (status == 2 || status == 3) {
  
          int hextemp = min(max(temp + 127, 0), 255);
          int hexhum = min(max(humidity, 0), 255);
  
          valuesMask = 0;
          if (pm10SensorOK) {
            valuesMask |= (byte)1;
          }
          if (noise > 10) {
            valuesMask |= (byte)2;
          }
          if (hasBME280 || hasBME680) {
            valuesMask |= (byte)4;
          }
          if (hasBME680) {
            //disabled for now
            //valuesMask |= (byte)8;
          }
  
          //    dataframe
          //- 1 byte: version
          //  1 byte, to add: bitmask on values: pm10, noise, temp/hum/pres, gasres (lsb to msb)
          //- 1 byte: temperature (offsetted 127)
          //- 1 byte: humidity
          //- 1 byte: noise (scaled from 0 to 255)
          //- 2 bytes: pm10
          //- 2 bytes: pm25
          //- 2 bytes pressure
          //- 2 bytes: pm1
  
          packet[0] = 5; //version to be changed to something else
          packet[1] = valuesMask;
          //couple of versions shold be used, bitmask sort of present values
          //sds
          //temp/hum/pres
          //gas
          //noise
          //maybe the first byte to have a different version number
          //and a second one to be a mask of the used values.
          packet[2] = (byte)hextemp;
          packet[3] = (byte)hexhum;
          packet[4] = (byte)noise; //noise
          packet[5] = (byte)(pm10 / 256);
          packet[6] = (byte)(pm10 % 256);
          packet[7] = (byte)(pm25 / 256);
          packet[8] = (byte)(pm25 % 256);
          packet[9] = (byte)(pm1 / 256);
          packet[10] = (byte)(pm1 % 256);
          packet[11] = (byte)(pressure / 256);
          packet[12] = (byte)(pressure % 256);
  
          SH_DEBUG_PRINTLN("TXing: ");
          for (int i = 0; i < 13; i++) {
            sprintf(hexbuffer, "%02x", (int)packet[i]);
            SH_DEBUG_PRINT(hexbuffer);
            SH_DEBUG_PRINT(" ");
          }
          SH_DEBUG_PRINTLN("");
          #ifndef NO_CONNECTION_PROFILE
            // Send it off
            //ttn.sendBytes(packet, sizeof(packet));
            // Start job
            inSending = true;
            do_send(&sendjob);
            dataPacketsSentCount++;
          #endif
        }
  
        //reset
        noiseTotal = 0;
        loopCycleCount = 0;
      }
    }
  } else {
    delayWithDecency(100);
  }
  #ifndef NO_CONNECTION_PROFILE
  if (status == 2 || status == 3) {
    os_runloop_once();
  }
  #endif
}


//Web server params below
void handleGetHomepage() {
  String output = FPSTR(homepage);
  server.send(200, "text/html", output);
}


void handleGetWifi() {
  String output = FPSTR(configureWifi);
  server.send(200, "text/html", output);
}

void handleGetLorawan() {
  String output = FPSTR(configureLora);
  server.send(200, "text/html", output);
}

void handlePostWifi() {

  SH_DEBUG_PRINT("Number of args:");
  SH_DEBUG_PRINTLN(server.args());
  for (int i = 0; i < server.args(); i++) {
    SH_DEBUG_PRINT("Argument no.");
    SH_DEBUG_PRINT_DEC(i, DEC);
    SH_DEBUG_PRINT(": name: ");
    SH_DEBUG_PRINT(server.argName(i));
    SH_DEBUG_PRINT(" value: ");
    SH_DEBUG_PRINTLN(server.arg(i));
  }

  if (server.args() == 3
      && server.argName(0).equals("deviceId")
      && server.argName(1).equals("ssid")
      && server.argName(2).equals("password")) {
    //it's ok

    String data = "[" + server.arg(0) + ":" + server.arg(1) + ":" + server.arg(2) + "]";
    data.replace("+", " ");

    if (data.length() < EEPROM_SIZE) {
      server.send(200, "text/html", "<h1>The device will restart now.</h1>");
      //It's ok

      SH_DEBUG_PRINTLN("Storing data in EEPROM:");
      #ifdef DEBUG_PROFILE
        SH_DEBUG_PRINTLN(data);
      #endif
      for (int i = 0; i < data.length(); i++) {
        EEPROM.write(i, (byte)data[i]);
      }
      EEPROM.commit();
      delay(500);

      SH_DEBUG_PRINTLN("Stored to EEPROM. Restarting.");
      ESP.restart();

    } else {
      server.send(400, "text/html", "<h1>The parameter string is too long.</h1>");
    }

  } else {
    server.send(400, "text/html", "<h1>Incorrect input. Please try again.</h1>");
  }

}

void handlePostLorawan() {

  SH_DEBUG_PRINT("Number of args:");
  SH_DEBUG_PRINTLN(server.args());
  for (int i = 0; i < server.args(); i++) {
    SH_DEBUG_PRINT("Argument no.");
    SH_DEBUG_PRINT_DEC(i, DEC);
    SH_DEBUG_PRINT(": name: ");
    SH_DEBUG_PRINT(server.argName(i));
    SH_DEBUG_PRINT(" value: ");
    SH_DEBUG_PRINTLN(server.arg(i));
  }

  if (server.args() == 6
      && server.argName(0).equals("operationMode")
      && server.argName(1).equals("ssid")
      && server.argName(2).equals("password")
      && server.argName(3).equals("devaddr")
      && server.argName(4).equals("nwksKey")
      && server.argName(5).equals("appsKey")) {
    //it's ok

    String data = "[" + server.arg(0) + ":" + server.arg(1) + ":" + server.arg(2) + ":" + server.arg(3) + ":" + server.arg(4) + ":" + server.arg(5) + "]";
    data.replace("+", " ");

    if (data.length() < EEPROM_SIZE) {
      server.send(200, "text/html", "<h1>The device will restart now.</h1>");
      //It's ok

      SH_DEBUG_PRINTLN("Storing data in EEPROM:");
      #ifdef DEBUG_PROFILE
        SH_DEBUG_PRINTLN(data);
      #endif
      for (int i = 0; i < data.length(); i++) {
        EEPROM.write(i, (byte)data[i]);
      }
      EEPROM.commit();
      delay(500);

      SH_DEBUG_PRINTLN("Stored to EEPROM. Restarting.");
      ESP.restart();

    } else {
      server.send(400, "text/html", "<h1>The parameter string is too long.</h1>");
    }

  } else {
    server.send(400, "text/html", "<h1>Incorrect input. Please try again.</h1>");
  }

}

void wipeSettings() {
  for (int i = 0; i < 10; i++) {
    EEPROM.write(i, (byte)0);
  }
  EEPROM.commit();
  SH_DEBUG_PRINTLN("Settings removed from EEPROM. Restarting.");
}

void handleStatusCheck() {
  String output = "1";
  server.send(200, "text/html", output);
}

void handleStatusGet() {
  String output = FPSTR(STATUS_page);
  server.send(200, "text/html", output);
}

void handleStatusValues() {

  String valuesString = "";
  if (pm10SensorOK) {
    valuesString += "pm10=" + String(pm10) + " ug/m3";
    valuesString += ";pm25=" + String(pm25) + " ug/m3";
    valuesString += ";pm1=" + String(pm1) + " ug/m3";
    valuesString += ";sds=OK";
  } else {
    valuesString += "pm10=N/A";
    valuesString += ";pm25=N/A";
    valuesString += ";pm1=N/A";
    valuesString += ";sds=missing or bad";
  }

  if (noise > 10) {
    valuesString += ";noise=" + String(noise);
    valuesString += ";snoise=OK";
  } else {
    valuesString += ";noise=N/A";
    valuesString += ";snoise=missing or bad";
  }
  if (hasBME280 || hasBME680) {
    valuesString += ";temperature=" + String(temp) + " C";
    valuesString += ";humidity=" + String(humidity) + " %";
    valuesString += ";pressure=" + String(pressure) + " hPa";
    if (hasBME280) {
      valuesString += ";bme=280 OK";
    } else {
      valuesString += ";bme=680 OK";
    }
  } else {
    valuesString += ";temperature=N/A";
    valuesString += ";humidity=N/A";
    valuesString += ";pressure=N/A";
    valuesString += ";bme=missing or bad";
  }

  valuesString += ";packets=" + String(dataPacketsSentCount);
  valuesString += ";network=" + ssid;

  //stringformat: pm10,pm25,temp,hum,noise,packets,bme,sds,noise,wifi

  server.send(200, "text/html", valuesString);
}

String formJsonPair(String key, int value, bool comma) {
  return "\"" + key + "\":" + value +  (comma ? "," : "");
}

void handleStatusValuesJSON() {
  String valuesString = "{";
  if (pm10SensorOK) {
    valuesString += formJsonPair("pm10", pm10, true);
    valuesString += formJsonPair("pm25", pm25, true);
  }

  if (noise > 10) {
    valuesString += formJsonPair("noise", noise, true);
  }
  if (hasBME280 || hasBME680) {
    valuesString += formJsonPair("temperature", temp, true);
    valuesString += formJsonPair("humidity", humidity, true);
    valuesString += formJsonPair("pressure", pressure, true);

  }

  valuesString += formJsonPair("packets", dataPacketsSentCount, false);
  valuesString += "}";

  //stringformat: pm10,pm25,temp,hum,noise,packets,bme,sds,noise,wifi

  server.send(200, "application/json", valuesString);
}

void handleReboot() {
  rebootOnNextLoop = true;
  String output = FPSTR(REBOOT_page);
  server.send(200, "text/html", output);
}

void handleResetRequest() {
  String output = FPSTR(RESET_REQUEST_page);
  server.send(200, "text/html", output);
}

void handleResetResult() {
  resetOnNextLoop = true;
  String output = FPSTR(RESET_RESULT_page);
  server.send(200, "text/html", output);
}


void delayWithDecency(int units) {
  for (int i = 0; i < units / 10; i++) {
    delay(10);
    #ifndef NO_CONNECTION_PROFILE
      if (status == 2 || status == 3) {
        os_runloop_once();
      }
    #endif
    if (!inSending) {
      server.handleClient();
    }
  }
}

int invertDisplay = 0;
void displayValuesOnScreen() {
  if (hasScreen) {
    display.clearDisplay();
    display.setRotation(3);
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.invertDisplay(invertDisplay);
    invertDisplay++;
    invertDisplay %= -2;
    display.setTextSize(1);
    display.setCursor(5, 2);
    display.println("pulse.eco");

    display.setCursor(2, 20);
    display.print("pm10:");
    if (pm10SensorOK) {
      display.setCursor(35, 20);
      display.print(pm10, DEC);
    }

    display.setCursor(2, 30);
    display.print("pm25:");
    if (pm10SensorOK) {
      display.setCursor(35, 30);
      display.print(pm25, DEC);
    }

    display.setCursor(2, 40);
    display.print("temp:");
    if (hasBME680 || hasBME280) {
      display.setCursor(35, 40);
      display.print(temp, DEC);
    }

    display.setCursor(2, 50);
    display.print("humi:");
    if (hasBME680 || hasBME280) {
      display.setCursor(35, 50);
      display.print(humidity, DEC);
    }

    display.setCursor(2, 60);
    display.print("pres:");
    if (hasBME680 || hasBME280) {
      display.setCursor(35, 60);
      display.print(pressure, DEC);
    }

    display.display();
  }
}

void displayInitScreen(bool waiting) {
  display.clearDisplay();
  display.setRotation(3);
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(5, 40);
  display.println("pulse.eco");

  if (!waiting) {
    display.setCursor(8, 50);
    display.println("Init ...");
  } else {
    display.setCursor(2, 50);
    display.println("Waiting on");
    display.setCursor(2, 60);
    display.println("first poll");
  }

  display.display();
}


void displayConfigScreen() {
  display.clearDisplay();
  display.setRotation(3);
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.invertDisplay(invertDisplay);
  invertDisplay++;
  invertDisplay %= -2;
  display.setCursor(5, 40);
  display.println("pulse.eco");

  display.setCursor(15, 60);
  display.println("CONFIG");

  display.display();
}

void setupWifiInSTAMode() {
  //Try to connect to the network
  SH_DEBUG_PRINTLN("Trying to connect...");
  char ssidBuf[ssid.length() + 1];
  ssid.toCharArray(ssidBuf, ssid.length() + 1);
  char passBuf[password.length() + 1];
  password.toCharArray(passBuf, password.length() + 1);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin ( ssidBuf, passBuf );
  SH_DEBUG_PRINT("SSID: ");
  SH_DEBUG_PRINTLN(ssidBuf);
  #ifdef DEBUG_PROFILE
    SH_DEBUG_PRINT("Password: ");
    SH_DEBUG_PRINTLN(passBuf);
  #endif

  // Wait for connection
  int numTries = 200;

  while (WiFi.status() != WL_CONNECTED && --numTries > 0) {
    delay (250);
    SH_DEBUG_PRINT(".");
  }

  SH_DEBUG_PRINT(WiFi.status());
  if (WiFi.status() != WL_CONNECTED) {
    SH_DEBUG_PRINT("Unable to connect to the network: ");
    SH_DEBUG_PRINTLN( ssid );
    status = 0;
  } else {
    //Connected to the network
    SH_DEBUG_PRINT("Connected to:");
    SH_DEBUG_PRINTLN( ssid );
    SH_DEBUG_PRINT( "IP address: " );
    SH_DEBUG_PRINTLN( WiFi.localIP() );

    //Set up MDNS
    if (!MDNS.begin("pulse-eco")) {
      SH_DEBUG_PRINTLN("Error setting up MDNS responder!");
    }
    MDNS.addService("http", "tcp", 80);

    //Set up status respond
    server.on("/", HTTP_GET, handleStatusGet);
    server.on("/check", HTTP_GET, handleStatusCheck);
    server.on("/values", HTTP_GET, handleStatusValues);
    server.on("/valuesJson", HTTP_GET, handleStatusValuesJSON);
    server.on("/reboot", HTTP_POST, handleReboot);
    server.on("/reset", HTTP_GET, handleResetRequest);
    server.on("/reset", HTTP_POST, handleResetResult);
    server.onNotFound(handleStatusGet);
    server.begin();

    SH_DEBUG_PRINTLN("Joined network, waiting for modem...");
    isOkSetup = true;
  }
}

void doLoRaWAN() {
  // LMIC init
  SH_DEBUG_PRINTLN("Startup LoRaWAN...");
  os_init();
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();

  // Set static session parameters. Instead of dynamically establishing a session
  // by joining the network, precomputed session parameters are be provided.
  #ifdef PROGMEM

    // Convert strings to C-style strings
    const char* devAddrCStr = r_devaddr.c_str();
    const char* nwkSKeyCStr = r_nwksKey.c_str();
    const char* appSKeyCStr = r_appsKey.c_str();

    hexStringToByteArray(nwkSKeyCStr, NWKSKEY, 16); // Convert NWKSKEY
    hexStringToByteArray(appSKeyCStr, APPSKEY, 16); // Convert APPSKEY
    sscanf(devAddrCStr, "%8lx", &DEVADDR);

    
    // On AVR, these values are stored in flash and only copied to RAM
    // once. Copy them to a temporary buffer here, LMIC_setSession will
    // copy them into a buffer of its own again.
    uint8_t appskey[sizeof(APPSKEY)];
    uint8_t nwkskey[sizeof(NWKSKEY)];
    memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
    memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));

    LMIC_setSession (0x13, DEVADDR, nwkskey, appskey);
//    SH_DEBUG_PRINTLN("setup with PROGMEM");
//    debugSerial.println(DEVADDR);
//    for (int i=0; i< sizeof(appskey); i++) {
//      debugSerial.print(appskey[i],HEX);
//    }
//    debugSerial.println("");
//    for (int i=0; i< sizeof(nwkskey); i++) {
//      debugSerial.print(nwkskey[i],HEX);
//    }
//    debugSerial.println("");
  #else
    // If not running an AVR with PROGMEM, just use the arrays directly
    LMIC_setSession (0x13, DEVADDR, NWKSKEY, APPSKEY);
  #endif

  #if defined(CFG_eu868)
    // Set up the channels used by the Things Network, which corresponds
    // to the defaults of most gateways. Without this, only three base
    // channels from the LoRaWAN specification are used, which certainly
    // works, so it is good for debugging, but can overload those
    // frequencies, so be sure to configure the full frequency range of
    // your network here (unless your network autoconfigures them).
    // Setting up channels should happen after LMIC_setSession, as that
    // configures the minimal channel set. The LMIC doesn't let you change
    // the three basic settings, but we show them here.
    LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI);      // g-band
    LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(DR_FSK,  DR_FSK),  BAND_MILLI);      // g2-band
    // TTN defines an additional channel at 869.525Mhz using SF9 for class B
    // devices' ping slots. LMIC does not have an easy way to define set this
    // frequency and support for class B is spotty and untested, so this
    // frequency is not configured here.
  #elif defined(CFG_us915) || defined(CFG_au915)
    // NA-US and AU channels 0-71 are configured automatically
    // but only one group of 8 should (a subband) should be active
    // TTN recommends the second sub band, 1 in a zero based count.
    // https://github.com/TheThingsNetwork/gateway-conf/blob/master/US-global_conf.json
    LMIC_selectSubBand(1);
  #elif defined(CFG_as923)
    // Set up the channels used in your country. Only two are defined by default,
    // and they cannot be changed.  Use BAND_CENTI to indicate 1% duty cycle.
    // LMIC_setupChannel(0, 923200000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);
    // LMIC_setupChannel(1, 923400000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);

    // ... extra definitions for channels 2..n here
  #elif defined(CFG_kr920)
    // Set up the channels used in your country. Three are defined by default,
    // and they cannot be changed. Duty cycle doesn't matter, but is conventionally
    // BAND_MILLI.
    // LMIC_setupChannel(0, 922100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);
    // LMIC_setupChannel(1, 922300000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);
    // LMIC_setupChannel(2, 922500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);

    // ... extra definitions for channels 3..n here.
  #elif defined(CFG_in866)
    // Set up the channels used in your country. Three are defined by default,
    // and they cannot be changed. Duty cycle doesn't matter, but is conventionally
    // BAND_MILLI.
    // LMIC_setupChannel(0, 865062500, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);
    // LMIC_setupChannel(1, 865402500, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);
    // LMIC_setupChannel(2, 865985000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);

    // ... extra definitions for channels 3..n here.
  #else
  # error Region not supported
  #endif

  // Disable link check validation
  LMIC_setLinkCheckMode(0);

  // TTN uses SF9 for its RX2 window.
  LMIC.dn2Dr = DR_SF9;

  // Set data rate and transmit power for uplink
  LMIC_setDrTxpow(LW_DATARATE, 14);
}

void onEvent (ev_t ev) {
  SH_DEBUG_PRINT(os_getTime());
  SH_DEBUG_PRINT(": ");
  switch (ev) {
    case EV_SCAN_TIMEOUT:
      SH_DEBUG_PRINTLN(F("EV_SCAN_TIMEOUT"));
      break;
    case EV_BEACON_FOUND:
      SH_DEBUG_PRINTLN(F("EV_BEACON_FOUND"));
      break;
    case EV_BEACON_MISSED:
      SH_DEBUG_PRINTLN(F("EV_BEACON_MISSED"));
      break;
    case EV_BEACON_TRACKED:
      SH_DEBUG_PRINTLN(F("EV_BEACON_TRACKED"));
      break;
    case EV_JOINING:
      SH_DEBUG_PRINTLN(F("EV_JOINING"));
      break;
    case EV_JOINED:
      SH_DEBUG_PRINTLN(F("EV_JOINED"));
      break;
    /*
      || This event is defined but not used in the code. No
      || point in wasting codespace on it.
      ||
      || case EV_RFU1:
      ||     SH_DEBUG_PRINTLN(F("EV_RFU1"));
      ||     break;
    */
    case EV_JOIN_FAILED:
      SH_DEBUG_PRINTLN(F("EV_JOIN_FAILED"));
      break;
    case EV_REJOIN_FAILED:
      SH_DEBUG_PRINTLN(F("EV_REJOIN_FAILED"));
      break;
    case EV_TXCOMPLETE:
      SH_DEBUG_PRINTLN(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
      if (LMIC.txrxFlags & TXRX_ACK)
        SH_DEBUG_PRINTLN(F("Received ack"));
      if (LMIC.dataLen) {
        SH_DEBUG_PRINTLN(F("Received "));
        SH_DEBUG_PRINTLN(LMIC.dataLen);
        SH_DEBUG_PRINTLN(F(" bytes of payload"));
      }
      // Schedule next transmission
      //os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
      inSending = false;
      break;
    case EV_LOST_TSYNC:
      SH_DEBUG_PRINTLN(F("EV_LOST_TSYNC"));
      break;
    case EV_RESET:
      SH_DEBUG_PRINTLN(F("EV_RESET"));
      break;
    case EV_RXCOMPLETE:
      // data received in ping slot
      SH_DEBUG_PRINTLN(F("EV_RXCOMPLETE"));
      break;
    case EV_LINK_DEAD:
      SH_DEBUG_PRINTLN(F("EV_LINK_DEAD"));
      break;
    case EV_LINK_ALIVE:
      SH_DEBUG_PRINTLN(F("EV_LINK_ALIVE"));
      break;
    case EV_TXSTART:
      SH_DEBUG_PRINTLN(F("EV_TXSTART"));
      inSending = true;
      break;
    case EV_TXCANCELED:
      SH_DEBUG_PRINTLN(F("EV_TXCANCELED"));
      inSending = false;
      break;
    case EV_RXSTART:
      /* do not print anything -- it wrecks timing */
      break;
    case EV_JOIN_TXCOMPLETE:
      SH_DEBUG_PRINTLN(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
      break;
    default:
      SH_DEBUG_PRINT(F("Unknown event: "));
      SH_DEBUG_PRINTLN((unsigned) ev);
      break;
  }
}

void do_send(osjob_t* j) {
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND) {
    SH_DEBUG_PRINTLN(F("OP_TXRXPEND, not sending"));
  } else {
    // Prepare upstream data transmission at the next possible time.
    LMIC_setTxData2(1, packet, sizeof(packet), 0);
    SH_DEBUG_PRINTLN(F("Packet queued"));
  }
  // Next TX is scheduled after TX_COMPLETE event.
}

// Mesurement methods below

void measureNoise() {
  int noiseSessionMax = 0;
  int noiseSessionMin = 4096;
  int currentSample = 0;
  int noiseMeasureLength = millis();

  for (int sample = 0; sample < NUM_NOISE_SAMPLES; sample++) {
    currentSample = analogRead(NOISE_MEASURE_PIN);
    if (currentSample > 0 && currentSample < 4090) {
      if (currentSample > noiseSessionMax) {
        noiseSessionMax = currentSample;
      }
      if (currentSample < noiseSessionMin) {
        noiseSessionMin = currentSample;
      }
    }
  }

  int currentSessionNoise = noiseSessionMax - noiseSessionMin;
  if (currentSessionNoise < 0) {
    currentSessionNoise = 0;
  }

  noiseTotal += currentSessionNoise / 4;

  #ifdef NO_CONNECTION_PROFILE
    noiseMeasureLength = millis() - noiseMeasureLength;
    SH_DEBUG_PRINT("Noise measurement took: ");
    SH_DEBUG_PRINT_DEC(noiseMeasureLength, DEC);
    SH_DEBUG_PRINT("ms with ");
    SH_DEBUG_PRINT_DEC(NUM_NOISE_SAMPLES, DEC);
    SH_DEBUG_PRINT(" samples. Minumum = ");
    SH_DEBUG_PRINT_DEC(noiseSessionMin, DEC);
    SH_DEBUG_PRINT(", Maxiumum = ");
    SH_DEBUG_PRINT_DEC(noiseSessionMax, DEC);
    SH_DEBUG_PRINT(" samples. Value = ");
    SH_DEBUG_PRINT_DEC(currentSessionNoise, DEC);
    SH_DEBUG_PRINT(", normalized: ");
    SH_DEBUG_PRINTLN_DEC(currentSessionNoise / 16, DEC);
  #endif
}

// Method to read temperature, humidity, pressure, and gas resistance
void readEnvironmentSensors() {
  int countTempHumReadouts = 5;

  while (--countTempHumReadouts > 0) {
    if (hasBME680) {
      if (! bme680.performReading()) {
        SH_DEBUG_PRINTLN("Failed to perform BME reading!");
      } else {
        temp = bme680.temperature;
        humidity = bme680.humidity;
        pressure = bme680.pressure / 100;
        gasResistance = bme680.gas_resistance;
        altitude = bme680.readAltitude(SEALEVELPRESSURE_HPA);
      }
    } else if (hasBME280) {
      temp = bme280.readTemperature();
      humidity = bme280.readHumidity();
      pressure = bme280.readPressure() / 100;
      altitude = bme280.readAltitude(SEALEVELPRESSURE_HPA);
    } else {
      // No temp/hum sensor
      break;
    }

    if (humidity <= 0 || humidity > 100 || temp > 100 || temp < -100 || pressure <= 0) {
      //fake result, pause and try again.
      SH_DEBUG_PRINT("Fake BME result. Temp: ");
      SH_DEBUG_PRINT_DEC(temp, DEC);
      SH_DEBUG_PRINT(" Humidity: ");
      SH_DEBUG_PRINT_DEC(humidity, DEC);
      SH_DEBUG_PRINT(" Pressure: ");
      SH_DEBUG_PRINTLN_DEC(pressure, DEC);
      
      delayWithDecency(3000);
    } else {
      // OK result
      break;
    }
  }

    if (countTempHumReadouts <= 0) {
      //failed to read temp/hum/pres/gas
      //disable BME sensors
      SH_DEBUG_PRINTLN("Disabled BME sensor");
      hasBME680 = false;
      hasBME280 = false;
    }
}

// Method to measure PM10 and PM2.5 using an SPS30 sensor
void measurePM() {
  int countPMReadouts = 5;
  struct sps30_measurement m;
  char serial[SPS30_MAX_SERIAL_LEN];
  uint16_t data_ready;

  sps30_wake_up();
  delayWithDecency(15000);

  while (--countPMReadouts > 0) {

    operationResult = sps30_read_data_ready(&data_ready);
    
    if (operationResult < 0) {
      SH_DEBUG_PRINTLN("error reading data-ready flag: ");
      SH_DEBUG_PRINTLN(operationResult);
    } else if (!data_ready)
      SH_DEBUG_PRINTLN("data not ready, no new measurement available");
    else
      break;
    delay(100); /* retry in 100ms */
  }

  operationResult = sps30_read_measurement(&m);
  if (operationResult < 0) {
    SH_DEBUG_PRINTLN("error reading measurement");
  } else {
    pm25 = m.mc_2p5;
    pm10 = m.mc_10p0;
    pm1 = m.mc_1p0;
    pm10SensorOK = true;
  }

  sps30_sleep();
  delayWithDecency(15000);
}

// Util methods below

int splitCommand(String * text, char splitChar, String returnValue[], int maxLen) {
  int splitCount = countSplitCharacters(text, splitChar);
  SH_DEBUG_PRINT("Split count: ");
  SH_DEBUG_PRINTLN_DEC(splitCount, DEC);
  if (splitCount + 1 > maxLen) {
    return -1;
  }

  int index = -1;
  int index2;

  for (int i = 0; i <= splitCount; i++) {
    //    index = text->indexOf(splitChar, index + 1);
    index2 = text->indexOf(splitChar, index + 1);

    if (index2 < 0) index2 = text->length();
    returnValue[i] = text->substring(index + 1, index2);
    index = index2;
  }

  return splitCount + 1;
}

int countSplitCharacters(String * text, char splitChar) {
  int returnValue = 0;
  int index = -1;

  while (true) {
    index = text->indexOf(splitChar, index + 1);

    if (index > -1) {
      returnValue += 1;
    } else {
      break;
    }
  }

  return returnValue;
}

void bubbleSort(short A[], int len) {
  unsigned long newn;
  unsigned long n = len;
  short temp = 0;
  do {
    newn = 1;
    for (int p = 1; p < len; p++) {
      if (A[p - 1] > A[p]) {
        temp = A[p];         //swap places in array
        A[p] = A[p - 1];
        A[p - 1] = temp;
        newn = p;
      } //end if
    } //end for
    n = newn;
  } while (n > 1);
}


short median(short sorted[], int m) //calculate the median
{
  //First bubble sort the values: https://en.wikipedia.org/wiki/Bubble_sort
  bubbleSort(sorted, m); // Sort the values
  if (bitRead(m, 0) == 1) { //If the last bit of a number is 1, it's odd. This is equivalent to "TRUE". Also use if m%2!=0.
    return sorted[m / 2]; //If the number of data points is odd, return middle number.
  } else {
    return (sorted[(m / 2) - 1] + sorted[m / 2]) / 2; //If the number of data points is even, return avg of the middle two numbers.
  }
}

void hexStringToByteArray(const char* hexString, uint8_t* byteArray, int byteArraySize) {
    for (int i = 0; i < byteArraySize; i++) {
        sscanf(&hexString[i * 2], "%2hhx", &byteArray[i]);
    }
}
