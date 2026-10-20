#pragma once

#include "app/layout_metrics.h"

#include <algorithm>
#include <vector>

namespace app::layout {
  // Remote indices include group headings and may contain unloaded gaps. Compact
  // only known headings; retain ordinary row space for missing member pages.
  class MemberListLayout {
  public:
    template <class Rows> MemberListLayout(const Rows& rows, int count) : m_count(std::max(0, count)) {
      for (const auto& row : rows) {
        if (row.index < 0)
          continue;
        m_count = std::max(m_count, row.index + 1);
        if (!row.group.empty())
          m_groups.push_back(row.index);
      }
      std::ranges::sort(m_groups);
      const auto duplicates = std::ranges::unique(m_groups);
      m_groups.erase(duplicates.begin(), duplicates.end());
    }
    float top(int index) const {
      const auto preceding = std::lower_bound(m_groups.begin(), m_groups.end(), index) - m_groups.begin();
      return std::max(0, index) * Members::rowHeight - preceding * (Members::rowHeight - Members::groupHeight);
    }
    float height() const { return top(m_count); }
    float maximumScroll(float viewport) const { return std::max(0.0F, height() - viewport); }
    int indexAt(float offset) const {
      if (!m_count)
        return 0;
      int first = 0, last = m_count;
      while (first + 1 < last) {
        const int middle = first + (last - first) / 2;
        if (top(middle) <= offset)
          first = middle;
        else
          last = middle;
      }
      return first;
    }

  private:
    std::vector<int> m_groups;
    int m_count;
  };
} // namespace app::layout
