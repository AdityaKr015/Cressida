#include "app/billing_amount.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

int main() {
  try {
    const auto check = [](std::int64_t value, const std::string& currency, const std::string& expected) {
      if (app::billingAmount(value, currency) != expected)
        throw std::runtime_error("Incorrect minor-unit amount for " + currency);
    };
    check(999, "eur", "9.99 EUR");
    check(999, "jpy", "999 JPY");
    check(9900, "krw", "9900 KRW");
    check(12345, "kwd", "12.345 KWD");
    check(1, "bhd", "0.001 BHD");
    check(1, "usd", "0.01 USD");
    check(0, "gbp", "0.00 GBP");
    check(-1205, "usd", "-12.05 USD");
    check(std::numeric_limits<std::int64_t>::min(), "eur", "-92233720368547758.08 EUR");
    check(std::numeric_limits<std::int64_t>::max(), "jpy", "9223372036854775807 JPY");
    if (app::billingAmount(999, "xxx")
        || app::billingAmount(999, "")
        || app::billingAmount(999, "USD\n")
        || app::billingAmount(999, "US1"))
      throw std::runtime_error("Unknown currency must not guess a financial unit scale");
    std::cout << "Currency minor units, credits, bounds and invalid codes passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
