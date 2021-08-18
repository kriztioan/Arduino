/**
 *  @file    main.cpp
 *  @brief   Sensor Pod
 *  @author  KrizTioaN (christiaanboersma@hotmail.com)
 *  @date    2021-08-06
 *  @note    BSD-3 licensed
 *
 ***********************************************/

#include <Arduino.h>
#include <U8x8lib.h>
#include <Wire.h>

#define DSM501PM10_PIN 2
#define DSM501PM25_PIN 3

#define DSM501PM10 0
#define DSM501PM25 1
#define DSM501NPMS 2

#define DSM501WINDOW 3600000ul

void post_handler();
void updateUVI_handler();

struct DSM501 {
  struct {
    unsigned long t0; // in ms
    unsigned long dt; // in ms
    float r;          // in %
    char pm_str[4];
    uint8_t pin;
  } pm[DSM501NPMS];
  uint8_t state;
} dsm501;

#define DSM501_WARMUP 0
#define DSM501_MEASURE 1

void dsm_start() {

  pinMode(DSM501PM10_PIN, INPUT);
  pinMode(DSM501PM25_PIN, INPUT);

  dsm501.pm[DSM501PM10].pin = DSM501PM10_PIN;
  dsm501.pm[DSM501PM25].pin = DSM501PM25_PIN;

  dsm501.state = DSM501_MEASURE;

  for (uint8_t i = 0; i < DSM501NPMS; i++)
    dsm501.pm[i].t0 = millis(); // in ms
}

void dsm_loop() {
  switch (dsm501.state) {
  case DSM501_MEASURE: {
    for (uint8_t i = 0; i < DSM501NPMS; i++) {
      unsigned long pulse = pulseIn(dsm501.pm[i].pin, LOW, 240000ul),
                    ms = millis(),
                    dt = ms - dsm501.pm[i].t0; // in ms

      dsm501.pm[i].t0 = ms;

      dsm501.pm[i].r =
          ((float)dsm501.pm[i].dt * dsm501.pm[i].r + 0.1f * (float)pulse);
      dsm501.pm[i].dt += dt;
      dsm501.pm[i].r /= (float)dsm501.pm[i].dt;

      if (dsm501.pm[i].dt > DSM501WINDOW)
        dsm501.pm[i].dt = DSM501WINDOW;

      float pm =
          0.000328773f * dsm501.pm[i].r * dsm501.pm[i].r * dsm501.pm[i].r -
          0.00368712f * dsm501.pm[i].r * dsm501.pm[i].r +
          0.117507f * dsm501.pm[i].r - 0.0420336f; // in mg/m3

      if (pm > 0.0f)
        dtostrf(pm, 3, 0, (char *)dsm501.pm[i].pm_str);
    }
  } break;
  case DSM501_WARMUP:
    if (millis() > 60000ul) { // wait 1 minute to warm up
      Serial.println(F("DSM501 ready"));
      dsm_start();
    }
  }
}

#define SDD1306_ADDR 0x78
U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(U8X8_PIN_NONE);

#define PCF8583_ADDR (0xA2 >> 1)

#include <Adafruit_BME280.h>
Adafruit_BME280 bme;

struct BME280SensorValues {

  char temperature_str[5];
  char humidity_str[5];
  char pressure_str[6];
} sv;

void bme_loop() {

  static unsigned long timer = millis();

  if ((millis() - timer) >= 5000) {

    dtostrf(bme.readTemperature(), 4, 1, sv.temperature_str);
    dtostrf(bme.readHumidity(), 4, 1, sv.humidity_str);
    dtostrf(bme.readPressure() / 1e5, 5, 3, sv.pressure_str);

    timer = millis();
  }
}

uint16_t uv_error, post_error, wfc_error, wifi_error = 503, ntp_error;

struct WeatherFC {
  float temperature;
  float dewpoint;
  float windDirection;
  float windSpeed;
  float windGust;
  float relativeHumidity;
} weather;

uint8_t uv;

