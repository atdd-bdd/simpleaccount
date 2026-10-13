#pragma once
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "chart.h"
#include "holding.h"
#include "ledger.h"
#include "money.h"
#include "text_types.h"
#include "transaction_id.h"

// What happens to a brokerage account: buying, selling, recording a price, a
// dividend, and a stated value. Each one is a pure function of the figures in
// hand, so the ledger entries they build can be checked without a book on
// disk. See Investments.spectable.
//
// No lots are kept and no realised gain is computed. The holding carries a
// single averaged price (holdings::after_buying), and selling above or below
// it moves the difference to Equity:Market Value Changes rather than to
// income -- both so that cost basis stays on the 1099, the document that
// actually controls it.
namespace investing {

inline const types::AccountPath& fees_account() {
    static const types::AccountPath a("Expenses:Investment:Fees");
    return a;
}

inline const types::AccountPath& market_value_changes_account() {
    static const types::AccountPath a("Equity:Market Value Changes");
    return a;
}

// A brokerage's cash sits beside its holdings, one level up: a holding's
// account is Assets:Brokerage:VTSAX and its cash is Assets:Brokerage:Cash. See
// "The shape of a brokerage account" in Investments.spectable.
inline types::AccountPath cash_account_for(const types::AccountPath& holding_account) {
    const std::string parent = types::parent_of(holding_account);
    return types::AccountPath(parent.empty() ? "Cash" : parent + ":Cash");
}

// A share count the way a person reads it -- 100, not 100.000000 -- for the
// one place a count is shown in prose rather than a table cell.
inline std::string format_shares(double shares) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", shares);
    std::string s(buf);
    std::size_t last = s.find_last_not_of('0');
    if (s[last] == '.') --last;
    return s.substr(0, last + 1);
}

inline ledger::Transaction new_transaction(const types::Date& date, const std::string& ref) {
    ledger::Transaction t;
    t.id = ledger::new_id();
    t.ref = types::TransactionRef(ref);
    t.date = date;
    return t;
}

// ---------------------------------------------------------------------- buying

struct BuyResult {
    ledger::Transaction transaction;
    // A line per the holding and, where there was a fee, a second for it --
    // present only when there is more than one, same as any other split.
    std::vector<ledger::TransactionSplit> splits;
    double new_shares = 0;
    Money new_value;
    double new_price = 0;
};

// held_value is the holding account's own ledger balance, not shares times the
// carried price: averaging rounds the price, so recomputing the value from it
// would drift a cent from what is actually posted. See the invariant note under
// "Selling shares at the carried price needs no gain" in Investments.spectable.
inline BuyResult buy_shares(const chart::Chart& accounts, const types::AccountPath& account,
                            double held_shares,
                            const Money& held_value, double bought_shares,
                            double bought_price, const Money& fee, const types::Date& date,
                            const std::string& ref) {
    BuyResult out;
    const Money bought_value = holdings::value_of(bought_shares, bought_price);

    ledger::Transaction t = new_transaction(date, ref);
    t.source_account = accounts.ref_for_path(account.value());
    t.amount = bought_value;

    // A commission is an expense, not part of the holding: folding it into the
    // holding's value would make the invariant false, because the shares did
    // not cost more than their price.
    if (fee.cents() != 0) {
        t.destination = chart::split_ref();
        ledger::TransactionSplit cash;
        cash.transaction_id = t.id;
        cash.destination = accounts.ref_for_path(cash_account_for(account).value());
        cash.amount = -(bought_value + fee);
        out.splits.push_back(cash);
        ledger::TransactionSplit f;
        f.transaction_id = t.id;
        f.destination = accounts.ref_for_path(fees_account().value());
        f.amount = fee;
        out.splits.push_back(f);
    } else {
        t.destination = accounts.ref_for_path(cash_account_for(account).value());
    }
    out.transaction = t;

    const holdings::AveragedHolding after =
        holdings::after_buying(held_shares, held_value, bought_shares, bought_price);
    out.new_shares = after.shares;
    out.new_value = after.value;
    out.new_price = after.price;
    return out;
}

// --------------------------------------------------------------------- selling

struct SellResult {
    chart::Rejection rejection;
    ledger::Transaction transaction;
    // A line for the holding and, where the sale was above or below the
    // carried price, a second for the market-value change.
    std::vector<ledger::TransactionSplit> splits;
    double new_shares = 0;
    Money new_value;
    double new_price = 0;
};

inline SellResult sell_shares(const chart::Chart& accounts, const types::AccountPath& account,
                              const types::Symbol& symbol,
                              double held_shares, double held_price, const Money& held_value,
                              double sold_shares, double sale_price, const types::Date& date,
                              const std::string& ref) {
    SellResult out;
    // Short positions are not modelled, so this is always either a missing
    // purchase or a typing mistake.
    if (sold_shares > held_shares + 1e-9) {
        out.rejection = {true, "Only " + format_shares(held_shares) + " shares of " +
                              symbol.value() + " are held"};
        return out;
    }

    // What leaves the holding is valued at the carried price, not the sale
    // price: the difference between the two is a market movement, not part of
    // what this sale itself moved.
    const Money carried_value_sold = holdings::value_of(sold_shares, held_price);
    const Money proceeds = holdings::value_of(sold_shares, sale_price);
    const Money gain = proceeds - carried_value_sold;

    ledger::Transaction t = new_transaction(date, ref);
    t.source_account = accounts.ref_for_path(cash_account_for(account).value());
    t.amount = proceeds;
    // A sale at exactly the carried price needs no split: there is nothing to
    // put to Equity.
    if (gain.cents() != 0) {
        t.destination = chart::split_ref();
        ledger::TransactionSplit held;
        held.transaction_id = t.id;
        held.destination = accounts.ref_for_path(account.value());
        held.amount = -carried_value_sold;
        out.splits.push_back(held);
        ledger::TransactionSplit equity;
        equity.transaction_id = t.id;
        equity.destination = accounts.ref_for_path(market_value_changes_account().value());
        equity.amount = -gain;
        out.splits.push_back(equity);
    } else {
        t.destination = accounts.ref_for_path(account.value());
    }
    out.transaction = t;

    out.new_shares = held_shares - sold_shares;
    out.new_value = held_value - carried_value_sold;
    // The price is re-derived from the value that is left, rather than carried
    // forward, so the invariant stays exact. A closed position has no value to
    // derive one from, so it carries the price it was last sold at instead.
    out.new_price = out.new_shares != 0 ? holdings::round_price(out.new_value, out.new_shares)
                                        : sale_price;
    return out;
}

