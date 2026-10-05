#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "money.h"
#include "posting.h"
#include "text_types.h"

// Every posting in a book, and the balances read off them. Nothing stores a
// total: a balance is always computed, so it cannot disagree with the register,
// and a correction made in 2007 shows in a report run today without anything
// being rebuilt. See Accounts.spectable.
namespace ledger {

// One posting with the date of the transaction it belongs to, which is all a
// balance needs.
struct DatedPosting {
    types::Date date;
    types::AccountPath account;
    Money amount;
    // Which transaction it came from. A balance does not need it, but a report
    // that asks where the money was does: see the investment-activity rule in
    // Reports.spectable. Empty where nothing claims a transaction, as for an
    // opening balance.
    std::string ref;
};

class Ledger {
public:
    void add(const DatedPosting& p) { postings_.push_back(p); }
    const std::vector<DatedPosting>& postings() const { return postings_; }

    // The sum of the amounts of every posting to one account, with the sign those
    // postings carry. Debits are positive throughout.
    Money raw_balance(const std::string& account) const {
        Money total;
        for (const DatedPosting& p : postings_)
            if (p.account.value() == account) total += p.amount;
        return total;
    }

    // The raw balance multiplied by the display sign of the account type, so that
    // each account reads the way its own statement reads.
    Money display_balance(const std::string& account, types::AccountType type) const {
        const Money raw = raw_balance(account);
        return types::display_sign(type) < 0 ? -raw : raw;
    }

    // Every balance in the program is a balance as at a date; the figure shown
    // without one is the balance as at today. The date itself is included.
    Money balance_as_at(const std::string& account, const types::Date& as_at) const {
        Money total;
        for (const DatedPosting& p : postings_)
            if (p.account.value() == account && p.date <= as_at) total += p.amount;
        return total;
    }

    // What was posted to this account itself, and what the whole subtree holds. A
    // placeholder takes no postings of its own, so its own balance is zero and its
    // total is entirely the roll-up; a node that is both posted to and a parent
    // reports both, which is why there are two figures.
    Money own_balance(const std::string& account) const { return raw_balance(account); }

    Money total_balance(const std::string& account) const {
        Money total;
        for (const DatedPosting& p : postings_)
            if (chart::Chart::is_descendant_or_self(p.account.value(), account))
                total += p.amount;
        return total;
    }

    // Net worth is the total of the real accounts. The nominal ones are left out:
    // including them would always give zero, because every transaction balances.
    struct NetWorth {
        Money assets;
        Money liabilities;   // shown as an amount owed, so with its display sign
        Money net;
    };

    NetWorth net_worth(const chart::Chart& accounts) const {
        NetWorth out;
        for (const chart::Account& a : accounts.all()) {
            if (types::class_of(a.type) != types::AccountClass::Real) continue;
            const Money raw = raw_balance(a.path.value());
            if (raw.cents() == 0) continue;
            // An asset is positive raw; a liability is negative raw and is shown
            // as what is owed.
            if (types::display_sign(a.type) > 0) out.assets += raw;
            else out.liabilities += -raw;
        }
        out.net = out.assets - out.liabilities;
        return out;
    }

    // Opening a real account with a balance is a transaction against
    // Equity:Opening Balances, so the book balances from its first day. That is
    // what lets an import of twenty years of history start from a known position.
    // An opening balance of zero is not worth a transaction.
    //
    // The figure is entered the way the statement reads -- a credit card balance
    // owed is positive -- and is stored with the sign the posting needs.
    static std::vector<DatedPosting> opening_balance_postings(
            const types::AccountPath& account, types::AccountType type,
            const Money& stated, const types::Date& on) {
        if (stated.cents() == 0) return {};
        const Money signed_amount =
            types::display_sign(type) < 0 ? -stated : stated;
        return {
            DatedPosting{on, account, signed_amount},
            DatedPosting{on, types::AccountPath("Equity:Opening Balances"), -signed_amount},
        };
    }

private:
    std::vector<DatedPosting> postings_;
};

}  // namespace ledger