uint8_t bcdToDec(byte value) { return ((value / 16) * 10 + value % 16); }

uint8_t decToBcd(byte value) { return (value / 10 * 16 + value % 10); }

struct PCF8583_DateTime {
  uint8_t year;
  uint8_t month;
  uint8_t weekday;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t vl;
} DateTime;

void PCF8583SetTime() {

  unsigned char raw[7];

  raw[0] = decToBcd(DateTime.second);
  raw[1] = decToBcd(DateTime.minute);
  raw[2] = decToBcd(DateTime.hour);
  raw[3] = decToBcd(DateTime.day);
  raw[4] = decToBcd(DateTime.weekday);
  raw[5] = decToBcd(DateTime.month);
  raw[6] = decToBcd(DateTime.year);

  Wire.beginTransmission(PCF8583_ADDR);
  Wire.write(0x02);
  Wire.write((uint8_t *)raw, sizeof(raw));
  Wire.endTransmission();
}

void PCF8583ReadTime() {

  Wire.beginTransmission(PCF8583_ADDR);
  Wire.write(0x02);
  Wire.endTransmission();

  unsigned char raw[7];
  unsigned i = 0;
  Wire.requestFrom(PCF8583_ADDR, 7);
  while (Wire.available()) {

    raw[i] = Wire.read();

    if (++i >= 7)
      break;
  }

  DateTime.vl = (raw[0] & B10000000) >> 7;        // retrieve VL error bit
  DateTime.second = bcdToDec(raw[0] & B01111111); // remove VL error bit
  DateTime.minute =
      bcdToDec(raw[1] & B01111111); // remove unwanted bits from MSB
  DateTime.hour = bcdToDec(raw[2] & B00111111);
  DateTime.day = bcdToDec(raw[3] & B00111111);
  DateTime.weekday = bcdToDec(raw[4] & B00000111);
  DateTime.month =
      bcdToDec(raw[5] & B00011111); // remove century bit, 1999 is over
  DateTime.year = bcdToDec(raw[6]);
}

#define SCREEN_CLOCK 0
#define SCREEN_WEATHER 1
#define SCREEN_SENSORS 2
#define SCREEN_STATUS 3
#define SCREEN_NUMBER 4

struct {
  uint8_t view = 0;
  uint8_t state = 0;
} screen;

void switch_screen() {
  if (++screen.view >= SCREEN_NUMBER)
    screen.view = SCREEN_CLOCK;
  u8x8.clearDisplay();
}

void toggle_screen() {
  screen.state = !screen.state;
  u8x8.setPowerSave(screen.state);
}

#include <AltSoftSerial.h>

/*  AltSofSerial hardcoded pins:

    RX_PIN = 8;
    TX_PIN = 9;

    NOTE PIN 10 DOES NOT ALLOW PWM
*/

#define ESP8266_RST_PIN 4

AltSoftSerial ESP8266;

// 1kOhm resistor in voltage divider
#define KLS6_PIN 0

void kls_read(char *lux_str) {
  static float lux = 0.0f;
  float Vout = (float)analogRead(KLS6_PIN),
        Rphoto = 1.0 * (1023.0 - Vout) / Vout,
        value = pow(10.0, (3.2784 - log10(Rphoto) * 1.63675));
  dtostrf(0.9f * lux + 0.1f * value, 5, 1, lux_str);
}

