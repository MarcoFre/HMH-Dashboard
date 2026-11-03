#pragma once
// Gaia 0.7: deterministic first-fit occupancy grid, independent of widget type.
// Coordinates use the HmhLayout reference tile size and physical pixel scale.
#include <vector>
#include <algorithm>

struct HmhTileSlot { int x, y, w, h; };

class HmhTileGrid {
 public:
  HmhTileGrid(int available_width, int columns, int cell_height, int gap)
      : columns_(std::max(1,columns)), gap_(std::max(0,gap)),
        cell_height_(std::max(1,cell_height)),
        cell_width_(std::max(1,(available_width - (columns_-1)*gap_)/columns_)) {}

  void reset(int origin_y) {
    origin_y_ = origin_y;
    rows_.clear();
    used_rows_ = 0;
  }
  int bottom() const { return origin_y_ + used_rows_ * (cell_height_ + gap_); }
  int columns() const { return columns_; }
  HmhTileSlot place(int col_span, int row_span) {
    col_span = std::clamp(col_span,1,columns_);
    row_span = std::max(1,row_span);
    for (int row=0;row<4096;++row) {
      while (static_cast<int>(rows_.size()) < row+row_span) rows_.push_back(0);
      for (int col=0;col<=columns_-col_span;++col) {
        const unsigned mask=((1u<<col_span)-1u)<<col;
        bool free=true;
        for(int k=0;k<row_span;++k)
          if(rows_[row+k]&mask) {free=false;break;}
        if(!free) continue;
        for(int k=0;k<row_span;++k) rows_[row+k] |= mask;
        used_rows_=std::max(used_rows_,row+row_span);
        return {col*(cell_width_+gap_),origin_y_+row*(cell_height_+gap_),
                col_span*cell_width_+(col_span-1)*gap_,
                row_span*cell_height_+(row_span-1)*gap_};
      }
    }
    // Defensive fallback: very long lists can still scroll without collision.
    const int y=bottom();
    used_rows_+=row_span;
    return {0,y,col_span*cell_width_+(col_span-1)*gap_,
            row_span*cell_height_+(row_span-1)*gap_};
  }
 private:
  int columns_, gap_, cell_height_, cell_width_, origin_y_=0, used_rows_=0;
  std::vector<unsigned> rows_;
};
