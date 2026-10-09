#pragma once
#include <stdexcept>
#include <string>

// What kind of account a node in the tree is, and the things that follow from
// it: which group it is listed under, which side of a balance sheet it is on,
// and which way round it reads on screen. See CoreTypes.spectable.
namespace types {

// The types a person chooses from, in the groups they are chosen within. The
// group is not stored anywhere: it follows from the type, so an account filed
// once cannot drift out of its heading.
enum class AccountType {
    // Banking
    Checking, Savings, Cash,
    // Credit
    CreditCard, LineOfCredit,
    // Investments
    Brokerage, IRA, HSA, Retirement401k,
    // Loan & Debt
    Mortgage, AutoLoan, PersonalLoan, OtherDebt,
    // Property & Asset
    RealEstate, Vehicle, OtherAsset,
    // Business
    AccountsReceivable, AccountsPayable,
    // Transfer. Real accounts that are deliberately not on the balance sheet;
    // see the balance-sheet rule below.
    TransferIn, TransferOut,
    // The categories, which are accounts too -- that is what makes every
    // transaction balance -- and are in no group because the account list does
    // not show them.
    Income, Expense, Equity
};

// The headings the account list is divided into, one per kind of real account.
enum class AccountGroup {
    Banking, Credit, Investments, LoanAndDebt, PropertyAndAsset, Business,
    Transfer
};

// The two groups the types fall into. Reports ask this rather than listing the
// types.
enum class AccountClass { Real, Nominal };

// Which column of a balance sheet a type belongs in. Every real type has a
// side, including the ones kept off the balance sheet, because the side is also
// what says which way round the account reads.
enum class BalanceSide { Asset, Liability };

enum class ClearedStatus { Uncleared, Cleared, Reconciled };

// Where an identifier came from. Two files describing one transaction do not
// agree on what to call it, so the name of the source travels with the name of
// the transaction; see the identifier rule in ImportOfx.spectable.
enum class ImportSource { Ofx, Csv, Qif };

inline std::string to_string(AccountType t) {
    switch (t) {
        case AccountType::Checking:           return "Checking";
        case AccountType::Savings:            return "Savings";
        case AccountType::Cash:               return "Cash";
        case AccountType::CreditCard:         return "CreditCard";
        case AccountType::LineOfCredit:       return "LineOfCredit";
        case AccountType::Brokerage:          return "Brokerage";
        case AccountType::IRA:                return "IRA";
        case AccountType::HSA:                return "HSA";
        case AccountType::Retirement401k:     return "Retirement401k";
        case AccountType::Mortgage:           return "Mortgage";
        case AccountType::AutoLoan:           return "AutoLoan";
        case AccountType::PersonalLoan:       return "PersonalLoan";
        case AccountType::OtherDebt:          return "OtherDebt";
        case AccountType::RealEstate:         return "RealEstate";
        case AccountType::Vehicle:            return "Vehicle";
        case AccountType::OtherAsset:         return "OtherAsset";
        case AccountType::AccountsReceivable: return "AccountsReceivable";
        case AccountType::AccountsPayable:    return "AccountsPayable";
        case AccountType::TransferIn:         return "TransferIn";
        case AccountType::TransferOut:        return "TransferOut";
        case AccountType::Income:             return "Income";
        case AccountType::Expense:            return "Expense";
        case AccountType::Equity:             return "Equity";
    }
    throw std::invalid_argument("unknown account type");
}

inline AccountType account_type_from_string(const std::string& s) {
    if (s == "Checking")           return AccountType::Checking;
    if (s == "Savings")            return AccountType::Savings;
    if (s == "Cash")               return AccountType::Cash;
    if (s == "CreditCard")         return AccountType::CreditCard;
    if (s == "LineOfCredit")       return AccountType::LineOfCredit;
    if (s == "Brokerage")          return AccountType::Brokerage;
    if (s == "IRA")                return AccountType::IRA;
    if (s == "HSA")                return AccountType::HSA;
    if (s == "Retirement401k")     return AccountType::Retirement401k;
    if (s == "Mortgage")           return AccountType::Mortgage;
    if (s == "AutoLoan")           return AccountType::AutoLoan;
    if (s == "PersonalLoan")       return AccountType::PersonalLoan;
    if (s == "OtherDebt")          return AccountType::OtherDebt;
    if (s == "RealEstate")         return AccountType::RealEstate;
    if (s == "Vehicle")            return AccountType::Vehicle;
    if (s == "OtherAsset")         return AccountType::OtherAsset;
    if (s == "AccountsReceivable") return AccountType::AccountsReceivable;
    if (s == "AccountsPayable")    return AccountType::AccountsPayable;
    if (s == "TransferIn")         return AccountType::TransferIn;
    if (s == "TransferOut")        return AccountType::TransferOut;
    if (s == "Income")             return AccountType::Income;
    if (s == "Expense")            return AccountType::Expense;
    if (s == "Equity")             return AccountType::Equity;
    // The names used before the types were split into groups. A book written by
    // an earlier build has them in it, and a book that cannot be opened by the
    // next build is a book lost -- so these are read for ever, and only read.
    // Nothing writes them again: the type is stored by to_string above.
    if (s == "Bank")       return AccountType::Checking;
    if (s == "Asset")      return AccountType::OtherAsset;
    if (s == "Liability")  return AccountType::OtherDebt;
    if (s == "Investment") return AccountType::Brokerage;
    throw std::invalid_argument("unknown account type: " + s);
}

inline std::string to_string(AccountGroup g) {
    switch (g) {
        case AccountGroup::Banking:          return "Banking";
        case AccountGroup::Credit:           return "Credit";
        case AccountGroup::Investments:      return "Investments";
        case AccountGroup::LoanAndDebt:      return "LoanAndDebt";
        case AccountGroup::PropertyAndAsset: return "PropertyAndAsset";
        case AccountGroup::Business:         return "Business";
        case AccountGroup::Transfer:         return "Transfer";
    }
    throw std::invalid_argument("unknown account group");
}

inline AccountGroup account_group_from_string(const std::string& s) {
    if (s == "Banking")          return AccountGroup::Banking;
    if (s == "Credit")           return AccountGroup::Credit;
    if (s == "Investments")      return AccountGroup::Investments;
    if (s == "LoanAndDebt")      return AccountGroup::LoanAndDebt;
    if (s == "PropertyAndAsset") return AccountGroup::PropertyAndAsset;
    if (s == "Business")         return AccountGroup::Business;
    if (s == "Transfer")         return AccountGroup::Transfer;
    throw std::invalid_argument("unknown account group: " + s);
}

// What the heading reads on screen. Separate from the name above because a
// heading carries spaces and an ampersand and an identifier cannot.
inline std::string heading_of(AccountGroup g) {
    switch (g) {
        case AccountGroup::Banking:          return "Banking";
        case AccountGroup::Credit:           return "Credit";
        case AccountGroup::Investments:      return "Investments";
        case AccountGroup::LoanAndDebt:      return "Loan & Debt";
        case AccountGroup::PropertyAndAsset: return "Property & Asset";
        case AccountGroup::Business:         return "Business";
        case AccountGroup::Transfer:         return "Transfer";
    }
    throw std::invalid_argument("unknown account group");
}

// The order the groups are listed in, which is the order they are declared.
inline const AccountGroup* account_groups_in_order(int* many) {
    static const AccountGroup order[] = {
        AccountGroup::Banking, AccountGroup::Credit, AccountGroup::Investments,
        AccountGroup::LoanAndDebt, AccountGroup::PropertyAndAsset,
        AccountGroup::Business, AccountGroup::Transfer};
    if (many != nullptr) *many = 7;
    return order;
}

inline std::string to_string(AccountClass c) {
    return c == AccountClass::Real ? "Real" : "Nominal";
}

inline AccountClass account_class_from_string(const std::string& s) {
    if (s == "Real")    return AccountClass::Real;
    if (s == "Nominal") return AccountClass::Nominal;
    throw std::invalid_argument("unknown account class: " + s);
}

inline std::string to_string(BalanceSide s) {
    return s == BalanceSide::Asset ? "Asset" : "Liability";
}

inline BalanceSide balance_side_from_string(const std::string& s) {
    if (s == "Asset")     return BalanceSide::Asset;
    if (s == "Liability") return BalanceSide::Liability;
    throw std::invalid_argument("unknown balance side: " + s);
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

inline std::string to_string(ImportSource s) {
    switch (s) {
        case ImportSource::Ofx: return "Ofx";
        case ImportSource::Csv: return "Csv";
        case ImportSource::Qif: return "Qif";
    }
    throw std::invalid_argument("unknown import source");
}

inline ImportSource import_source_from_string(const std::string& s) {
    if (s == "Ofx") return ImportSource::Ofx;
    if (s == "Csv") return ImportSource::Csv;
    if (s == "Qif") return ImportSource::Qif;
    throw std::invalid_argument("unknown import source: " + s);
}

// Which heading an account is listed under. The categories have none: they are
// reached through a report rather than browsed, so nothing calls this for one.
inline AccountGroup group_of(AccountType t) {
    switch (t) {
        case AccountType::Checking:
        case AccountType::Savings:
        case AccountType::Cash:
            return AccountGroup::Banking;
        case AccountType::CreditCard:
        case AccountType::LineOfCredit:
            return AccountGroup::Credit;
        case AccountType::Brokerage:
        case AccountType::IRA:
        case AccountType::HSA:
        case AccountType::Retirement401k:
            return AccountGroup::Investments;
        case AccountType::Mortgage:
        case AccountType::AutoLoan:
        case AccountType::PersonalLoan:
        case AccountType::OtherDebt:
            return AccountGroup::LoanAndDebt;
        case AccountType::RealEstate:
        case AccountType::Vehicle:
        case AccountType::OtherAsset:
            return AccountGroup::PropertyAndAsset;
        case AccountType::AccountsReceivable:
        case AccountType::AccountsPayable:
            return AccountGroup::Business;
        case AccountType::TransferIn:
        case AccountType::TransferOut:
            return AccountGroup::Transfer;
        case AccountType::Income:
        case AccountType::Expense:
        case AccountType::Equity:
            break;
    }
    throw std::invalid_argument("a category is in no account group");
}

// The types that hold money are real and are reconciled against a statement.
// Income, Expense and Equity are the categories; reconciling one of those would
// mean nothing.
inline AccountClass class_of(AccountType t) {
    switch (t) {
        case AccountType::Income:
        case AccountType::Expense:
        case AccountType::Equity:
            return AccountClass::Nominal;
        default:
            return AccountClass::Real;
    }
}

// Which column of a balance sheet a real account belongs in. Business is the
// group that proves this has to be per type rather than per group: a receivable
// is owed to the business and a payable is owed by it.
inline BalanceSide balance_side(AccountType t) {
    switch (t) {
        case AccountType::Checking:
        case AccountType::Savings:
        case AccountType::Cash:
        case AccountType::Brokerage:
        case AccountType::IRA:
        case AccountType::HSA:
        case AccountType::Retirement401k:
        case AccountType::RealEstate:
        case AccountType::Vehicle:
        case AccountType::OtherAsset:
        case AccountType::AccountsReceivable:
        case AccountType::TransferIn:
            return BalanceSide::Asset;
        case AccountType::CreditCard:
        case AccountType::LineOfCredit:
        case AccountType::Mortgage:
        case AccountType::AutoLoan:
        case AccountType::PersonalLoan:
        case AccountType::OtherDebt:
        case AccountType::AccountsPayable:
        case AccountType::TransferOut:
            return BalanceSide::Liability;
        case AccountType::Income:
        case AccountType::Expense:
        case AccountType::Equity:
            break;
    }
    throw std::invalid_argument("a category is on neither side of a balance sheet");
}

// Transfer accounts are real and are deliberately left off the balance sheet: a
// transfer is money on its way between two accounts that are both already
// there, so counting it would count the same money twice. See the balance-sheet
// rule in Reports.spectable.
inline bool on_balance_sheet(AccountType t) {
    if (class_of(t) == AccountClass::Nominal) return false;
    return group_of(t) != AccountGroup::Transfer;
}

// Postings are signed with debits positive, whatever the account. The display
// sign is what turns that back into the figure the account's own statement
// shows: a charge to a credit card credits that account, and the cardholder
// thinks of it as an amount owed.
inline int display_sign(AccountType t) {
    switch (t) {
        case AccountType::Expense:
            return 1;
        case AccountType::Income:
        case AccountType::Equity:
            return -1;
        default:
            // Everything owned reads positive and everything owed reads as the
            // amount owed, which is the same question the balance sheet asks.
            return balance_side(t) == BalanceSide::Asset ? 1 : -1;
    }
}

}  // namespace types
