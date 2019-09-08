#include <EEPROM.h>
#include <SoftwareSerial.h>
#include "Sds011.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h> 
#include <ESP8266WebServer.h>

// General def for adafruit sensor
#include <Adafruit_Sensor.h>

// Includes for BME680 
#include <bme680.h>
#include <Adafruit_BME680.h>
#include <bme680_defs.h>

// Includes for BME280
#include <Adafruit_BME280.h>

#define SEALEVELPRESSURE_HPA (1013.25)

//Development / production profiles
//#define NO_CONNECTION_PROFILE 1
//#define DEBUG_PROFILE 1
#ifdef DEBUG_PROFILE
  #define NUM_MEASURE_SESSIONS 10
  #define CYCLE_DELAY 2000
#else
  #define NUM_MEASURE_SESSIONS 30
  #define CYCLE_DELAY 30000
#endif
#define SH_DEBUG_PRINTLN(a) Serial.println(a)
#define SH_DEBUG_PRINT(a) Serial.print(a)
#define SH_DEBUG_PRINT_DEC(a,b) Serial.print(a,b)
#define SH_DEBUG_PRINTLN_DEC(a,b) Serial.println(a,b)

//Sharp Dust Sensor Pins
#define SDS_RX_PIN D5
#define SDS_TX_PIN D6

//Noise sensor pins
#define NOISE_MEASURE_PIN A0
#define NUM_NOISE_SAMPLES 1200

//Status LED pin
#define STATUS_LED_PIN D0
 
//Init global objects
Adafruit_BME680 bme680; // I2C
Adafruit_BME280 bme280; // I2C
SoftwareSerial sdsSerial(SDS_RX_PIN, SDS_TX_PIN); // RX, TX
sds011::Sds011 sdsSensor(sdsSerial);
ESP8266WebServer server(80);
WiFiClientSecure client;

//EEPROM Data
const int EEPROM_SIZE = 256;
String deviceName="";
String ssid="";
String password="";

//Flags
int status = -1;
bool hasBME680 = false;
bool hasBME280 = false;

// TCP + TLS
IPAddress apIP(192, 168, 1, 1);
const char *ssidDefault = "PulseEcoSensor";

const char* host = "pulse.eco";
const char* fingerprint = "2F 83 76 75 26 FA F2 17 CA 83 90 AC CF 34 05 4F 37 65 F5 FB";

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
    //It's still not connected to anything
    //broadcast net and display form
    SH_DEBUG_PRINTLN("Setting status code to 0: dipslay config options.");
    digitalWrite(STATUS_LED_PIN, LOW);
    status = 0;
    
  } else {
    
    
    status = 1;
    digitalWrite(STATUS_LED_PIN, HIGH);
    SH_DEBUG_PRINTLN("Initially setting status to 1: try to connect to the network.");

  }
}




// the setup routine runs once when you press reset:
void setup() {

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  // Open serial communications and wait for port to open:

  Serial.begin(57600);
   SH_DEBUG_PRINTLN("Startup");
  
  //Init the temp/hum sensor
  // Set up oversampling and filter initialization
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
    if (!bme280.begin(0x76)) {
      if (!bme280.begin(0x77)) {
        SH_DEBUG_PRINTLN("Could not find a valid BME280 sensor, check wiring!");
        hasBME280 = false;
      }
    }
  }

  if (hasBME680) {
    SH_DEBUG_PRINTLN("Found a BME680 sensor attached");
  } else if (hasBME280) {
    SH_DEBUG_PRINTLN("Found a BME280 sensor attached");
  }

  //Init the pm SENSOR
  sdsSerial.begin(9600);
  delay(2000);
  sdsSensor.set_sleep(true);

  delay(2000);

  EEPROM.begin(EEPROM_SIZE);

  #ifndef NO_CONNECTION_PROFILE
    discoverAndSetStatus();
  
    if (status == 1) {
      //Try to connect to the network
      SH_DEBUG_PRINTLN("Trying to connect...");
      char ssidBuf[ssid.length()+1];
      ssid.toCharArray(ssidBuf,ssid.length()+1);
      char passBuf[password.length()+1];
      password.toCharArray(passBuf,password.length()+1);
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
        digitalWrite(STATUS_LED_PIN, toggleLed); 
      }
  
      if (WiFi.status() != WL_CONNECTED) {
        SH_DEBUG_PRINT("Undable to connect to the network: ");
        SH_DEBUG_PRINTLN( ssid );
        status = 0;
        digitalWrite(STATUS_LED_PIN, LOW);
      } else {
        //Connected to the network
        SH_DEBUG_PRINT("Connected to:");
        SH_DEBUG_PRINTLN( ssid );
        SH_DEBUG_PRINT( "IP address: " );
        SH_DEBUG_PRINTLN( WiFi.localIP() );
        digitalWrite(STATUS_LED_PIN, HIGH);
      }
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
      
        for (int i=0; i<AP_NameString.length(); i++)
          AP_NameChar[i] = AP_NameString.charAt(i);
        
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
        WiFi.softAP(AP_NameChar);
        delay(500);
        server.on("/", HTTP_GET, handleRootGet);      
        server.on("/post", HTTP_POST, handleRootPost);
        server.onNotFound(handleRootGet);
        server.begin();
        SH_DEBUG_PRINTLN("HTTP server started");
        SH_DEBUG_PRINT("AP IP address: ");
        SH_DEBUG_PRINTLN(apIP);
        
    }
  #else
   status = 1;
  #endif


  

  //wait a bit before your start
  delay(2000);
}

