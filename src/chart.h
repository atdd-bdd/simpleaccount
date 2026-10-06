#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "account_type.h"
#include "text_types.h"

// The chart of accounts: one tree holding both the accounts that keep money and
// the ones Quicken calls categories. Having them in one tree is what lets every
// transaction balance with no special case for a category. See Accounts.spectable.
namespace chart {

struct Account {
    types::AccountPath path;
    types::AccountType type = types::AccountType::Bank;
    bool placeholder = false;   // a grouping node; takes no postings
    bool hidden = false;        // kept out of pickers, history retained
    // A short way to type this account when categorising; see the finding rule
    // in Accounts.spectable. Empty for most accounts.
    std::string alias;
    // What a payment to this account is called on the statement of whatever
    // account pays it -- CHASEBANK on the bank statement, for a Chase card.
    // It is how a payment on one side finds the account on the other without
    // searching every account in the book. Several accounts may share one.
    std::string payment_payee;
};

// Why an operation was refused. The text is what the register shows, so it names
// the account and says what to do instead rather than only what went wrong.
struct Rejection {
    bool refused = false;
    std::string reason;
};

class Chart {
public:
    // How many postings an account has. The chart does not hold postings, so a
    // caller that wants the rules about deleting and retyping has to say.
    using PostingCounts = std::map<std::string, int>;

    void set_posting_counts(const PostingCounts& counts) { counts_ = counts; }

    int postings_on(const std::string& path) const {
        const auto at = counts_.find(path);
        return at == counts_.end() ? 0 : at->second;
    }

    bool has(const std::string& path) const { return accounts_.count(path) != 0; }

    const Account* find(const std::string& path) const {
        const auto at = accounts_.find(path);
        return at == accounts_.end() ? nullptr : &at->second;
    }

    // Every account, in path order, which is also the order a report wants.
    std::vector<Account> all() const {
        std::vector<Account> out;
        for (const auto& entry : accounts_) out.push_back(entry.second);
        return out;
    }

    // Used to establish a chart in a Given, where nothing is being tested about
    // how it was built.
    void put(const Account& a) { accounts_[a.path.value()] = a; }

    // Adding an account creates any missing level above it as a placeholder,
    // because importing twenty years of data names deep categories that were
    // never created by hand. A filled-in parent takes the type of its child, so
    // the roll-up in a report adds up.
    Rejection add(const types::AccountPath& path, types::AccountType type,
                  std::vector<Account>* created = nullptr) {
        if (has(path.value()))
            return {true, "An account named " + path.value() + " exists"};

        const std::vector<std::string> segments = types::segments_of(path);
        std::string so_far;
        for (std::size_t i = 0; i < segments.size(); ++i) {
            so_far += (i == 0 ? "" : ":") + segments[i];
            if (has(so_far)) continue;
            Account a;
            a.path = types::AccountPath(so_far);
            a.type = type;
            a.placeholder = (i + 1 < segments.size());
            accounts_[so_far] = a;
            if (created) created->push_back(a);
        }
        return {};
    }

    // The path of a child is built from the path of its parent, so a rename
    // rewrites every path beneath it. Postings are untouched: they refer to the
    // account, not to the text of its path.
    Rejection rename(const types::AccountPath& path, const types::AccountName& new_name) {
        if (!has(path.value()))
            return {true, "There is no account named " + path.value()};
        const std::string parent = types::parent_of(path);
        const std::string to = parent.empty() ? new_name.value()
                                              : parent + ":" + new_name.value();
        return move_subtree(path.value(), to);
    }

    Rejection move(const types::AccountPath& path, const types::AccountPath& new_parent) {
        if (!has(path.value()))
            return {true, "There is no account named " + path.value()};
        // Moving an account beneath itself would make it its own ancestor.
        if (is_descendant_or_self(new_parent.value(), path.value()))
            return {true, "An account may not be its own descendant"};
        const std::string to = new_parent.value() + ":" + types::name_of(path);
        return move_subtree(path.value(), to);
    }

    // Twenty years of history is the point of the program, so nothing silently
    // discards a posting. Hide the account instead.
    Rejection remove(const types::AccountPath& path) {
        const int n = postings_on(path.value());
        if (n > 0)
            return {true, path.value() + " has " + std::to_string(n) +
                          " postings; hide it instead"};
        accounts_.erase(path.value());
        return {};
    }

    Rejection hide(const types::AccountPath& path) {
        const auto at = accounts_.find(path.value());
        if (at == accounts_.end())
            return {true, "There is no account named " + path.value()};
        at->second.hidden = true;
        return {};
    }

    // Changing an Expense into a Bank would change the sign that every existing
    // posting is read with, and so rewrite history in silence. Within one class
    // the display sign is the same, so no posting changes meaning.
    Rejection change_type(const types::AccountPath& path, types::AccountType to) {
        const auto at = accounts_.find(path.value());
        if (at == accounts_.end())
            return {true, "There is no account named " + path.value()};
        const bool crosses = types::class_of(at->second.type) != types::class_of(to);
        if (crosses && postings_on(path.value()) > 0) {
            const bool was_nominal =
                types::class_of(at->second.type) == types::AccountClass::Nominal;
            return {true, path.value() + " has postings; " +
                          (was_nominal ? "a category cannot become an account"
                                       : "an account cannot become a category")};
        }
        at->second.type = to;
        return {};
    }

    // What a category picker offers: the accounts that can actually take a
    // posting. A placeholder cannot, and a hidden one is deliberately out of the
    // way while its history still reports.
    std::vector<Account> picker() const {
        std::vector<Account> out;
        for (const auto& entry : accounts_)
            if (!entry.second.placeholder && !entry.second.hidden)
                out.push_back(entry.second);
        return out;
    }

    std::vector<Account> children_of(const std::string& parent) const {
        std::vector<Account> out;
        for (const auto& entry : accounts_)
            if (types::parent_of(entry.second.path) == parent) out.push_back(entry.second);
        return out;
    }

    static bool is_descendant_or_self(const std::string& candidate,
                                      const std::string& ancestor) {
        if (candidate == ancestor) return true;
        return candidate.size() > ancestor.size() &&
               candidate.compare(0, ancestor.size(), ancestor) == 0 &&
               candidate[ancestor.size()] == ':';
    }

private:
    std::map<std::string, Account> accounts_;
    PostingCounts counts_;

    Rejection move_subtree(const std::string& from, const std::string& to) {
        std::vector<Account> moved;
        for (auto it = accounts_.begin(); it != accounts_.end();) {
            if (is_descendant_or_self(it->first, from)) {
                Account a = it->second;
                a.path = types::AccountPath(to + it->first.substr(from.size()));
                moved.push_back(a);
                it = accounts_.erase(it);
            } else {
                ++it;
            }
        }
        for (const Account& a : moved) accounts_[a.path.value()] = a;
        return {};
    }
};

}  // namespace chart
