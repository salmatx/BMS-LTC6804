# BMS-LTC6804

Battery management system for measuring voltage up to 5 battery cells in range 0.5 V to 2 V.

## Features

- Up to 5 battery cells
- ESP 32 microcontroller
- Transfer of measured data using MQTT

## Configuration

Projects uses ESP-IDF framework. It is recommended to use sdkconfig settings as follows:

**Component config --> ESP System Settings**

- Set Enable Task Watchdog Timer to Enabled
- Set Initialize Task Watchdog Timer to Disabled
- Set Interrupt watchdog to Enabled
- Set value of Interrupt watchdog timeout to 1000 ms

**Component config --> FreeRTOS --> Kernel**

- Set configTICK_RATE_HZ to 1000 Hz

**Serial flasher config --> Flash size**

- Set Flash size to 4MB

**BMS Network Configuration**

- Set Wi-Fi SSID of used AP
- Set Wi-Fi password of used AP
- Set MQTT broker URI of used MQTT broker
