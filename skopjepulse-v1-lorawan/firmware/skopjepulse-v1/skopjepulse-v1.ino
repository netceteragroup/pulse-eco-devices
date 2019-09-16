#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <DHT.h>
#include "rn2483.h"
#include "Sds011.h"


//Development / production profiles
//#define NO_CONNECTION_PROFILE 1
//#define DEBUG_PROFILE 1

#ifdef DEBUG_PROFILE
  #define NUM_MEASURE_SESSIONS 20
  #define CYCLE_DELAY 2000
  #define SH_DEBUG_PRINTLN(a) Serial.println(a)
  #define SH_DEBUG_PRINT(a) Serial.print(a)
  #define SH_DEBUG_PRINT_DEC(a,b) Serial.print(a,b)
  #define SH_DEBUG_PRINTLN_DEC(a,b) Serial.println(a,b)
#else
  #define NUM_MEASURE_SESSIONS 90
  #define CYCLE_DELAY 10000
  #define SH_DEBUG_PRINTLN(a) Serial.println(a)
  #define SH_DEBUG_PRINT(a) Serial.print(a)
  #define SH_DEBUG_PRINT_DEC(a,b) Serial.print(a,b)
  #define SH_DEBUG_PRINTLN_DEC(a,b) Serial.println(a,b)

#endif

//DHT 22 Temp/Hum sensor PINS
#define DHTPIN 2
#define SENSOR_DHT22 1
#define DHTTYPE DHT22

//Sharp Dust Sensor Pins
#define SDS_RX_PIN 5
#define SDS_TX_PIN 6

//RN2483 Pins
#define RN2483_RST_PIN 12
#define RN2483_RX_ARD_TX 11
#define RN2483_TX_ARD_RX 10

//Noise sensor pins
#define NOISE_MEASURE_PIN A1
#define NUM_NOISE_SAMPLES 1000
 
//Init global objects
SoftwareSerial rn2483Serial(RN2483_TX_ARD_RX, RN2483_RX_ARD_TX); // RX, TX
DHT dht(DHTPIN, DHTTYPE);
rn2483 rn2483Lora(rn2483Serial);
SoftwareSerial sdsSerial(SDS_RX_PIN, SDS_TX_PIN); // RX, TX
sds011::Sds011 sdsSensor(sdsSerial);

//EEPROM Data
const int EEPROM_SIZE = 78;
char eepromData[EEPROM_SIZE+1];
String appeui = "<app eui goes here>";
String nwsKey;
String appsKey;
String devAddr;
int dataRate=5;
boolean isOkSetup = false;

void(* resetFunc) (void) = 0;

// the setup routine runs once when you press reset:
void setup() {

  // Open serial communications and wait for port to open:
  
  Serial.begin(57600);
  
  rn2483Serial.begin(9600);

  //Init the temp/hum sensor
  dht.begin();
  
  SH_DEBUG_PRINTLN("Startup");
  #ifndef NO_CONNECTION_PROFILE
    //reset rn2483
    pinMode(RN2483_RST_PIN, OUTPUT);
    digitalWrite(RN2483_RST_PIN, LOW);
    delay(500);
    digitalWrite(RN2483_RST_PIN, HIGH);
  
    //initialise the rn2483 module
    rn2483Lora.autobaud();
    
    //print out the HWEUI so that we can register it via ttnctl
    SH_DEBUG_PRINTLN(rn2483Lora.hweui());
    SH_DEBUG_PRINTLN(rn2483Lora.sysver());
  #endif
      
  //Init the pm SENSOR
  sdsSerial.begin(9600);
  delay(2000);
  sdsSensor.set_sleep(true);

  delay(2000);
  
  #ifndef NO_CONNECTION_PROFILE
    SH_DEBUG_PRINTLN("Read EEPROM");
    //expected EEPROM DATA: "[Nwskey,AppsKey,DevId,DataRate]"  [32,32,8,2]  78 chars in total
    for (int i=0; i<EEPROM_SIZE; i++) {
      eepromData[i] = EEPROM.read(i);
    }
    eepromData[EEPROM_SIZE] = 0;
  
    if (eepromData[0] == '['
      && eepromData[EEPROM_SIZE-1] == ']'
      && eepromData[33] == ','
      && eepromData[66] == ','
      && eepromData[75] == ',') {
        //It's good
        String whole = String(eepromData);
        nwsKey = whole.substring(1,33);
        appsKey = whole.substring(34,66);
        devAddr = whole.substring(67, 75);
        dataRate = (int)(whole[EEPROM_SIZE - 2] - '0');
  //      SH_DEBUG_PRINTLN("Read success.");
  //      SH_DEBUG_PRINT("nwsKey: ");
  //      SH_DEBUG_PRINTLN(nwsKey);
  //      SH_DEBUG_PRINT("appsKey: ");
  //      SH_DEBUG_PRINTLN(appsKey);
        SH_DEBUG_PRINT("ID: ");
        SH_DEBUG_PRINTLN(devAddr);
        SH_DEBUG_PRINT("DR: ");
        SH_DEBUG_PRINTLN_DEC(dataRate,DEC);
        rn2483Serial.listen();
        rn2483Lora.init(&appeui, &nwsKey, &appsKey, &devAddr, dataRate);
        isOkSetup = true;
        SH_DEBUG_PRINTLN("Setup OK");
      } else {
        //EEPROM not good
        SH_DEBUG_PRINTLN("Setup NOT OK!");
      }
   #else
     isOkSetup = true;
   #endif

  //wait a bit before your start
  delay(2000);
}

