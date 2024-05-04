/*******************************************************************************
   Do not forget to define the radio type correctly in
   arduino-lmic/project_config/lmic_project_config.h or from your BOARDS.txt.

 *******************************************************************************/

#define LW_DEVADDR 0x00000000
#define LW_NWKSKEY { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define LW_APPSKEY { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define LW_DATARATE DR_SF7

#define WL_MAC_ADDR_LENGTH 6

// Uncomment if you want to test the device without LoRaWAN/WiFi connectivity
//#define NO_CONNECTION_PROFILE 1
// Uncomment if you want to enable debug lines printing in console and more 2 minutes interval
// USE WITH CARE SINCE IT MIGHT RESULT IN A DEVIVCE BAN FROM pulse.eco IF USED LIVE
#define DEBUG_PROFILE 1

#include <EEPROM.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

#include <SoftwareSerial.h>


#include <bme680.h>
#include <Adafruit_BME680.h>
#include <bme680_defs.h>
#include <Adafruit_BME280.h>

#include <sps30.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "homepage.h"
#include "configureLora.h"
#include "configureWifi.h"

#define debugSerial Serial

#ifdef DEBUG_PROFILE
//  #define NUM_MEASURE_SESSIONS 20
//  #define CYCLE_DELAY 2000
#define NUM_MEASURE_SESSIONS 40
#define CYCLE_DELAY 2000
#define SH_DEBUG_PRINTLN(a) debugSerial.println(a)
#define SH_DEBUG_PRINT(a) debugSerial.print(a)
#define SH_DEBUG_PRINT_DEC(a,b) debugSerial.print(a,b)
#define SH_DEBUG_PRINTLN_DEC(a,b) debugSerial.println(a,b)
#else
#define NUM_MEASURE_SESSIONS 2
#define CYCLE_DELAY 10000
#define SH_DEBUG_PRINTLN(a)
#define SH_DEBUG_PRINT(a)
#define SH_DEBUG_PRINT_DEC(a,b)
#define SH_DEBUG_PRINTLN_DEC(a,b)
#endif

////OLED pins for v1
//#define OLED_SDA 4
//#define OLED_SCL 15
//#define OLED_RST 16
//#define SCREEN_WIDTH 128 // OLED display width, in pixels
//#define SCREEN_HEIGHT 64 // OLED display height, in pixels

//OLED pins for v1.3
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RST 16
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

Adafruit_BME680 bme680; // I2C
Adafruit_BME280 bme280; // I2C


//Noise sensor pins
#define NOISE_MEASURE_PIN 36
#define NUM_NOISE_SAMPLES 1000

WebServer server(80);

WiFiClientSecure client;

//EEPROM Data
const int EEPROM_SIZE = 256;
String wifiMode = "";
String devaddr = "";
String nwksKey = "";
String appsKey = "";
String wifi = "";
String ssid = "";
String password = "";
String passcode = "";
String deviceName = "";


static uint8_t mydata[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA,};
byte packet[11];

//Flags
int status = -1;
bool hasBME680 = false;
bool hasBME280 = false;
bool pm10SensorOK = true;

bool hasScreen = true;

byte valuesMask = 0;

// TCP + TLS
IPAddress apIP(192, 168, 1, 1);
const char *ssidDefault = "PulseEcoSensor";

//static const PROGMEM u1_t NWKSKEY[16] = LW_NWKSKEY;
//static const u1_t PROGMEM APPSKEY[16] = LW_APPSKEY;
//static const u4_t DEVADDR = LW_DEVADDR;


u1_t NWKSKEY[16] = LW_NWKSKEY;
u1_t APPSKEY[16] = LW_APPSKEY;
u4_t DEVADDR = LW_DEVADDR;


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

const char* host = "pulse.eco";

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

  // String readFields[6] for lorawan fields. Will read the first field, and then decide how will handle the rest of the data.
  String readFields[3];
  if (validData) {
    //Try to properly split the string
    //String format: SSID:password:mode

    int count = splitCommand(&data, ':', readFields, 3);
    if (count != 3) {
      validData = false;
      SH_DEBUG_PRINTLN("Incorrect data format.");
    } else {
#ifdef DEBUG_PROFILE
      SH_DEBUG_PRINTLN("Read data parts:");
      SH_DEBUG_PRINTLN(readFields[0]);
      SH_DEBUG_PRINTLN(readFields[1]);
      SH_DEBUG_PRINTLN(readFields[2]);
#endif
      deviceName = readFields[0];
      ssid = readFields[1];
      password = readFields[2];
      validData = true;
    }
  }

  if (ssid == NULL || ssid.equals("")) {
    SH_DEBUG_PRINTLN("No WiFi settings found.");
    //no network set yet
    validData = false;
  }

  if (!validData) {
    // It's still not connected to anything
    // broadcast net and display form
    SH_DEBUG_PRINTLN("Setting status code to 0: dipslay config options.");
    //digitalWrite(STATUS_LED_PIN, LOW);
    status = 0;

  } else {


    status = 1;
    //should be rewriten based on the wifi mode. either set up AP mode or client with MDNS
    //digitalWrite(STATUS_LED_PIN, HIGH);
    SH_DEBUG_PRINTLN("Initially setting status to 1: try to connect to the network.");

  }
}

