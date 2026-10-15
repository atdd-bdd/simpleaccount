#pragma once
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "register_lines.h"
#include "text_types.h"

// Looking across the book for transactions matching what somebody typed. The
// results are read the way a register reads a line -- under an account, with
// the other side of the transaction beside it -- because that is the shape a
// person recognises. See the Find section of UserInterface.spectable.
namespace ui {

// Which registers a find looks through.
enum class FindScope { CurrentRegister, AllRegisters };

inline std::string to_string(FindScope s) {
    return s == FindScope::CurrentRegister ? "CurrentRegister" : "AllRegisters";
}

inline std::optional<FindScope> find_scope_from(const std::string& text) {
    if (text == "CurrentRegister") return FindScope::CurrentRegister;
    if (text == "AllRegisters") return FindScope::AllRegisters;
    return std::nullopt;
}

// What was typed into the dialog. A field left out is not a filter: no text
// matches every payee and memo, a missing bound is open-ended, and no account
// matches every account. The amounts bound the absolute value, because
// somebody recalling a transaction rarely remembers which side of it they are
// thinking of -- a search from 80 to 100 finds a payment of 89.99 and a
// deposit of 89.99 alike.
struct FindCriteria {
    std::string text;
    std::optional<types::Date> date_from;
    std::optional<types::Date> date_to;
    std::optional<Money> amount_from;
    std::optional<Money> amount_to;
    std::string account;
    FindScope scope = FindScope::CurrentRegister;
};

// One match, named the way a register names the other side of a transfer:
// Account is the side the row is read under and Category is the other side of
// it, with its full path rather than the register's bracketed short form --
// a result list is read away from the account it belongs to and has to say
// which one that is.
struct FindResultRow {
    std::string ref;
    types::Date date;
    std::string payee;
    std::string account;
    std::string category;
    Money amount;
};

namespace detail {

inline std::string folded(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Case is not what somebody is searching on: "harris" is how the name is
// remembered as often as "Harris".
inline bool contains_folded(const std::string& haystack, const std::string& needle) {
    return folded(haystack).find(folded(needle)) != std::string::npos;
}

// Every account a line's transaction touches other than the register it is
// read under. For a split that is each destination, which is why the
// transaction is looked up rather than the line being read on its own: the
// line says --Split-- and nothing more.
inline std::vector<std::string> other_sides(
        const chart::Chart& accounts,
        const std::vector<ledger::Transaction>& transactions,
        const std::vector<ledger::TransactionSplit>& splits,
        const reg::Line& line) {
    if (line.category != "--Split--") {
        if (line.category_detail.empty()) return {};
        return {line.category_detail};
    }
    for (const ledger::Transaction& t : transactions) {
        if (t.id.value() != line.id) continue;
        std::vector<std::string> out;
        out.push_back(accounts.path_for_ref(t.source_account));
        for (const ledger::TransactionSplit& s : ledger::split_lines_of(splits, t.id))
            out.push_back(accounts.path_for_ref(s.destination));
        return out;
    }
    return {};
}

}  // namespace detail

// The registers a find looks through, in path order. A placeholder is left
// out: its register is everything beneath it, so searching it as well as its
// children would find the same transaction twice. Categories are left out
// because a result is read under an account -- see the Find section's note on
// Account and Category.
inline std::vector<std::string> registers_in_scope(const chart::Chart& accounts,
                                                   const FindCriteria& criteria,
                                                   const std::string& current) {
    if (criteria.scope == FindScope::CurrentRegister) {
        if (current.empty()) return {};
        return {current};
    }
    std::vector<std::string> out;
    for (const chart::Account& a : accounts.all()) {
        if (a.placeholder) continue;
        if (types::class_of(a.type) != types::AccountClass::Real) continue;
        out.push_back(a.path.value());
    }
    std::sort(out.begin(), out.end());
    return out;
}

inline std::vector<FindResultRow> find(const chart::Chart& accounts,
                                       const std::vector<ledger::Transaction>& transactions,
                                       const std::vector<ledger::TransactionSplit>& splits,
                                       const FindCriteria& criteria,
                                       const std::string& current) {
    std::vector<FindResultRow> out;
    for (const std::string& account : registers_in_scope(accounts, criteria, current)) {
        for (const reg::Line& line :
             reg::lines_for(accounts, transactions, splits, account)) {
            if (!criteria.text.empty() &&
                !detail::contains_folded(line.payee, criteria.text) &&
                !detail::contains_folded(line.memo, criteria.text))
                continue;
            if (criteria.date_from && line.date < *criteria.date_from) continue;
            if (criteria.date_to && *criteria.date_to < line.date) continue;

            const Money amount = line.deposit - line.payment;
            const Money size = amount.cents() < 0 ? -amount : amount;
            if (criteria.amount_from && size < *criteria.amount_from) continue;
            if (criteria.amount_to && *criteria.amount_to < size) continue;

            if (!criteria.account.empty()) {
                bool named = chart::Chart::is_descendant_or_self(account, criteria.account);
                if (!named)
                    for (const std::string& other :
                         detail::other_sides(accounts, transactions, splits, line))
                        if (chart::Chart::is_descendant_or_self(other, criteria.account))
                            named = true;
                if (!named) continue;
            }

            FindResultRow row;
            row.ref = line.ref;
            row.date = line.date;
            row.payee = line.payee;
            row.account = account;
            row.category = line.category == "--Split--" ? line.category : line.category_detail;
            row.amount = amount;
            out.push_back(row);
        }
    }
    return out;
}

}  // namespace ui
