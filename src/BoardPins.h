#pragma once

// Xteink X3/X4 e-ink panel SPI pins.
// Verified against CrossInk's lib/hal/HalGPIO.h (same repo this project was
// scoped from) - these are custom pins, not the ESP32-C3's default SPI pins.
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

// SD card and display share this SPI MISO line on X3/X4. eNMEA does not use
// the SD card, so it is not wired up here - only listed for reference.
#define SD_SPI_MISO 7
