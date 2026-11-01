#pragma once

// Hardware adapter for mrcodetastic/ESP32-HUB75-MatrixPanel-DMA.
// It is the only project file that knows the chosen LED-driver library.

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "flight_ticket.hpp"

namespace flight_display {

class Hub75Canvas final : public Canvas {
 public:
  explicit Hub75Canvas(MatrixPanel_I2S_DMA& panel) : panel_(panel) {}

  void pixel(int x, int y, Color color) override {
    panel_.drawPixel(x, y, panel_.color565(color.r, color.g, color.b));
  }

  void fill_rect(int x, int y, int width, int height, Color color) override {
    panel_.fillRect(x, y, width, height, panel_.color565(color.r, color.g, color.b));
  }

 private:
  MatrixPanel_I2S_DMA& panel_;
};

}  // namespace flight_display
