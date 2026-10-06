#pragma once
#include <algorithm>
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
// An identifier a file gave to one posting, and which file gave it. Per posting
// rather than per transaction, because a download describes one side of it: the
// bank knows what left the chequing account and has never heard of the category
// it was spent on.
struct ImportId {
    types::ImportSource source = types::ImportSource::Ofx;
    std::string id;
};

struct Posting {
    types::AccountPath account;
    Money amount;
    std::string memo;
    // Per posting, not per transaction: a transfer leaves one account and
    // arrives in another on two different statements, so it clears twice.
    types::ClearedStatus cleared = types::ClearedStatus::Uncleared;
    // The identifiers this posting has collected, at most one per source. More
    // than one because a posting imported from a CSV and later recognised in a
    // QFX has a name in each, and losing either would make that file import
    // twice over. Empty for anything entered by hand.
    std::vector<ImportId> import_ids;
};

// What this posting is called by that kind of file, if anything. An identifier
// is only an identifier to the file that gave it: against any other source the
// posting counts as having none, and the weaker tests decide.
inline const std::string* id_from(const Posting& p, types::ImportSource source) {
    for (const ImportId& one : p.import_ids)
        if (one.source == source) return &one.id;
    return nullptr;
}

// Recording what a file called it. Stamping the same source twice replaces the
// identifier rather than collecting both, because a posting has one name in any
// one file.
inline void stamp(Posting* p, types::ImportSource source, const std::string& id) {
    if (id.empty()) return;
    for (ImportId& one : p->import_ids) {
        if (one.source != source) continue;
        one.id = id;
        return;
    }
    p->import_ids.push_back(ImportId{source, id});
}

struct Transaction {
    // Given once when the transaction is created and never changed. See
    // transaction_id.h and the naming rule in Transactions.spectable. Empty only
    // on a transaction that has not been created yet -- a candidate being shown
    // in a dialog, say.
    types::TransactionId id;
    // What the register shows and what postings are grouped by in the spec
    // tables. Not the identity: see id.
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

// Removing one side would leave the book out of balance, so a delete is always
// of the whole transaction, from whichever register it was asked for. See the
// delete scenario in Transactions.spectable.
//
// By id, never by ref. A ref is a label and two transactions can carry the same
// one, so a delete by ref would remove whichever happened to come first -- which
// is the quiet version of the bug that made this an id in the first place.
//
// True when something was removed, so a caller can tell a delete from a request
// to delete something that is not there.
// The transactions a label names. More than one is possible -- a ref is a label,
// not an identity -- and a caller that means to act on one of them has to say
// which, so this returns all of them and decides nothing.
inline std::vector<std::string> ids_labelled(
        const std::vector<Transaction>& transactions, const std::string& ref) {
    std::vector<std::string> out;
    for (const Transaction& t : transactions)
        if (t.ref.value() == ref) out.push_back(t.id.value());
    return out;
}

inline bool erase_transaction(std::vector<Transaction>* transactions,
                              const std::string& id) {
    const auto at = std::find_if(transactions->begin(), transactions->end(),
        [&](const Transaction& t) { return t.id.value() == id; });
    if (at == transactions->end()) return false;
    transactions->erase(at);
    return true;
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
