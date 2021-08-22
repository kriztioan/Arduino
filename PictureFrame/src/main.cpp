/**
 *  @file    main.cpp
 *  @brief   ESP32 & ST7789 320x240 Display
 *  @author  KrizTioaN (christiaanboersma@hotmail.com)
 *  @date    2021-08-22
 *  @note    BSD-3 licensed
 *
 ***********************************************/

#include <SPIFFS.h>
#include <TFT_eSPI.h>
#include <cstring>
#include <string>
#include <vector>

TFT_eSPI tft = TFT_eSPI();

std::vector<std::string> images;

int drawbmp(const char *filename) {

  File file = SPIFFS.open(filename, "r");

  if (!file) {
    Serial.print("failed to open '");
    Serial.print(filename);
    Serial.println('\'');
    return 0;
  }

  Serial.print("reading '");
  Serial.print(filename);
  Serial.print("' (");
  Serial.print(file.size());
  Serial.println(" bytes)");

  unsigned char header[54];

  file.readBytes(reinterpret_cast<char *>(&header), sizeof(header));

  uint32_t width;

  std::memcpy(&width, header + 18, sizeof(int));

  uint32_t height;

  std::memcpy(&height, header + 22, sizeof(int));

  Serial.print("image size: ");
  Serial.print(width);
  Serial.print('x');
  Serial.println(height);

  if (width * height <= 0) {
    Serial.println("invalid size");
    return 0;
  }

  const uint16_t padding = (4 - ((width * 3) & 3)) & 3,
                 padded = 3 * width + padding;

  unsigned char *dma1 = new unsigned char[padded],
                *dma2 = new unsigned char[padded], *row = dma1;

  bool dma = false;

  unsigned long ms = millis();

  for (int16_t y = height - 1; y >= 0; --y) {
    file.readBytes((char *)row, padded);
    uint8_t *bit888 = row;
    uint16_t *color565 = (uint16_t *)row;
    for (uint16_t x = 0; x < width; x++) {
      uint8_t r = *bit888++;
      uint8_t g = *bit888++;
      uint8_t b = *bit888++;
      *color565++ = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
    tft.pushImageDMA(0, y, width, 1, (uint16_t *)row);
    row = dma ? dma1 : dma2;
    dma = !dma;
  }
  tft.dmaWait();

  Serial.print("rendered in ");
  Serial.print(millis() - ms);
  Serial.println(" ms");

  delete[] dma1;
  delete[] dma2;

  file.close();

  return 1;
}

void setup(void) {
  Serial.begin(115200);
  if (!SPIFFS.begin(true)) {
    Serial.println("failed to mount SPIFFS");
    return;
  }
  ledcSetup(0, 1000, 8);
  ledcAttachPin(32, 0);
  ledcWrite(0, 64);

  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);
  tft.initDMA(true);

  File root = SPIFFS.open("/", "r");
  File file;
  while (file = root.openNextFile("r"))
    images.emplace_back(file.name());

  if (!images.empty()) {
    while (!drawbmp(images[rand() % images.size()].c_str())) {
    }
  }

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {

  static unsigned long timer = millis();

  unsigned long ms = millis();
  if (ms - timer >= 10000ul) {
    if (drawbmp(images[rand() % images.size()].c_str())) {
      timer = ms;
    }
  }
}