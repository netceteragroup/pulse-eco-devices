# Disclaimer

This is still just a test code. Not yet ready to be used for live sensor device! It is tested with the TTGO LoRa32 V1 board only.

# Install the ESP32 board in Arduino IDE

Details available [here](https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/)

# Needed libraries

In order for the sketch to work, these libraries must be installed. Some of them you might need to install them manually:

- MCCI LoRaWAN LMIC Library v. 4.1.0
- MCCI Arduino LoRaWAN Library v. 0.9.1
- ESPSoftwareSerial v. 6.14.1 ?
- Adafruit Unified Sensor v. 1.0.2
- Adafruit BME280 Library v. 1.0.7
- Adafruit BME680 Library v. 1.0.7
- Adafruit GFX Library v. 1.4.13
- Adafruit SSD1306 Library v. 1.2.9

# Configure MCCI LMIC

Before using the LMIC lib, it needs to be configured propertly for the region. Find the lmic_project_config.h within the library itself. On MacOS, that is in ``~/Documents/Arduino/libraries/MCCI_LoRaWAN_LMIC_library/project_config `` . Edit the file so that it's configured for eu868:

```
// project-specific definitions
#define CFG_eu868 1
//#define CFG_us915 1
//#define CFG_au915 1
//#define CFG_as923 1
// #define LMIC_COUNTRY_CODE LMIC_COUNTRY_CODE_JP      /* for as923-JP; also define CFG_as923 */
//#define CFG_kr920 1
//#define CFG_in866 1
#define CFG_sx1276_radio 1
//#define LMIC_USE_INTERRUPTS
```

At the moment, the source code is configured to work with the TTGO LoRa32 V1. If you need to use a different ESP32 + RFM device, you might need to adapt the pin settings:

```
// Pin mapping
// Allegedly compatible with TTGO LoRa32 V1
const lmic_pinmap lmic_pins = {
    .nss = 18,                       
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 14,                       
    .dio = {26, 33, 32}, 
};
```