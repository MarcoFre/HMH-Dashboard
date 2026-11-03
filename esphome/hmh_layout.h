#pragma once
// HMH Gaia Responsive 0.6 — shared reference geometry.
// All dimensions are designed on an 800 x 480 reference canvas.
// Prefer min(scale_x, scale_y) for touch targets/fonts and axis-specific
// scaling for positions. Keeps existing 800 x 480 visuals pixel-identical.
#include "lvgl.h"
#include <algorithm>
#include <cmath>

struct HmhLayout {
  int width = 800;
  int height = 480;
  static constexpr int reference_width = 800;
  static constexpr int reference_height = 480;

  explicit HmhLayout(int w = 800, int h = 480)
      : width(std::max(w, 1)), height(std::max(h, 1)) {}

  static HmhLayout current() {
    lv_display_t *display = lv_display_get_default();
    if (!display) return HmhLayout{};
    return HmhLayout(lv_display_get_horizontal_resolution(display),
                     lv_display_get_vertical_resolution(display));
  }

  float scale_x() const { return static_cast<float>(width) / reference_width; }
  float scale_y() const { return static_cast<float>(height) / reference_height; }
  float scale() const { return std::min(scale_x(), scale_y()); }

  int x(int reference) const { return std::lround(reference * scale_x()); }
  int y(int reference) const { return std::lround(reference * scale_y()); }
  int size(int reference) const { return std::max(1, static_cast<int>(std::lround(reference * scale()))); }

  // Absolute positions take the full axis; sizes retain a uniform shape.
  int sidebar_width() const { return size(60); }
  int status_height() const { return y(54); }
  int content_left() const { return sidebar_width() + size(14); }
  int content_width() const { return std::max(1, width - content_left() - size(12)); }
  int columns(int min_card_width = 260) const {
    const int gap = size(12);
    return std::clamp((content_width() + gap) / (size(min_card_width) + gap), 1, 4);
  }
  int card_width(int columns_count) const {
    const int gap = size(12);
    return std::max(1, (content_width() - gap * (columns_count - 1)) / columns_count);
  }
};
