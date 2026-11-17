// Waveshare ESP32-S3-Touch-AMOLED-2.16 onboard wiring
// Source: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16
#pragma once

// AMOLED, CO5300 over QSPI, 480x480
#define PIN_LCD_CS    12
#define PIN_LCD_SCLK  38
#define PIN_LCD_SDIO0 4
#define PIN_LCD_SDIO1 5
#define PIN_LCD_SDIO2 6
#define PIN_LCD_SDIO3 7
#define PIN_LCD_RST   39
#define LCD_WIDTH     480
#define LCD_HEIGHT    480

// Shared onboard I2C: touch, AXP2101, QMI8658, PCF85063, ES8311, ES7210
#define PIN_I2C_SDA 15
#define PIN_I2C_SCL 14

// Touch, CST9220
#define PIN_TP_INT 11
#define PIN_TP_RST 40

// IMU interrupts, RTC interrupt
#define PIN_IMU_INT1 17
#define PIN_IMU_INT2 21
#define PIN_RTC_INT  13

// Audio: ES8311 (DAC) + ES7210 (mics) share one I2S bus
#define PIN_I2S_MCLK 42
#define PIN_I2S_BCLK 9
#define PIN_I2S_LRCK 45
#define PIN_I2S_DOUT 8   // ESP -> ES8311
#define PIN_I2S_DIN  10  // ES7210 -> ESP
#define PIN_SPK_PA   46  // onboard speaker amp enable

// TF card (SD 1-bit)
#define PIN_SD_CMD 1
#define PIN_SD_CLK 2
#define PIN_SD_D0  3

// User button (pulled up, low when pressed)
#define PIN_KEY 18
