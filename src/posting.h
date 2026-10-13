#pragma once
#include <algorithm>
#include <set>
#include <string>
#include <vector>
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "money.h"
#include "text_types.h"

// The transaction and the rules that decide what one looks like. See
// Transactions.spectable.
//
// A split and a transfer are not separate kinds of thing here. A split is a
// transaction whose Destination is Split, with three or more lines in
// TransactionSplit; a transfer is a pair of transactions each naming the
// other as Destination. Both fall out of the same shape, which is why
// nothing downstream needs a special case for either.
namespace ledger {

// An identifier a file gave to a transaction, and which file gave it. At most
// one per source: a transaction has exactly one home account -- the one it
// was downloaded into or typed against -- so there is no second side with an
// identifier of its own the way a second posting once had.
struct ImportId {
    types::ImportSource source = types::ImportSource::Ofx;
    std::string id;
};

struct Transaction {
    // Given once when the transaction is created and never changed. See
    // transaction_id.h and the naming rule in Transactions.spectable. Empty only
    // on a transaction that has not been created yet -- a candidate being shown
    // in a dialog, say.
    types::TransactionId id;
    // What the register shows and what rows are grouped by in the spec
    // tables. Not the identity: see id.
    types::TransactionRef ref;
    types::Date date;
    types::PayeeName payee;
    // What the bank actually sent, kept beside the name a rule tidied it into.
    // A rule written next year has to be able to match what the bank sent this
    // year, so the original text is stored rather than replaced. Empty where
    // nothing renamed it, which is also where the payee is already the raw name.
    std::string raw_name;
    types::CheckNumber check_no;
    std::string memo;
    std::string tag;            // a Quicken class, kept on migration

    // What a transaction is: the account it lives in, the other side, and the
    // signed amount between them. The other side is a plain account or
    // category, or the literal Split sentinel when the parts are in
    // TransactionSplit instead. See the section on naming both accounts
    // directly in Transactions.spectable.
    chart::Ref source_account;
    chart::Ref destination;
    Money amount;
    types::ClearedStatus cleared = types::ClearedStatus::Uncleared;

