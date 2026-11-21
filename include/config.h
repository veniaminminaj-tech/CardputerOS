#pragma once
// Конфигурация оборудования и экрана CardputerOS
#include <M5GFX.h>
#include <lgfx/v1/platforms/esp32/Bus_SPI.hpp>
#include <lgfx/v1/platforms/esp32/Light_PWM.hpp>
#include "panel_ili9341.h"

// ---- Внешний TFT ILI9341 (320x240) на разъёме EXT 2.54-14P ----
// ВНИМАНИЕ: значения ниже — примеры свободных GPIO. Сверьте с распиновкой
// вашей платы. Не занимайте пины SD-карты (ниже) и внутренние пины Cardputer.
#define EXT_TFT_MOSI  4
#define EXT_TFT_SCLK  5
#define EXT_TFT_CS    6
#define EXT_TFT_DC    7
#define EXT_TFT_RST   10
#define EXT_TFT_BL    18

// ---- SD-карта Cardputer (SPI) ----
#define CARD_SD_CS    12
#define CARD_SD_SCK   40
#define CARD_SD_MISO  39
#define CARD_SD_MOSI  14

// Часовой пояс (POSIX-формат). Поменяйте под себя.
#define TIMEZONE_POSIX "MSK-3"

constexpr int SCR_W = 320;
constexpr int SCR_H = 240;
constexpr int STATUS_H = 14;   // верхняя панель
constexpr int TITLE_H  = 12;   // заголовок окна

// Цвета RGB565
constexpr uint16_t COL_BLACK     = 0x0000;
constexpr uint16_t COL_WHITE     = 0xFFFF;
constexpr uint16_t COL_GRAY      = 0x7BEF;
constexpr uint16_t COL_DGRAY     = 0x2104;
constexpr uint16_t COL_GREEN     = 0x07E0;
constexpr uint16_t COL_CYAN      = 0x07FF;
constexpr uint16_t COL_YELLOW    = 0xFFE0;
constexpr uint16_t COL_ORANGE    = 0xFD20;
constexpr uint16_t COL_DESK      = 0x0841;   // фон рабочего стола
constexpr uint16_t COL_TITLE_ON  = 0x1A94;   // заголовок активного окна
constexpr uint16_t COL_TITLE_OFF = 0x4208;

class CardDisplay : public lgfx::LGFX_Device {
  lgfx::Panel_KolibriX_ILI9341 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

public:
  CardDisplay() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI3_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = EXT_TFT_SCLK;
      cfg.pin_mosi = EXT_TFT_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = EXT_TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = EXT_TFT_CS;
      cfg.pin_rst = EXT_TFT_RST;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = EXT_TFT_BL;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

extern CardDisplay tft;
