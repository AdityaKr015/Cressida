#include "app/server_recurrence.h"

#include <array>
#include <cassert>

int main() {
  using namespace app;
  constexpr std::array workdays{0, 1, 2, 3, 4};
  constexpr std::array weekend{5, 6};
  static_assert(recurrenceDayOffset(1, 0) == 1);
  static_assert(recurrenceDayOffset(0, 1) == -1);
  static_assert(recurrenceDayOffset(0, 6) == 1);
  static_assert(recurrenceDayOffset(6, 0) == -1);
  static_assert(recurrenceUtcWeekdays(workdays, 1) == std::array{6, 0, 1, 2, 3});
  static_assert(recurrenceUtcWeekdays(workdays, -1) == std::array{1, 2, 3, 4, 5});
  static_assert(recurrenceUtcWeekdays(weekend, 1) == std::array{4, 5});
  static_assert(recurrenceUtcWeekdays(weekend, -1) == std::array{6, 0});
  static_assert(recurrenceUtcWeekdays(workdays, 0) == workdays);
  for (int offset = -1; offset <= 1; ++offset)
    for (int local = 0; local < 7; ++local)
      assert(recurrenceLocalWeekday(recurrenceUtcWeekday(local, offset), offset) == local);
}
