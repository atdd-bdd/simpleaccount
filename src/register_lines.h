#pragma once
#include <algorithm>
#include <map>
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
    // The transaction's own identity, the same string a report's detail line
    // carries for the same transaction -- what lets a click on one page find
    // the right line on the other. See the naming rule in Transactions.
    std::string id;
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
// transactions of its own, so a register of only its own would be empty.
// Everything a register needs that belongs to the book rather than to the
// account being read: a ref's path, a transaction's split lines, which
// accounts a transaction starts from, and the order the transactions are read
// in. Built once and handed to every register that is being built, because
// the work is identical for all of them -- a find over the 111 registers of
// the author's book was 111 sorts of the whole book, which was four of its
// four and a quarter seconds.
//
// Like chart::RefIndex it holds pointers into what it was built from, so it is
// good for one computation only: build it, use it, let it go. An index that
// outlives the book it indexes is how a transaction ends up reported against
// the wrong account.
class BookIndex {
public:
    BookIndex(const chart::Chart& accounts,
              const std::vector<ledger::Transaction>& transactions,
              const std::vector<ledger::TransactionSplit>& splits)
        : accounts_(&accounts), transactions_(&transactions), refs_(accounts) {
        // A transaction's split lines by where the transaction is, not by its
        // id: the register asks for them per transaction per account, and a
        // map keyed by a thirty-character string answers that in string
        // compares. The id map is kept for a caller holding only an id.
        for (std::size_t i = 0; i < transactions.size(); ++i)
            where_[transactions[i].id.value()] = i;
        parts_.assign(transactions.size(), {});
        for (const ledger::TransactionSplit& s : splits) {
            const auto at = where_.find(s.transaction_id.value());
            if (at != where_.end()) parts_[at->second].push_back(&s);
        }
        for (const ledger::Transaction& t : transactions)
            ++starts_from_[t.source_account.value()];
        // Date, then the order the transactions were entered. Entry order is
        // stable: importing more never reshuffles lines already there, so the
        // running balance against a line does not change under the reader.
        for (std::size_t i = 0; i < transactions.size(); ++i) order_.push_back(i);
        std::stable_sort(order_.begin(), order_.end(),
                         [&transactions](std::size_t x, std::size_t y) {
                             return transactions[x].date < transactions[y].date;
                         });
    }

    const chart::Chart& accounts() const { return *accounts_; }
    const chart::RefIndex& refs() const { return refs_; }
    const std::vector<ledger::Transaction>& transactions() const { return *transactions_; }
    const std::vector<std::size_t>& order() const { return order_; }

    // Nothing is at this position, which is what a transaction the index has
    // never heard of answers to.
    static constexpr std::size_t nowhere = static_cast<std::size_t>(-1);

    std::size_t position_of(const types::TransactionId& id) const {
        const auto at = where_.find(id.value());
        return at == where_.end() ? nowhere : at->second;
    }

    const std::vector<const ledger::TransactionSplit*>& split_parts(
            std::size_t position) const {
        static const std::vector<const ledger::TransactionSplit*> none;
        return position < parts_.size() ? parts_[position] : none;
    }

    const std::vector<const ledger::TransactionSplit*>& split_parts(
            const types::TransactionId& id) const {
        return split_parts(position_of(id));
    }

    // How many transactions start from this account, which is what says
    // whether a transfer into it is matched.
    int starts_from(const chart::Ref& account) const {
        const auto at = starts_from_.find(account.value());
        return at == starts_from_.end() ? 0 : at->second;
    }

private:
    const chart::Chart* accounts_;
    const std::vector<ledger::Transaction>* transactions_;
    chart::RefIndex refs_;
    std::map<std::string, std::size_t> where_;
    std::vector<std::vector<const ledger::TransactionSplit*>> parts_;
    std::map<long long, int> starts_from_;
    std::vector<std::size_t> order_;
};

