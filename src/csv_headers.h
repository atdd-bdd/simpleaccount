#pragma once
#include <set>
#include <map>
#include <optional>
#include <string>
#include "money.h"

// Matching the column headings of a bank CSV file to the one fixed set of fields
// the rest of the import speaks. Every bank names its columns differently, so the
// headings are normalised and looked up; what the user corrects is saved as a
// profile. See ImportCsv.spectable.
namespace csv {

// The fields an imported row can supply. Nothing outside this set is read.
enum class Field {
    Date, Payee, Amount, Debit, Credit, Memo, CheckNo, Category, Balance, Ignore
};

inline std::string to_string(Field f) {
    switch (f) {
        case Field::Date:     return "Date";
        case Field::Payee:    return "Payee";
        case Field::Amount:   return "Amount";
        case Field::Debit:    return "Debit";
        case Field::Credit:   return "Credit";
        case Field::Memo:     return "Memo";
        case Field::CheckNo:  return "CheckNo";
        case Field::Category: return "Category";
        case Field::Balance:  return "Balance";
        case Field::Ignore:   return "Ignore";
    }
    return "Ignore";
}

inline Field field_from_string(const std::string& name) {
    if (name == "Date")     return Field::Date;
    if (name == "Payee")    return Field::Payee;
    if (name == "Amount")   return Field::Amount;
    if (name == "Debit")    return Field::Debit;
    if (name == "Credit")   return Field::Credit;
    if (name == "Memo")     return Field::Memo;
    if (name == "CheckNo")  return Field::CheckNo;
    if (name == "Category") return Field::Category;
    if (name == "Balance")  return Field::Balance;
    return Field::Ignore;
}

enum class AmountStyle { Signed, DebitCredit, SignedPaired };

inline std::string to_string(AmountStyle s) {
    switch (s) {
        case AmountStyle::Signed:       return "Signed";
        case AmountStyle::DebitCredit:  return "DebitCredit";
        case AmountStyle::SignedPaired: return "SignedPaired";
    }
    return "Signed";
}

inline AmountStyle amount_style_from_string(const std::string& s) {
    if (s == "DebitCredit")  return AmountStyle::DebitCredit;
    if (s == "SignedPaired") return AmountStyle::SignedPaired;
    return AmountStyle::Signed;
}

// What money leaving the account looks like in the file. A statement written from
// the bank's point of view has the signs the other way round from one written for
// the customer, and both exist.
enum class OutwardSign { Negative, Positive };

inline std::string to_string(OutwardSign s) {
    return s == OutwardSign::Positive ? "Positive" : "Negative";
}

inline OutwardSign outward_sign_from_string(const std::string& s) {
    return s == "Positive" ? OutwardSign::Positive : OutwardSign::Negative;
}

// Case, spaces and punctuation differ between exports of the same bank, so they
// are removed before comparing. What is left is letters and digits.
inline std::string normalise_heading(const std::string& heading) {
    std::string out;
    out.reserve(heading.size());
    for (char c : heading) {
        if (c >= 'A' && c <= 'Z') out += static_cast<char>(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
    }
    return out;
}

// The alias table: known spellings of each field, in normalised form. It is data,
// not code -- a spelling added here is the whole of supporting another bank that
// happens to use it.
inline const std::map<std::string, Field>& heading_aliases() {
    static const std::map<std::string, Field> table = {
        {"date", Field::Date}, {"transactiondate", Field::Date},
        {"posteddate", Field::Date}, {"postdate", Field::Date},
        {"postingdate", Field::Date}, {"transdate", Field::Date},
        {"effectivedate", Field::Date},

        {"payee", Field::Payee}, {"description", Field::Payee},
        {"name", Field::Payee}, {"merchant", Field::Payee},
        {"merchantname", Field::Payee}, {"transactiondescription", Field::Payee},
        {"originaldescription", Field::Payee},

        {"amount", Field::Amount}, {"transactionamount", Field::Amount},
        {"amountusd", Field::Amount}, {"netamount", Field::Amount},

        {"debit", Field::Debit}, {"debitamount", Field::Debit},
        {"withdrawal", Field::Debit}, {"withdrawals", Field::Debit},
        {"withdrawalamount", Field::Debit}, {"amountdebit", Field::Debit},
        {"paidout", Field::Debit},

        {"credit", Field::Credit}, {"creditamount", Field::Credit},
        {"deposit", Field::Credit}, {"deposits", Field::Credit},
        {"depositamount", Field::Credit}, {"amountcredit", Field::Credit},
        {"paidin", Field::Credit},

        {"memo", Field::Memo}, {"notes", Field::Memo}, {"note", Field::Memo},
        {"extendeddescription", Field::Memo}, {"additionalinfo", Field::Memo},

        {"check", Field::CheckNo}, {"checknumber", Field::CheckNo},
        {"checkno", Field::CheckNo}, {"serialnumber", Field::CheckNo},
        {"referencenumber", Field::CheckNo},

        {"category", Field::Category},

        {"balance", Field::Balance}, {"runningbalance", Field::Balance},
        {"ledgerbalance", Field::Balance}, {"runningbal", Field::Balance},

        {"type", Field::Ignore}, {"transactiontype", Field::Ignore},
        {"status", Field::Ignore}, {"currency", Field::Ignore},
        {"accountnumber", Field::Ignore}, {"accountname", Field::Ignore},
    };
    return table;
}

// Nothing matched is not an error by itself: the column is simply not read.
inline std::optional<Field> field_for_heading(const std::string& heading) {
    const std::string normalised = normalise_heading(heading);
    const auto& table = heading_aliases();
    const auto at = table.find(normalised);
    if (at == table.end()) return std::nullopt;
    return at->second;
}

// The style follows from which amount fields matched; the user does not choose
// it, because the headings already said. Two columns win over one.
struct StyleChoice {
    AmountStyle style = AmountStyle::Signed;
    bool valid = false;
};

inline StyleChoice style_for(bool amount_matched, bool debit_matched, bool credit_matched) {
    if (debit_matched && credit_matched) return {AmountStyle::DebitCredit, true};
    if (amount_matched) return {AmountStyle::Signed, true};
    // A debit without a credit, or neither, leaves no way to read a row.
    return {AmountStyle::Signed, false};
}

// How a row becomes one signed amount.
inline Money amount_of_row(AmountStyle style, OutwardSign outward,
                           const std::string& amount_cell,
                           const std::string& debit_cell,
                           const std::string& credit_cell) {
    const auto money_or_zero = [](const std::string& cell) {
        return cell.empty() ? Money() : Money(cell);
    };
    switch (style) {
        case AmountStyle::Signed: {
            const Money m = money_or_zero(amount_cell);
            // The profile says which way the file signs money leaving.
            return outward == OutwardSign::Positive ? -m : m;
        }
        case AmountStyle::DebitCredit: {
            // The column decides the direction, not the sign of the number: a
            // debit is money out however it is written.
            if (!debit_cell.empty()) {
                const Money d = Money(debit_cell);
                return d.cents() > 0 ? -d : d;
            }
            if (!credit_cell.empty()) {
                const Money c = Money(credit_cell);
                return c.cents() < 0 ? -c : c;
            }
            return Money();
        }
        case AmountStyle::SignedPaired:
            // Both already signed; they are added.
            return money_or_zero(debit_cell) + money_or_zero(credit_cell);
    }
    return Money();
}

}  // namespace csv
