#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "money.h"
#include "transaction.h"
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
    void add(const DatedPosting& p) {
        postings_.push_back(p);
        indexed_ = false;
    }
    const std::vector<DatedPosting>& postings() const { return postings_; }

    // The sum of the amounts of every posting to one account, with the sign those
    // postings carry. Debits are positive throughout.
    Money raw_balance(const std::string& account) const {
        index();
        const auto at = totals_.find(account);
        return at == totals_.end() ? Money() : at->second;
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
        index();
        Money total;
        const auto at = mine_.find(account);
        if (at == mine_.end()) return total;
        for (const std::size_t which : at->second)
            if (postings_[which].date <= as_at) total += postings_[which].amount;
        return total;
    }

    // What was posted to this account itself, and what the whole subtree holds. A
    // placeholder takes no postings of its own, so its own balance is zero and its
    // total is entirely the roll-up; a node that is both posted to and a parent
    // reports both, which is why there are two figures.
    Money own_balance(const std::string& account) const { return raw_balance(account); }

    Money total_balance(const std::string& account) const {
        index();
        Money total;
        for (auto at = totals_.lower_bound(account);
             at != totals_.end() && at->first.compare(0, account.size(), account) == 0; ++at)
            if (chart::Chart::is_descendant_or_self(at->first, account))
                total += at->second;
        return total;
    }

    // The same figure bounded to a date, which is what a balance sheet is made
    // of. Every account at or below this one, so a placeholder reads as the
    // whole of what is under it.
    Money total_balance_as_at(const std::string& account, const types::Date& as_at) const {
        index();
        Money total;
        for (auto at = mine_.lower_bound(account);
             at != mine_.end() && at->first.compare(0, account.size(), account) == 0; ++at) {
            if (!chart::Chart::is_descendant_or_self(at->first, account)) continue;
            for (const std::size_t which : at->second)
                if (postings_[which].date <= as_at) total += postings_[which].amount;
        }
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
    // The postings of each account, and their total, worked out once and kept
    // until a posting is added. Every balance was a walk over every posting in
    // the book -- 560 accounts over 74,000 postings whenever a list of
    // balances was drawn, which on the author's book was half a second a
    // listing and a third of a second a balance sheet.
    //
    // It is built on demand rather than maintained, and add() throws it away
    // rather than updating it, because a half-updated index is how a balance
    // comes to disagree with the register it is read beside. Nothing outside
    // can reach it, and nothing but add() can change what it is built from, so
    // it cannot go stale the way an index handed out to a caller can.
    void index() const {
        if (indexed_) return;
        mine_.clear();
        totals_.clear();
        for (std::size_t i = 0; i < postings_.size(); ++i) {
            const std::string& path = postings_[i].account.value();
            mine_[path].push_back(i);
            totals_[path] += postings_[i].amount;
        }
        indexed_ = true;
    }

    std::vector<DatedPosting> postings_;
    mutable bool indexed_ = false;
    mutable std::map<std::string, std::vector<std::size_t>> mine_;
    mutable std::map<std::string, Money> totals_;
};

// Filling a ledger from the transactions of a book. Every transaction credits
// its own SourceAccount; its Destination is credited too, UNLESS that
// Destination is itself another transaction's own SourceAccount -- a matched
// transfer, where the other half of the movement is that transaction's own
// entry, and crediting it again here would count one movement of money
// twice. A split's lines are never paired this way, so each is always
// credited. See the two-rows-for-a-transfer rule in Transactions.spectable.
inline void add_to_book(Ledger* book, const chart::Chart& accounts,
                        const std::vector<Transaction>& transactions,
                        const std::vector<TransactionSplit>& splits) {
    // Three lookups that were each a walk over the whole book, done once here
    // rather than once per transaction. Filling the ledger is what every
    // balance, every report and the account list are built on, so this is the
    // one place in the program where the difference is measured in seconds.
    const chart::RefIndex index(accounts);
    std::map<std::string, std::vector<const TransactionSplit*>> parts;
    for (const TransactionSplit& s : splits) parts[s.transaction_id.value()].push_back(&s);
    // How many transactions run from one account to another, so a matched
    // transfer is recognised by asking rather than by searching for its other
    // half -- that search was over every transaction, for every transaction.
    std::map<std::pair<long long, long long>, int> movements;
    for (const Transaction& t : transactions)
        ++movements[{t.source_account.value(), t.destination.value()}];

    for (const Transaction& t : transactions) {
        book->add({t.date, types::AccountPath(index.path_for(t.source_account)),
                   t.amount, t.ref.value()});
        if (is_split(t.destination)) {
            const auto at = parts.find(t.id.value());
            if (at != parts.end())
                for (const TransactionSplit* s : at->second)
                    book->add({t.date, types::AccountPath(index.path_for(s->destination)),
                              s->amount, t.ref.value()});
            continue;
        }
        if (t.destination.is_none()) continue;
        bool paired = false;
        if (index.is_real(t.destination)) {
            const auto at = movements.find({t.destination.value(), t.source_account.value()});
            int halves = at == movements.end() ? 0 : at->second;
            // The other half has to be another transaction. One whose two
            // sides are the same account is counted in the figure it is
            // asking about, and would otherwise pair with itself.
            if (t.source_account == t.destination) halves -= 1;
            paired = halves > 0;
        }
        if (!paired)
            book->add({t.date, types::AccountPath(index.path_for(t.destination)),
                      -t.amount, t.ref.value()});
    }
}

}  // namespace ledger