    // The identifiers this transaction has collected, at most one per source.
    // More than one because a transaction imported from a CSV and later
    // recognised in a QFX has a name in each, and losing either would make
    // that file import twice over. Empty for anything entered by hand.
    std::vector<ImportId> import_ids;
};

// What this transaction is called by that kind of file, if anything. An
// identifier is only an identifier to the file that gave it: against any
// other source the transaction counts as having none, and the weaker tests
// decide.
inline const std::string* id_from(const Transaction& t, types::ImportSource source) {
    for (const ImportId& one : t.import_ids)
        if (one.source == source) return &one.id;
    return nullptr;
}

// Recording what a file called it. Stamping the same source twice replaces
// the identifier rather than collecting both, because a transaction has one
// name in any one file.
inline void stamp(Transaction* t, types::ImportSource source, const std::string& id) {
    if (id.empty()) return;
    for (ImportId& one : t->import_ids) {
        if (one.source != source) continue;
        one.id = id;
        return;
    }
    t->import_ids.push_back(ImportId{source, id});
}

// One category line of a split recorded the new way: kept apart from the
// transaction that carries the total and the account it was entered or
// downloaded against, rather than folded into Postings the way a split is
// today. See the TransactionSplit entity in Transactions.spectable.
struct TransactionSplit {
    types::TransactionId transaction_id;
    chart::Ref destination;
    Money amount;
    std::string memo;
};

// The parts of one split, in the order they were entered.
inline std::vector<TransactionSplit> split_lines_of(
        const std::vector<TransactionSplit>& all, const types::TransactionId& id) {
    std::vector<TransactionSplit> out;
    for (const TransactionSplit& s : all)
        if (s.transaction_id.value() == id.value()) out.push_back(s);
    return out;
}

inline Money split_total(const std::vector<TransactionSplit>& lines) {
    Money total;
    for (const TransactionSplit& s : lines) total += s.amount;
    return total;
}

// One side of a transaction read out generically -- an account and the
// amount against it -- the shape almost everything that walks a book wants,
// whether the other side is a plain Destination or a split's several lines.
// The source is always first, at the transaction's own Amount and Memo.
struct Side {
    chart::Ref account;
    Money amount;
    std::string memo;
};

inline std::vector<Side> sides_of(const Transaction& t,
                                  const std::vector<TransactionSplit>& splits) {
    std::vector<Side> out;
    out.push_back({t.source_account, t.amount, t.memo});
    if (is_split(t.destination)) {
        for (const TransactionSplit& s : split_lines_of(splits, t.id))
            out.push_back({s.destination, s.amount, s.memo});
    } else {
        out.push_back({t.destination, -t.amount, t.memo});
    }
    return out;
}

// The same, for listing every side in the book without a movement appearing
// twice: a real Destination that is itself another transaction's own
// SourceAccount is a matched transfer, already listed from that other
// transaction's side, so it is left out here. See the two-rows-for-a-transfer
// rule in Transactions.spectable and ledger::add_to_book, which this mirrors.
inline std::vector<Side> sides_of_display(const chart::Chart& accounts, const Transaction& t,
                                          const std::vector<Transaction>& transactions,
                                          const std::vector<TransactionSplit>& splits) {
    if (is_split(t.destination) || t.destination.is_none() || !accounts.is_real(t.destination))
        return sides_of(t, splits);
    for (const Transaction& other : transactions)
        if (&other != &t && other.source_account == t.destination &&
            other.destination == t.source_account)
            return {{t.source_account, t.amount, t.memo}};
    return sides_of(t, splits);
}

// The whole of double entry: a split's lines sum to its transaction's amount,
// the one place under this shape where more than one figure has to agree with
// another. Checked on every commit and on every import.
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
// A well-known path rather than a ref: the account this names may not exist
// in the chart yet -- creating it on demand is the caller's next step -- so
// there is nothing yet to resolve an id from.
inline types::AccountPath uncategorized_for(const Money& known_amount) {
    return types::AccountPath(known_amount.cents() > 0 ? "Income:Uncategorized"
                                                       : "Expenses:Uncategorized");
}

// Where the other side of a payment goes until the account is known. A different
// unknown from Uncategorized and so a different account: Uncategorized asks what
// the money was for and may be left for ever, Unassigned asks which account it
// went to and is expected to reach zero. See the two-unknowns rule in
// Transactions.spectable.
//
// A real account rather than a category, because money in transit is an asset
// and because a category would put it in a spending report, where it is not
// spending at all.
inline const char* kUnassigned = "Unassigned";

inline types::AccountPath unassigned() { return types::AccountPath(kUnassigned); }

inline chart::Ref unassigned_ref(const chart::Chart& accounts) {
    return accounts.ref_for_path(kUnassigned);
}

inline bool is_unassigned(const chart::Chart& accounts, const chart::Ref& r) {
    return !r.is_none() && r == unassigned_ref(accounts);
}

// The new-shape equivalent of assigning the other account: the transaction
// itself names the other side, so there is no second posting to edit, only
// this one row's Destination.
inline bool assign_destination(const chart::Chart& accounts,
                               std::vector<Transaction>* transactions,
                               const types::TransactionId& id,
                               const chart::Ref& account) {
    for (Transaction& t : *transactions) {
        if (t.id.value() != id.value()) continue;
        if (!is_unassigned(accounts, t.destination)) return false;
        t.destination = account;
        return true;
    }
    return false;
}

// The new-shape equivalent of matching two entries: each already exists as
// its own row, so joining them is pointing each one's Destination at the
// other's SourceAccount, not merging rows into one.
inline bool match_destinations(std::vector<Transaction>* transactions,
                               const types::TransactionId& a_id,
                               const types::TransactionId& b_id) {
    Transaction* a = nullptr;
    Transaction* b = nullptr;
    for (Transaction& t : *transactions) {
        if (t.id.value() == a_id.value()) a = &t;
        if (t.id.value() == b_id.value()) b = &t;
    }
    if (a == nullptr || b == nullptr) return false;
    a->destination = b->source_account;
    b->destination = a->source_account;
    return true;
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

// How many distinct transactions are already there, for naming the next one
// "T" plus this. Counts refs, not rows: a transfer is two rows sharing one
// ref, and numbering by row count would skip a number for every transfer
// already in the book.
inline std::size_t transaction_count(const std::vector<Transaction>& transactions) {
    std::set<std::string> refs;
    for (const Transaction& t : transactions) refs.insert(t.ref.value());
    return refs.size();
}

inline bool erase_transaction(std::vector<Transaction>* transactions,
                              const std::string& id) {
    const auto at = std::find_if(transactions->begin(), transactions->end(),
        [&](const Transaction& t) { return t.id.value() == id; });
    if (at == transactions->end()) return false;
    transactions->erase(at);
    return true;
}

// Checked on every path that builds a transaction directly rather than
// through the register or the split window, which each enforce their own
// shape as they go: a source and a destination, naming two different
// accounts, neither a placeholder, and -- for a split -- lines that sum to
// the transaction's amount. See the shape-of-a-transaction section of
// Transactions.spectable.
inline chart::Rejection validate(const chart::Chart& accounts, const Transaction& t,
                                 const std::vector<TransactionSplit>& splits = {}) {
    if (t.source_account.is_none() || t.destination.is_none())
        return {true, "A transaction needs at least two postings"};
    const chart::Account* source = accounts.find_account_by_ref(t.source_account);
    if (source != nullptr && source->placeholder)
        return {true, source->path.value() + " is a placeholder; choose one below it"};
    if (is_split(t.destination)) {
        const std::vector<TransactionSplit> lines =
            split_lines_of(splits, t.id);
        std::set<long long> seen{t.source_account.value()};
        for (const TransactionSplit& s : lines) {
            if (!seen.insert(s.destination.value()).second)
                return {true, "A transfer must name two different accounts"};
            const chart::Account* a = accounts.find_account_by_ref(s.destination);
            if (a != nullptr && a->placeholder)
                return {true, a->path.value() + " is a placeholder; choose one below it"};
        }
        const Money off = t.amount - split_total(lines);
        if (off.cents() != 0) {
            const Money magnitude = off.cents() < 0 ? -off : off;
            return {true, "The split is " + magnitude.in_register() + " short of " +
                          t.amount.in_register()};
        }
        return {};
    }
    if (t.source_account == t.destination)
        return {true, "A transfer must name two different accounts"};
    const chart::Account* dest = accounts.find_account_by_ref(t.destination);
    if (dest != nullptr && dest->placeholder)
        return {true, dest->path.value() + " is a placeholder; choose one below it"};
    return {};
}

// A transfer is recognised by its shape rather than recorded as its own kind:
// both the source and the destination are real accounts.
inline bool is_transfer(const chart::Chart& accounts, const Transaction& t) {
    return accounts.is_real(t.source_account) && accounts.is_real(t.destination);
}

}  // namespace ledger