void screen_draw_datetime() {

  u8x8.setFont(u8x8_font_open_iconic_thing_2x2);
  u8x8.drawGlyph(0, 0, 0x4b);
  u8x8.setFont(u8x8_font_chroma48medium8_r);

  const char *weekdays[] PROGMEM = {"Sunday",    "Monday",   "Tuesday",
                                    "Wednesday", "Thursday", "Friday",
                                    "Saturday"};
  const char *months[] PROGMEM = {
      "January", "February", "March",     "April",   "May",      "June",
      "July",    "August",   "September", "October", "November", "December"};

  const char *line PROGMEM = "________________";
  u8x8.drawString(0, 3, line);

  PCF8583ReadTime();
  if (DateTime.vl == 1) {

    u8x8.setFont(u8x8_font_open_iconic_embedded_1x1);
    u8x8.drawGlyph(4, 0, 0x47);
    u8x8.setFont(u8x8_font_chroma48medium8_r);
  } else
    u8x8.drawGlyph(4, 0, ' ');

  char buf[16];
  snprintf_P(buf, 16, PSTR("20%02d"), DateTime.year);
  u8x8.draw2x2String(8, 0, buf);

  int n = snprintf_P(buf, 16, PSTR("%9s %d"), months[DateTime.month - 1],
                     DateTime.day);
  u8x8.drawString(16 - n, 2, buf);

  snprintf_P(buf, 16, PSTR("%-9s"), weekdays[DateTime.weekday]);
  u8x8.drawString(0, 5, buf);

  snprintf_P(buf, 16, PSTR("%02d:%02d:%02d"), DateTime.hour, DateTime.minute,
             DateTime.second);
  u8x8.draw2x2String(0, 6, buf);
}

void screen_draw_weather() {

  u8x8.setFont(u8x8_font_open_iconic_weather_2x2);
  u8x8.drawGlyph(0, 0, 0x45);

  char flt[5], buf[17];

  snprintf_P(buf, sizeof(buf), PSTR("%d"), uv);
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  u8x8.draw1x2String(3, 0, buf);

  u8x8.setFont(u8x8_font_chroma48medium8_r);

  PCF8583ReadTime();

  snprintf_P(buf, sizeof(buf), PSTR("%02d:%02d:%02d"), DateTime.hour,
             DateTime.minute, DateTime.second);
  u8x8.drawString(8, 0, buf);

  dtostrf(weather.temperature, 4, 1, flt);
  snprintf_P(buf, sizeof(buf), PSTR("temp:  %s  C"), flt);
  u8x8.drawString(0, 2, buf);

  dtostrf(weather.dewpoint, 4, 1, flt);
  snprintf_P(buf, sizeof(buf), PSTR("dewp:  %s  C"), flt);
  u8x8.drawString(0, 3, buf);

  dtostrf(weather.windDirection, 3, 0, flt);
  snprintf_P(buf, sizeof(buf), PSTR("wind: %s    deg"), flt);
  u8x8.drawString(0, 4, buf);

  dtostrf(weather.windSpeed, 4, 1, flt);
  snprintf_P(buf, sizeof(buf), PSTR("wind:  %s  m/s"), flt);
  u8x8.drawString(0, 5, buf);

  dtostrf(weather.windGust, 4, 1, flt);
  snprintf_P(buf, sizeof(buf), PSTR("gust:  %s  m/s"), flt);
  u8x8.drawString(0, 6, buf);

  dtostrf(weather.relativeHumidity, 2, 0, flt);
  snprintf_P(buf, sizeof(buf), PSTR("humi:  %s    %%"), flt);
  u8x8.drawString(0, 7, buf);
}

void screen_draw_sensors() {

  u8x8.setFont(u8x8_font_open_iconic_embedded_2x2);
  u8x8.drawGlyph(0, 0, 0x46);
  u8x8.setFont(u8x8_font_chroma48medium8_r);

  PCF8583ReadTime();

  char buf[17], flt[5];
  snprintf_P(buf, sizeof(buf), PSTR("%02d:%02d:%02d"), DateTime.hour,
             DateTime.minute, DateTime.second);
  u8x8.drawString(8, 0, buf);

  kls_read(flt);
  snprintf_P(buf, sizeof(buf), PSTR("phot:%s    lx"), flt);
  u8x8.drawString(0, 2, buf);

  snprintf_P(buf, sizeof(buf), PSTR("humi: %s     %%"), sv.humidity_str);
  u8x8.drawString(0, 3, buf);

  snprintf_P(buf, sizeof(buf), PSTR("temp: %s   deg"), sv.temperature_str);
  u8x8.drawString(0, 4, buf);

  snprintf_P(buf, sizeof(buf), PSTR("pres:  %s bar"), sv.pressure_str);
  u8x8.drawString(0, 5, buf);

  snprintf_P(buf, sizeof(buf), PSTR("pm10:%3s   mg/m3"),
             dsm501.pm[DSM501PM10].pm_str);
  u8x8.drawString(0, 6, buf);

  snprintf_P(buf, sizeof(buf), PSTR("pm25:%3s   mg/m3"),
             dsm501.pm[DSM501PM25].pm_str);
  u8x8.drawString(0, 7, buf);
}

