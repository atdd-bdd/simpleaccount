#pragma once
#include <cmath>
#include <string>
#include <vector>
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

// The price that goes with a value and a share count: the value divided by the
// shares, to four decimal places (half away from zero), which is what a price
// is kept to. Shared by every place a price is derived from a value rather than
// carried forward, so the rounding is the same wherever it happens. Zero where
// there are no shares to divide by -- a closed position carries no price.
inline double round_price(const Money& value, double shares) {
    if (shares == 0) return 0;
    const double exact = (static_cast<double>(value.cents()) / 100.0) / shares;
    return std::floor(exact * 10000.0 + 0.5) / 10000.0;
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
    // invariant holds.
    out.price = round_price(out.value, out.shares);
    return out;
}

// Thirty days, which is long enough that a monthly statement keeps a holding
// fresh and short enough that a forgotten one is noticed.
inline bool is_stale(long long days_old) { return days_old > 30; }

inline bool is_stale(const types::Date& priced_on, const types::Date& today) {
    return is_stale(types::Date::days_between(priced_on, today));
}

// One security beside the account that carries its value: the account is the
// identity, so there is one holding per Investment account, never two for the
// same account. SecurityName is blank wherever a scenario never gave one --
// which, in the QIF export this is all for, is every one of them.
struct Holding {
    types::AccountPath account;
    types::SecurityName security_name;
    types::Symbol symbol;
    double shares = 0;
    double price = 0;
    types::Date price_as_of{2024, 1, 1};
};

inline Holding* find_by_account(std::vector<Holding>& book, const std::string& account) {
    for (Holding& h : book)
        if (h.account.value() == account) return &h;
    return nullptr;
}

inline const Holding* find_by_account(const std::vector<Holding>& book,
                                      const std::string& account) {
    for (const Holding& h : book)
        if (h.account.value() == account) return &h;
    return nullptr;
}

}  // namespace holdings
