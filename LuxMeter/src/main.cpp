/**
 *  @file    main.cpp
 *  @brief   Arduino Lux Meter with OLED Graph
 *  @author  KrizTioaN (christiaanboersma@hotmail.com)
 *  @date    2021-08-06
 *  @note    BSD-3 licensed
 *  @details Dim hallway                           0.1 lux
 *           Moonlit night                         1   lux
 *           Dark room                            10   lux
 *           Dark overcast day / Bright room     100   lux
 *           Overcast day                       1000   lux
 *
 ***********************************************/

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

#define LED_PIN 2
#define BUTTON_PIN 3

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0);

// 1kOhm resistor in voltage divider
#define KLS6_PIN 0
static float lux_accumulator = 0.0f;
void kls_read() {
  float Vout = (float)analogRead(KLS6_PIN),
        Rphoto = 1.0f * (1023.0f - Vout) / Vout,              // in kOhm
      lux = pow(10.0f, (3.2784f - log10(Rphoto) * 1.63675f)); // from datasheet
  lux_accumulator = 0.9f * lux_accumulator + 0.1f * lux;
}

#define NHISTORY 98
static float lux_history[NHISTORY] = {0};
void history() {
  memmove((void *)&(lux_history[1]), lux_history,
          (NHISTORY - 1) * sizeof(float));
  lux_history[0] = lux_accumulator;
}

void setup() {
  delay(3000);

  u8g2.begin();
  u8g2.setContrast(0x00);

  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

static unsigned oled_accumulator = 0ul;

void draw() {

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_inr16_mr);
  unsigned int len = u8g2.drawStr(0, 15, "LUX");
  u8g2.setFont(u8g2_font_5x8_tf);
  char flt[11];
  snprintf(flt, 7, "%6lu", (unsigned long)lux_accumulator);
  u8g2.drawStr(98, 30, flt);
  if (oled_accumulator >= 500ul)
    u8g2.drawDisc(118, 9, 8, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_LOWER_LEFT);
  else
    u8g2.drawDisc(118, 9, 8, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_RIGHT);
  u8g2.drawCircle(118, 9, 9);
  float lux_max = lux_history[0], lux_min = lux_history[0];
  for (unsigned int i = 1; i < NHISTORY; i++) {
    if (lux_history[i] > lux_max)
      lux_max = lux_history[i];
    if (lux_history[i] < lux_min)
      lux_min = lux_history[i];
  }
  for (unsigned int i = 0; i < NHISTORY; i++) {
    int value =
        1 + (int)(15.0 * (lux_history[i] - lux_min) / (lux_max - lux_min));
    u8g2.drawVLine(i, 32 - value, value);
  }
  snprintf(flt, 10, "max: %-5lu", (unsigned long)lux_max);
  u8g2.drawStr(len + 2, 6, flt);
  snprintf(flt, 10, "min: %-5lu", (unsigned long)lux_min);
  u8g2.drawStr(len + 2, 15, flt);

  u8g2.sendBuffer();
}

bool led_state = false;
bool led_flash = false;
bool screen_state = false;

static unsigned long oled_timer = 0ul;
static unsigned long serial_timer = 0ul;
static unsigned long led_timer = 0ul;
static unsigned long button_timer = 0ul;
static unsigned long kls_timer = 0ul;
static unsigned long history_timer = 0ul;

void loop() {
  unsigned long ms = millis();
  if ((ms - oled_timer) > 40ul) {
    oled_accumulator += (ms - oled_timer);
    draw();
    if (oled_accumulator >= 1000ul)
      oled_accumulator = oled_accumulator - 1000ul;
    oled_timer = ms;
  }

  ms = millis();
  if ((ms - serial_timer) > 10000ul) {
    const unsigned long lux = (unsigned long)lux_accumulator;
    Serial.write((char *)&lux, sizeof(unsigned long)); // uint32_t
    led_flash = true;
    serial_timer = ms;
  }

  ms = millis();
  if (led_flash && (ms - led_timer) > 50ul) {
    if (!led_state) {
      digitalWrite(LED_PIN, HIGH);
      led_state = true;
    } else {
      digitalWrite(LED_PIN, LOW);
      led_state = false;
      led_flash = false;
    }
    led_timer = ms;
  }

  ms = millis();
  if (digitalRead(BUTTON_PIN) == LOW && (ms - button_timer) > 500ul) {
    screen_state = !screen_state;
    u8g2.setPowerSave(screen_state);
    button_timer = ms;
  }

  ms = millis();
  if ((ms - kls_timer) > 200ul) {
    kls_read();
    kls_timer = ms;
  }

  ms = millis();
  if ((ms - history_timer) > 10000ul) {
    history();
    history_timer = ms;
  }
}