#pragma once
#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "transaction.h"
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
    // The tag field the transaction carries, shown as one column. A split's
    // parts carry the transaction's tags, so an expansion line shows the same
    // field; see the tag rule in UserInterface.spectable.
    std::string tag;
    Money payment;          // money out of this account
    Money deposit;          // money into it
    types::ClearedStatus cleared = types::ClearedStatus::Uncleared;
    Money balance;          // running, with the display sign of the account

    // Where the line came when the register was built: date order, then the
    // order the transactions were entered in. A sort carries this, so sorting
    // back to Date puts the lines back where they were rather than nearly
    // where they were.
    std::size_t seq = 0;
    // What the balance column said in that order. A sort empties the balance
    // rather than throwing it away, so sorting back to Date shows the same
    // figures again -- not a recomputation over whatever is on screen now,
    // which for a register filtered to a date range would start from the
    // wrong number. See "Sorting back by date brings the running balance
    // back" in TransactionRegister.spectable.
    Money balance_in_date_order;
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

private:
    const chart::Chart* accounts_;
    const std::vector<ledger::Transaction>* transactions_;
    chart::RefIndex refs_;
    // Hashed rather than ordered: an id is thirty characters and nothing here
    // reads the index in id order, so every lookup was a run of string
    // compares for no purpose. See decisions.md.
    std::unordered_map<std::string, std::size_t> where_;
    std::vector<std::vector<const ledger::TransactionSplit*>> parts_;
    std::vector<std::size_t> order_;
};

// The three figures across the top of one register, in the display sign of
// the account, the same as the running balance column.
//
// Balance is every transaction, which is what the money is. Cleared is only
// what is marked cleared, which is what the bank should agree with -- and it is
// the one the downloaded figure is compared against, because comparing the bank
// against Balance would show a difference every time something was in flight,
// which is most of the time, and a warning that is usually on is not read.
//
// Downloaded is held on the account in display sign already, as the statement
// reads it; see the account's downloaded_balance in chart.h. Without one,
// downloaded_at is absent and difference is zero rather than the whole
// balance: there is nothing to differ from.
//
// See the two balances at the top of the register in
// TransactionRegister.spectable.
struct Balances {
    std::string account;
    Money balance;
    Money cleared;
    Money downloaded;
    std::optional<types::Date> downloaded_at;
    Money difference;
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
            line.tag = t.tag;
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
                line.tag = t.tag;
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

        // A pair matched by hand: two rows already in the book that describe
        // one movement of money. Neither was deleted and each names the
        // other's account, so this account's own row already shows the
        // movement and showing the other one here would be the same line
        // twice. See the assignment rule in Transactions.spectable.
        //
        // It is the recorded match that says so, and nothing else. It used to
        // be read off the accounts -- if the account this points at has any
        // transaction pointing back here, hide this one -- and that is wrong
        // about two accounts which pay each other both ways. One repayment
        // from a personal account to the joint one hid all 329 transfers the
        // joint account had made to it, and the register was out by the whole
        // year's funding with every transaction in the book correct. See a
        // transfer is a line in each of the two registers it names, in
        // TransactionRegister.spectable, and findings/register-and-import.md.
        if (!t.matched_to.value().empty()) {
            const std::size_t other = book.position_of(t.matched_to);
            if (other != BookIndex::nowhere &&
                chart::Chart::is_descendant_or_self(
                    index.path_for(transactions[other].source_account), account))
                continue;
        }

        Line line;
        line.ref = t.ref.value();
        line.id = t.id.value();
        line.date = t.date;
        line.check_no = t.check_no.value();
        line.payee = t.payee.value();
        line.memo = t.memo;
        line.tag = t.tag;
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
    // The order they are in here is the order Date means, and the balance is
    // the figure that order adds up to. Recorded in one place at the end
    // rather than at each of the three points a line is made.
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i].seq = i;
        out[i].balance_in_date_order = out[i].balance;
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

