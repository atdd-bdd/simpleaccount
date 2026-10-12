#pragma once
#include <string>
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "text_types.h"
#include "transaction_id.h"

// Money moving between two books -- a draw from the business is the case that
// makes people want one book. It is recorded as two self-balanced
// transactions, one in each book, both touching Equity rather than a shared
// transfer: nothing links the two books, so nothing has to be kept in step
// across them. See "Money moving between the two books is two transactions"
// in Books.spectable.
namespace books {

// The checking and equity accounts a draw moves through. Fixed rather than
// asked for, because the spec gives no account to choose: a draw is always
// cash leaving or arriving at the main checking account against the one
// equity account set aside for it.
inline const types::AccountPath& draw_cash_account() {
    static const types::AccountPath a("Assets:Checking");
    return a;
}

inline const types::AccountPath& draw_equity_account() {
    static const types::AccountPath a("Equity:Owner Draw");
    return a;
}

// One book's half of the draw. signed_cash_amount is from this book's own
// point of view: positive where the money arrives, negative where it leaves.
inline ledger::Transaction draw_transaction(chart::Chart* accounts, const types::Date& date,
                                            Money signed_cash_amount,
                                            const std::string& ref) {
    if (accounts->find(draw_cash_account().value()) == nullptr)
        accounts->add(draw_cash_account(), types::AccountType::Checking);
    if (accounts->find(draw_equity_account().value()) == nullptr)
        accounts->add(draw_equity_account(), types::AccountType::Equity);

    ledger::Transaction t;
    t.id = ledger::new_id();
    t.ref = types::TransactionRef(ref);
    t.date = date;

    ledger::Posting cash;
    cash.account = accounts->ref_for_path(draw_cash_account().value());
    cash.amount = signed_cash_amount;

    ledger::Posting equity;
    equity.account = accounts->ref_for_path(draw_equity_account().value());
    equity.amount = -signed_cash_amount;

    t.postings = {cash, equity};
    return t;
}

}  // namespace books