inline std::vector<Line> lines_for(const BookIndex& book, const std::string& account) {
    const chart::Chart& accounts = book.accounts();
    const chart::RefIndex& index = book.refs();
    const std::vector<ledger::Transaction>& transactions = book.transactions();
    const chart::Account* a = accounts.find(account);
    const int sign = a != nullptr ? types::display_sign(a->type) : 1;

    std::vector<Line> out;
    Money running;
    for (std::size_t position : book.order()) {
        const ledger::Transaction& t = transactions[position];
        const std::string& source_path = index.path_for(t.source_account);

        // This account is the one the transaction itself lives in: its own
        // line, named the way the register has always named one.
        if (chart::Chart::is_descendant_or_self(source_path, account)) {
            Line line;
            line.ref = t.ref.value();
            line.id = t.id.value();
            line.date = t.date;
            line.check_no = t.check_no.value();
            line.payee = t.payee.value();
            line.memo = t.memo;
            line.cleared = t.cleared;
            if (t.amount.cents() < 0) line.payment = -t.amount;
            else line.deposit = t.amount;

            if (chart::is_split(t.destination)) {
                line.category = "--Split--";
                for (const ledger::TransactionSplit* s : book.split_parts(position)) {
                    if (!line.category_detail.empty()) line.category_detail += '\n';
                    line.category_detail += index.path_for(s->destination) + "   " +
                                            s->amount.in_register();
                    if (!s->memo.empty()) line.category_detail += "   " + s->memo;
                }
            } else {
                const std::string& other = index.path_for(t.destination);
                line.category = other.empty() ? std::string() : detail::displayed(accounts, other);
                line.category_detail = other;
            }

            running += t.amount;
            line.balance = sign < 0 ? -running : running;
            out.push_back(line);
            continue;
        }
        // Otherwise this account is not where the transaction lives, but a
        // split line may still be its own row here: the other legs of the
        // split are not this account's business, so only the matching line
        // is shown, with the source account named back as its category. See
        // "A split seen from one of its category registers shows only its
        // own line" in TransactionRegister.spectable.
        if (chart::is_split(t.destination)) {
            for (const ledger::TransactionSplit* part : book.split_parts(position)) {
                const ledger::TransactionSplit& s = *part;
                const std::string& s_path = index.path_for(s.destination);
                if (!chart::Chart::is_descendant_or_self(s_path, account)) continue;
                Line line;
                line.ref = t.ref.value();
                line.id = t.id.value();
                line.date = t.date;
                line.check_no = t.check_no.value();
                line.payee = t.payee.value();
                line.memo = s.memo.empty() ? t.memo : s.memo;
                line.cleared = types::ClearedStatus::Uncleared;
                if (s.amount.cents() < 0) line.payment = -s.amount;
                else line.deposit = s.amount;
                line.category = source_path.empty() ? std::string()
                                                     : detail::displayed(accounts, source_path);
                line.category_detail = source_path;

                running += s.amount;
                line.balance = sign < 0 ? -running : running;
                out.push_back(line);
            }
            continue;
        }
        if (t.destination.is_none()) continue;
        const std::string& dest_path = index.path_for(t.destination);
        if (!chart::Chart::is_descendant_or_self(dest_path, account)) continue;

        // A real destination that is itself another transaction's own
        // SourceAccount is a matched transfer; that other transaction already
        // shows this movement from its own side, so showing it again here
        // would be the same line twice. See the two-rows-for-a-transfer rule
        // in Transactions.spectable.
        if (index.is_real(t.destination)) {
            int others = book.starts_from(t.destination);
            // This transaction is counted in that figure if it is the one
            // whose two sides are the same account, and it is not its own
            // other half.
            if (t.source_account == t.destination) others -= 1;
            if (others > 0) continue;
        }

        Line line;
        line.ref = t.ref.value();
        line.id = t.id.value();
        line.date = t.date;
        line.check_no = t.check_no.value();
        line.payee = t.payee.value();
        line.memo = t.memo;
        line.cleared = t.cleared;
        const Money here_amount = -t.amount;
        if (here_amount.cents() < 0) line.payment = -here_amount;
        else line.deposit = here_amount;
        line.category = source_path.empty() ? std::string() : detail::displayed(accounts, source_path);
        line.category_detail = source_path;

        running += here_amount;
        line.balance = sign < 0 ? -running : running;
        out.push_back(line);
    }
    return out;
}

// One register on its own, for a caller with nothing else to build. A caller
// building several -- a find, or a window refreshing every open tab -- should
// make one BookIndex and hand it to each, rather than paying for a new one per
// register.
inline std::vector<Line> lines_for(
        const chart::Chart& accounts,
        const std::vector<ledger::Transaction>& transactions,
        const std::vector<ledger::TransactionSplit>& splits,
        const std::string& account) {
    return lines_for(BookIndex(accounts, transactions, splits), account);
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

// Expanding a split line: its categories, one line each. A split shows as
// --Split-- in the category column, which says nothing on its own; this is
// what it stands for.
inline std::vector<SplitLine> split_lines_of(
        const chart::Chart& accounts,
        const std::vector<ledger::Transaction>& transactions,
        const std::vector<ledger::TransactionSplit>& splits,
        const std::string& ref) {
    std::vector<SplitLine> out;
    for (const ledger::Transaction& t : transactions) {
        if (t.ref.value() != ref) continue;
        for (const ledger::TransactionSplit& s : ledger::split_lines_of(splits, t.id)) {
            SplitLine line;
            line.category = accounts.path_for_ref(s.destination);
            line.amount = s.amount;
            line.memo = s.memo;
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
