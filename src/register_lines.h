#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "posting.h"
#include "register_view.h"
#include "text_types.h"

// Building the lines of a register from the transactions of a book. One line per
// posting to the account the register is open on, with the other side of the
// transaction summarised in the category column. See TransactionRegister.spectable.
namespace reg {

struct Line {
    std::string ref;
    types::Date date;
    std::string check_no;
    std::string payee;
    std::string category;   // the other account, or --Split-- where there is no one other
    // The whole of the other side, for a reader who wants it: the full path
    // where there is one other posting, and every category with its amount
    // where the transaction is a split. --Split-- in a column says nothing on
    // its own, and this is what it stands for.
    std::string category_detail;
    std::string memo;
    Money payment;          // money out of this account
    Money deposit;          // money into it
    types::ClearedStatus cleared = types::ClearedStatus::Uncleared;
    Money balance;          // running, with the display sign of the account
};

// How an account reads when it is the other side of a line rather than the
// account the register is open on. A real account -- the other side of a
// transfer -- is shown in brackets with its root segment dropped, the way
// Quicken shows one: [Checking], not Assets:Checking. The group heading on
// the account list already says what kind of account it is; the root segment
// is left over from before that heading existed. A category keeps its full
// path here, unchanged -- that convention is its own separate piece of work,
// not done in this pass.
namespace detail {

inline std::string without_legacy_root(const std::string& path) {
    for (const char* root : {"Assets:", "Liabilities:", "Income:", "Expenses:", "Equity:"})
        if (path.rfind(root, 0) == 0) return path.substr(std::string(root).size());
    return path;
}

inline std::string displayed(const chart::Chart& accounts, const std::string& path) {
    const chart::Account* a = accounts.find(path);
    if (a != nullptr && types::class_of(a->type) == types::AccountClass::Real)
        return "[" + without_legacy_root(path) + "]";
    return path;
}

}  // namespace detail

// Selecting a placeholder shows everything beneath it: a placeholder takes no
// postings of its own, so a register of only its own would be empty.
inline std::vector<Line> lines_for(
        const chart::Chart& accounts,
        const std::vector<ledger::Transaction>& transactions,
        const std::string& account) {
    const chart::Account* a = accounts.find(account);
    const int sign = a != nullptr ? types::display_sign(a->type) : 1;

    // Date, then the order the transactions were entered. Entry order is stable:
    // importing more never reshuffles lines already there, so the running balance
    // against a line does not change under the reader.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < transactions.size(); ++i) order.push_back(i);
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t x, std::size_t y) {
                         return transactions[x].date < transactions[y].date;
                     });

    std::vector<Line> out;
    Money running;
    for (std::size_t index : order) {
        const ledger::Transaction& t = transactions[index];
        // Every posting of this transaction that lands in the register.
        for (const ledger::Posting& p : t.postings) {
            if (!chart::Chart::is_descendant_or_self(p.account.value(), account)) continue;
            Line line;
            line.ref = t.ref.value();
            line.date = t.date;
            line.check_no = t.check_no.value();
            line.payee = t.payee.value();
            line.memo = p.memo.empty() ? t.memo : p.memo;
            line.cleared = p.cleared;
            if (p.amount.cents() < 0) line.payment = -p.amount;
            else line.deposit = p.amount;

            // The other side: the one other account where there is exactly one,
            // and otherwise that there are several.
            std::vector<std::string> others;
            for (const ledger::Posting& q : t.postings)
                if (&q != &p) others.push_back(q.account.value());
            // The other side, as this register sees it. One other posting is
            // simply that account. Where there are several, what to name depends
            // on where you are standing: from the bank, a split across three
            // categories is --Split--, because no one of them is the other side.
            // From inside one of those categories, the other side is the bank --
            // the rest of the split is not that category's business.
            line.category = std::string("--Split--");
            if (others.size() == 1) {
                line.category = detail::displayed(accounts, others.front());
            } else if (!others.empty()) {
                std::vector<std::string> real;
                for (const std::string& other : others) {
                    const chart::Account* b = accounts.find(other);
                    if (b != nullptr && types::class_of(b->type) == types::AccountClass::Real)
                        real.push_back(other);
                }
                if (real.size() == 1) line.category = detail::displayed(accounts, real.front());
            }
            if (others.empty()) line.category.clear();

            // The literal path, for a reader -- or the dialog that asks for a
            // new category -- who wants the account rather than its display
            // text. Only meaningful where there is exactly one other posting;
            // see the comment on the split branch below.
            if (others.size() == 1) {
                line.category_detail = others.front();
            } else {
                for (const ledger::Posting& q : t.postings) {
                    if (&q == &p) continue;
                    if (!line.category_detail.empty()) line.category_detail += '\n';
                    line.category_detail += q.account.value() + "   " +
                                            q.amount.in_register();
                    if (!q.memo.empty()) line.category_detail += "   " + q.memo;
                }
            }

            running += p.amount;
            line.balance = sign < 0 ? -running : running;
            out.push_back(line);
        }
    }
    return out;
}

// --- what the register shows, beyond the lines themselves -------------------
//
// Each of these takes the lines and gives back lines. None of them knows about a
// window, which is what lets the whole of the register's behaviour be specified
// and tested without one: a widget decides where the cursor is, and these decide
// what there is to look at.

