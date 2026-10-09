#pragma once
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "posting.h"
#include "transaction_id.h"
#include "register_lines.h"
#include "text_types.h"

// What the windows show, and what selecting things does -- with no Qt in it, so
// that it can be tested headlessly and the widgets stay a thin view over it.
// See UserInterface.spectable.
namespace ui {

// The headings the account list is divided into live with the account types,
// because the group follows from the type rather than from how it is shown.
// Nothing here but a name for them and the question the list asks.
using Group = types::AccountGroup;

// The categories are in no group: Income, Expense and Equity are reached
// through a report rather than browsed, so there is nothing to list them under.
inline std::optional<Group> group_of(types::AccountType t) {
    if (types::class_of(t) == types::AccountClass::Nominal) return std::nullopt;
    return types::group_of(t);
}

// How far below a root of its own group an account sits, so a subtree indents.
inline int depth_within_group(const chart::Account& a,
                              const std::vector<chart::Account>& in_group);

struct AccountRow {
    std::string group;        // set on a heading line only
    std::string account;      // set on an account line only
    std::string name;
    int indent = 0;
    Money balance;
    bool is_heading = false;
};

// Groups in a fixed order, each with the total of what is under it, and a group
// with nothing in it is left out rather than shown empty.
inline std::vector<AccountRow> account_list(const chart::Chart& accounts,
                                            const ledger::Ledger& book,
                                            bool show_hidden = false) {
    // Every subtree total in one pass over the postings. Asking the ledger per
    // account instead is one pass each, which on the real book is 559 accounts
    // over 80,904 postings every time the list is drawn.
    std::map<std::string, Money> subtree;
    for (const ledger::DatedPosting& p : book.postings()) {
        std::string path = p.account.value();
        while (true) {
            subtree[path] += p.amount;
            const std::string parent = types::parent_of(types::AccountPath(path));
            if (parent.empty()) break;
            path = parent;
        }
    }
    const auto total_of = [&subtree](const std::string& path) {
        const auto at = subtree.find(path);
        return at == subtree.end() ? Money() : at->second;
    };

    std::vector<AccountRow> out;
    int many = 0;
    const Group* order = types::account_groups_in_order(&many);
    for (int at = 0; at < many; ++at) {
        const Group g = order[at];
        std::vector<chart::Account> in_group;
        for (const chart::Account& a : accounts.all()) {
            // Qualified: Group is an alias for the type in types::, so an
            // unqualified call finds types::group_of as well and is ambiguous.
            const auto which = ui::group_of(a.type);
            if (!which || *which != g) continue;
            if (a.hidden && !show_hidden) continue;
            in_group.push_back(a);
        }
        if (in_group.empty()) continue;

        // The heading total is the total of the roots of this group, so a
        // subtree is counted once rather than once per level.
        Money group_total;
        for (const chart::Account& a : in_group) {
            const std::string parent = types::parent_of(a.path);
            const bool parent_in_group = std::any_of(
                in_group.begin(), in_group.end(),
                [&](const chart::Account& x) { return x.path.value() == parent; });
            if (parent_in_group) continue;
            const Money raw = total_of(a.path.value());
            group_total += types::display_sign(a.type) < 0 ? -raw : raw;
        }

        AccountRow heading;
        heading.group = types::to_string(g);
        heading.is_heading = true;
        heading.balance = group_total;
        out.push_back(heading);

        std::sort(in_group.begin(), in_group.end(),
                  [](const chart::Account& x, const chart::Account& y) {
                      return x.path.value() < y.path.value();
                  });
        for (const chart::Account& a : in_group) {
            AccountRow row;
            row.account = a.path.value();
            row.name = types::name_of(a.path);
            // One level in for the group, and one more per level of the tree
            // below whatever root of the group it sits under.
            row.indent = 1 + depth_within_group(a, in_group);
            const Money raw = total_of(a.path.value());
            row.balance = types::display_sign(a.type) < 0 ? -raw : raw;
            out.push_back(row);
        }
    }
    return out;
}

inline int depth_within_group(const chart::Account& a,
                              const std::vector<chart::Account>& in_group) {
    int depth = 0;
    std::string parent = types::parent_of(a.path);
    while (!parent.empty()) {
        const bool here = std::any_of(
            in_group.begin(), in_group.end(),
            [&](const chart::Account& x) { return x.path.value() == parent; });
        if (!here) break;
        ++depth;
        parent = types::parent_of(types::AccountPath(parent));
    }
    return depth;
}

// ---------------------------------------------------------------------------
// Panes
// ---------------------------------------------------------------------------

enum class PaneContent { Register, ImportReview, Empty };

inline std::string to_string(PaneContent c) {
    switch (c) {
        case PaneContent::Register:     return "Register";
        case PaneContent::ImportReview: return "ImportReview";
        case PaneContent::Empty:        return "Empty";
    }
    return "Empty";
}

// What a disposition says about a downloaded or read row, as the import worked
// it out. A row the import believes it has seen before starts unticked, so that
// accepting everything does not bring it in.
struct ReviewRow {
    int line = 0;
    types::Date date;
    std::string payee;
    std::string account;
    std::string category;
    Money amount;
    std::string disposition = "New";
    bool accepted = true;
    // What the file called this row, and which kind of file said so. This is
    // what makes a second download of the same statement do nothing, so it is
    // carried through the review to the posting rather than being rebuilt from
    // the five columns above -- which could not carry it. See the accepted-row
    // rule in UserInterface.spectable.
    std::string identifier;
    types::ImportSource source = types::ImportSource::Ofx;
    // The name as the bank wrote it, kept so that a rule written next year can
    // still match what arrived this year.
    std::string raw_name;
    // Both sent by a download and both worth keeping: the check number is how a
    // check already written is recognised when it clears.
    std::string memo;
    std::string check_no;
    // Whether the bank has settled this row, which the import says and the
    // review carries: an OFX file's rows are a statement, a CSV's are not.
    types::ClearedStatus cleared = types::ClearedStatus::Cleared;
};

struct Pane {
    PaneContent showing = PaneContent::Empty;
    std::string account;
};

class Workspace {
public:
    Workspace(const chart::Chart& accounts, const ledger::Ledger& book,
              const std::vector<ledger::Transaction>& transactions)
        : accounts_(accounts), book_(book), transactions_(transactions) {
        panes_.push_back(Pane{});
    }