inline Balances balances_for(const BookIndex& book, const std::string& account) {
    const chart::Chart& accounts = book.accounts();
    const chart::Account* a = accounts.find(account);
    const int sign = a != nullptr ? types::display_sign(a->type) : 1;

    // The register's own lines, because which transactions belong to this
    // account is a question only lines_for answers: a transfer in is somebody
    // else's transaction and a line here all the same, a matched transfer is a
    // line in one register and not in both, and a split is a line in each
    // category it touches.
    const std::vector<Line> lines = lines_for(book, account);

    Balances out;
    out.account = account;
    // The last line's running balance is the balance, in the display sign
    // already. The cleared figure is added up here, where a line says what it
    // contributed: deposit less payment, which is what the running balance
    // moved by.
    if (!lines.empty()) out.balance = lines.back().balance;
    Money cleared;
    for (const Line& line : lines)
        if (line.cleared == types::ClearedStatus::Cleared)
            cleared += line.deposit - line.payment;
    out.cleared = sign < 0 ? -cleared : cleared;

    if (a != nullptr && a->downloaded_balance_at.has_value()) {
        out.downloaded = a->downloaded_balance;
        out.downloaded_at = a->downloaded_balance_at;
        out.difference = out.cleared - out.downloaded;
    }
    return out;
}

// The three figures for one register, for the same caller.
inline Balances balances_for(const chart::Chart& accounts,
                             const std::vector<ledger::Transaction>& transactions,
                             const std::vector<ledger::TransactionSplit>& splits,
                             const std::string& account) {
    return balances_for(BookIndex(accounts, transactions, splits), account);
}

// --- what the register shows, beyond the lines themselves -------------------
//
// Each of these takes the lines and gives back lines. None of them knows about a
// window, which is what lets the whole of the register's behaviour be specified
// and tested without one: a widget decides where the cursor is, and these decide
// what there is to look at.

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

// A check number as a number, with the blank ones first: 7 is above 100, and a
// line with no number on it is not scattered through the list and not dropped.
// A number nobody can read as one -- ATM, EFT -- sorts with the numbered lines
// and keeps the order it was in, which is as much as can be said for it.
inline std::pair<int, long long> check_key(const Line& line) {
    if (line.check_no.empty()) return {0, 0};
    long long n = 0;
    bool any = false;
    for (const char c : line.check_no)
        if (c >= '0' && c <= '9') { n = n * 10 + (c - '0'); any = true; }
    return {1, any ? n : 0};
}

// What a column sorts on: the thing the column means, rather than the text it
// happens to show. Amount is the one this matters most for -- a register sorted
// on the printed text puts 9.99 after 100.00, and the person who clicked the
// heading was looking for the largest payment. See "What each column sorts on"
// in TransactionRegister.spectable.
inline bool sorts_before(const std::string& column, const Line& x, const Line& y) {
    if (column == "Payee")    return fold(x.payee) < fold(y.payee);
    if (column == "Category") return x.category < y.category;
    if (column == "Memo")     return x.memo < y.memo;
    if (column == "Tag")      return fold(x.tag) < fold(y.tag);
    if (column == "CheckNo")  return check_key(x) < check_key(y);
    if (column == "Amount")
        return x.deposit.cents() - x.payment.cents() < y.deposit.cents() - y.payment.cents();
    if (column == "Cleared")
        return static_cast<int>(x.cleared) < static_cast<int>(y.cleared);
    // Date, and anything a heading does not name: the date, then the order the
    // lines were already in.
    return std::make_pair(x.date, x.seq) < std::make_pair(y.date, y.seq);
}

}  // namespace detail

// Sorting by something other than date. Allowed, because sorting by payee is how
// a person finds something -- but the running balance goes away, because a
// running balance down a list that is not in date order is a column of
// meaningless numbers. Emptying it is honest; filling it in is not.
struct Sorted {
    std::vector<Line> lines;
    bool balance_shown = true;
    std::string reason;
};

// Which way round a sort starts. Every column starts ascending: Date because
// that is the order the register is already in and a first click on Date should
// look like no change rather than a jump to the far end of the file, Amount
// because ascending puts the largest payment first and a person sorting by
// amount is looking for a big one, the rest because A to Z and
// uncleared-first are what the column is for. The second click reverses it,
// which is what every list in every program does; the heading is a toggle.
inline std::string first_direction_for(const std::string& /*column*/) {
    return "Ascending";
}

inline Sorted sorted_by(const std::vector<Line>& lines, const std::string& column,
                        const std::string& direction = "Ascending") {
    Sorted out;
    out.lines = lines;
    const bool descending = direction == "Descending";
    // Stable, so lines the sort cannot tell apart keep the order they were in
    // and clicking a heading never reshuffles what the person is looking at.
    std::stable_sort(out.lines.begin(), out.lines.end(),
                     [&](const Line& x, const Line& y) {
                         return descending ? detail::sorts_before(column, y, x)
                                           : detail::sorts_before(column, x, y);
                     });

    if (column == "Date" || column.empty()) {
        // Back in date order, so the balance column means something again and
        // says what it said before.
        for (Line& line : out.lines) line.balance = line.balance_in_date_order;
        return out;
    }

    out.balance_shown = false;
    out.reason = "The register is not in date order";
    for (Line& line : out.lines) line.balance = Money();
    return out;
}

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
    // Whether there is an opening line at all. A range that begins at the first
    // transaction has nothing before it, and nothing before it is no line
    // rather than a line reading zero.
    bool has_opening = false;
    // What the register says about itself where an empty list would otherwise
    // look like a fault. See the hide-before rule below.
    std::string note;
};