void screen_draw_status() {

  u8x8.setFont(u8x8_font_open_iconic_embedded_2x2);
  u8x8.drawGlyph(0, 0, 0x42);
  u8x8.setFont(u8x8_font_chroma48medium8_r);

  PCF8583ReadTime();

  char buf[17];
  snprintf_P(buf, sizeof(buf), PSTR("%02d:%02d:%02d"), DateTime.hour,
             DateTime.minute, DateTime.second);
  u8x8.drawString(8, 0, buf);

  snprintf_P(buf, sizeof(buf), PSTR("WiFi   : %3d"), wifi_error);
  u8x8.drawString(0, 2, buf);

  snprintf_P(buf, sizeof(buf), PSTR("RTC    : %3d"),
             DateTime.vl == 0 ? 200 : 408);
  u8x8.drawString(0, 3, buf);

  snprintf_P(buf, sizeof(buf), PSTR("NTP    : %3d"), ntp_error);
  u8x8.drawString(0, 4, buf);

  snprintf_P(buf, sizeof(buf), PSTR("Weather: %3d"), wfc_error);
  u8x8.drawString(0, 5, buf);

  snprintf_P(buf, sizeof(buf), PSTR("UV     : %3d"), uv_error);
  u8x8.drawString(0, 6, buf);

  snprintf_P(buf, sizeof(buf), PSTR("Post   : %3d"), post_error);
  u8x8.drawString(0, 7, buf);
}

void default_handler() {

  while (ESP8266.available())
    Serial.write(ESP8266.read());
}

#define QUEUE_IDLE 0
#define QUEUE_START 1
#define QUEUE_EXEC 2
#define QUEUE_ACT 3
#define QUEUE_BLOCK 4
#define QUEUE_TIMEOUT 5

struct HandlerQueue {
  uint8_t stage;
  unsigned long timeout;
  unsigned long timer;
  void (*handler)();
};

struct HandlerQueue queue = {
    .stage = 0, .timeout = 10000ul, .timer = 0ul, .handler = default_handler};

void queue_start(void (*handler)()) {

  queue.handler = handler;
  ESP8266.print(F("RSP\n"));
  queue.timer = millis();
  queue.stage = QUEUE_EXEC;
}

void queue_execute(const __FlashStringHelper *cmd) {

  if (ESP8266.read() != 0x06) {
    Serial.println(F("Invalid response"));
    return;
  }
  ESP8266.print(cmd);
  queue.stage = QUEUE_ACT;
}

void queue_execute(const char *cmd) {

  if (ESP8266.read() != 0x06) {
    Serial.println(F("Invalid response"));
    return;
  }
  ESP8266.print(cmd);
  queue.stage = QUEUE_ACT;
}

void queue_reset() {

  queue.handler = default_handler;
  queue.stage = QUEUE_IDLE;
}

void queue_loop() {

  if (queue.handler != default_handler &&
      (millis() - queue.timer) >= queue.timeout) {
    queue.stage = QUEUE_TIMEOUT;
    queue.handler();
    queue_reset();
  }
}

#define BUTTON_PIN A3

typedef uint8_t BUTTON;
#define BUTTON0 0
#define BUTTON1 1
#define BUTTON2 2
#define BUTTON3 3
#define BUTTON4 4

