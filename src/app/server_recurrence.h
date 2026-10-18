#pragma once

#include <array>

namespace app {
  // Dates expose Sunday=0; Discord recurrence days use Monday=0. A local
  // occurrence can land on the previous/next UTC date near midnight.
  constexpr int recurrenceDay(int value) { return (value % 7 + 7) % 7; }
  constexpr int recurrenceDayOffset(int localSunday, int utcSunday) {
    const int delta = recurrenceDay(localSunday - utcSunday);
    return delta > 3 ? delta - 7 : delta;
  }
  constexpr int recurrenceUtcWeekday(int localMonday, int offset) { return recurrenceDay(localMonday - offset); }
  constexpr int recurrenceLocalWeekday(int utcMonday, int offset) { return recurrenceDay(utcMonday + offset); }
  template <std::size_t N> constexpr std::array<int, N> recurrenceUtcWeekdays(std::array<int, N> local, int offset) {
    for (auto& day : local)
      day = recurrenceUtcWeekday(day, offset);
    return local;
  }
} // namespace app
