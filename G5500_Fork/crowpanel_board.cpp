#include "crowpanel_board.hpp"
#if SELECTED_BOARD == BOARD_CROWPANEL_21
#include "board.hpp"
#include "quadrature.hpp"
#include "crowpanel_touch.hpp"
#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include "crowpanel_rgb_driver.h"
#include <esp_heap_caps.h>
#include <algorithm>

namespace forkrot::crowpanel {
namespace {
bool busReady = false;
bool touchReady = false;
uint8_t latch = 0xff; // PCF8574 P5 (button) and P2 (INT) must stay released.
bool writeBytes(uint8_t address, const uint8_t *data, size_t size) {
  if (!busReady) return false;
  Wire.beginTransmission(address);
  Wire.write(data, size);
  return Wire.endTransmission() == 0;
}
bool portBit(unsigned bit, bool high) {
  if (high) latch |= 1u << bit;
  else latch &= ~(1u << bit);
  return writeBytes(0x21, &latch, 1);
}
bool readRegister(uint8_t address, uint8_t reg, uint8_t *data, size_t size) {
  if (!busReady) return false;
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(address, size) != size) return false;
  for (size_t i = 0; i < size; ++i) data[i] = Wire.read();
  return true;
}
portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;
QuadratureDecoder decoder(2);
int delta = 0;
void encoderEdge() {
  int state = (gpio_get_level(gpio_num_t(board::PIN_ENCODER_A)) << 1) |
              gpio_get_level(gpio_num_t(board::PIN_ENCODER_B));
  portENTER_CRITICAL_ISR(&encoderMux);
  delta = std::clamp(delta + decoder.update(state), -256, 256);
  portEXIT_CRITICAL_ISR(&encoderMux);
}
void spiWord(unsigned word) {
  for (int bit = 8; bit >= 0; --bit) {
    gpio_set_level(GPIO_NUM_2, 0);
    gpio_set_level(GPIO_NUM_1, (word >> bit) & 1);
    delayMicroseconds(2);
    gpio_set_level(GPIO_NUM_2, 1);
    delayMicroseconds(2);
  }
  gpio_set_level(GPIO_NUM_2, 0);
}
#include "crowpanel_lcd_init.hpp"
}
bool beginBus() {
  if (!busReady) {
    busReady = Wire.begin(board::PIN_I2C_SDA, board::PIN_I2C_SCL, 100000);
    Wire.setTimeOut(10);
  }
  return busReady;
}
bool readButton(bool &down) {
  if (!busReady || Wire.requestFrom(uint8_t(0x21), size_t(1)) != 1) return false;
  down = !(Wire.read() & (1u << 5));
  return true;
}
bool readTouch(uint16_t &x, uint16_t &y) {
  uint8_t data[5];
  return touchReady && readRegister(0x15, 0x02, data, sizeof data) && decodeTouch(data, x, y);
}
void beginEncoder() {
  pinMode(board::PIN_ENCODER_A, INPUT_PULLUP);
  pinMode(board::PIN_ENCODER_B, INPUT_PULLUP);
  decoder.reset((digitalRead(board::PIN_ENCODER_A) << 1) | digitalRead(board::PIN_ENCODER_B));
  attachInterrupt(board::PIN_ENCODER_A, encoderEdge, CHANGE);
  attachInterrupt(board::PIN_ENCODER_B, encoderEdge, CHANGE);
}
int encoderDelta() {
  portENTER_CRITICAL(&encoderMux);
  int value = delta;
  delta = 0;
  portEXIT_CRITICAL(&encoderMux);
  return value;
}
esp_lcd_panel_handle_t beginPanel() {
  pinMode(board::PIN_BACKLIGHT, OUTPUT);
  digitalWrite(board::PIN_BACKLIGHT, LOW);
  if (!psramFound() || !beginBus() || !portBit(3, true)) return nullptr;
  delay(50);
  if (!portBit(4, false)) return nullptr;
  delay(100);
  if (!portBit(4, true)) return nullptr;
  delay(100);
  if (!portBit(0, false)) return nullptr;
  delay(100);
  if (!portBit(0, true)) return nullptr;
  delay(100);
  Wire.beginTransmission(0x15);
  touchReady = Wire.endTransmission() == 0;
  for (int pin : {16, 2, 1}) pinMode(pin, OUTPUT);
  gpio_set_level(GPIO_NUM_16, 1);
  for (const auto &cmd : lcd_commands) {
    gpio_set_level(GPIO_NUM_16, 0);
    spiWord(cmd.command);
    for (unsigned i = 0; i < cmd.size; ++i) spiWord(0x100 | cmd.data[i]);
    gpio_set_level(GPIO_NUM_16, 1);
    if (cmd.delay_ms) delay(cmd.delay_ms);
  }
  esp_lcd_rgb_panel_config_t rgb{};
  rgb.clk_src = LCD_CLK_SRC_DEFAULT;
  rgb.data_width = 16;
  rgb.bits_per_pixel = 16;
  // Arduino's prebuilt SDK has no PSRAM XIP. Avoid cache-dependent bounce ISRs.
  rgb.num_fbs = 2;
  rgb.bounce_buffer_size_px = 0;
  rgb.dma_burst_size = 64;
  rgb.flags.fb_in_psram = true;
  rgb.timings.pclk_hz = 8000000;
  rgb.timings.h_res = rgb.timings.v_res = 480;
  rgb.timings.hsync_pulse_width = rgb.timings.vsync_pulse_width = 4;
  rgb.timings.hsync_back_porch = rgb.timings.vsync_back_porch = 20;
  rgb.timings.hsync_front_porch = rgb.timings.vsync_front_porch = 10;
  rgb.hsync_gpio_num = 15; rgb.vsync_gpio_num = 7;
  rgb.de_gpio_num = 40; rgb.pclk_gpio_num = 41; rgb.disp_gpio_num = -1;
  // MADCTL=0x08: same R/B wiring compensation as uedx_rotator.
  const int data[] = {46, 3, 8, 18, 17, 14, 13, 12, 11, 10, 9, 5, 45, 48, 47, 21};
  for (unsigned i = 0; i < 16; ++i) rgb.data_gpio_nums[i] = data[i];
  esp_lcd_panel_handle_t panel = nullptr;
  if (esp_lcd_new_rgb_panel(&rgb, &panel) != ESP_OK) return nullptr;
  if (esp_lcd_panel_reset(panel) != ESP_OK || esp_lcd_panel_init(panel) != ESP_OK) {
    esp_lcd_panel_del(panel);
    return nullptr;
  }
  return panel; // Backlight is enabled only after the first complete UI frame.
}
}
#endif