void button_handler() {

  static BUTTON p_button = BUTTON0;

  BUTTON button = BUTTON0;

  int voltage = analogRead(BUTTON_PIN);

  if (voltage > 205) {
    if (voltage > 410) {
      if (voltage > 615) {
        if (voltage > 820)
          button = BUTTON4;
        else
          button = BUTTON3;
      } else
        button = BUTTON2;
    } else
      button = BUTTON1;
  } else
    button = BUTTON0;

  if (button != p_button) {
    p_button = button;
    switch (button) {
    case BUTTON0:
      break;
    case BUTTON1:
      toggle_screen();
      break;
    case BUTTON2:
      if (queue.stage == QUEUE_IDLE)
        queue_start(post_handler);
      break;
    case BUTTON3:
      if (queue.stage == QUEUE_IDLE)
        queue_start(updateUVI_handler);
      break;
    case BUTTON4:
      switch_screen();
      break;
    }
  }
}

struct NTPResponse {
  uint8_t year;
  uint8_t month;
  uint8_t weekday;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t error;
};

void updateNTPTime_handler() {

  switch (queue.stage) {
  case QUEUE_EXEC:
    queue_execute(F("NTP\n"));
    break;
  case QUEUE_ACT:
    struct NTPResponse ntp;
    if (static_cast<unsigned int>(ESP8266.available()) >=
        sizeof(struct NTPResponse)) {
      for (uint8_t i = 0; i < sizeof(struct NTPResponse); i++)
        *((char *)&ntp + i) = ESP8266.read();
      ntp_error = ntp.error;
      if (ntp_error == 200) {
        memcpy((char *)&DateTime, (char *)&ntp, 7);
        PCF8583SetTime();
        Serial.println(F("Time synced"));
      } else
        Serial.println(F("Time not valid yet"));
      queue_reset();
    }
    break;
  case QUEUE_TIMEOUT:
    ntp_error = 408;
    Serial.println(F("NTP handler timed out"));
  }
}

struct WFCResponse {
  float temperature;
  float dewpoint;
  float windDirection;
  float windSpeed;
  float windGust;
  float relativeHumidity;
  int16_t error;
  uint8_t padding[2];
};

void updateWeather_handler() {

  switch (queue.stage) {
  case QUEUE_EXEC:
    queue_execute(F("WFC\n"));
    break;
  case QUEUE_ACT:
    struct WFCResponse wfc;
    if (static_cast<unsigned int>(ESP8266.available()) >=
        sizeof(struct WFCResponse)) {
      for (uint8_t i = 0; i < sizeof(struct WFCResponse); i++)
        *((char *)&wfc + i) = ESP8266.read();
      wfc_error = wfc.error;
      if (wfc.error == 200) {
        memcpy((char *)&weather, (char *)&wfc, sizeof(struct WeatherFC));
        Serial.println(F("Weather synced"));
      } else {
        Serial.print(F("FC error "));
        Serial.println(wfc.error);
      }
      queue_reset();
    }
    break;
  case QUEUE_TIMEOUT:
    wfc_error = 408;
    Serial.println(F("Weather handler timed out"));
  }
}

struct UVIResponse {
  uint16_t error;
  uint8_t uv;
  uint8_t padding;
};

void updateUVI_handler() {

  switch (queue.stage) {
  case QUEUE_EXEC:
    queue_execute(F("UVI\n"));
    break;
  case QUEUE_ACT:
    struct UVIResponse uvi;
    if (static_cast<unsigned int>(ESP8266.available()) >=
        sizeof(struct UVIResponse)) {
      for (uint8_t i = 0; i < sizeof(struct UVIResponse); i++)
        *((char *)&uvi + i) = ESP8266.read();
      uv_error = uvi.error;
      if (uvi.error == 200) {
        uv = uvi.uv;
        Serial.println(F("UV synced"));
      } else {
        Serial.print(F("UV error "));
        Serial.println(uvi.error);
      }
      queue_reset();
    }
    break;
  case QUEUE_TIMEOUT:
    uv_error = 408;
    Serial.println(F("UV handler timed out"));
  }
}

