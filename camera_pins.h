// camera_pins.h
// Pin definitions for XIAO ESP32S3 Camera
#pragma once

// Camera does not have PWDN and RESET control pins
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1

// Camera clock and I2C pins
#define XCLK_GPIO_NUM     10
#define SIOC_GPIO_NUM     39  // I2C SCL (Camera control)
#define SIOD_GPIO_NUM     40  // I2C SDA (Camera control)

// Camera data pins (8-bit parallel interface)
#define Y2_GPIO_NUM       15  // Data bit 0
#define Y3_GPIO_NUM       17  // Data bit 1
#define Y4_GPIO_NUM       18  // Data bit 2
#define Y5_GPIO_NUM       16  // Data bit 3
#define Y6_GPIO_NUM       14  // Data bit 4
#define Y7_GPIO_NUM       12  // Data bit 5
#define Y8_GPIO_NUM       11  // Data bit 6
#define Y9_GPIO_NUM       48  // Data bit 7

// Camera sync pins
#define VSYNC_GPIO_NUM    38  // Vertical sync
#define HREF_GPIO_NUM     47  // Horizontal reference
#define PCLK_GPIO_NUM     13  // Pixel clock

/* 
 * Alternative SD Card pins if needed:
 * You can also try these pins if the ones in main code don't work:
 * 
 * #define SD_CS_PIN     4   // Alternative CS pin
 * #define SD_MOSI_PIN   6   // Alternative MOSI
 * #define SD_MISO_PIN   5   // Alternative MISO  
 * #define SD_SCK_PIN    3   // Alternative SCK
 * 
 * Or use the default SPI pins:
 * CS: GPIO 21, MOSI: GPIO 8, MISO: GPIO 9, SCK: GPIO 7
 */