#pragma once
#include <optional>
#include <string>
#include <vector>
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "money.h"
#include "register_lines.h"
#include "transaction.h"
#include "transaction_id.h"

// Reconciling an account against what the bank said. See "Reconciling an
// account against what the bank said" in UserInterface.spectable.
//
// Nothing here knows about a window. The window shows these three shapes and
// calls these four functions, which is what lets the whole of reconciling be
// specified without one: the figures, what is not cleared yet, what is left
// over, and the one transaction that accounts for it.
namespace recon {

// The other side of a balance adjustment. Equity rather than an expense,
// because nobody knows what the money was: a spending report that said
// Groceries would be inventing an answer, and the same reasoning put opening
// balances in Equity. See decision 29 in questions.md.
inline const char* kBalanceAdjustments = "Equity:Balance Adjustments";
inline const char* kBalanceAdjustmentPayee = "Balance Adjustment";

// One line of the account that is not cleared, offered to be ticked. The
// amount is signed the way the register shows it -- a payment is negative --
// because a statement is read by its figures.
struct Item {
    std::string ref;
    types::Date date;
    std::string payee;
    Money amount;
};

// What the window says across its top. Cleared is the book's cleared balance
// in the display sign of the account and OnLine is what the last download
// said; without a download there is no online_at and the difference is
// nothing, because there is nothing to differ from.
struct Figures {
    bool shown = false;
    std::string account;
    Money balance;
    Money cleared;
    Money online;
    std::optional<types::Date> online_at;
    Money difference;
    bool agrees = false;
};

// What the window offers to write for a difference nobody could explain.
// Uncleared, as asked for: the register goes on showing the difference and the
// adjustment is the line that says what it was.
struct Adjustment {
    bool shown = false;
    Money amount;
    types::Date date;
    std::string payee;
    std::string category;
    types::ClearedStatus cleared = types::ClearedStatus::Uncleared;
};

inline bool reconcilable(const chart::Chart& accounts, const std::string& account) {
    const chart::Account* a = accounts.find(account);
    return a != nullptr && !a->placeholder &&
           types::class_of(a->type) == types::AccountClass::Real;
}

inline Figures figures_for(const reg::BookIndex& book, const std::string& account,
                           bool quicken_signs = false) {
    const reg::Balances b = reg::balances_for(book, account, quicken_signs);
    Figures out;
    out.shown = true;
    out.account = account;
    out.balance = b.balance;
    out.cleared = b.cleared;
    out.online = b.downloaded;
    out.online_at = b.downloaded_at;
    out.difference = b.difference;
    // Agreeing is a statement to agree with and a difference of nothing. An
    // account no download has reached does not agree: there is no second
    // figure, so the question has no answer rather than the answer yes.
    out.agrees = b.downloaded_at.has_value() && b.difference.cents() == 0;
    return out;
}

inline std::vector<Item> uncleared_in(const reg::BookIndex& book,
                                      const std::string& account) {
    std::vector<Item> out;
    for (const reg::Line& line : reg::lines_for(book, account)) {
        // Reconciled is cleared: a book imported from Quicken is almost all
        // reconciled, and offering twenty years of it to be ticked again is
        // not reconciling.
        if (types::counts_as_cleared(line.cleared)) continue;
        Item item;
        item.ref = line.ref;
        item.date = line.date;
        item.payee = line.payee;
        item.amount = line.deposit - line.payment;
        out.push_back(item);
    }
    return out;
}

// What is left when there is nothing more to tick. The adjustment moves the
// book to the bank, so it is the difference the other way about: a book 50.00
// above the bank takes 50.00 out. Dated the day the balance was stated,
// because that is the day the two figures were compared.
inline Adjustment offer_for(const Figures& f) {
    Adjustment out;
    if (!f.online_at.has_value() || f.difference.cents() == 0) return out;
    out.shown = true;
    out.amount = -f.difference;
    out.date = *f.online_at;
    out.payee = kBalanceAdjustmentPayee;
    out.category = kBalanceAdjustments;
    out.cleared = types::ClearedStatus::Uncleared;
    return out;
}

// The transaction an accepted offer writes. The amount offered is in the
// display sign of the account, which is the sign a reader of the window sees;
// what is stored is the account's own sign, so a credit card -- shown with the
// sign turned over -- is adjusted in the direction the window said.
inline ledger::Transaction balance_adjustment(chart::Chart* accounts,
                                              const std::string& account,
                                              const Adjustment& offer,
                                              const std::string& ref) {
    ledger::Transaction t;
    if (accounts == nullptr || !offer.shown) return t;
    if (accounts->find(kBalanceAdjustments) == nullptr)
        accounts->add(types::AccountPath(kBalanceAdjustments), types::AccountType::Equity);
    const chart::Account* a = accounts->find(account);
    const int sign = a != nullptr ? types::display_sign(a->type) : 1;
    t.id = ledger::new_id();
    t.ref = types::TransactionRef(ref);
    t.date = offer.date;
    t.payee = types::PayeeName(offer.payee);
    t.source_account = accounts->ref_for_path(account);
    t.destination = accounts->ref_for_path(kBalanceAdjustments);
    t.amount = sign < 0 ? -offer.amount : offer.amount;
    t.cleared = offer.cleared;
    return t;
}

}  // namespace recon
