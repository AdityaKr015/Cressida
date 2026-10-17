#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace app {
  // Store several visible rows so deleting the first one can use its successor.
  // User motion since the previous presented frame is independent of reflow.
  class TimelineAnchor {
  public:
    struct Row {
      std::string id;
      float y;
    };
    void reset() { m_rows.clear(); }
    void remember(std::vector<Row> rows, float offset) {
      m_rows = std::move(rows);
      m_offset = offset;
    }
    std::optional<float> correction(const std::vector<Row>& rows, float offset) const {
      for (const auto& old : m_rows)
        for (const auto& row : rows)
          if (old.id == row.id) {
            const float delta = old.y + (offset - m_offset) - row.y;
            return std::abs(delta) > 0.05F ? delta : 0;
          }
      return {};
    }

  private:
    std::vector<Row> m_rows;
    float m_offset = 0;
  };
} // namespace app
