#pragma once
// Pinagem fixa do armário. Toda referência a GPIO no firmware passa por aqui.

// PN532 em SPI (VSPI)
#define PIN_NFC_SCK 18
#define PIN_NFC_MISO 19
#define PIN_NFC_MOSI 23
#define PIN_NFC_SS 4

// OLED SSD1306 em I2C
#define PIN_OLED_SDA 21
#define PIN_OLED_SCL 22
#define OLED_I2C_ADDR 0x3C

// Servo da tranca (LEDC a 50 Hz)
#define PIN_SERVO 13

// GPIO 25 é RTC GPIO: é o único sensor usado como fonte de wake-up.
#define PIN_REED_DOOR 25

// Microswitches: C ao GND, NA ao GPIO. Pressionado (notebook presente) = LOW.
#define PIN_SLOT_1 26
#define PIN_SLOT_2 27

#define PIN_LED_GREEN 32
#define PIN_LED_RED 33

// Base do BC547 via 1 kΩ. Nível ALTO = toca.
#define PIN_BUZZER 14

// Segundo atuador (pendência de hardware — ver README).
// GPIO 16/17 ficam livres porque o módulo WROOM-32 não tem PSRAM.
#define PIN_AUX_ACTUATOR 16
