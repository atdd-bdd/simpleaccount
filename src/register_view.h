#pragma once
#include <string>
#include "account_type.h"
#include "money.h"

// The register of one account. Every line of it is one posting to that account,
// with the other side summarised in the category column. See
// TransactionRegister.spectable.
namespace reg {

// The register is read as a statement of the account it is open on, so the two
// amount columns are headed with the words that account uses.
struct ColumnHeadings {
    std::string out_column;
    std::string in_column;
};

inline ColumnHeadings headings_for(types::AccountType t) {
    switch (t) {
        case types::AccountType::Bank:       return {"Payment",  "Deposit"};
        case types::AccountType::Cash:       return {"Spend",    "Receive"};
        case types::AccountType::CreditCard: return {"Charge",   "Payment"};
        case types::AccountType::Liability:  return {"Increase", "Payment"};
        case types::AccountType::Asset:      return {"Decrease", "Increase"};
        case types::AccountType::Investment: return {"Payment",  "Deposit"};
        case types::AccountType::Expense:    return {"Refund",   "Spent"};
        case types::AccountType::Income:     return {"Earned",   "Returned"};
        case types::AccountType::Equity:     return {"Decrease", "Increase"};
    }
    return {"Payment", "Deposit"};
}

// A credit card register shows what is owed, growing as it is charged, because
// that is what the statement shows.
inline Money balance_shown(types::AccountType t, const Money& posting_amount) {
    return types::display_sign(t) < 0 ? -posting_amount : posting_amount;
}

// By date, and within a date by the order the transactions were entered. Entry
// order is used for the tie rather than amount or payee because it is stable:
// reopening the file, or importing more, never reshuffles lines that are already
// there, so the running balance against a line does not change under the user.
struct OrderKey {
    long long date_key = 0;
    int entry_seq = 0;

    bool operator<(const OrderKey& o) const {
        if (date_key != o.date_key) return date_key < o.date_key;
        return entry_seq < o.entry_seq;
    }
};

}  // namespace reg
