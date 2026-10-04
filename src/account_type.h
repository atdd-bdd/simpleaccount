#pragma once
#include <stdexcept>
#include <string>

// What kind of account a node in the tree is, and the two things that follow
// from it: which group it belongs to, and which way round it reads on screen.
// See Accounts.spectable.
namespace types {

enum class AccountType {
    Bank, Cash, CreditCard, Asset, Liability, Investment,
    Income, Expense, Equity
};

// The two groups the types fall into. Reports and the balance sheet ask this
// rather than listing the types.
enum class AccountClass { Real, Nominal };

enum class ClearedStatus { Uncleared, Cleared, Reconciled };

inline AccountType account_type_from_string(const std::string& s) {
    if (s == "Bank")       return AccountType::Bank;
    if (s == "Cash")       return AccountType::Cash;
    if (s == "CreditCard") return AccountType::CreditCard;
    if (s == "Asset")      return AccountType::Asset;
    if (s == "Liability")  return AccountType::Liability;
    if (s == "Investment") return AccountType::Investment;
    if (s == "Income")     return AccountType::Income;
    if (s == "Expense")    return AccountType::Expense;
    if (s == "Equity")     return AccountType::Equity;
    throw std::invalid_argument("unknown account type: " + s);
}

inline std::string to_string(AccountType t) {
    switch (t) {
        case AccountType::Bank:       return "Bank";
        case AccountType::Cash:       return "Cash";
        case AccountType::CreditCard: return "CreditCard";
        case AccountType::Asset:      return "Asset";
        case AccountType::Liability:  return "Liability";
        case AccountType::Investment: return "Investment";
        case AccountType::Income:     return "Income";
        case AccountType::Expense:    return "Expense";
        case AccountType::Equity:     return "Equity";
    }
    throw std::invalid_argument("unknown account type");
}

inline AccountClass account_class_from_string(const std::string& s) {
    if (s == "Real")    return AccountClass::Real;
    if (s == "Nominal") return AccountClass::Nominal;
    throw std::invalid_argument("unknown account class: " + s);
}

inline std::string to_string(AccountClass c) {
    return c == AccountClass::Real ? "Real" : "Nominal";
}

inline ClearedStatus cleared_status_from_string(const std::string& s) {
    if (s == "Uncleared")  return ClearedStatus::Uncleared;
    if (s == "Cleared")    return ClearedStatus::Cleared;
    if (s == "Reconciled") return ClearedStatus::Reconciled;
    throw std::invalid_argument("unknown cleared status: " + s);
}

inline std::string to_string(ClearedStatus c) {
    switch (c) {
        case ClearedStatus::Uncleared:  return "Uncleared";
        case ClearedStatus::Cleared:    return "Cleared";
        case ClearedStatus::Reconciled: return "Reconciled";
    }
    throw std::invalid_argument("unknown cleared status");
}

// The types that hold money are real and are reconciled against a statement.
// Income, Expense and Equity are the categories; reconciling one of those would
// mean nothing.
inline AccountClass class_of(AccountType t) {
    switch (t) {
        case AccountType::Bank:
        case AccountType::Cash:
        case AccountType::CreditCard:
        case AccountType::Asset:
        case AccountType::Liability:
        case AccountType::Investment:
            return AccountClass::Real;
        case AccountType::Income:
        case AccountType::Expense:
        case AccountType::Equity:
            return AccountClass::Nominal;
    }
    throw std::invalid_argument("unknown account type");
}

// Postings are signed with debits positive, whatever the account. The display
// sign is what turns that back into the figure the account's own statement
// shows: a charge to a credit card credits that account, and the cardholder
// thinks of it as an amount owed.
inline int display_sign(AccountType t) {
    switch (t) {
        case AccountType::Bank:
        case AccountType::Cash:
        case AccountType::Asset:
        case AccountType::Investment:
        case AccountType::Expense:
            return 1;
        case AccountType::CreditCard:
        case AccountType::Liability:
        case AccountType::Income:
        case AccountType::Equity:
            return -1;
    }
    throw std::invalid_argument("unknown account type");
}

}  // namespace types