int loopCycleCount = 0;
int noiseTotal = 0;
int pm10 = 0;
int pm25 = 0;


// No Connection counter
int noConnectionLoopCount = 0;


void loop() {
  if (status == 1) {
    //wait
    delay(CYCLE_DELAY);

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

    //This will be different on Arduino and ESP. Probably need to attenuate the ESP a bit.
    //TODO: see up to which freq. is needed to sample in order to keep into 'noise' band.
    //Its fine: Noise measurement took: 95ms with 1000 samples.
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
    
    
    
    noiseTotal += currentSessionNoise;
    
    if (loopCycleCount >= NUM_MEASURE_SESSIONS) {
      //done measuring
      //measure dust, temp, hum and send data.
      int countTempHumReadouts = 10;
      int temp = 0; 
      int humidity = 0;
      int pressure = 0;
      int altitude = 0;
      int gasResistance = 0;
      while (--countTempHumReadouts > 0) {
        if (hasBME680) {
          if (! bme680.performReading()) {
            SH_DEBUG_PRINTLN("Failed to perform BME reading!");
            //return;
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
          delay(3000);
        } else {
          // OK result
          break;
        }
      }

      if (countTempHumReadouts <=0) {
        //failed to read temp/hum/pres/gas
        //disable BME sensors
        hasBME680 = false;
        hasBME280 = false;
      }
      
      int noise = ((int)noiseTotal / loopCycleCount) / 4 + 10; //mapped to 0-255
      
      bool pm10SensorOK = true;
      //sdsSerial.listen();
      sdsSensor.set_sleep(false);
      sdsSensor.set_mode(sds011::QUERY);
      //wait just enough for it to get back on its senses
      delay(15000);
      pm10SensorOK = sdsSensor.query_data_auto(&pm25, &pm10, 10);
      delay(100);
      sdsSensor.set_sleep(true);

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
        SH_DEBUG_PRINT(", alt: ");
        SH_DEBUG_PRINT_DEC(altitude, DEC);
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
      if (pm10SensorOK) {
        url += "&pm10=" + String(pm10);
        url += "&pm25=" + String(pm25);
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
        if (!client.connect(host, 443)) {
          SH_DEBUG_PRINTLN("Connection failed. Restarting");
          ESP.restart();
          return;
        }
  
        if (client.verify(fingerprint, host)) {
          SH_DEBUG_PRINTLN("certificate matches");
        } else {
          SH_DEBUG_PRINTLN("certificate doesn't match! Restarting");
          ESP.restart();
        }
  
        client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "User-Agent: WIFI_SENSOR_V2\r\n" +
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
    
      //reset
      noiseTotal = 0;
      loopCycleCount = 0;

      
    }
      
  } else {
    server.handleClient();
    delay(50);
    noConnectionLoopCount++;
    // second 20 cycles
    // 1 minute 60 * 20 = 1200 cycles
    // 10 minutes 10 * 1200 = 12000  cycles
    if(noConnectionLoopCount >= 12000) {
      //Reboot after 10 minutes in setup mode. Might be a temp failure in the network
      ESP.restart();
    }
    
  }

}


//Web server params below
void handleRootGet() {

  String output = "<!DOCTYPE html> \
<html><head><title>Configure WiFi</title>";
  output += "<style>b ";
  output += "body { font-family: 'Verdana'} ";
  output += "div { font-size: 2em;} ";
  output += "input[type='text'] { font-size: 1.5em; width: 100%;} ";
  output += "input[type='submit'] { font-size: 1.5em; width: 100%;} ";
  output += "input[type='radio'] { display: none;} ";
  output += "input[type='radio'] { ";
  output += "   height: 2.5em; width: 2.5em; display: inline-block;cursor: pointer; ";
  output += "   vertical-align: middle; background: #FFF; border: 1px solid #d2d2d2; border-radius: 100%;} ";
  output += "input[type='radio'] { border-color: #c2c2c2;} ";
  output += "input[type='radio']:checked { background:gray;} ";
  output += "</style>";
  output += "</head> \
<body> \
<div style='text-align: center; font-weight: bold'>SkopjePulse node config</div> \
<form method='post' action='/post'> \
<div>Device key:</div> \
<div><input type='text' name='deviceId' /><br/><br/></div> \
<div>SSID:</div> \
<div><input type='text' name='ssid' /><br/><br/></div> \
<div>Password:</div> \
<div><input type='text' name='password' /><br/><br/></div> \
<div><input type='submit' /><div> \
</form></body></html>";

  server.send(200, "text/html", output);
}

void handleRootPost() {
  
  SH_DEBUG_PRINT("Number of args:");
  SH_DEBUG_PRINTLN(server.args());
  for (int i=0; i<server.args(); i++) {
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
    data.replace("+"," ");

    if (data.length() < EEPROM_SIZE) {
      server.send(200, "text/html", "<h1>The device will restart now.</h1>");
      //It's ok

      SH_DEBUG_PRINTLN("Storing data in EEPROM:");
      #ifdef DEBUG_PROFILE
        SH_DEBUG_PRINTLN(data);
      #endif
      for (int i=0; i < data.length(); i++) {
        EEPROM.write(i, (byte)data[i]);
      }
      EEPROM.commit();
      delay(500);

      SH_DEBUG_PRINTLN("Stored to EEPROM. Restarting.");
      ESP.restart();
      
    } else {
      server.send(200, "text/html", "<h1>The parameter string is too long.</h1>");
    }
    
  } else {
    server.send(200, "text/html", "<h1>Incorrect input. Please try again.</h1>");
  }
  
}


// Util methods below

int splitCommand(String* text, char splitChar, String returnValue[], int maxLen) {
  int splitCount = countSplitCharacters(text, splitChar);
  SH_DEBUG_PRINT("Split count: ");
  SH_DEBUG_PRINTLN_DEC(splitCount, DEC);
  if (splitCount + 1 > maxLen) {
    return -1;
  }

  int index = -1;
  int index2;

  for(int i = 0; i <= splitCount; i++) {
//    index = text->indexOf(splitChar, index + 1);
    index2 = text->indexOf(splitChar, index + 1);

    if(index2 < 0) index2 = text->length();
    returnValue[i] = text->substring(index+1, index2);
    index = index2;
  }

  return splitCount + 1;
}

int countSplitCharacters(String* text, char splitChar) {
 int returnValue = 0;
 int index = -1;

 while (true) {
   index = text->indexOf(splitChar, index + 1);

   if(index > -1) {
    returnValue+=1;
   } else {
    break;
   }
 }

 return returnValue;
} 





void bubbleSort(short A[],int len) {
  unsigned long newn;
  unsigned long n=len;
  short temp=0;
  do {
    newn=1;
    for(int p=1;p<len;p++){
      if(A[p-1]>A[p]){
        temp=A[p];           //swap places in array
        A[p]=A[p-1];
        A[p-1]=temp;
        newn=p;
      } //end if
    } //end for
    n=newn;
  } while(n>1);
}


short median(short sorted[],int m) //calculate the median
{
  //First bubble sort the values: https://en.wikipedia.org/wiki/Bubble_sort
  bubbleSort(sorted,m);  // Sort the values
  if (bitRead(m,0)==1) {  //If the last bit of a number is 1, it's odd. This is equivalent to "TRUE". Also use if m%2!=0.
    return sorted[m/2]; //If the number of data points is odd, return middle number.
  } else {    
    return (sorted[(m/2)-1]+sorted[m/2])/2; //If the number of data points is even, return avg of the middle two numbers.
  }
}


