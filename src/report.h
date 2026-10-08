#pragma once
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "text_types.h"

// The spending report: what was earned and spent, by category, over a period.
//
// Every figure is a total of postings. Nothing reads a stored total, so a report
// cannot disagree with the register, and a correction made in 2007 shows in a
// report run today without anything being rebuilt. See Reports.spectable.
namespace reports {

struct Row {
    std::string account;
    int level = 0;          // depth in the tree; a root is 0
    Money amount;           // with the display sign, so income and expenses are both positive
    bool is_subtotal = false;
};

struct Totals {
    Money income;
    Money expenses;
    Money net;
};

// How much of the tree a report shows. This is the choice offered on the report
// itself; see the detail section of Reports.spectable.
enum class Detail {
    HighestOnly,    // one line per category at the top of a section
    AllCategories,  // every category, indented under its parent
    Transactions,   // every category, with the transactions behind each one
};

inline std::string to_string(Detail detail) {
    switch (detail) {
        case Detail::HighestOnly:   return "HighestOnly";
        case Detail::AllCategories: return "AllCategories";
        case Detail::Transactions:  return "Transactions";
    }
    return "AllCategories";
}

inline Detail detail_from_string(const std::string& text) {
    if (text == "HighestOnly") return Detail::HighestOnly;
    if (text == "Transactions") return Detail::Transactions;
    return Detail::AllCategories;
}

// What a line of a report shown with its transactions is.
enum class LineKind {
    Category,     // a category or a section, carrying the figure beneath it
    Other,        // the Other line standing for a category's own transactions
    Transaction,  // one transaction, under the line whose figure it is part of
};

inline std::string to_string(LineKind kind) {
    switch (kind) {
        case LineKind::Category:    return "Category";
        case LineKind::Other:       return "Other";
        case LineKind::Transaction: return "Transaction";
    }
    return "Category";
}

struct Spec {
    types::Date from{2024, 1, 1};
    types::Date to{2024, 12, 31};
    int depth = 0;          // 0 is no limit
    // Which of the three shapes is wanted. HighestOnly is a depth limit of one
    // under the name the report offers, and it wins over Depth.
    Detail detail = Detail::AllCategories;
    bool zero_rows = false;
    // A spending report answers what was earned and spent. A dividend
    // reinvested inside a retirement account was neither, so by default the
    // income and expense of a transaction that touches an investment account
    // is left out. See the investment-activity rule in Reports.spectable.
    bool include_investment_activity = false;
    // Expenses negative, as Quicken writes them, for reading a figure against a
    // Quicken report side by side. Income is untouched: the two conventions
    // already agree on it. The net does not move either -- only the sign of the
    // expenses figure. See the Quicken signs scenario in Reports.spectable.
    bool quicken_signs = false;
};

struct CategoryReport {
    std::vector<Row> rows;
    Totals totals;
};

namespace detail {

inline int level_of(const std::string& path) {
    int n = 0;
    for (char c : path)
        if (c == ':') ++n;
    return n;
}

// The transactions that touch an investment account, by ref. The test is where
// the money was rather than which category was used: the same category is real
// income when the dividend lands in a bank account and is not when it stays in
// the brokerage, so the category alone cannot decide it.
//
// A posting with no ref claims no transaction -- an opening balance -- and can
// taint nothing, or every one of them would share the same empty name.
inline std::set<std::string> investment_refs(const chart::Chart& accounts,
                                             const std::vector<ledger::DatedPosting>& postings) {
    std::set<std::string> out;
    for (const ledger::DatedPosting& p : postings) {
        if (p.ref.empty()) continue;
        const chart::Account* a = accounts.find(p.account.value());
        if (a != nullptr && a->type == types::AccountType::Investment) out.insert(p.ref);
    }
    return out;
}

inline bool inside_investment(const std::set<std::string>& refs,
                              const std::string& ref) {
    return !ref.empty() && refs.count(ref) != 0;
}

}  // namespace detail

// Income first, then expenses, each tree depth first with siblings in
// alphabetical order. Not by amount: a report compared with the same report from
// last month should have its rows in the same places, so that the eye can find a
// line without reading every one of them.
inline CategoryReport category_report(const chart::Chart& accounts,
                                      const ledger::Ledger& book,
                                      const Spec& spec) {
    // HighestOnly is the summary, which is a depth limit of one. It wins over
    // Depth so that choosing it is not quietly undone by a depth left over from
    // somewhere else; the two mean the same thing and there is one of them.
    const int depth = spec.detail == Detail::HighestOnly ? 1 : spec.depth;

    // What each account holds over the range, and what its subtree holds.
    std::map<std::string, Money> own;
    std::map<std::string, Money> total;
    const std::set<std::string> investment =
        spec.include_investment_activity ? std::set<std::string>()
                                         : detail::investment_refs(accounts, book.postings());
    for (const ledger::DatedPosting& p : book.postings()) {
        if (p.date < spec.from || spec.to < p.date) continue;
        if (detail::inside_investment(investment, p.ref)) continue;
        const chart::Account* a = accounts.find(p.account.value());
        if (a == nullptr) continue;
        // Only the categories. A transfer between two real accounts touches none
        // of these, so it cannot appear -- and Equity is left out too, which is
        // what keeps a market revaluation out of a spending report.
        if (a->type != types::AccountType::Income && a->type != types::AccountType::Expense)
            continue;
        own[p.account.value()] += p.amount;
        // Every ancestor, including the account itself.
        std::string path = p.account.value();
        while (true) {
            total[path] += p.amount;
            const std::string parent = types::parent_of(types::AccountPath(path));
            if (parent.empty()) break;
            path = parent;
        }
    }

    // Every account that could appear: the categories in the chart, plus any
    // ancestor of one, so a placeholder nobody declared still heads its subtree.
    std::set<std::string> candidates;
    for (const chart::Account& a : accounts.all()) {
        if (a.type != types::AccountType::Income && a.type != types::AccountType::Expense)
            continue;
        std::string path = a.path.value();
        while (true) {
            candidates.insert(path);
            const std::string parent = types::parent_of(types::AccountPath(path));
            if (parent.empty()) break;
            path = parent;
        }
    }

    const auto rolled = [&](const std::string& path) {
        const auto at = total.find(path);
        return at == total.end() ? Money() : at->second;
    };
    const auto has_activity = [&](const std::string& path) {
        return rolled(path).cents() != 0;
    };

    // Which root a path belongs to, and the order the roots appear in.
    const auto root_of = [](const std::string& path) {
        const std::size_t at = path.find(':');
        return at == std::string::npos ? path : path.substr(0, at);
    };

    CategoryReport out;
    for (const char* root : {"Income", "Expenses"}) {
        // Depth-first preorder over the candidates under this root.
        std::vector<std::string> paths;
        for (const std::string& p : candidates)
            if (root_of(p) == root) paths.push_back(p);
        std::sort(paths.begin(), paths.end());   // a path sorts into preorder

        // Other rows, kept aside until the categories they belong under are all
        // placed. Pairs of the category and the row that follows its subtree.
        std::vector<std::pair<std::string, Row>> trailing;

        for (const std::string& path : paths) {
            const int level = detail::level_of(path);
            if (depth > 0 && level > depth) continue;
            if (!spec.zero_rows && !has_activity(path)) continue;

            // A node is a subtotal when something below it is being shown. At a
            // depth limit the node is a leaf of the report, so it is not marked
            // one even though its figure came from below.
            bool shows_children = false;
            for (const std::string& other : paths) {
                if (other == path) continue;
                if (!chart::Chart::is_descendant_or_self(other, path)) continue;
                const int other_level = detail::level_of(other);
                if (depth > 0 && other_level > depth) continue;
                if (!spec.zero_rows && !has_activity(other)) continue;
                shows_children = true;
                break;
            }

            const chart::Account* a = accounts.find(path);
            // A path with no account of its own is a placeholder standing over
            // its children, and those are all one kind, so take the sign from
            // whichever root it is under.
            const int sign = a != nullptr
                                 ? types::display_sign(a->type)
                                 : (std::string(root) == "Income" ? -1 : 1);
            Row row;
            row.account = path;
            row.level = level;
            row.amount = sign < 0 ? -rolled(path) : rolled(path);
            // Uniform, with no special case for a category whose figure is a
            // credit: a Quicken expense figure is the negation of ours
            // whichever way it runs.
            if (spec.quicken_signs && std::string(root) == "Expenses")
                row.amount = -row.amount;
            row.is_subtotal = shows_children;
            out.rows.push_back(row);

            // A category that holds both transactions and other categories owes
            // the reader a row for its own transactions, or the rows beneath it
            // do not add up to it and the difference is unexplained on the page.
            // See the Other-row rule in Reports.spectable.
            if (!shows_children) continue;
            const auto mine = own.find(path);
            if (mine == own.end() || mine->second.cents() == 0) continue;
            Row other;
            // Named as Quicken names it -- the word, then the path without the
            // section -- because these reports are checked against Quicken's.
            const std::string under = std::string(root) + ":";
            other.account = "Other " + (path.rfind(under, 0) == 0
                                            ? path.substr(under.size()) : path);
            other.level = level + 1;
            other.amount = sign < 0 ? -mine->second : mine->second;
            if (spec.quicken_signs && std::string(root) == "Expenses")
                other.amount = -other.amount;
            other.is_subtotal = false;
            // Held back and placed after this category's descendants, which is
            // where Quicken puts it.
            trailing.push_back({path, other});
        }

        // Each held-back row goes after the last row belonging to its category.
        for (const auto& held : trailing) {
            std::size_t at = out.rows.size();
            for (std::size_t i = 0; i < out.rows.size(); ++i)
                if (chart::Chart::is_descendant_or_self(out.rows[i].account, held.first))
                    at = i + 1;
            out.rows.insert(out.rows.begin() + static_cast<long>(at), held.second);
        }
        trailing.clear();
    }

    // Income and expenses are both shown positive and the net is their
    // difference, which is easier to read down a column. Quicken writes the same
    // report in natural signs, so the comparison negates one side; see
    // AcceptanceAgainstQuicken.
    for (const auto& entry : own) {
        const chart::Account* a = accounts.find(entry.first);
        if (a == nullptr) continue;
        if (a->type == types::AccountType::Income) out.totals.income += -entry.second;
        else if (a->type == types::AccountType::Expense) out.totals.expenses += entry.second;
    }
    // The net is the same number in both conventions, so it is taken before
    // the sign of the expenses figure is turned round and not after.
    out.totals.net = out.totals.income - out.totals.expenses;
    if (spec.quicken_signs) out.totals.expenses = -out.totals.expenses;
    return out;
}

// One line of a report shown with its transactions.
//
// Two amount columns, because they are two different kinds of figure: Each is
// the individual amount and is filled only on a transaction line, Amount is the
// figure for the category and is filled only on a category line. A column with
// nothing in it on a line is empty rather than zero -- a category line has no
// individual amount to show, which is not the same as showing it as 0.00 -- so
// each one says whether it is there.
struct DetailLine {
    LineKind kind = LineKind::Category;
    std::string account;      // on a transaction line, the category it was posted to
    int level = 0;
    types::Date date{2024, 1, 1};
    std::string payee;
    Money each;
    Money amount;
    bool has_each = false;
    bool has_amount = false;
    // Carried through from the row this line came from, so that a page showing
    // transactions emphasises the same lines as a page without them.
    bool is_subtotal = false;
};

namespace detail {

// The Other line is written by the report and is in no chart, so it is known by
// its name and by not being an account. Its category is the section it is under
// plus the rest of the label, which is how the name was built.
inline bool is_other_line(const chart::Chart& accounts, const std::string& label) {
    return label.rfind("Other ", 0) == 0 && accounts.find(label) == nullptr;
}

inline bool touches_an_investment(const chart::Chart& accounts,
                                  const ledger::Transaction& t) {
    for (const ledger::Posting& p : t.postings) {
        const chart::Account* a = accounts.find(p.account.value());
        if (a != nullptr && a->type == types::AccountType::Investment) return true;
    }
    return false;
}

}  // namespace detail

// The category report with the transactions behind each figure underneath it,
// which is the drill-down of every line at once rather than one at a time.
//
// The figures are the ones the full tree already shows: this walks the report
// rather than totalling anything again, so a transaction line can never
// disagree with the category line above it. See Reports.spectable.
inline std::vector<DetailLine> detail_report(
        const chart::Chart& accounts, const ledger::Ledger& book,
        const std::vector<ledger::Transaction>& transactions, const Spec& spec) {
    // The whole tree, whatever was asked for: the transactions are the detail,
    // so a depth limit would hide the categories they belong to.
    Spec whole = spec;
    whole.detail = Detail::AllCategories;
    whole.depth = 0;
    const CategoryReport report = category_report(accounts, book, whole);

    std::vector<DetailLine> out;
    std::string section;
    for (const Row& row : report.rows) {
        const bool other = detail::is_other_line(accounts, row.account);
        // Which half of the report this line is in, taken from the root of the
        // last real path rather than from a level 0 row: a chart that never
        // declared Income or Expenses still reports under them, so there is not
        // always a row to read it off.
        if (!other) {
            const std::size_t colon = row.account.find(':');
            section = colon == std::string::npos ? row.account
                                                 : row.account.substr(0, colon);
        }

        DetailLine line;
        line.kind = other ? LineKind::Other : LineKind::Category;
        line.account = row.account;
        line.level = row.level;
        line.amount = row.amount;
        line.has_amount = true;
        line.is_subtotal = row.is_subtotal;
        out.push_back(line);

        // A subtotal's own postings are on its Other line, so they are listed
        // there and not twice. Everything else lists what was posted to it.
        if (!other && row.is_subtotal) continue;
        const std::string holds =
            other ? section + ":" + row.account.substr(std::string("Other ").size())
                  : row.account;

        const chart::Account* a = accounts.find(holds);
        const int sign = a != nullptr
                             ? types::display_sign(a->type)
                             : (section == "Income" ? -1 : 1);
        const bool turn = spec.quicken_signs && section == "Expenses";

        std::vector<DetailLine> lines;
        for (const ledger::Transaction& t : transactions) {
            if (t.date < spec.from || spec.to < t.date) continue;
            if (!spec.include_investment_activity &&
                detail::touches_an_investment(accounts, t))
                continue;
            // One line per posting against this category, so the lines add up
            // to the figure above them even where a split posted to the same
            // category twice.
            for (const ledger::Posting& p : t.postings) {
                if (p.account.value() != holds) continue;
                DetailLine under;
                under.kind = LineKind::Transaction;
                under.account = holds;
                under.level = row.level + 1;
                under.date = t.date;
                under.payee = t.payee.value();
                under.each = sign < 0 ? -p.amount : p.amount;
                if (turn) under.each = -under.each;
                under.has_each = true;
                lines.push_back(under);
            }
        }
        // In date order, and two on the same date keep the order the register
        // has them in.
        std::stable_sort(lines.begin(), lines.end(),
                         [](const DetailLine& l, const DetailLine& r) {
                             return l.date < r.date;
                         });
        for (const DetailLine& under : lines) out.push_back(under);
    }
    return out;
}

// One row per payee, flat because payees have no tree, ordered by amount because
// the question being asked is usually which is the largest. A split with four
// postings is one visit to the shop, so the count is of transactions and the
// amount is the total of its category postings -- not twice it.
struct PayeeRow {
    std::string payee;
    Money amount;
    int count = 0;
};

inline std::vector<PayeeRow> payee_report(
        const chart::Chart& accounts,
        const std::vector<ledger::Transaction>& transactions, const Spec& spec,
        const std::optional<std::string>& only_category = std::nullopt) {
    std::map<std::string, PayeeRow> by_payee;
    for (const ledger::Transaction& t : transactions) {
        if (t.date < spec.from || spec.to < t.date) continue;
        // The same rule as the category report: a brokerage would otherwise be
        // the largest payee in the book, for money that was never spent
        // anywhere. Here the transaction is in hand, so there is no need to go
        // by ref.
        if (!spec.include_investment_activity) {
            bool investment = false;
            for (const ledger::Posting& p : t.postings) {
                const chart::Account* a = accounts.find(p.account.value());
                if (a != nullptr && a->type == types::AccountType::Investment) {
                    investment = true;
                    break;
                }
            }
            if (investment) continue;
        }
        Money amount;
        bool counted = false;
        for (const ledger::Posting& p : t.postings) {
            const chart::Account* a = accounts.find(p.account.value());
            if (a == nullptr) continue;
            if (a->type != types::AccountType::Income &&
                a->type != types::AccountType::Expense)
                continue;
            if (only_category &&
                !chart::Chart::is_descendant_or_self(p.account.value(), *only_category))
                continue;
            amount += types::display_sign(a->type) < 0 ? -p.amount : p.amount;
            counted = true;
        }
        if (!counted) continue;
        PayeeRow& row = by_payee[t.payee.value()];
        row.payee = t.payee.value();
        row.amount += amount;
        ++row.count;
    }
    std::vector<PayeeRow> out;
    for (auto& entry : by_payee) out.push_back(entry.second);
    std::sort(out.begin(), out.end(), [](const PayeeRow& a, const PayeeRow& b) {
        if (a.amount.cents() != b.amount.cents())
            return a.amount.cents() > b.amount.cents();
        return a.payee < b.payee;
    });
    return out;
}

}  // namespace reports