    // ------------------------------------------------------------ selection

    void select(const std::string& account) {
        Pane& pane = panes_[active_];
        pane.account = account;
        // Selecting an account turns an import review back into a register: the
        // reader has asked for the account, not for the import.
        if (pane.showing != PaneContent::ImportReview) pane.showing = PaneContent::Register;
        else pane.showing = PaneContent::Register;
    }

    void split() {
        if (panes_.size() > 1) return;
        // The new pane starts where the first one is, so splitting shows
        // something rather than nothing, and becomes the active one.
        panes_.push_back(panes_.front());
        if (panes_.back().showing == PaneContent::Empty)
            panes_.back().showing = PaneContent::Empty;
        active_ = 1;
    }

    void close_split() {
        if (panes_.size() < 2) return;
        // Whichever pane was being worked in is the one that stays.
        const Pane keep = panes_[active_];
        panes_.clear();
        panes_.push_back(keep);
        active_ = 0;
        review_.clear();
    }

    void make_active(int pane_one_based) {
        const std::size_t index = static_cast<std::size_t>(pane_one_based - 1);
        if (index < panes_.size()) active_ = index;
    }

    // --------------------------------------------------------- import review

    // An import is shown beside the register it is about to change, so the view
    // splits by itself if it was not split already.
    void review_import(const std::vector<ReviewRow>& rows, const std::string& into) {
        review_ = rows;
        for (ReviewRow& r : review_)
            if (r.disposition != "New") r.accepted = false;
        committed_ = false;
        if (panes_.size() < 2) split();
        active_ = panes_.size() - 1;
        panes_[active_].showing = PaneContent::ImportReview;
        panes_[active_].account = into;
    }

    void tick(int line) { set_accepted(line, true); }
    void untick(int line) { set_accepted(line, false); }

    // The accepted rows become transactions, and the pane goes back to being a
    // register of the same account so the result can be read where the import was.
    void accept_import() {
        accepted_.clear();
        for (const ReviewRow& r : review_) {
            if (!r.accepted) continue;
            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef("T" + std::to_string(transactions_.size() + 1));
            t.date = r.date;
            t.payee = types::PayeeName(r.payee);
            t.raw_name = r.raw_name;
            t.memo = r.memo;
            t.check_no = types::CheckNumber(r.check_no);
            ledger::Posting here;
            here.account = types::AccountPath(r.account);
            here.amount = r.amount;
            here.cleared = r.cleared;
            // The identifier belongs to the side the statement is for. The bank
            // has never heard of the category, so the other side carries none.
            ledger::stamp(&here, r.source, r.identifier);
            ledger::Posting other;
            other.account = types::AccountPath(r.category);
            other.amount = -r.amount;
            t.postings = {here, other};
            transactions_.push_back(t);
            accepted_.push_back(t);
            book_.add({r.date, here.account, here.amount});
            book_.add({r.date, other.account, other.amount});
        }
        committed_ = true;
        panes_[active_].showing = PaneContent::Register;
    }

    void cancel_import() {
        review_.clear();
        accepted_.clear();
        committed_ = false;
        panes_[active_].showing = PaneContent::Register;
    }

    // ---------------------------------------------------------------- asking

    bool split_open() const { return panes_.size() > 1; }
    int pane_count() const { return static_cast<int>(panes_.size()); }
    int active_pane() const { return static_cast<int>(active_) + 1; }
    const std::vector<Pane>& panes() const { return panes_; }
    const std::vector<ReviewRow>& review() const { return review_; }
    bool committed() const { return committed_; }
    // The transactions the last accept added, in the order it added them. Only
    // those: what the book already held is not an outcome of the import.
    const std::vector<ledger::Transaction>& accepted() const { return accepted_; }

    int accepted_count() const {
        return static_cast<int>(std::count_if(review_.begin(), review_.end(),
                                              [](const ReviewRow& r) { return r.accepted; }));
    }

    int count_with(const std::string& disposition) const {
        return static_cast<int>(std::count_if(
            review_.begin(), review_.end(),
            [&](const ReviewRow& r) { return r.disposition == disposition; }));
    }

    std::vector<reg::Line> register_lines(int pane_one_based) const {
        const std::size_t index = static_cast<std::size_t>(pane_one_based - 1);
        if (index >= panes_.size() || panes_[index].account.empty()) return {};
        return reg::lines_for(accounts_, transactions_, panes_[index].account);
    }

    std::vector<AccountRow> account_rows(bool show_hidden) const {
        return account_list(accounts_, book_, show_hidden);
    }

    const ledger::Ledger& book() const { return book_; }
    const std::vector<ledger::Transaction>& transactions() const { return transactions_; }

private:
    chart::Chart accounts_;
    ledger::Ledger book_;
    std::vector<ledger::Transaction> transactions_;
    std::vector<Pane> panes_;
    std::size_t active_ = 0;
    std::vector<ReviewRow> review_;
    std::vector<ledger::Transaction> accepted_;
    bool committed_ = false;

    void set_accepted(int line, bool accepted) {
        for (ReviewRow& r : review_)
            if (r.line == line) r.accepted = accepted;
    }
};

}  // namespace ui
