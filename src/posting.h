#pragma once
#include <string>
#include <vector>
#include "account_type.h"
#include "date.h"
#include "money.h"
#include "text_types.h"

// The transaction and its postings, and the three rules that decide what a
// transaction looks like. See Transactions.spectable.
//
// A split and a transfer are not separate kinds of thing here. A split is a
// transaction with three or more postings; a transfer is one whose postings are
// all to real accounts. Both fall out of the same shape, which is why nothing
// downstream needs a special case for either.
namespace ledger {

// One side of a transaction. Debits are positive and credits are negative, for
// every account type alike; what each type shows on screen is then the display
// sign from Accounts.
struct Posting {
    types::AccountPath account;
    Money amount;
    std::string memo;
    // Per posting, not per transaction: a transfer leaves one account and
    // arrives in another on two different statements, so it clears twice.
    types::ClearedStatus cleared = types::ClearedStatus::Uncleared;
};

struct Transaction {
    types::TransactionRef ref;
    types::Date date;
    types::PayeeName payee;
    types::CheckNumber check_no;
    std::string memo;
    std::string tag;            // a Quicken class, kept on migration
    std::vector<Posting> postings;
};

// The whole of double entry: the amounts of the postings of one transaction sum
// to zero. Checked on every commit, on every import, and when a file is opened,
// because a book that does not balance cannot be trusted to report anything.
inline Money imbalance_of(const std::vector<Money>& amounts) {
    Money total;
    for (const Money& a : amounts) total += a;
    return total;
}

inline bool balances(const std::vector<Money>& amounts) {
    // One posting is never balanced, whatever it says: a transaction needs at
    // least two sides for there to be a transfer of anything.
    if (amounts.size() < 2) return false;
    return imbalance_of(amounts).cents() == 0;
}

// What is typed on one line of a register. Payment and Deposit are its two
// amount columns and normally only one is filled in.
struct RegisterEntry {
    types::AccountPath account;
    types::Date date;
    types::PayeeName payee;
    types::CheckNumber check_no;
    types::AccountPath category;
    Money payment;
    Money deposit;
    std::string memo;
};

// The amount on the register account is the deposit less the payment, and the
// category takes the opposite, so the two sum to zero. Both columns filled in
// means the net, which is the only reading that keeps that true.
inline Money account_amount_of(const Money& payment, const Money& deposit) {
    return deposit - payment;
}

inline Money category_amount_of(const Money& payment, const Money& deposit) {
    return -account_amount_of(payment, deposit);
}

// An import knows one side of a transaction and often not the other. Rather than
// refuse it or leave the book out of balance, the other side goes to an
// Uncategorized account chosen by the sign of the side that is known.
//
// By the sign of THAT posting, never of a running total: Quicken reports the two
// as separate lines, and classifying the net instead leaves both figures wrong
// by the same amount with the net still right -- the shape of error that looks
// like success. Measured against the 2026 report, where it is 104,300.69 one way
// and 91,331.00 the other.
inline types::AccountPath uncategorized_for(const Money& known_amount) {
    return types::AccountPath(known_amount.cents() > 0 ? "Income:Uncategorized"
                                                       : "Expenses:Uncategorized");
}

// A transfer is recognised by its shape rather than recorded as its own kind:
// every posting is to a real account.
inline bool is_transfer(const std::vector<types::AccountType>& types_of_postings) {
    if (types_of_postings.size() < 2) return false;
    for (types::AccountType t : types_of_postings)
        if (types::class_of(t) != types::AccountClass::Real) return false;
    return true;
}

}  // namespace ledger