inline Filtered between(const std::vector<Line>& lines, const types::Date& from,
                        const types::Date& to) {
    Filtered out;
    out.opening_as_at = types::Date::from_day_number(types::Date::day_number(from) - 1);
    for (const Line& line : lines) {
        if (line.date < from) {
            // Everything before the range is what the range opens with.
            out.opening = line.balance;
            out.has_opening = true;
            continue;
        }
        if (to < line.date) continue;
        out.lines.push_back(line);
    }
    return out;
}

// --- the ranges a register offers by name -----------------------------------
//
// A range the user picks by name rather than by typing two dates. Each one is
// worked out from today every time it is applied rather than stored, so a book
// left open overnight into January shows the new year the next time the range is
// applied -- and nothing is recomputed behind the user, because the range in
// force is the one they chose. See "The date ranges the register offers by name"
// in TransactionRegister.spectable.
struct DateRange {
    types::Date from{2024, 1, 1};
    types::Date to{2024, 12, 31};
};

// Everything there is, which is what a register with no range chosen shows --
// All dates being a named range too keeps those two the same register. The rule
// writes its To as Today; this runs on past the last line where the register
// holds a transaction dated after today, because the register with no range
// shows those lines and this one is meant to show the same ones.
//
// The lines arrive in date order, which is the order a register is built in, so
// the first of them is the first transaction.
inline DateRange all_dates(const std::vector<Line>& lines, const types::Date& today) {
    DateRange out;
    out.from = lines.empty() ? today : lines.front().date;
    out.to = today;
    for (const Line& line : lines)
        if (out.to < line.date) out.to = line.date;
    return out;
}

inline DateRange named_range(const std::string& name, const types::Date& today,
                             const std::vector<Line>& lines, const std::string& since) {
    DateRange out = all_dates(lines, today);
    // The calendar year, not the fiscal one: the register is a list of what
    // happened and the user reading it means the year on the calendar. The
    // fiscal year start is a reporting decision and does not move this.
    if (name == "This year" || name == "Year to date") {
        out.from = types::Date(today.year(), 1, 1);
        out.to = today;
        return out;
    }
    if (name == "Last year") {
        out.from = types::Date(today.year() - 1, 1, 1);
        out.to = types::Date(today.year() - 1, 12, 31);
        return out;
    }
    if (name == "Past two years") {
        // Two years back from today rather than from January, so the range
        // rolls with today. The clamp is for one day in four years: two years
        // back from the 29th of February is the 28th.
        const int day = std::min(
            today.day(), types::Date::days_in_month(today.year() - 2, today.month()));
        out.from = types::Date(today.year() - 2, today.month(), day);
        out.to = today;
        return out;
    }
    if (name == "Since a date") {
        const auto typed = types::Date::from_iso(since);
        if (typed) out.from = *typed;
        out.to = today;
        return out;
    }
    return out;  // All dates, and any name this does not know
}

// What the register shows, once the book's hide-before date has had its say.
//
// Hidden is not deleted and not left out of the arithmetic: the balance brought
// forward onto the opening line includes every hidden transaction, so the first
// visible line reads the balance it read before the date was set. A range the
// user picks inside the hidden years shows nothing and says why -- that is the
// book's setting winning, which is the point of it being on the book, and an
// empty register with no explanation reads as a defect. See "A book may hide
// everything before a date".
//
// The date arrives as the book stores it: an ISO date, or the word none, or
// nothing at all, which is a book that hides nothing.
inline Filtered in_range(const std::vector<Line>& lines, const DateRange& range,
                         const std::string& hide_before) {
    const auto hidden = types::Date::from_iso(hide_before);
    types::Date from = range.from;
    if (hidden.has_value() && from < *hidden) from = *hidden;
    Filtered out = between(lines, from, range.to);
    if (hidden.has_value() && range.to < *hidden) {
        out.lines.clear();
        out.has_opening = false;
        out.note = "This book hides transactions before " + hidden->iso();
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