void post_handler() {

  switch (queue.stage) {
  case QUEUE_EXEC:
    char cmd[192], photo_str[6];
    kls_read(photo_str);
    snprintf_P(cmd, sizeof(cmd),
               PSTR("PST\ntwonky.centralpark.lan\n/"
                    "sensors.php\n{\"timestamp\":\"20%02d-%02d-%02dT%02d:%02d:%"
                    "02d\",\"temperature\":%s,\"humidity\":%s,\"pressure\":%s,"
                    "\"photo\":%s,\"pm10\":%s,\"pm25\":%s}\n"),
               DateTime.year, DateTime.month, DateTime.day, DateTime.hour,
               DateTime.minute, DateTime.second, sv.temperature_str,
               sv.humidity_str, sv.pressure_str, photo_str,
               dsm501.pm[DSM501PM10].pm_str, dsm501.pm[DSM501PM25].pm_str);
    queue_execute(cmd);
    break;
  case QUEUE_ACT:
    if (ESP8266.available() >= 3) {
      char code[4];
      for (uint8_t i = 0; i < 3; i++)
        code[i] = ESP8266.read();
      code[3] = '\0';
      post_error = atoi(code);
      if (post_error != 200) {
        Serial.print(F("Post error "));
        Serial.println(code);
      } else
        Serial.println(F("Sensors posted"));
      queue_reset();
    }
    break;
  case QUEUE_TIMEOUT:
    post_error = 408;
    Serial.println(F("Post handler timed out"));
  }
}

#define WIFI_MAX_FAIL 4

void wifi_handler() {

  static uint8_t wifi_fail = 0;

  switch (queue.stage) {
  case QUEUE_EXEC:
    queue_execute(F("WIF\n"));
    break;
  case QUEUE_ACT:
    if (ESP8266.available() >= 2) {
      wifi_error = 0;
      for (uint8_t i = 0; i < 2; i++)
        wifi_error |= (ESP8266.read() << (i * 8));
      if (wifi_error != 200) {
        Serial.print(F("WiFi error "));
        Serial.println(wifi_error);
      }
      wifi_fail = 0;
      queue_reset();
    }
    break;
  case QUEUE_TIMEOUT:
    wifi_error = 408;
    Serial.println(F("WiFi handler timed out"));
    if (++wifi_fail >= WIFI_MAX_FAIL) {
      wifi_fail = 0;
      Serial.println(F("Maximum number of fails reached: resetting ESP8622"));
      digitalWrite(ESP8266_RST_PIN, LOW);
      delayMicroseconds(20);
      digitalWrite(ESP8266_RST_PIN, HIGH);
    }
  }
}

void websiteFromURL_handler() {

  static uint8_t pck_len = 0;
  switch (queue.stage) {
  case QUEUE_EXEC:
    queue_execute(F("URL\ntwonky.centralpark.lan\n"));
    break;
  case QUEUE_ACT: {
    unsigned long timeout = millis();
    while (ESP8266.available()) {
      char c = ESP8266.read();
      if (c == 0x04) {
        pck_len = 0;
        queue_reset();
      }
      if (++pck_len == 56) {
        ESP8266.write(0x06);
        pck_len = 0;
      }
      Serial.write(c);
      if ((millis() - timeout) >= 200ul)
        break;
    }
  } break;
  case QUEUE_TIMEOUT:
    Serial.println(F("URL handler timed out"));
  }
}

void setup() {

  Serial.begin(115200);

  Serial.println(F("Booting Uno"));

  Serial.println(F("Turning off builtin LED"));
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(BUTTON_PIN, INPUT);

  u8x8.setI2CAddress(SDD1306_ADDR);
  u8x8.begin();
  u8x8.setContrast(0x00);

  bme.begin(0x76);
  dtostrf(bme.readTemperature(), 4, 1, sv.temperature_str);
  dtostrf(bme.readHumidity(), 4, 1, sv.humidity_str);
  dtostrf(bme.readPressure() / 1e5, 4, 2, sv.pressure_str);

  Serial.println(F("Enable ESP8266"));
  pinMode(ESP8266_RST_PIN, OUTPUT);
  digitalWrite(ESP8266_RST_PIN, HIGH);
  ESP8266.begin(19200);

  Wire.begin();

  Serial.println(F("Uno Ready"));
}

