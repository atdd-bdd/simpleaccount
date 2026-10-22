#pragma once
#include <algorithm>
#include <cctype>
#include <map>
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

// Whether the amount typed is the amount wanted or one end of an interval it
// falls in.
enum class AmountMatch { Exact, Range };

inline std::string to_string(AmountMatch m) {
    return m == AmountMatch::Exact ? "Exact" : "Range";
}

inline std::optional<AmountMatch> amount_match_from(const std::string& text) {
    if (text == "Exact") return AmountMatch::Exact;
    if (text == "Range") return AmountMatch::Range;
    return std::nullopt;
}

// The amount fields the dialog shows, which is the one thing about the find
// that the dropdown decides rather than the criteria. On Exact there is one
// field, labelled Amount; on Range there are two, and the first one's label
// changes to say which end it is. Stated here rather than in the window so
// that the words are in one place and can be read back in a table. See "The
// second amount field is only there for a range" in UserInterface.spectable.
struct AmountFields {
    int shown = 1;
    std::string first_label = "Amount";
    std::string second_label;
};

inline AmountFields amount_fields(AmountMatch match) {
    AmountFields out;
    if (match == AmountMatch::Exact) return out;
    out.shown = 2;
    out.first_label = "Amount from";
    out.second_label = "to";
    return out;
}

// What was typed into the dialog. A field left out is not a filter: no text
// matches every payee and memo, a missing date bound is open-ended, no amount
// matches every amount, and no account matches every account.
//
// The amount is exact unless Range was chosen. A pair of bounds was all this
// had, and a pair with the upper one blank is a lower bound -- which is how a
// find for 69.38 on the author's book answered with hundreds of rows, not one
// of them for 69.38. Somebody who remembers an amount remembers the amount.
// A range is still worth having, so the dropdown says which is meant, and an
// open end is a fair answer once Range has been said out loud.
//
// Either way the absolute value is compared, because somebody recalling a
// transaction rarely remembers which side of it they are thinking of -- 89.99
// finds a payment of 89.99 and a deposit of it alike.
struct FindCriteria {
    std::string text;
    std::optional<types::Date> date_from;
    std::optional<types::Date> date_to;
    AmountMatch amount_match = AmountMatch::Exact;
    std::optional<Money> amount;
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
// The transaction a line came from is asked for by id rather than searched
// for: looking it up was a walk over the whole book per result, and a find on
// the author's book has thirty thousand results.
inline std::vector<std::string> other_sides(const reg::BookIndex& book,
                                            const reg::Line& line) {
    if (line.category != "--Split--") {
        if (line.category_detail.empty()) return {};
        return {line.category_detail};
    }
    const std::size_t where = book.position_of(types::TransactionId(line.id));
    if (where == reg::BookIndex::nowhere) return {};
    const ledger::Transaction& t = book.transactions()[where];
    std::vector<std::string> out;
    out.push_back(book.refs().path_for(t.source_account));
    for (const ledger::TransactionSplit* s : book.split_parts(where))
        out.push_back(book.refs().path_for(s->destination));
    return out;
}

// The amount of each line of a split, for comparing against what was typed.
// The number somebody has in front of them is as often the one on the receipt
// as the total the register shows.
inline std::vector<Money> split_amounts(const reg::BookIndex& book,
                                        const reg::Line& line) {
    if (line.category != "--Split--") return {};
    const std::size_t where = book.position_of(types::TransactionId(line.id));
    if (where == reg::BookIndex::nowhere) return {};
    std::vector<Money> out;
    for (const ledger::TransactionSplit* s : book.split_parts(where))
        out.push_back(s->amount);
    return out;
}

// Does what was typed describe this amount? The absolute value is what is
// compared, and a range includes both of its ends.
inline bool amount_matches(const FindCriteria& criteria, const Money& value) {
    const Money size = value.cents() < 0 ? -value : value;
    if (criteria.amount_match == AmountMatch::Exact)
        return !criteria.amount || size == *criteria.amount;
    if (criteria.amount && size < *criteria.amount) return false;
    if (criteria.amount_to && *criteria.amount_to < size) return false;
    return true;
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
    // One index for the whole find. Every register in scope is built from the
    // same book, and building one per register was all but a second in four
    // of what a find cost. See reg::BookIndex.
    const reg::BookIndex book(accounts, transactions, splits);
    for (const std::string& account : registers_in_scope(accounts, criteria, current)) {
        for (const reg::Line& line : reg::lines_for(book, account)) {
            if (!criteria.text.empty() &&
                !detail::contains_folded(line.payee, criteria.text) &&
                !detail::contains_folded(line.memo, criteria.text))
                continue;
            if (criteria.date_from && line.date < *criteria.date_from) continue;
            if (criteria.date_to && *criteria.date_to < line.date) continue;

            const Money amount = line.deposit - line.payment;
            // A split reached by one of its lines is still one result -- the
            // transaction as the register reads it -- because the line is not
            // a thing a register can be scrolled to. Matching is a question
            // asked of the line, so two equal lines answer it once.
            if (criteria.amount || criteria.amount_to) {
                bool sized = detail::amount_matches(criteria, amount);
                if (!sized)
                    for (const Money& part : detail::split_amounts(book, line))
                        if (detail::amount_matches(criteria, part)) {
                            sized = true;
                            break;
                        }
                if (!sized) continue;
            }

            if (!criteria.account.empty()) {
                bool named = chart::Chart::is_descendant_or_self(account, criteria.account);
                if (!named)
                    for (const std::string& other :
                         detail::other_sides(book, line))
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
