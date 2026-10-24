#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"

// What is owned and what is owed, as at a date. See the balance-sheet section
// of Reports.spectable.
//
// Unlike the category report, nothing here is bounded by a period: a balance
// sheet is a balance as at one date, and every posting up to and including it
// counts, whichever day it was made on.
namespace reports {

enum class BalanceSheetSection { Assets, Liabilities };

inline std::string to_string(BalanceSheetSection s) {
    return s == BalanceSheetSection::Assets ? "Assets" : "Liabilities";
}

// The group heading a balance sheet actually reads, which is not always the
// account list's own heading for the group: Loan & Debt reads as Liabilities
// here and Credit as CreditCards, because the account list is organised by
// what kind of debt something is and a balance sheet by which side it is on.
inline std::string balance_sheet_group_name(types::AccountGroup g) {
    switch (g) {
        case types::AccountGroup::Banking:          return "Banking";
        case types::AccountGroup::Credit:           return "CreditCards";
        case types::AccountGroup::Investments:      return "Investments";
        case types::AccountGroup::LoanAndDebt:      return "Liabilities";
        case types::AccountGroup::PropertyAndAsset: return "Assets";
        case types::AccountGroup::Business:         return "Business";
        case types::AccountGroup::Transfer:          break;
    }
    return "";
}

// One line of the sheet. A group heading carries the total of the accounts
// under it and names no account; an account names no group. See
// BalanceSheetRow in the spec for Level.
struct BalanceSheetRow {
    BalanceSheetSection section = BalanceSheetSection::Assets;
    std::string group;
    std::string account;
    int level = 0;
    Money balance;
    bool is_heading = false;
    bool is_subtotal = false;
};

struct BalanceSheetTotals {
    Money assets;
    Money liabilities;   // shown as an amount owed, so with its display sign
    Money net;
};

struct BalanceSheet {
    std::vector<BalanceSheetRow> rows;
    BalanceSheetTotals totals;
};

// Net worth read from the other side: what was put into the book, plus what
// it has earned, less what it has spent, all as at the same date. Agreeing
// with BalanceSheetTotals::net is what says the sheet is right -- see the
// tie-out scenario in Reports.spectable.
struct NetWorthTie {
    Money opening_equity;
    Money earned;
    Money spent;
    Money net_worth;
};

namespace detail {

// Both of these were a walk over every posting in the book, once per account
// on the sheet. The ledger answers the same two questions from its own index
// now; these stay as names, because a balance sheet reads better for having
// them.
inline Money subtree_balance_as_at(const ledger::Ledger& book, const std::string& path,
                                   const types::Date& as_at) {
    return book.total_balance_as_at(path, as_at);
}

inline Money own_balance_as_at(const ledger::Ledger& book, const std::string& path,
                               const types::Date& as_at) {
    return book.balance_as_at(path, as_at);
}

}  // namespace detail

inline BalanceSheet balance_sheet(const chart::Chart& accounts, const ledger::Ledger& book,
                                  const types::Date& as_at, bool zero_rows,
                                  bool quicken_signs = false) {
    BalanceSheet out;
    int many = 0;
    const types::AccountGroup* order = types::account_groups_in_order(&many);

    for (BalanceSheetSection wanted : {BalanceSheetSection::Assets,
                                       BalanceSheetSection::Liabilities}) {
        for (int gi = 0; gi < many; ++gi) {
            const types::AccountGroup group = order[gi];
            if (group == types::AccountGroup::Transfer) continue;

            // Every chart account in this group, on this section's side --
            // Business is the one group that can be either, depending on the
            // account's own type rather than the group as a whole.
            std::vector<std::string> members;
            for (const chart::Account& a : accounts.all()) {
                if (!types::on_balance_sheet(a.type)) continue;
                if (types::group_of(a.type) != group) continue;
                const BalanceSheetSection side =
                    types::balance_side(a.type) == types::BalanceSide::Asset
                        ? BalanceSheetSection::Assets
                        : BalanceSheetSection::Liabilities;
                if (side != wanted) continue;
                members.push_back(a.path.value());
            }
            if (members.empty()) continue;
            std::sort(members.begin(), members.end());

            // Under the Quicken reading what is owed is printed negative, so
            // the liabilities side keeps the sign its postings have. Net worth
            // does not move either way -- see below.
            const int sign = wanted == BalanceSheetSection::Assets || quicken_signs ? 1 : -1;
            const auto rolled = [&](const std::string& p) {
                return detail::subtree_balance_as_at(book, p, as_at);
            };
            // A root is a member with no other member above it; its own figure
            // is already the whole of its subtree, so only roots are added into
            // the group's total -- a child counted again would count its money
            // twice.
            const auto is_root = [&](const std::string& p) {
                for (const std::string& other : members) {
                    if (other != p && chart::Chart::is_descendant_or_self(p, other))
                        return false;
                }
                return true;
            };
            const auto level_within = [&](const std::string& p) {
                int n = 1;
                for (const std::string& other : members)
                    if (other != p && chart::Chart::is_descendant_or_self(p, other)) ++n;
                return n;
            };

            std::vector<BalanceSheetRow> group_rows;
            Money group_total;
            for (const std::string& p : members) {
                const Money raw = rolled(p);
                if (!zero_rows && raw.cents() == 0) continue;

                bool shows_children = false;
                for (const std::string& other : members) {
                    if (other == p || !chart::Chart::is_descendant_or_self(other, p)) continue;
                    if (!zero_rows && rolled(other).cents() == 0) continue;
                    shows_children = true;
                    break;
                }

                BalanceSheetRow row;
                row.section = wanted;
                row.account = p;
                row.level = level_within(p);
                row.balance = sign < 0 ? -raw : raw;
                row.is_subtotal = shows_children;
                group_rows.push_back(row);
                if (is_root(p)) group_total += row.balance;
            }
            if (group_rows.empty()) continue;

            BalanceSheetRow heading;
            heading.section = wanted;
            heading.group = balance_sheet_group_name(group);
            heading.is_heading = true;
            heading.balance = group_total;
            out.rows.push_back(heading);
            for (const BalanceSheetRow& row : group_rows) out.rows.push_back(row);

            if (wanted == BalanceSheetSection::Assets) out.totals.assets += group_total;
            else out.totals.liabilities += group_total;
        }
    }
    // Assets plus liabilities in the Quicken reading and assets less
    // liabilities in the other, which come to the same number: what somebody is
    // worth is not a matter of how a liability is printed.
    out.totals.net = quicken_signs ? out.totals.assets + out.totals.liabilities
                                   : out.totals.assets - out.totals.liabilities;
    return out;
}

// Opening equity, what has been earned and what has been spent, all as at the
// same date a balance sheet is drawn up to. Taken from the own balance of
// each nominal account, the same figures a category report's totals come
// from, just not bounded to a period.
inline NetWorthTie net_worth_tie(const chart::Chart& accounts, const ledger::Ledger& book,
                                 const types::Date& as_at) {
    NetWorthTie out;
    for (const chart::Account& a : accounts.all()) {
        if (a.type != types::AccountType::Equity && a.type != types::AccountType::Income &&
            a.type != types::AccountType::Expense)
            continue;
        const Money raw = detail::own_balance_as_at(book, a.path.value(), as_at);
        if (a.type == types::AccountType::Equity) out.opening_equity += -raw;
        else if (a.type == types::AccountType::Income) out.earned += -raw;
        else out.spent += raw;
    }
    out.net_worth = out.opening_equity + out.earned - out.spent;
    return out;
}

}  // namespace reports
