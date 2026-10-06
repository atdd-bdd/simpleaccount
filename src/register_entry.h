#pragma once
#include <string>
#include <vector>

#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "posting.h"
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
    }

    ledger::Transaction t;
    t.id = ledger::new_id();
    t.ref = types::TransactionRef("T" + std::to_string(already_in_book + 1));
    t.date = line.date;
    t.payee = types::PayeeName(line.payee);
    t.check_no = types::CheckNumber(line.check_no);
    t.memo = line.memo;

    ledger::Posting here;
    here.account = types::AccountPath(account);
    here.amount = amount;
    // Entered by hand, so nothing has cleared it: the bank has not been asked.
    here.cleared = line.cleared;
    ledger::Posting there;
    there.account = types::AccountPath(other);
    there.amount = -amount;
    t.postings = {here, there};

    out.transaction = t;
    out.committed = true;
    return out;
}

// Assigning the other account of a transfer by hand, in the register. The one
// posting that said Unassigned is given an account; nothing is added and nothing
// is removed, which is why the book still balances without anything being
// checked. See the assignment scenario in Transactions.spectable.
inline bool assign_other_account(std::vector<ledger::Transaction>* transactions,
                                 const std::string& id, const std::string& account) {
    for (ledger::Transaction& t : *transactions) {
        if (t.id.value() != id) continue;
        for (ledger::Posting& p : t.postings) {
            if (!ledger::is_unassigned(p.account)) continue;
            p.account = types::AccountPath(account);
            return true;
        }
        return false;        // found it, and it had nothing waiting
    }
    return false;
}

// Joining two transactions that describe one movement of money: the half with an
// unassigned side takes the other's real account, and the other goes away.
//
// Used where the two sides were imported too far apart for anything to have
// paired them, and a person has picked the match out of the other register. The
// date and payee kept are the ones already on screen, because the user is
// looking at that row and did not ask for it to change.
inline bool join_as_transfer(std::vector<ledger::Transaction>* transactions,
                             const std::string& id, const std::string& other_id) {
    std::string takes;
    types::ClearedStatus cleared = types::ClearedStatus::Uncleared;
    for (const ledger::Transaction& t : *transactions) {
        if (t.id.value() != other_id) continue;
        for (const ledger::Posting& p : t.postings) {
            if (ledger::is_unassigned(p.account)) continue;
            takes = p.account.value();
            cleared = p.cleared;
            break;
        }
        break;
    }
    if (takes.empty()) return false;
    if (!assign_other_account(transactions, id, takes)) return false;

    // The newly filled posting carries the other side's cleared status, because
    // that side's bank is what reported it.
    for (ledger::Transaction& t : *transactions) {
        if (t.id.value() != id) continue;
        for (ledger::Posting& p : t.postings)
            if (p.account.value() == takes) p.cleared = cleared;
    }
    return ledger::erase_transaction(transactions, other_id);
}

}  // namespace reg
