#pragma once
#include <optional>
#include <string>
#include "account_type.h"
#include "money.h"

// Reading the pieces of a QIF file. Every rule here was checked against the two
// real exports in testdata; the comments say what the file showed.
namespace qif {

// What an exclamation-mark header introduces. The ones marked observed are in
// those two files; Class, Security and Prices are not, because Quicken replaced
// classes with tags and neither export carries a price list.
enum class Section {
    AccountList, CategoryList, TagList, ClassList, SecurityList, PriceList,
    MemorizedList, InvoiceItemList, TemplateList,
    BankTxns, CashTxns, CreditCardTxns, AssetTxns, LiabilityTxns,
    InvestmentTxns, InvoiceTxns, BillTxns, TaxTxns,
    AutoSwitchOn, AutoSwitchOff, Unknown
};

inline std::string to_string(Section s) {
    switch (s) {
        case Section::AccountList:     return "AccountList";
        case Section::CategoryList:    return "CategoryList";
        case Section::TagList:         return "TagList";
        case Section::ClassList:       return "ClassList";
        case Section::SecurityList:    return "SecurityList";
        case Section::PriceList:       return "PriceList";
        case Section::MemorizedList:   return "MemorizedList";
        case Section::InvoiceItemList: return "InvoiceItemList";
        case Section::TemplateList:    return "TemplateList";
        case Section::BankTxns:        return "BankTxns";
        case Section::CashTxns:        return "CashTxns";
        case Section::CreditCardTxns:  return "CreditCardTxns";
        case Section::AssetTxns:       return "AssetTxns";
        case Section::LiabilityTxns:   return "LiabilityTxns";
        case Section::InvestmentTxns:  return "InvestmentTxns";
        case Section::InvoiceTxns:     return "InvoiceTxns";
        case Section::BillTxns:        return "BillTxns";
        case Section::TaxTxns:         return "TaxTxns";
        case Section::AutoSwitchOn:    return "AutoSwitchOn";
        case Section::AutoSwitchOff:   return "AutoSwitchOff";
        case Section::Unknown:         return "Unknown";
    }
    return "Unknown";
}

namespace detail {

inline std::string trim(const std::string& s) {
    const std::size_t b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return "";
    const std::size_t e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

inline std::string lower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    return out;
}

}  // namespace detail

// Case and trailing blanks are ignored. Not optional: both files write
// "!Type:Bank " and "!Type:Cash " with a trailing space and the rest without.
inline Section section_of(const std::string& line) {
    const std::string s = detail::lower(detail::trim(line));
    if (s == "!account")            return Section::AccountList;
    if (s == "!type:cat")           return Section::CategoryList;
    if (s == "!type:tag")           return Section::TagList;
    if (s == "!type:class")         return Section::ClassList;
    if (s == "!type:security")      return Section::SecurityList;
    if (s == "!type:prices")        return Section::PriceList;
    if (s == "!type:memorized")     return Section::MemorizedList;
    if (s == "!type:invitem")       return Section::InvoiceItemList;
    if (s == "!type:template")      return Section::TemplateList;
    if (s == "!type:bank")          return Section::BankTxns;
    if (s == "!type:cash")          return Section::CashTxns;
    if (s == "!type:ccard")         return Section::CreditCardTxns;
    if (s == "!type:oth a")         return Section::AssetTxns;
    if (s == "!type:oth l")         return Section::LiabilityTxns;
    if (s == "!type:invst")         return Section::InvestmentTxns;
    if (s == "!type:invoice")       return Section::InvoiceTxns;
    if (s == "!type:bill")          return Section::BillTxns;
    if (s == "!type:tax")           return Section::TaxTxns;
    if (s == "!option:autoswitch")  return Section::AutoSwitchOn;
    if (s == "!clear:autoswitch")   return Section::AutoSwitchOff;
    return Section::Unknown;
}

// Quicken has written four markers for this over the years and they mean two
// things between them. All four occur in the business file: X, a star, c, and R
// once.
inline types::ClearedStatus cleared_of(const std::string& text) {
    const std::string s = detail::lower(detail::trim(text));
    if (s.empty())          return types::ClearedStatus::Uncleared;
    if (s == "*" || s == "c") return types::ClearedStatus::Cleared;
    if (s == "x" || s == "r") return types::ClearedStatus::Reconciled;
    return types::ClearedStatus::Uncleared;
}

// An amount, with thousands separators and optionally the European convention
// where the roles of comma and point are swapped. An empty amount is zero and is
// not an error; text that is not a number is.
//
// U and T are the same field written twice. They are equal in all 42,322
// transaction records of the business file, so the check that compares them is
// expected never to fire -- and is kept because a disagreement would matter.
inline std::optional<Money> parse_amount(const std::string& text, bool decimal_comma) {
    const std::string s = detail::trim(text);
    if (s.empty()) return Money();

    std::string normalised;
    normalised.reserve(s.size());
    for (char c : s) {
        if (decimal_comma) {
            if (c == '.') continue;            // a thousands separator
            if (c == ',') { normalised += '.'; continue; }
        }
        normalised += c;
    }
    if (!Money::is_valid(normalised)) return std::nullopt;
    return Money(normalised);
}

// A split line is signed from the point of view of the account the record is in,
// and the lines sum to T. So the posting to the category is the line with its
// sign turned round, and the posting to the account itself is T.
//
// Confirmed rather than assumed: in all 1,045 records with dollar lines across
// both files the lines sum exactly to T, and in 268 of them the lines do not
// share one sign -- a paycheque has gross pay one way and deductions the other.
// So the rule is per line, not per record.
inline Money posting_amount_of_split_line(const Money& dollar) { return -dollar; }

// QIF names a type and a bare name; the tree needs a root, and which root
// follows from the type. Every type below occurs in the two files. Invoice, Bill
// and Tax are Quicken Home and Business accounts: a receivable is an asset and a
// payable is a liability, and nothing else about them needs modelling.
struct AccountMapping {
    types::AccountType type = types::AccountType::Bank;
    std::string root;
};

inline std::optional<AccountMapping> account_mapping_of(const std::string& qif_type) {
    const std::string s = detail::lower(detail::trim(qif_type));
    if (s == "bank")    return AccountMapping{types::AccountType::Bank,       "Assets"};
    if (s == "cash")    return AccountMapping{types::AccountType::Cash,       "Assets"};
    if (s == "ccard")   return AccountMapping{types::AccountType::CreditCard, "Liabilities"};
    if (s == "oth a")   return AccountMapping{types::AccountType::Asset,      "Assets"};
    if (s == "oth l")   return AccountMapping{types::AccountType::Liability,  "Liabilities"};
    if (s == "invst")   return AccountMapping{types::AccountType::Investment, "Assets"};
    if (s == "port")    return AccountMapping{types::AccountType::Investment, "Assets"};
    if (s == "invoice") return AccountMapping{types::AccountType::Asset,      "Assets"};
    if (s == "bill")    return AccountMapping{types::AccountType::Liability,  "Liabilities"};
    if (s == "tax")     return AccountMapping{types::AccountType::Asset,      "Assets"};
    return std::nullopt;
}

// A colon in a QIF account name would split it into two levels of the tree, so
// it is replaced and the change is reported.
inline std::string account_path_of(const std::string& qif_type, const std::string& name) {
    const auto mapping = account_mapping_of(qif_type);
    std::string safe = detail::trim(name);
    for (char& c : safe)
        if (c == ':') c = '-';
    return (mapping ? mapping->root : std::string("Assets")) + ":" + safe;
}

// The Q field of a stock split is the number of new shares for each old one,
// times ten. Confirmed by the one split in the business file: iShares US
// Transportation on 7 March 2024, a four-for-one, with Q of 40.
inline double split_ratio_of(double q_field) { return q_field / 10.0; }

// A split changes the share count and the price and leaves the value alone, so
// there is nothing to post.
inline double shares_after_split(double held, double ratio) { return held * ratio; }
inline double price_after_split(double price, double ratio) {
    return ratio == 0.0 ? 0.0 : price / ratio;
}

// The literal text Quicken puts in L on a split record. Read as a category name
// it would create an account called --Split-- with a thousand transactions in it.
inline const char* kSplitMarker = "--Split--";

// The rule is not "ignore L when it says --Split--" but "ignore L whenever there
// are split lines", which covers both: 1,026 business records carry the marker,
// and 19 more carry a real category alongside their splits.
inline bool categories_come_from_split_lines(bool has_split_lines) {
    return has_split_lines;
}

// Brackets in L mean an account rather than a category.
inline bool is_transfer_category(const std::string& l_field) {
    const std::string s = detail::trim(l_field);
    return s.size() >= 2 && s.front() == '[' && s.back() == ']';
}

inline std::string transfer_account_of(const std::string& l_field) {
    const std::string s = detail::trim(l_field);
    return is_transfer_category(s) ? s.substr(1, s.size() - 2) : std::string();
}

// A field that will not parse as a date is a continuation of the field before
// it: QIF has no way to quote a value, so a value containing a newline becomes a
// line beginning with whatever follows it. Used only to tell the two apart.
inline bool looks_like_a_date(const std::string& value) {
    const std::string s = detail::trim(value);
    if (s.size() < 3) return false;
    std::size_t digits = 0;
    bool separator = false;
    for (char c : s) {
        if (c >= '0' && c <= '9') { ++digits; continue; }
        if (c == '/' || c == '\'' || c == '-' || c == ' ') { separator = true; continue; }
        return false;   // a letter means prose
    }
    return separator && digits >= 3;
}

}  // namespace qif
