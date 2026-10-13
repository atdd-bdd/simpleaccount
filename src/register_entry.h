#pragma once
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "posting.h"
#include "register_lines.h"
#include "transaction_id.h"

// Entering a transaction on the blank line at the end of a register. See the
// entry section of TransactionRegister.spectable.
//
// The blank line is not a transaction and is not in the book. It is a place to
// type, and what decides whether anything is committed is whether an amount was
// typed -- so a payee entered and then thought better of leaves nothing behind.
//
// None of this knows about a window. A register is a list of lines and a blank
// line is the last of them; filling it in is setting fields, leaving it is one
// call. That is what lets the whole of entry be specified and tested without a
// screen, and it is also the right shape: a widget should decide where the cursor
// is, not whether a transaction exists.
namespace reg {

// What has been typed on the blank line so far. Everything optional, because a
// line half filled in is the ordinary state of one.
struct BlankLine {
    types::Date date{2024, 1, 1};
    std::string check_no;
    std::string payee;
    std::string category;
    std::string memo;
    Money payment;      // money out of the account the register is of
    Money deposit;      // money into it
    types::ClearedStatus cleared = types::ClearedStatus::Uncleared;

    // An amount is what makes it a transaction. Not a payee, not a date -- the
    // date is always there, because the line starts dated today.
    bool has_an_amount() const {
        return payment.cents() != 0 || deposit.cents() != 0;
    }
};

// The blank line a register ends with: dated today and otherwise empty.
inline BlankLine blank_line_on(const types::Date& today) {
    BlankLine out;
    out.date = today;
    return out;
}

// What the register shows as money out and money in, taken together as one
// signed amount from the account's own point of view. A payment leaves, so it is
// negative; a deposit arrives, so it is positive. Both filled in is not a line
// anybody meant, and the payment wins only because something has to.
inline Money amount_of(const BlankLine& line) {
    if (line.payment.cents() != 0) return -line.payment;
    return line.deposit;
}

struct Committed {
    bool committed = false;
    std::string reason;          // why not, where it was not
    ledger::Transaction transaction;
    // The category lines, where the transaction's own Destination is Split.
    // Empty otherwise.
    std::vector<ledger::TransactionSplit> splits;
    // A transfer is two rows, one for each account, sharing transaction's own
    // Ref but each its own Id -- so each clears on its own account's
    // statement without the other. Unset where the other side is a category
    // rather than a real account.
    bool is_transfer = false;
    ledger::Transaction other_side;
};

// Leaving the blank line. Committed only where an amount was typed; otherwise
// the line is discarded and the register is as it was.
//
// The other side is the category typed, or Uncategorized chosen by the sign
// where none was. A category that is not in the chart is created, because a
// person typing a new category has said what they want and refusing it would
// mean two operations for one intention.
inline Committed commit(const BlankLine& line, const std::string& account,
                        std::size_t already_in_book, chart::Chart* accounts) {
    Committed out;
    if (!line.has_an_amount()) {
        // Not an error and not reported as one: a line nobody finished is a line
        // nobody meant, and the register simply goes on as before.
        out.reason = "nothing was entered";
        return out;
    }

    const chart::Account* into = accounts->find(account);
    if (into == nullptr) {
        out.reason = "there is no account " + account;
        return out;
    }
    if (into->placeholder) {
        // A placeholder stands over its children and holds nothing itself, so a
        // posting to it would be a posting to a heading.
        out.reason = account + " is a heading, not an account";
        return out;
    }

    const Money amount = amount_of(line);
    std::string other = line.category;
    if (other.empty() || other == "none") {
        other = ledger::uncategorized_for(amount).value();
        accounts->add(types::AccountPath(other),
                      amount.cents() < 0 ? types::AccountType::Expense
                                         : types::AccountType::Income);
    } else if (accounts->find(other) == nullptr) {
        accounts->add(types::AccountPath(other),
                      amount.cents() < 0 ? types::AccountType::Expense
                                         : types::AccountType::Income);
    } else if (accounts->find(other)->placeholder) {
        // The register's own account is already checked above; this is the
        // same rule for the category typed into it, which a placeholder fails
        // for the same reason -- it groups its children and holds nothing
        // itself.
        out.reason = other + " is a placeholder; choose one below it";
        return out;
    }

    ledger::Transaction t;
    t.id = ledger::new_id();
    t.ref = types::TransactionRef("T" + std::to_string(already_in_book + 1));
    t.date = line.date;
    t.payee = types::PayeeName(line.payee);
    t.check_no = types::CheckNumber(line.check_no);
    t.memo = line.memo;
    t.source_account = accounts->ref_for_path(account);
    t.destination = accounts->ref_for_path(other);
    t.amount = amount;
    // Entered by hand, so nothing has cleared it: the bank has not been asked.
    t.cleared = line.cleared;

    out.transaction = t;
    out.committed = true;

    // A transfer is two downloads of one movement of money, entered here as
    // both at once: the other account's own row, naming this one back. Each
    // clears on its own statement, which is why it is not one row with two
    // accounts on it.
    if (accounts->is_real(t.destination)) {
        out.is_transfer = true;
        ledger::Transaction other_t;
        other_t.id = ledger::new_id();
        other_t.ref = t.ref;
        other_t.date = t.date;
        other_t.payee = t.payee;
        other_t.check_no = t.check_no;
        other_t.memo = t.memo;
        other_t.source_account = t.destination;
        other_t.destination = t.source_account;
        other_t.amount = -amount;
        out.other_side = other_t;
    }
    return out;
}

// SplitLine itself -- category, amount, memo -- is register_lines.h's: one
// category line of a split, the same shape whether it is being typed into the
// split window or expanded back out of one already in the book.
inline Money abs_of(const Money& m) { return m.cents() < 0 ? -m : m; }

// Committing the split window: as commit() above, except the other side is
// however many category lines were typed rather than the one Uncategorized
// guess. They are taken exactly as entered -- signed, so an income line and
// an expense line can sit in the same split -- and the whole of what makes it
// a split rather than a guess is that they are never adjusted to fit. If they
// do not sum to the opposite of what the register line says left or arrived,
// nothing is committed: the shortfall is reported and the split window is
// where it gets fixed, not here.
inline Committed commit_split(const BlankLine& line, const std::string& account,
                              const std::vector<SplitLine>& lines,
                              std::size_t already_in_book, chart::Chart* accounts) {
    Committed out;
    if (!line.has_an_amount()) {
        out.reason = "nothing was entered";
        return out;
    }
    const chart::Account* into = accounts->find(account);
    if (into == nullptr) {
        out.reason = "there is no account " + account;
        return out;
    }
    if (into->placeholder) {
        out.reason = account + " is a heading, not an account";
        return out;
    }

    const Money amount = amount_of(line);
    const Money needed = -amount;
    Money allocated;
    for (const SplitLine& l : lines) allocated += l.amount;
    if (allocated.cents() != needed.cents()) {
        const Money shortfall = abs_of(needed - allocated);
        out.reason = "The split is " + shortfall.in_register() + " short of " +
                     abs_of(needed).in_register();
        return out;
    }

    ledger::Transaction t;
    t.id = ledger::new_id();
    t.ref = types::TransactionRef("T" + std::to_string(already_in_book + 1));
    t.date = line.date;
    t.payee = types::PayeeName(line.payee);
    t.check_no = types::CheckNumber(line.check_no);
    t.memo = line.memo;
    t.source_account = accounts->ref_for_path(account);
    t.destination = chart::split_ref();
    t.amount = amount;
    t.cleared = line.cleared;

    for (const SplitLine& l : lines) {
        if (accounts->find(l.category) == nullptr)
            accounts->add(types::AccountPath(l.category),
                          l.amount.cents() < 0 ? types::AccountType::Expense
                                               : types::AccountType::Income);
        ledger::TransactionSplit s;
        s.transaction_id = t.id;
        s.destination = accounts->ref_for_path(l.category);
        s.amount = l.amount;
        s.memo = l.memo;
        out.splits.push_back(s);
    }

    out.transaction = t;
    out.committed = true;
    return out;
}

// One row offered while picking the entry an Unassigned side should join.
struct OfferedEntry {
    std::string ref;
    types::Date date;
    std::string payee;
    Money amount;   // this candidate's own amount in the target account
};

// What the register offers to match an Unassigned side against: every other
// transaction whose SourceAccount is the target account, nearest first to the
// amount that would actually balance -- equal and opposite to the known real
// side of the transaction being assigned, because that is the one meant nine
// times in ten. See the matching section of Transactions.spectable.
inline std::vector<OfferedEntry> entries_offered_for(
        const chart::Chart& accounts,
        const std::vector<ledger::Transaction>& transactions, const std::string& ref,
        const std::string& account) {
    Money target;
    for (const ledger::Transaction& t : transactions) {
        if (t.ref.value() != ref) continue;
        target = -t.amount;
        break;
    }

    const chart::Ref account_ref = accounts.ref_for_path(account);
    std::vector<OfferedEntry> out;
    for (const ledger::Transaction& t : transactions) {
        if (t.ref.value() == ref) continue;
        if (t.source_account != account_ref) continue;
        out.push_back({t.ref.value(), t.date, t.payee.value(), t.amount});
    }
    std::sort(out.begin(), out.end(), [&](const OfferedEntry& a, const OfferedEntry& b) {
        return std::abs((a.amount - target).cents()) < std::abs((b.amount - target).cents());
    });
    return out;
}

}  // namespace reg