// --------------------------------------------------------------- recording a price

struct RevalueResult {
    bool posted = false;   // false where the revaluation was zero: not worth a transaction
    ledger::Transaction transaction;
};

inline RevalueResult revalue(const chart::Chart& accounts, const types::AccountPath& account,
                             const Money& held_value,
                             double shares, double new_price, const types::Date& as_of,
                             const std::string& ref) {
    RevalueResult out;
    const Money new_value = holdings::value_of(shares, new_price);
    const Money diff = new_value - held_value;
    if (diff.cents() == 0) return out;
    ledger::Transaction t = new_transaction(as_of, ref);
    t.source_account = accounts.ref_for_path(account.value());
    t.destination = accounts.ref_for_path(market_value_changes_account().value());
    t.amount = diff;
    out.transaction = t;
    out.posted = true;
    return out;
}

// ----------------------------------------------------------------------- income

struct DividendResult {
    ledger::Transaction transaction;
    double new_shares = 0;
    Money new_value;
    double new_price = 0;
};

// Still income whether or not it is reinvested, so it still appears in an
// income report; a reinvested one is also a purchase, averaged in exactly as
// buy_shares would. The cash of one not reinvested never touches the holding
// at all -- it lands in the account's cash leg.
inline DividendResult record_dividend(const chart::Chart& accounts,
                                      const types::AccountPath& account,
                                      const types::AccountPath& category, const Money& amount,
                                      bool reinvest, double held_shares, const Money& held_value,
                                      double bought_shares, double bought_price,
                                      const types::Date& date, const std::string& ref) {
    DividendResult out;
    ledger::Transaction t = new_transaction(date, ref);
    t.source_account = accounts.ref_for_path(
        reinvest ? account.value() : cash_account_for(account).value());
    t.destination = accounts.ref_for_path(category.value());
    t.amount = amount;
    out.transaction = t;

    if (reinvest) {
        const holdings::AveragedHolding after =
            holdings::after_buying(held_shares, held_value, bought_shares, bought_price);
        out.new_shares = after.shares;
        out.new_value = after.value;
        out.new_price = after.price;
    } else {
        out.new_shares = held_shares;
        out.new_value = held_value;
    }
    return out;
}

// ------------------------------------------------------------------ a stated value

struct ValueResult {
    chart::Rejection rejection;
    ledger::Transaction transaction;
};

// The value of an account that holds something is the total of its holdings,
// so setting it directly would make the invariant false with no way to tell
// which figure was wrong -- hence the refusal whenever has_holding is true.
inline ValueResult state_value(const chart::Chart& accounts, const types::AccountPath& account,
                               bool has_holding,
                               const types::Symbol& symbol, const Money& current_value,
                               const Money& new_value, const types::Date& as_of,
                               const std::string& ref) {
    ValueResult out;
    if (has_holding) {
        out.rejection = {true, account.value() + " holds " + symbol.value() +
                              "; record a price instead of a value"};
        return out;
    }
    const Money diff = new_value - current_value;
    ledger::Transaction t = new_transaction(as_of, ref);
    t.source_account = accounts.ref_for_path(account.value());
    t.destination = accounts.ref_for_path(market_value_changes_account().value());
    t.amount = diff;
    out.transaction = t;
    return out;
}

// ---------------------------------------------------------------------- the gain report

// Asking for a gain report says what the program does instead, rather than
// offering a figure that would not agree with the 1099 and would be believed
// anyway.
inline const char* gain_report_notice() {
    return "Realised gains are not tracked; the carried price is an average, not a basis";
}

// -------------------------------------------------------------------- the holdings report

struct HoldingsReportRow {
    std::string security_name;
    std::string symbol;
    double shares = 0;
    double price = 0;
    Money value;
    types::Date price_as_of;
    long long days_old = 0;
    bool stale = false;
};

// By symbol where there is one, else by name -- the same preference the
// account naming rule uses -- so the eye reads the report in the order a
// statement would list it.
inline std::vector<HoldingsReportRow> holdings_report_rows(
        const std::vector<holdings::Holding>& book, const types::Date& today) {
    std::vector<HoldingsReportRow> out;
    for (const holdings::Holding& h : book) {
        HoldingsReportRow row;
        row.security_name = h.security_name.value();
        row.symbol = h.symbol.value();
        row.shares = h.shares;
        row.price = h.price;
        row.value = holdings::value_of(h.shares, h.price);
        row.price_as_of = h.price_as_of;
        row.days_old = types::Date::days_between(h.price_as_of, today);
        row.stale = holdings::is_stale(row.days_old);
        out.push_back(row);
    }
    std::sort(out.begin(), out.end(), [](const HoldingsReportRow& a, const HoldingsReportRow& b) {
        const std::string& ka = a.symbol.empty() ? a.security_name : a.symbol;
        const std::string& kb = b.symbol.empty() ? b.security_name : b.symbol;
        return ka < kb;
    });
    return out;
}

}  // namespace investing
