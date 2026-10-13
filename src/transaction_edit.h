#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
#include "account_type.h"
#include "chart.h"
#include "ledger.h"
#include "money.h"
#include "text_types.h"

// Changing a transaction that is already in the book.
//
// Nothing here touches an amount. A category says where money went, so
// assigning one cannot change how much of it there was -- which is why a report
// total does not move when a month is categorised, and why a total that does
// move means a posting has been lost. See the working-the-month section of
// WorkedExample.spectable and the category scenarios in Transactions.spectable.
namespace edit {

struct Outcome {
    bool changed = false;
    std::string reason;
};

// Repoints the side named by Account at another. Naming which side is what
// makes this usable on a split, where there is more than one to choose
// between.
//
// The account moved to may be a real one, and then the transaction has become
// a transfer rather than an expense -- which is the whole of what makes a card
// payment a transfer, and it needs no special case here.
inline Outcome change_category(const chart::Chart& accounts, ledger::Transaction* t,
                               std::vector<ledger::TransactionSplit>* splits,
                               const std::string& from, const std::string& to) {
    if (t == nullptr) return {false, "no transaction"};
    if (to.empty()) return {false, "a category is required"};
    const chart::Ref from_ref = accounts.ref_for_path(from);
    const chart::Ref to_ref = accounts.ref_for_path(to);
    if (t->source_account == from_ref) {
        t->source_account = to_ref;
        return {true, std::string()};
    }
    if (!chart::is_split(t->destination)) {
        if (t->destination != from_ref) return {false, "nothing is posted to " + from};
        t->destination = to_ref;
        return {true, std::string()};
    }
    if (splits != nullptr)
        for (ledger::TransactionSplit& s : *splits) {
            if (s.transaction_id.value() != t->id.value() || s.destination != from_ref)
                continue;
            s.destination = to_ref;
            return {true, std::string()};
        }
    return {false, "nothing is posted to " + from};
}

// The same, for the flat ledger a lower-level test reads balances from rather
// than the full transactions a window keeps.
inline ledger::Ledger move_postings(const ledger::Ledger& book,
                                    const std::string& from, const std::string& to) {
    ledger::Ledger out;
    for (ledger::DatedPosting p : book.postings()) {
        if (chart::Chart::is_descendant_or_self(p.account.value(), from))
            p.account = types::AccountPath(to + p.account.value().substr(from.size()));
        out.add(p);
    }
    return out;
}

struct AmountOutcome {
    bool changed = false;
    std::string reason;    // refusal; changed is false
    std::string warning;   // non-blocking; changed is still true
};

inline Money abs_of(const Money& m) { return m.cents() < 0 ? -m : m; }

// Changing the amount typed against one posting of a transaction already in
// the book. With exactly two postings the other side is unambiguous and is
// moved to match, so the transaction balances without asking anything. With
// three or more there is no one other side to adjust, so this never guesses:
// either what is left over already sums to the new amount and nothing but the
// named posting changes, or it does not and the change is refused with the
// shortfall, same as a split entry that does not add up. See the changing
// section of Transactions.spectable.
inline AmountOutcome change_amount(const chart::Chart& accounts,
                                   std::vector<ledger::Transaction>* transactions,
                                   std::vector<ledger::TransactionSplit>* splits,
                                   const std::string& ref, const std::string& account,
                                   const Money& new_signed_amount) {
    AmountOutcome out;
    ledger::Transaction* t = nullptr;
    for (ledger::Transaction& one : *transactions)
        if (one.ref.value() == ref) { t = &one; break; }
    if (t == nullptr) { out.reason = "no transaction named " + ref; return out; }

    const chart::Ref account_ref = accounts.ref_for_path(account);
    const bool split = chart::is_split(t->destination);
    const Money needed = -new_signed_amount;

    if (t->source_account == account_ref) {
        const bool reconciled = t->cleared == types::ClearedStatus::Reconciled;
        if (split) {
            Money other_sum;
            for (ledger::TransactionSplit& s : *splits)
                if (s.transaction_id.value() == t->id.value()) other_sum += s.amount;
            if (other_sum.cents() != needed.cents()) {
                const Money shortfall = abs_of(needed - other_sum);
                out.reason = ref + " is a split; the split is " + shortfall.in_register() +
                             " short of " + abs_of(new_signed_amount).in_register();
                return out;
            }
        }
        if (reconciled)
            out.warning = "This posting is reconciled; changing it will alter a "
                          "reconciled balance";
        t->amount = new_signed_amount;
        out.changed = true;
        return out;
    }

    if (!split) {
        if (t->destination != account_ref) {
            out.reason = "nothing is posted to " + account;
            return out;
        }
        if (t->cleared == types::ClearedStatus::Reconciled)
            out.warning = "This posting is reconciled; changing it will alter a "
                          "reconciled balance";
        t->amount = needed;
        out.changed = true;
        return out;
    }

    ledger::TransactionSplit* named = nullptr;
    Money other_sum = t->amount;
    for (ledger::TransactionSplit& s : *splits) {
        if (s.transaction_id.value() != t->id.value()) continue;
        if (s.destination == account_ref) { named = &s; continue; }
        other_sum += s.amount;
    }
    if (named == nullptr) { out.reason = "nothing is posted to " + account; return out; }
    if (other_sum.cents() != needed.cents()) {
        const Money shortfall = abs_of(needed - other_sum);
        out.reason = ref + " is a split; the split is " + shortfall.in_register() +
                     " short of " + abs_of(new_signed_amount).in_register();
        return out;
    }
    named->amount = new_signed_amount;
    out.changed = true;
    return out;
}

// Removes a transaction from the book entirely: both of its sides, never one
// alone, because deleting one side would leave the book out of balance. Its
// split lines, if it has any, go with it -- a line naming a deleted
// transaction is not a line of anything. See the deleting section of
// Transactions.spectable.
inline Outcome delete_transaction(std::vector<ledger::Transaction>* transactions,
                                  const std::string& ref,
                                  std::vector<ledger::TransactionSplit>* splits = nullptr) {
    if (transactions == nullptr) return {false, "no book"};
    const auto it = std::find_if(
        transactions->begin(), transactions->end(),
        [&](const ledger::Transaction& t) { return t.ref.value() == ref; });
    if (it == transactions->end()) return {false, "no transaction named " + ref};
    const types::TransactionId id = it->id;
    transactions->erase(it);
    if (splits != nullptr)
        splits->erase(std::remove_if(splits->begin(), splits->end(),
                                     [&](const ledger::TransactionSplit& s) {
                                         return s.transaction_id.value() == id.value();
                                     }),
                      splits->end());
    return {true, std::string()};
}

// The same where the posting to move is not named: the one category posting of
// the transaction, which is what an import leaves in Uncategorized.
//
// A transaction with several category postings is a split, and it is refused
// rather than guessed at: moving the wrong one would be silent and wrong, and
// the postings still sum to zero afterwards either way, so nothing would say so.
inline Outcome assign_category(ledger::Transaction* t, const chart::Chart& accounts,
                               const std::string& to) {
    if (t == nullptr) return {false, "no transaction"};
    if (to.empty()) return {false, "a category is required"};
    if (chart::is_split(t->destination))
        return {false, "it is a split; name the posting to change"};
    t->destination = accounts.ref_for_path(to);
    return {true, std::string()};
}

}  // namespace edit
