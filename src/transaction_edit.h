#pragma once
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

namespace detail {

inline bool is_a_category(const chart::Chart& accounts, const std::string& path) {
    const chart::Account* a = accounts.find(path);
    return a != nullptr && (a->type == types::AccountType::Income ||
                            a->type == types::AccountType::Expense);
}

}  // namespace detail

// Repoints the posting against one account at another. Naming the posting is
// what makes this usable on a split, where there is more than one to choose
// between.
//
// The account moved to may be a real one, and then the transaction has become a
// transfer rather than an expense -- which is the whole of what makes a card
// payment a transfer, and it needs no special case here.
inline Outcome change_category(ledger::Transaction* t, const std::string& from,
                               const std::string& to) {
    if (t == nullptr) return {false, "no transaction"};
    if (to.empty()) return {false, "a category is required"};
    for (ledger::Posting& p : t->postings) {
        if (p.account.value() != from) continue;
        p.account = types::AccountPath(to);
        return {true, std::string()};
    }
    return {false, "nothing is posted to " + from};
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
    std::vector<std::size_t> found;
    for (std::size_t i = 0; i < t->postings.size(); ++i)
        if (detail::is_a_category(accounts, t->postings[i].account.value()))
            found.push_back(i);
    if (found.empty()) return {false, "it has no category posting"};
    if (found.size() > 1)
        return {false, "it is a split; name the posting to change"};
    t->postings[found.front()].account = types::AccountPath(to);
    return {true, std::string()};
}

}  // namespace edit