int16_t ret;


void setup() {
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
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
  SH_DEBUG_PRINTLN("Init SPS sensor.");
  //Init the pm SENSOR
  sensirion_i2c_init();

  ret = sps30_start_measurement();

  SH_DEBUG_PRINTLN("Waiting SPS sensor to boot.");
  delay(2000);

  sps30_sleep();

  //Init the temp/hum sensor
  // Set up oversampling and filter initialization
  SH_DEBUG_PRINTLN("Init BME sensor.");
  hasBME680 = true;
  if (!bme680.begin(0x76)) {
    if (!bme680.begin(0x77)) {
      SH_DEBUG_PRINTLN("No BME680 found! Check wiring!");
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
    if (!bme280.begin(0x76)) {
      if (!bme280.begin(0x77)) {
        SH_DEBUG_PRINTLN("No BME280 found! Check wiring!");
        hasBME280 = false;
      }
    }
  }

  if (hasBME680) {
    SH_DEBUG_PRINTLN("Found BME680");
  } else if (hasBME280) {
    SH_DEBUG_PRINTLN("Found BME280");
  }

  //should invoke and check status here.
  EEPROM.begin(EEPROM_SIZE);

#ifndef NO_CONNECTION_PROFILE
  discoverAndSetStatus();
  // statuses: 0 -> initial AP; 1-> active mode client; 2 -> active mode AP

  SH_DEBUG_PRINT("STATUS: ");
  SH_DEBUG_PRINTLN(status);

  if (status == 1) {
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
    boolean toggleLed = false;
    int numTries = 200;

    while (WiFi.status() != WL_CONNECTED && --numTries > 0) {
      delay (250);
      SH_DEBUG_PRINT(".");
      toggleLed = !toggleLed;
      //digitalWrite(STATUS_LED_PIN, toggleLed);
    }

    if (WiFi.status() ==  WL_CONNECTED) {

      //Connected to the network
      SH_DEBUG_PRINT("Connected to:");
      SH_DEBUG_PRINTLN( ssid );
      SH_DEBUG_PRINT( "IP address: " );
      SH_DEBUG_PRINTLN( WiFi.localIP() );

      //Set up MDNS
      //      if (!MDNS.begin("pulse-eco")) {
      //        SH_DEBUG_PRINTLN("Error setting up MDNS responder!");
      //      }
      //      MDNS.addService("http", "tcp", 80);

      //Set up status respond
      //          server.on("/", HTTP_GET, handleStatusGet);
      //          server.on("/check", HTTP_GET, handleStatusCheck);
      //          server.on("/values", HTTP_GET, handleStatusValues);
      //          server.on("/valuesJson", HTTP_GET, handleStatusValuesJSON);
      //          server.on("/reboot", HTTP_POST, handleReboot);
      //          server.on("/reset", HTTP_GET, handleResetRequest);
      //          server.on("/reset", HTTP_POST, handleResetResult);
      //          server.onNotFound(handleStatusGet);
      //          server.begin();


      //digitalWrite(STATUS_LED_PIN, HIGH);
      SH_DEBUG_PRINTLN("Joined network, waiting for modem...");
    } else {

      SH_DEBUG_PRINT("Undable to connect to the network: ");
      SH_DEBUG_PRINTLN( ssid );
      status = 0; //should be reconsidered what this means. LoRaWAN should be OK but wifi setup fails. can potentailly lead to configuration deadlock...
      //should start working in AP mode technically, but with different SSID and same password. retry after couple of sessions or so.
      //same logic should probably happen if it get's disconnected for some reason, so technically this should happen in a back channel.

      //digitalWrite(STATUS_LED_PIN, LOW);
    }
    //    doLoRaWAN();
  }

  if (status != 1) {
    //Input params
    //Start up the web server
    SH_DEBUG_PRINTLN("Setting up configuration web server");
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
  digitalWrite(13, LOW);
  displayInitScreen(true);
}

char hexbuffer[3];

int loopCycleCount = 0;
long noiseTotal = 0;
float pm10 = 0;
float pm25 = 0;
int temp = 0;
int humidity = 0;
int pressure = 0;
int gasResistance = 0;
bool inSending = false;

int noConnectionLoopCount = 0;

void loop() {

  SH_DEBUG_PRINTLN("ENTERS LOOP");

  //  if ( && !inSending)
  if ( status == 1 ) {

    //wait
    delayWithDecency(CYCLE_DELAY);

    //increase counter
    loopCycleCount++;

    //measure noise
    int noiseSessionMax = 0;
    int noiseSessionMin = 1024;
    int currentSample = 0;
    int noiseMeasureLength = millis();
    for (int sample = 0; sample < NUM_NOISE_SAMPLES; sample++) {
      currentSample = analogRead(NOISE_MEASURE_PIN);
      if (currentSample > 0 && currentSample < 1020) {
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
      //something bad has happened, but rather send a 0.
    }

    SH_DEBUG_PRINTLN("AFTER NOISE MEASUREMENT");


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
    SH_DEBUG_PRINTLN_DEC(currentSessionNoise / 4, DEC);
#endif


    noiseTotal += (long)currentSessionNoise;

    if (loopCycleCount >= NUM_MEASURE_SESSIONS) {

      //done measuring
      //measure dust, temp, hum and send data.
      int countTempHumReadouts = 10;
      while (--countTempHumReadouts > 0) {
        if (hasBME680) {
          if (! bme680.performReading()) {
            SH_DEBUG_PRINTLN("BME read failure!");
            //return;
          } else {
            temp = bme680.temperature;
            humidity = bme680.humidity;
            pressure = bme680.pressure / 100;
            gasResistance = bme680.gas_resistance;
          }
        }
        if (hasBME280) {
          temp = bme280.readTemperature();
          humidity = bme280.readHumidity();
          pressure = bme280.readPressure() / 100;
        }

        if (humidity <= 0 || humidity > 100 || temp > 100 || temp < -100 || pressure <= 0) {
          //fake result, pause and try again.
          delayWithDecency(3000);
        } else {
          // OK result
          break;
        }
      }

      int noise = ((long)noiseTotal / loopCycleCount) / 4; // mapped to 0-255

      struct sps30_measurement m;
      char serial[SPS30_MAX_SERIAL_LEN];
      uint16_t data_ready;

      sps30_wake_up();
      sps30_start_measurement();

      //wait just enough for it to get back on its senses
      delayWithDecency(15000);

      ret = sps30_read_data_ready(&data_ready);

      if (ret == 0) {
        pm10SensorOK = sps30_read_measurement(&m);
        Serial.println(ret);
      } else {
        Serial.print("error reading measurement\n");
      }


      sps30_stop_measurement();
      sps30_sleep();
      delayWithDecency(100);

      if (pm10SensorOK != 0) {
        SH_DEBUG_PRINT("Failed to verify PM10 data: ");
        SH_DEBUG_PRINT("pm25: ");
        SH_DEBUG_PRINT_DEC(m.mc_2p5, DEC);
        SH_DEBUG_PRINT(", pm10: ");
        SH_DEBUG_PRINT(m.mc_1p0);
        SH_DEBUG_PRINT_DEC(m.mc_1p0, DEC);
        SH_DEBUG_PRINTLN(".");
      }

      if (pm10SensorOK == 0) {
        SH_DEBUG_PRINT("pm25: ");
        pm25 = m.mc_2p5;
        SH_DEBUG_PRINT_DEC(pm25, DEC);
        SH_DEBUG_PRINT(", pm10: ");
        pm10 = m.mc_1p0;
        SH_DEBUG_PRINT_DEC(pm10, DEC);
      }
      if (noise > 10) {
        SH_DEBUG_PRINT(", noise: ");
        SH_DEBUG_PRINT_DEC(noise, DEC);
      }
      if (hasBME280 || hasBME680) {
        SH_DEBUG_PRINT(", temp: ");
        SH_DEBUG_PRINT_DEC(temp, DEC);
        SH_DEBUG_PRINT(", hum: ");
        SH_DEBUG_PRINT_DEC(humidity, DEC);
        SH_DEBUG_PRINT(", pres: ");
        SH_DEBUG_PRINT_DEC(pressure, DEC);
      }
      if (hasBME680) {
        SH_DEBUG_PRINT(", gasresistance: ");
        SH_DEBUG_PRINT_DEC(gasResistance, DEC);
      }
      SH_DEBUG_PRINTLN("");

      //do the send here

      String url = "/wifipoint/store";
      url += "?devAddr=" + deviceName;
      url += "&version=2";
      if (pm10SensorOK == 0) {
        url += "&pm10=" + String(pm10);
        url += "&pm25=" + String(pm25);
      }
      if (noiseTotal > 10) {
        url += "&noise=" + String(noiseTotal);
      }
      if (hasBME280 || hasBME680) {
        url += "&temperature=" + String(temp);
        url += "&humidity=" + String(humidity);
        url += "&pressure=" + String(pressure);
      }
      if (hasBME680) {
        url += "&gasresistance=" + String(gasResistance);
      }

      SH_DEBUG_PRINTLN(url);

#ifdef DEBUG_PROFILE
      SH_DEBUG_PRINT("Invoking: ");
      SH_DEBUG_PRINTLN(url);
#endif

#ifndef NO_CONNECTION_PROFILE
      SH_DEBUG_PRINT("connecting to ");
      SH_DEBUG_PRINTLN(host);
      if (!client.connect(host, 443)) {
        SH_DEBUG_PRINTLN("Connection failed. Restarting");
        ESP.restart();
        return;
      }
      String userAgent = "WIFI_SENSOR_V2_1";
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
      } else {
        SH_DEBUG_PRINTLN("Transmission failed!");
      }
      //Serial.println("closing connection. CHECK THIS PROPERLY!!!");

      //client.disconnect(); //See how to properly dispose of the connection
#endif

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

      //      packet[0] = 4; //version to be changed to something else
      //      packet[1] = valuesMask;
      //couple of versions shold be used, bitmask sort of present values
      //sps
      //temp/hum/pres
      //gas
      //noise
      //maybe the first byte to have a different version number
      //and a second one to be a mask of the used values.
      //      packet[2] = (byte)hextemp;
      //      packet[3] = (byte)hexhum;
      //      packet[4] = (byte)noise; //noise
      //      packet[5] = (byte)(pm10 / 256);
      //      packet[6] = (byte)(pm10 % 256);
      //      packet[7] = (byte)(pm25 / 256);
      //      packet[8] = (byte)(pm25 % 256);
      //      packet[9] = (byte)(pressure / 256);
      //      packet[10] = (byte)(pressure % 256);

      digitalWrite(13, HIGH);
      SH_DEBUG_PRINTLN("TXing: ");
      for (int i = 0; i < 11; i++) {
        sprintf(hexbuffer, "%02x", (int)packet[i]);
        SH_DEBUG_PRINT(hexbuffer);
        SH_DEBUG_PRINT(" ");
      }
      SH_DEBUG_PRINTLN("");
#ifndef NO_CONNECTION_PROFILE
      // Send it off
      //ttn.sendBytes(packet, sizeof(packet));
      // Start job
      displayValuesOnScreen();
      inSending = true;
      do_send(&sendjob);
#endif
      digitalWrite(13, LOW);



      //reset
      noiseTotal = 0;
      //noiseMax = 0;
      loopCycleCount = 0;


    }

    //  } else if (!inSending) {
  } else {
    server.handleClient();
    delayWithDecency(100);

    noConnectionLoopCount++;

    // second 20 cycles
    // 1 minute 60 * 20 = 1200 cycles
    // 10 minutes 10 * 1200 = 12000  cycles
    if (noConnectionLoopCount >= 12000) {
      //Reboot after 10 minutes in setup mode. Might be a temp failure in the network
      ESP.restart();
    }
  }
  //#ifndef NO_CONNECTION_PROFILE
  //  if (status == 1) {
  //    os_runloop_once();
  //  }
  //#endif
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
      && server.argName(0).equals("wifiMode")
      && server.argName(1).equals("ssid")
      && server.argName(2).equals("password")
      && server.argName(3).equals("devaddr")
      && server.argName(4).equals("nwksKey")
      && server.argName(5).equals("appsKey")) {
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

void delayWithDecency(int units) {
  for (int i = 0; i < units; i += 10) {
    delay(10);
    //    #ifndef NO_CONNECTION_PROFILE
    //        if (status == 1) {
    //          os_runloop_once();
    //        }
    //    #endif
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

void doLoRaWAN() {
  // LMIC init
  os_init();
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();

  // Set static session parameters. Instead of dynamically establishing a session
  // by joining the network, precomputed session parameters are be provided.
#ifdef PROGMEM
  // On AVR, these values are stored in flash and only copied to RAM
  // once. Copy them to a temporary buffer here, LMIC_setSession will
  // copy them into a buffer of its own again.
  uint8_t appskey[sizeof(APPSKEY)];
  uint8_t nwkskey[sizeof(NWKSKEY)];
  memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
  memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
  LMIC_setSession (0x13, DEVADDR, nwkskey, appskey);
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
    LMIC_setTxData2(1, packet, sizeof(packet) - 1, 0);
    SH_DEBUG_PRINTLN(F("Packet queued"));
  }
  // Next TX is scheduled after TX_COMPLETE event.
}

int splitCommand(String* text, char splitChar, String returnValue[], int maxLen) {
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

int countSplitCharacters(String* text, char splitChar) {
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