// Sorting by something other than date. Allowed, because sorting by payee is how
// a person finds something -- but the running balance goes away, because a
// running balance down a list that is not in date order is a column of
// meaningless numbers. Emptying it is honest; filling it in is not.
struct Sorted {
    std::vector<Line> lines;
    bool balance_shown = true;
    std::string reason;
};

inline Sorted sorted_by(const std::vector<Line>& lines, const std::string& column) {
    Sorted out;
    out.lines = lines;
    if (column == "Date" || column.empty()) return out;

    out.balance_shown = false;
    out.reason = "The register is not in date order";
    const auto key = [&](const Line& line) -> std::string {
        if (column == "Payee")    return line.payee;
        if (column == "Category") return line.category;
        if (column == "Memo")     return line.memo;
        if (column == "CheckNo")  return line.check_no;
        return line.payee;
    };
    // Stable, so lines that sort the same keep the order they were in and the
    // list does not reshuffle itself every time it is sorted.
    std::stable_sort(out.lines.begin(), out.lines.end(),
                     [&](const Line& x, const Line& y) { return key(x) < key(y); });
    for (Line& line : out.lines) line.balance = Money();
    return out;
}

namespace detail {

inline std::string fold(const std::string& text) {
    std::string out;
    for (const char c : text)
        out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return out;
}

inline bool holds(const std::string& haystack, const std::string& needle) {
    return fold(haystack).find(fold(needle)) != std::string::npos;
}

}  // namespace detail

// Searching the register. Matches the payee, the memo, the category and either
// amount column, because a person searching a register is looking for a
// transaction and does not want to say which field it is in.
//
// An amount matches either column: 45.00 finds a payment of 45.00 and a deposit
// of 45.00, since a person who remembers the figure does not remember its sign.
inline std::vector<Line> matching(const std::vector<Line>& lines,
                                  const std::string& term) {
    std::vector<Line> out;
    if (term.empty()) return lines;

    bool is_amount = false;
    Money wanted;
    try {
        wanted = Money(term);
        is_amount = true;
    } catch (const std::exception&) {
        is_amount = false;
    }

    for (const Line& line : lines) {
        bool found = detail::holds(line.payee, term) || detail::holds(line.memo, term) ||
                     detail::holds(line.category, term);
        if (!found && is_amount)
            found = line.payment == wanted || line.deposit == wanted;
        if (found) out.push_back(line);
    }
    return out;
}

// Narrowing the register to a range of dates. The balance against each line is
// the balance of the account, not of the range -- a filter changes what is shown
// and not what the account holds, so the figures still agree with a statement.
struct Filtered {
    std::vector<Line> lines;
    // What the account stood at the day before the range began, shown above the
    // first line so the running balance has somewhere to start from.
    types::Date opening_as_at{2024, 1, 1};
    Money opening;
};

inline Filtered between(const std::vector<Line>& lines, const types::Date& from,
                        const types::Date& to) {
    Filtered out;
    out.opening_as_at = types::Date::from_day_number(types::Date::day_number(from) - 1);
    for (const Line& line : lines) {
        if (line.date < from) {
            // Everything before the range is what the range opens with.
            out.opening = line.balance;
            continue;
        }
        if (to < line.date) continue;
        out.lines.push_back(line);
    }
    return out;
}

// One line of a split, as the expansion beneath it shows them.
struct SplitLine {
    std::string category;
    Money amount;
    std::string memo;
};

// Expanding a split line: the other side, one line each. A split shows as
// --Split-- in the category column, which says nothing on its own; this is what
// it stands for.
inline std::vector<SplitLine> split_lines_of(
        const std::vector<ledger::Transaction>& transactions,
        const std::string& ref, const std::string& account) {
    std::vector<SplitLine> out;
    for (const ledger::Transaction& t : transactions) {
        if (t.ref.value() != ref) continue;
        for (const ledger::Posting& p : t.postings) {
            if (chart::Chart::is_descendant_or_self(p.account.value(), account)) continue;
            SplitLine line;
            line.category = p.account.value();
            line.amount = p.amount;
            line.memo = p.memo;
            out.push_back(line);
        }
    }
    return out;
}

// Where today falls in the register, and what is below it. A transaction dated
// after today is real and is kept, but it has not happened: showing it above the
// line would make the balance read as money the account has, which it has not.
struct TodayMarker {
    std::string after_ref;      // the last line on or before today
    Money balance_today;
    int future_count = 0;
    Money future_total;
};

inline TodayMarker marker_for(const std::vector<Line>& lines, const types::Date& today) {
    TodayMarker out;
    for (const Line& line : lines) {
        if (!(today < line.date)) {
            out.after_ref = line.ref;
            out.balance_today = line.balance;
            continue;
        }
        ++out.future_count;
        out.future_total += line.deposit - line.payment;
    }
    return out;
}

// What a register opens on: the end. A register of twenty years opened at 2005
// is a register nobody can use without scrolling for a minute, and the line a
// person wants is the one they are about to type on.
struct Opening {
    types::Date scrolled_to{2024, 1, 1};   // the date at the top of the view
    bool selected_is_last = true;          // the cursor sits on the blank line
    int lines_loaded = 0;
};

inline Opening opens_at(const types::Date& last_date, int line_count) {
    Opening out;
    out.scrolled_to = last_date;
    out.lines_loaded = line_count;
    return out;
}

}  // namespace reg
