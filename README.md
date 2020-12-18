# t-display_controller
an MQTT buttons controller with tft screen based on the ttgo t-display board

## takeover
sample based on https://github.com/ESP32Home/t-display_factory_test
# built versions
PACKAGES:
 - framework-arduinoespressif32 3.10004.201016 (1.0.4)
 - tool-esptoolpy 1.20600.0 (2.6.0)
 - tool-mkspiffs 2.230.0 (2.30)
 - toolchain-xtensa32 2.50200.80 (5.2.0)

Dependency Graph
|-- <TFT_eSPI> 2.3.52+sha.49cdca6    
|   |-- <SPI> 1.0
|   |-- <FS> 1.0
|   |-- <SPIFFS> 1.0
|   |   |-- <FS> 1.0
|-- <GfxUi> 1.0.0+sha.64d5ced        
|   |-- <TFT_eSPI> 2.3.52+sha.49cdca6
|   |   |-- <SPI> 1.0
|   |   |-- <FS> 1.0
|   |   |-- <SPIFFS> 1.0
|   |   |   |-- <FS> 1.0
|   |-- <SPI> 1.0
|   |-- <FS> 1.0
|   |-- <SPIFFS> 1.0
|   |   |-- <FS> 1.0
|-- <ArduinoJson> 6.16.1
|-- <MQTT> 2.4.7
|-- <SPI> 1.0
|-- <FS> 1.0
|-- <SPIFFS> 1.0
|   |-- <FS> 1.0
|-- <WiFi> 1.0
|-- <Wire> 1.0.1
|-- <Button2>
