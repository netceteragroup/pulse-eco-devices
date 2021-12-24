/*******************************************************************************
 * Do not forget to define the radio type correctly in
 * arduino-lmic/project_config/lmic_project_config.h or from your BOARDS.txt.
 *
 *******************************************************************************/

#define LW_DEVADDR 0x00000000
#define LW_NWKSKEY { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define LW_APPSKEY { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define LW_DATARATE DR_SF7

// Uncomment if you want to test the device without LoRaWAN connectivity
//#define NO_CONNECTION_PROFILE 1
// Uncomment if you want to enable debug lines printing in console and more 2 minutes interval
// USE WITH CARE SINCE IT MIGHT RESULT IN A DEVIVCE BAN FROM pulse.eco IF USED LIVE
#define DEBUG_PROFILE 1

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

#include <SoftwareSerial.h>

#include <bme680.h>
#include <Adafruit_BME680.h>
#include <bme680_defs.h>
#include <Adafruit_BME280.h>

#include "Sds011.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define debugSerial Serial

#ifdef DEBUG_PROFILE
//  #define NUM_MEASURE_SESSIONS 20
//  #define CYCLE_DELAY 2000
  #define NUM_MEASURE_SESSIONS 40
  #define CYCLE_DELAY 10000
  #define SH_DEBUG_PRINTLN(a) debugSerial.println(a)
  #define SH_DEBUG_PRINT(a) debugSerial.print(a)
  #define SH_DEBUG_PRINT_DEC(a,b) debugSerial.print(a,b)
  #define SH_DEBUG_PRINTLN_DEC(a,b) debugSerial.println(a,b)
#else
  #define NUM_MEASURE_SESSIONS 90
  #define CYCLE_DELAY 10000
  #define SH_DEBUG_PRINTLN(a) 
  #define SH_DEBUG_PRINT(a) 
  #define SH_DEBUG_PRINT_DEC(a,b) 
  #define SH_DEBUG_PRINTLN_DEC(a,b) 
#endif


//Sharp Dust Sensor Pins
#define SDS_RX_PIN 23
#define SDS_TX_PIN 22

//OLED pins
#define OLED_SDA 4
#define OLED_SCL 15 
#define OLED_RST 16
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

Adafruit_BME680 bme680; // I2C
Adafruit_BME280 bme280; // I2C


//Noise sensor pins
#define NOISE_MEASURE_PIN 36
#define NUM_NOISE_SAMPLES 1000

SoftwareSerial sdsSerial(SDS_TX_PIN, SDS_RX_PIN); 
sds011::Sds011 sdsSensor(sdsSerial);

static uint8_t mydata[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA,};
byte packet[11];

//Flags
boolean isOkSetup = false;
bool hasBME680 = false;
bool hasBME280 = false;
bool pm10SensorOK = true;

bool hasScreen = true;

byte valuesMask = 0;

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
    delay(1000);
    #endif

    //reset OLED display via software
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);
    delay(20);
    digitalWrite(OLED_RST, HIGH);
    
    //initialize OLED
    Wire.begin(OLED_SDA, OLED_SCL);
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) { // Address 0x3C for 128x32
      SH_DEBUG_PRINTLN(F("SSD1306 allocation failed"));
      hasScreen = false;
    }

    displayInitScreen(false);

    #ifndef NO_CONNECTION_PROFILE
      doLoRaWAN();
      isOkSetup = true;
      SH_DEBUG_PRINTLN("Joined network, waiting for modem...");
    #else
      isOkSetup = true;
    #endif

    SH_DEBUG_PRINTLN("Init SDS sensor.");
    //Init the pm SENSOR
    sdsSerial.begin(9600);
    SH_DEBUG_PRINTLN("Waiting SDS sensor to boot.");
    delayWithDecency(2000);
    SH_DEBUG_PRINTLN("Putting SDS in sleep.");
    sdsSensor.set_sleep(true);

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

    //wait a bit before your start
    delayWithDecency(2000);
    digitalWrite(13, LOW);
    displayInitScreen(true);
    
}

char hexbuffer[3];

int loopCycleCount = 0;
long noiseTotal = 0;
int pm10 = 0;
int pm25 = 0;
int temp = 0; 
int humidity = 0;
int pressure = 0;
int gasResistance = 0;
bool inSending = false;