int EEPROM_read_index=0;

//#ifdef DEBUG_PROFILE
void serialEvent() {
  SH_DEBUG_PRINTLN("Receiving...");
  eepromData[EEPROM_SIZE] = 0;
  
  while (EEPROM_read_index < EEPROM_SIZE && Serial.available() > 0) {
    eepromData[EEPROM_read_index] = (char)Serial.read();
    if (eepromData[EEPROM_read_index] == ']' && EEPROM_read_index == EEPROM_SIZE - 1) {

      SH_DEBUG_PRINTLN("Received & Reboot");
      //done
      //write to EEPROM
      for (int j=0; j<=EEPROM_read_index; j++) {
        EEPROM.write(j, (byte)eepromData[j]);
      }
      resetFunc();
    }
    EEPROM_read_index++;
    delay(50);
  }
  SH_DEBUG_PRINT("Received: ");
  SH_DEBUG_PRINTLN(eepromData);
}
//#endif

byte packet[8];
char hexbuffer[3];

int loopCycleCount = 0;
long noiseTotal = 0;
int pm10 = 0;
int pm25 = 0;

void loop() {
  if (isOkSetup) {
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
      int temp = 0; 
      int humidity = 0;
      while (temp == 0 && humidity == 0 && --countTempHumReadouts > 0) {
        temp = (int) dht.readTemperature();
        humidity = (int) dht.readHumidity();
        if (temp == 0 && humidity == 0) {
          //fake result, pause and try again.
          delay(3000);
        }
      }

      int noise = ((long)noiseTotal / loopCycleCount) / 4; // mapped to 0-255
      sdsSerial.listen();
      sdsSensor.set_sleep(false);
      sdsSensor.set_mode(sds011::QUERY);
      //wait just enough for it to get back on its senses
      delay(15000);
      sdsSensor.query_data_auto(&pm25, &pm10, 10);
      delay(100);
      sdsSensor.set_sleep(true);
  
      SH_DEBUG_PRINT("pm25: ");
      SH_DEBUG_PRINT_DEC(pm25, DEC);
      SH_DEBUG_PRINT("ug/m3, pm10: ");
      SH_DEBUG_PRINT_DEC(pm10, DEC);
      SH_DEBUG_PRINT("ug/m3, noise: ");
      SH_DEBUG_PRINT_DEC(noise, DEC);
      SH_DEBUG_PRINT(", temp: ");
      SH_DEBUG_PRINT_DEC(temp, DEC);
      SH_DEBUG_PRINT(", hum: ");
      SH_DEBUG_PRINTLN_DEC(humidity, DEC);
    
      int hextemp = min(max(temp + 127, 0), 255);
      int hexhum = min(max(humidity, 0), 255);
    
    //    dataframe
    //- 1 byte: version
    //- 1 byte: temperature (offsetted 127)
    //- 1 byte: humidity
    //- 1 byte: noise (scaled from 0 to 255)
    //- 2 bytes: pm10
    //- 2 bytes: pm25
        
      packet[0]=1; //version
      packet[1]=(byte)hextemp;
      packet[2]=(byte)hexhum;
      packet[3]=(byte)noise; //noise
      packet[4]=(byte)(pm10 / 256);
      packet[5]=(byte)(pm10 % 256);
      packet[6]=(byte)(pm25 / 256);
      packet[7]=(byte)(pm25 % 256);

      #ifndef NO_CONNECTION_PROFILE
        rn2483Serial.listen();
        rn2483Lora.txUncnf(packet, 8); 
        SH_DEBUG_PRINTLN("TXed: ");
        for(int i=0; i<8; i++) {
          sprintf(hexbuffer, "%02x", (int)packet[i]);
          SH_DEBUG_PRINT(hexbuffer);
          SH_DEBUG_PRINT(" ");
        }
        SH_DEBUG_PRINTLN("");
      #endif

      //reset
      noiseTotal = 0;
      //noiseMax = 0;
      loopCycleCount = 0;

      
    }
      
  } else {
    delay(100);
  }

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


