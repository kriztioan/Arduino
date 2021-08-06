/**
 *  @file    main.cpp
 *  @brief   Arduino Lux Spectrum Analyzer with OLED Graph
 *  @author  KrizTioaN (christiaanboersma@hotmail.com)
 *  @date    2021-08-06
 *  @note    BSD-3 licensed
 *
 ***********************************************/

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <arduinoFFT.h>

static constexpr const uint8_t nMicrophonePin = 0;
static constexpr const uint8_t nTriggerPin = 1;
static constexpr const uint8_t nThresholdPin = 2;

static constexpr const uint16_t nNumberOfSamples = 64;
static constexpr const uint16_t nSampleRate = 4000;
static constexpr const uint32_t nDelay = 1000000 / nSampleRate;

static constexpr const uint16_t f_trim = 8;

static constexpr const uint16_t f_max =
    (nSampleRate >> 1) -
    (((nSampleRate >> 1) - (nNumberOfSamples >> 1)) / nNumberOfSamples);
static constexpr const uint16_t f_min =
    (nNumberOfSamples >> 1) +
    f_trim *
        (((nSampleRate >> 1) - (nNumberOfSamples >> 1)) / nNumberOfSamples);
static constexpr const uint16_t f_mid = f_min + ((f_max - f_min) >> 1);

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0);

double vReal[nNumberOfSamples];
double vImag[nNumberOfSamples];

arduinoFFT FFT =
    arduinoFFT(vReal, vImag, nNumberOfSamples, (double)nSampleRate);

void setup() {
  delay(3000);
  u8g2.begin();
  u8g2.setContrast(0x00);
  u8g2.setFont(u8g2_font_5x8_tf);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {

  uint32_t ulTimer;
  uint16_t nSampleNumber = 0;
  char str[5];

  while (nSampleNumber < nNumberOfSamples) {
    ulTimer = micros();
    vReal[nSampleNumber] = (double)analogRead(nMicrophonePin);
    vImag[nSampleNumber++] = 0.0f;
    while ((micros() - ulTimer) < nDelay) {
    }
  }

  int trigger = analogRead(nTriggerPin);

  u8g2.clearBuffer();
  snprintf(str, 5, "%-3d%%", (int)((float)trigger / 10.23f));
  u8g2.drawStr(0, 7, str);
  u8g2.drawHLine(0, 23, u8g2.getDisplayWidth() - 1);
  snprintf(str, 5, "%-4d", f_min);
  u8g2.drawStr(0, 31, str);
  snprintf(str, 5, "%-4d", f_mid);
  u8g2.drawStr(53, 31, str);
  snprintf(str, 5, "%-4d", f_max);
  u8g2.drawStr(107, 31, str);

  FFT.Windowing(FFT_WIN_TYP_HANN, FFT_FORWARD);
  FFT.Compute(FFT_FORWARD);

  for (uint16_t i = f_trim; i < (nNumberOfSamples >> 1); i++) {
    vReal[i] = sqrt(vReal[i] * vReal[i] + vImag[i] * vImag[i]);
  }

  int threshold = analogRead(nThresholdPin);
  snprintf(str, 5, "%-3d%%", (int)((float)threshold / 10.23f));
  u8g2.drawStr(u8g2.getDisplayWidth() - 21, 7, str);

  double dRealMax = 0.0;
  for (uint16_t i = f_trim; i < (nNumberOfSamples >> 1); i++) {
    if (vReal[i] < double(threshold)) {
      vReal[i] = 0.0;
    }
    else if (vReal[i] > dRealMax) {
      dRealMax = vReal[i];
    }
  }

  if (dRealMax > (double)trigger) {
    for (uint16_t i = f_trim; i < (nNumberOfSamples >> 1); i++) {
      vReal[i] = 1.0 + 23.0 * (vReal[i] / dRealMax);
    }

    for (uint16_t i = 0; i < u8g2.getDisplayWidth(); i++) {
      double value =
          vReal[f_trim + (i * ((nNumberOfSamples >> 1) - (f_trim + 1))) /
                             (u8g2.getDisplayWidth() - 1)];
      u8g2.drawVLine(i, int(24.0 - value), (int)value);
    }
  }
  u8g2.sendBuffer();
}