void loop() {

    if (isOkSetup && !inSending) {
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
      if (currentSample >0 && currentSample < 1020) {
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

      sdsSerial.listen();
      sdsSensor.set_sleep(false);
      sdsSensor.set_mode(sds011::QUERY);
      //wait just enough for it to get back on its senses
      delayWithDecency(15000);
      pm10SensorOK = sdsSensor.query_data_auto(&pm25, &pm10, 10);
      delayWithDecency(100);
      sdsSensor.set_sleep(true);

      if (!pm10SensorOK) {
        SH_DEBUG_PRINT("Failed to verify PM10 data: ");
        SH_DEBUG_PRINT("pm25: ");
        SH_DEBUG_PRINT_DEC(pm25, DEC);
        SH_DEBUG_PRINT(", pm10: ");
        SH_DEBUG_PRINT_DEC(pm10, DEC);
        SH_DEBUG_PRINTLN(".");
      }

  
      if (pm10SensorOK) {
        SH_DEBUG_PRINT("pm25: ");
        SH_DEBUG_PRINT_DEC(pm25, DEC);
        SH_DEBUG_PRINT(", pm10: ");
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
        
      packet[0]=4; //version to be changed to something else
      packet[1]=valuesMask;
      //couple of versions shold be used, bitmask sort of present values
      //sds
      //temp/hum/pres
      //gas
      //noise
      //maybe the first byte to have a different version number
      //and a second one to be a mask of the used values.
      packet[2]=(byte)hextemp;
      packet[3]=(byte)hexhum;
      packet[4]=(byte)noise; //noise
      packet[5]=(byte)(pm10 / 256);
      packet[6]=(byte)(pm10 % 256);
      packet[7]=(byte)(pm25 / 256);
      packet[8]=(byte)(pm25 % 256);
      packet[9]=(byte)(pressure / 256);
      packet[10]=(byte)(pressure % 256);

      digitalWrite(13, HIGH);
      SH_DEBUG_PRINTLN("TXing: ");
      for(int i = 0; i < 11; i++) {
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
      
  } else if (!inSending) {
    delayWithDecency(100);
  }
  #ifndef NO_CONNECTION_PROFILE
  os_runloop_once();
  #endif
}

void delayWithDecency(int units) {
  for (int i=0; i<units; i+=10) {
    delay(10);
    #ifndef NO_CONNECTION_PROFILE
    os_runloop_once();
    #endif
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
    invertDisplay%=-2;
    display.setTextSize(1);
    display.setCursor(5,2);
    display.println("pulse.eco");
    
    display.setCursor(2,20);
    display.print("pm10:");
    if (pm10SensorOK) {
      display.setCursor(35,20);
      display.print(pm10,DEC);
    }
    
    display.setCursor(2,30);
    display.print("pm25:");
    if (pm10SensorOK) {
      display.setCursor(35,30);
      display.print(pm25,DEC);
    }

    display.setCursor(2,40);
    display.print("temp:");
    if (hasBME680 || hasBME280) {
      display.setCursor(35,40);
      display.print(temp,DEC);
    }

    display.setCursor(2,50);
    display.print("humi:");
    if (hasBME680 || hasBME280) {
      display.setCursor(35,50);
      display.print(humidity,DEC);
    }

    display.setCursor(2,60);
    display.print("pres:");
    if (hasBME680 || hasBME280) {
      display.setCursor(35,60);
      display.print(pressure,DEC);
    }
      
    display.display();
  }
}

void displayInitScreen(bool waiting) {
    display.clearDisplay();
    display.setRotation(3);
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(5,40);
    display.println("pulse.eco");

    if (!waiting) {
      display.setCursor(8,50);
      display.println("Init ...");
    } else {
      display.setCursor(2,50);
      display.println("Waiting on");
      display.setCursor(2,60);
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
    LMIC_setDrTxpow(LW_DATARATE,14);
}

void onEvent (ev_t ev) {
    SH_DEBUG_PRINT(os_getTime());
    SH_DEBUG_PRINT(": ");
    switch(ev) {
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

void do_send(osjob_t* j){
    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND) {
        SH_DEBUG_PRINTLN(F("OP_TXRXPEND, not sending"));
    } else {
        // Prepare upstream data transmission at the next possible time.
        LMIC_setTxData2(1, packet, sizeof(packet)-1, 0);
        SH_DEBUG_PRINTLN(F("Packet queued"));
    }
    // Next TX is scheduled after TX_COMPLETE event.
}
