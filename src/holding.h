#pragma once
#include <cmath>
#include <string>
#include "date.h"
#include "money.h"
#include "text_types.h"

// What an investment account is currently worth, without lots, holding periods or
// realised gains: those are on the 1099, and keeping a second set of them here
// would mean maintaining figures that have to agree with a document nobody
// controls. See Investments.spectable.
namespace holdings {

// The value of a holding is its shares times its price, and that value is the
// money balance of its account. Every operation leaves this true, and it is
// checked whenever a file is opened.
//
// Rounding is to the cent, half away from zero.
inline Money value_of(double shares, double price) {
    const double exact = shares * price;
    const double cents = exact * 100.0;
    const double rounded = cents < 0 ? -std::floor(-cents + 0.5) : std::floor(cents + 0.5);
    return Money::from_cents(static_cast<long long>(rounded));
}

// A security is identified by its name. The symbol is a shorter shorthand and is
// what a holdings report reads by, so the account takes it where there is one --
// and of the 44 securities in the real business export, not one has a symbol.
inline std::string account_name_for(const std::string& security_name,
                                   const std::string& symbol) {
    std::string chosen = symbol.empty() ? security_name : symbol;
    for (char& c : chosen)
        if (c == ':') c = '-';
    return chosen;
}

// Two purchases at different prices leave one holding at one price, and that
// price is the carrying value divided by the shares. It is not a cost basis and
// is not used as one.
struct AveragedHolding {
    double shares = 0;
    Money value;
    double price = 0;
};

inline AveragedHolding after_buying(double held_shares, const Money& held_value,
                                    double bought_shares, double bought_price) {
    AveragedHolding out;
    out.shares = held_shares + bought_shares;
    out.value = held_value + value_of(bought_shares, bought_price);
    // The value is the sum of what was paid; the price is derived from it, so the
    // invariant holds. Four decimal places, as a price has.
    if (out.shares != 0) {
        const double exact = (static_cast<double>(out.value.cents()) / 100.0) / out.shares;
        out.price = std::floor(exact * 10000.0 + 0.5) / 10000.0;
    }
    return out;
}

// Thirty days, which is long enough that a monthly statement keeps a holding
// fresh and short enough that a forgotten one is noticed.
inline bool is_stale(long long days_old) { return days_old > 30; }

inline bool is_stale(const types::Date& priced_on, const types::Date& today) {
    return is_stale(types::Date::days_between(priced_on, today));
}

}  // namespace holdings