struct Timer {
  unsigned long delay;
  unsigned long interval;
  unsigned long timer;
};

struct Timers {
  Timer button;
  Timer draw;
  Timer wifi;
  Timer post;
  Timer weather;
  Timer uv;
  Timer NTP;
};

void loop() {

  if (ESP8266.available())
    queue.handler();

  queue_loop();

  dsm_loop();

  bme_loop();

  static struct Timers timers = {
      .button = {.delay = 3000ul,
                 .interval = 10ul,
                 .timer = millis()}, // start up delay 3s, then every 10ms
      .draw = {.delay = 1000ul,
               .interval = 1000ul,
               .timer = millis()}, // every second
      .wifi = {.delay = 30000ul,
               .interval = 30000ul,
               .timer = millis()}, // every 30s
      .post = {.delay = 900000ul,
               .interval = 900000ul,
               .timer = millis()}, // every 15m
      .weather = {.delay = 30000ul,
                  .interval = 1800000ul,
                  .timer = millis()}, // after first 30s, then every 3h
      .uv = {.delay = 30000ul,
             .interval = 3600000ul,
             .timer = millis()}, // after first 30s, then every 1h
      .NTP = {.delay = 30000ul,
              .interval = 18000000ul,
              .timer = millis()} // after first 30s then every 30m
  };

  unsigned long ms = millis();
  if ((ms - timers.button.timer) >= timers.button.delay) {
    timers.button.timer = ms;
    button_handler();
    if (timers.button.delay > timers.button.interval)
      timers.button.delay = timers.button.interval;
  }

  ms = millis();
  if ((ms - timers.NTP.timer) >= timers.NTP.delay) {
    if (queue.stage == QUEUE_IDLE && wifi_error == 200) {
      timers.NTP.timer = ms;
      queue_start(updateNTPTime_handler);
      if (timers.NTP.delay < timers.NTP.interval)
        timers.NTP.delay = timers.NTP.interval;
    }
  }

  ms = millis();
  if ((ms - timers.weather.timer) >= timers.weather.delay) {
    if (queue.stage == QUEUE_IDLE && wifi_error == 200) {
      timers.weather.timer = ms;
      queue_start(updateWeather_handler);
      if (timers.weather.delay < timers.weather.interval)
        timers.weather.delay = timers.weather.interval;
    }
  }

  ms = millis();
  if ((ms - timers.uv.timer) >= timers.uv.delay) {
    if (queue.stage == QUEUE_IDLE && wifi_error == 200) {
      timers.uv.timer = ms;
      queue_start(updateUVI_handler);
      if (timers.uv.delay < timers.uv.interval)
        timers.uv.delay = timers.uv.interval;
    }
  }

  ms = millis();
  if ((ms - timers.draw.timer) >= timers.draw.delay) {
    timers.draw.timer = ms;
    switch (screen.view) {
    case SCREEN_CLOCK:
      screen_draw_datetime();
      break;
    case SCREEN_WEATHER:
      screen_draw_weather();
      break;
    case SCREEN_SENSORS:
      screen_draw_sensors();
      break;
    case SCREEN_STATUS:
      screen_draw_status();
    }
  }

  ms = millis();
  if ((ms - timers.post.timer) >= timers.post.delay) {
    if (queue.stage == QUEUE_IDLE && wifi_error == 200) {
      timers.post.timer = ms;
      queue_start(post_handler);
    }
  }

  ms = millis();
  if ((ms - timers.wifi.timer) >= timers.wifi.delay) {
    if (queue.stage == QUEUE_IDLE) {
      timers.wifi.timer = ms;
      queue_start(wifi_handler);
    }
  }
}
