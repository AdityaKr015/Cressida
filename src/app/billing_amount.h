#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace app {
  // Discord billing amounts use the currency's minor-unit exponent. Keep the
  // currency code explicit and calculate with integers, including credit amounts.
  inline std::optional<std::string> billingAmount(std::int64_t amount, std::string currency) {
    if (currency.size() != 3)
      return {};
    for (auto& c : currency) {
      if (c >= 'a' && c <= 'z')
        c -= 32;
      else if (c < 'A' || c > 'Z')
        return {};
    }
    static constexpr std::string_view exponent0[] = {"BIF", "BYR", "CLF", "CLP", "DJF", "GNF", "ISK", "JPY",
                                                     "KMF", "KRW", "PYG", "RWF", "UGX", "UYI", "VND", "VUV",
                                                     "XAF", "XAG", "XAU", "XBA", "XBB", "XBC", "XBD", "XDR",
                                                     "XFU", "XOF", "XPD", "XPF", "XPT", "XSU", "XTS", "XUA"};
    static constexpr std::string_view exponent2[] = {
        "AED", "AFN", "ALL", "AMD", "ANG", "AOA", "ARS", "AUD", "AWG", "AZN", "BAM", "BBD", "BDT", "BGN", "BMD", "BND",
        "BOB", "BOV", "BRL", "BSD", "BTN", "BWP", "BYN", "BZD", "CAD", "CDF", "CHE", "CHF", "CHW", "CNY", "COP", "COU",
        "CRC", "CUC", "CUP", "CVE", "CZK", "DKK", "DOP", "DZD", "EGP", "ERN", "ETB", "EUR", "FJD", "FKP", "GBP", "GEL",
        "GHS", "GIP", "GMD", "GTQ", "GYD", "HKD", "HNL", "HRK", "HTG", "HUF", "IDR", "ILS", "INR", "IRR", "JMD", "KES",
        "KGS", "KHR", "KPW", "KYD", "KZT", "LAK", "LBP", "LKR", "LRD", "LSL", "LTL", "LVL", "MAD", "MDL", "MGA", "MKD",
        "MMK", "MNT", "MOP", "MRO", "MUR", "MVR", "MWK", "MXN", "MXV", "MYR", "MZN", "NAD", "NGN", "NIO", "NOK", "NPR",
        "NZD", "PAB", "PEN", "PGK", "PHP", "PKR", "PLN", "QAR", "RON", "RSD", "RUB", "SAR", "SBD", "SCR", "SDG", "SEK",
        "SGD", "SHP", "SLL", "SOS", "SRD", "SSP", "STD", "SVC", "SYP", "SZL", "THB", "TJS", "TMT", "TOP", "TRY", "TTD",
        "TWD", "TZS", "UAH", "USD", "USN", "USS", "UYU", "UZS", "VEF", "WST", "XCD", "YER", "ZAR", "ZMW", "ZWL"
    };
    static constexpr std::string_view exponent3[] = {"BHD", "IQD", "JOD", "KWD", "LYD", "OMR", "TND"};
    unsigned digits = 0;
    if (std::ranges::find(exponent0, currency) != std::end(exponent0))
      digits = 0;
    else if (std::ranges::find(exponent2, currency) != std::end(exponent2))
      digits = 2;
    else if (std::ranges::find(exponent3, currency) != std::end(exponent3))
      digits = 3;
    else
      return {};
    const std::uint64_t magnitude =
        amount < 0 ? static_cast<std::uint64_t>(-(amount + 1)) + 1 : static_cast<std::uint64_t>(amount);
    const std::uint64_t scale = digits == 0 ? 1 : digits == 2 ? 100 : 1000;
    auto result = std::string(amount < 0 ? "-" : "") + std::to_string(magnitude / scale);
    if (digits)
      result += '.' + std::to_string(scale + magnitude % scale).substr(1);
    return result + ' ' + currency;
  }
} // namespace app
