#pragma once
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "account_type.h"
#include "category_search.h"
#include "chart.h"
#include "date.h"
#include "find.h"
#include "import_files.h"
#include "import_target.h"
#include "ledger.h"
#include "money.h"
#include "ofx_import.h"
#include "transaction.h"
#include "transaction_id.h"
#include "payee_rules.h"
#include "qif_import.h"
#include "register_lines.h"
#include "scrolling.h"
#include "split_edit.h"
#include "reconcile.h"
#include "standard_chart.h"
#include "text_types.h"
#include "transaction_edit.h"
#include "transfer_match.h"

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

// The five path segments that were, before groups existed, the only heading a
// real or nominal account had. Importers and the add() that builds missing
// ancestors still create them as ordinary placeholder accounts, so one can sit
// between a group heading and the account it is the heading for -- "Assets"
// between Banking and Checking -- saying nothing a heading does not already
// say. It is left in the chart and in every total; only its own line in the
// list is left out. See the account-list section of UserInterface.spectable.
inline bool is_legacy_root(const std::string& path) {
    return path == "Assets" || path == "Liabilities" || path == "Income" ||
           path == "Expenses" || path == "Equity";
}

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
            if (is_legacy_root(a.path.value())) continue;
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
        // A legacy root is walked through, not counted: it has no line of its
        // own, so nothing should be indented an extra step to sit under it.
        if (!is_legacy_root(parent)) ++depth;
        parent = types::parent_of(types::AccountPath(parent));
    }
    return depth;
}

// ---------------------------------------------------------------------------
// The listings
// ---------------------------------------------------------------------------

// One line of a listing: one node, whatever kind it is. The tree on the left is
// for working -- grouped, totalled, hidden ones left out, categories absent --
// and every one of those is wrong for maintaining the chart, which is what the
// listings are for. See the listings section of UserInterface.spectable.
struct ListingRow {
    std::string path;
    types::AccountType type = types::AccountType::Checking;
    bool placeholder = false;
    bool hidden = false;
    std::string bank;            // from the account's own dialog
    std::string account_number;  // the same
    std::string alias;           // the short way to type a category
    Money balance;
};

// Every account, in path order so a parent reads above its children. A category
// is not here: it is in the other listing.
inline std::vector<ListingRow> account_listing(const chart::Chart& accounts,
                                               const ledger::Ledger& book) {
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

    std::vector<ListingRow> out;
    for (const chart::Account& a : accounts.all()) {
        // Qualified for the same reason account_list above qualifies it.
        if (!ui::group_of(a.type)) continue;
        ListingRow row;
        row.path = a.path.value();
        row.type = a.type;
        row.placeholder = a.placeholder;
        row.hidden = a.hidden;
        row.bank = a.bank;
        row.account_number = a.account_number;
        row.alias = a.alias;
        const auto at = subtree.find(row.path);
        const Money raw = at == subtree.end() ? Money() : at->second;
        row.balance = types::display_sign(a.type) < 0 ? -raw : raw;
        out.push_back(row);
    }
    std::sort(out.begin(), out.end(),
              [](const ListingRow& x, const ListingRow& y) { return x.path < y.path; });
    return out;
}

// Every category, which no other window shows: the tree leaves them out, so
// without this one a category hidden by mistake cannot be found again. Both
// collections are walked, because a nominal node may be held either way.
inline std::vector<ListingRow> category_listing(const chart::Chart& accounts) {
    std::vector<ListingRow> out;
    std::map<std::string, bool> seen;
    const auto take = [&](const std::string& path, types::AccountType type,
                          bool placeholder, bool hidden, const std::string& alias) {
        if (seen.count(path) != 0) return;
        seen[path] = true;
        ListingRow row;
        row.path = path;
        row.type = type;
        row.placeholder = placeholder;
        row.hidden = hidden;
        row.alias = alias;
        out.push_back(row);
    };
    for (const chart::Account& a : accounts.all())
        if (!ui::group_of(a.type))
            take(a.path.value(), a.type, a.placeholder, a.hidden, a.alias);
    for (const chart::Category& c : accounts.all_categories())
        take(c.path.value(), c.type, c.placeholder, c.hidden, c.alias);
    std::sort(out.begin(), out.end(),
              [](const ListingRow& x, const ListingRow& y) { return x.path < y.path; });
    return out;
}

// ---------------------------------------------------------------------------
// Panes
// ---------------------------------------------------------------------------

enum class PaneContent { Register, Report, Empty };

inline std::string to_string(PaneContent c) {
    switch (c) {
        case PaneContent::Register:     return "Register";
        case PaneContent::Report:       return "Report";
        case PaneContent::Empty:        return "Empty";
    }
    return "Empty";
}

// What one tab holds. See the tabs section of UserInterface.spectable.
enum class TabKind { Register, Report };

inline std::string to_string(TabKind k) {
    return k == TabKind::Register ? "Register" : "Report";
}

// One tab of the bar on the right, in the order the tabs are shown. A
// register tab is named by the account's own name; a report tab's Name is
// its title with a number after it for a second one of the same report --
// Base is the title alone, kept so a third can be numbered correctly too.
struct Tab {
    TabKind kind = TabKind::Register;
    std::string account;
    std::string base;
    std::string name;
    // Which book the figures on a report tab were worked out from, as a count
    // of changes, and how many times this tab has been worked out. A register
    // tab needs neither: it is read from the book on every redraw.
    long long worked_out_at = -1;
    int times = 0;
};

// How many times the report on one tab has been worked out. Register tabs
// are not listed. See the report-worked-out section of UserInterface.spectable.
struct ReportWorkRow {
    int position = 1;
    int times = 0;
};

// One row of the open-tabs listing.
struct TabRow {
    int position = 1;
    TabKind kind = TabKind::Register;
    std::string account;
    std::string name;
    bool active = false;
};

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
    // The transactions already in the book that the import decided this row
    // already was, by identity rather than by label. One for a duplicate or a
    // completion, several for a possible, none for a new row. Carried so that
    // the claim can be looked at before the tick is believed: see the
    // show-the-duplicate section of UserInterface.spectable.
    std::vector<std::string> claimed_ids;
    // The qualifications the import wrote on this row, by name. The
    // disposition column reads them after the disposition itself; names
    // rather than an enum, because this file knows nothing of the OFX
    // reader. See the disposition column rule in UserInterface.spectable.
    std::vector<std::string> notes;
};

// What the disposition column reads: the disposition, and after one dash the
// notes that qualify it, in the order the note list declares them. One column,
// because a note is never anything but a qualification of the disposition.
// Named rather than enumerated, because a row carries the names the import
// wrote on the transaction and this file knows nothing of the OFX reader. See
// the disposition column rule in UserInterface.spectable.
inline std::string note_in_words(const std::string& note) {
    if (note == "DetailsDiffer") return "details differ";
    if (note == "CheckNumberSame") return "check number the same";
    if (note == "FitIdAdded") return "identifier added";
    if (note == "PayeeUnmatched") return "payee unmatched";
    if (note == "Uncleared") return "uncleared";
    return {};     // None, and anything a later reader invents
}

inline std::string disposition_reading(const std::string& disposition,
                                       const std::vector<std::string>& notes) {
    // The declared order, so that two notes on one row read the same way round
    // whichever order the import happened to find them in.
    static const std::vector<std::string> declared = {
        "DetailsDiffer", "CheckNumberSame", "FitIdAdded", "PayeeUnmatched", "Uncleared"};
    std::string said;
    for (const std::string& wanted : declared) {
        if (std::find(notes.begin(), notes.end(), wanted) == notes.end()) continue;
        if (!said.empty()) said += ", ";
        said += note_in_words(wanted);
    }
    return said.empty() ? disposition : disposition + " - " + said;
}

// One transaction a review row claimed, read off the book rather than off the
// file: this is what is already there, as the register would show it.
struct DuplicateRow {
    std::string ref;
    types::Date date;
    std::string payee;
    std::string account;
    std::string category;
    Money amount;
    std::string check_no;
    types::ClearedStatus cleared = types::ClearedStatus::Uncleared;
};

// What came of asking to see the claim. Heading is shown above the
// transactions because one of them and three of them mean different things.
struct DuplicateLook {
    bool shown = false;
    std::string heading;
    std::vector<DuplicateRow> rows;
};

struct Pane {
    PaneContent showing = PaneContent::Empty;
    std::string account;
};

// What one tab of the bottom panel is showing. See the bottom-panel section
// of UserInterface.spectable.
enum class BottomTabKind { ImportReview, FindResults };

inline std::string to_string(BottomTabKind k) {
    return k == BottomTabKind::ImportReview ? "ImportReview" : "FindResults";
}

// Whether the panel below the register is open, and how many tabs are in it.
// Open is not a thing to set: the panel is open exactly when it has a tab,
// and ActiveTab is 0 when it has none.
struct BottomPanelState {
    bool open = false;
    int tab_count = 0;
    int active_tab = 0;
};

// One tab of the bottom panel, in the order it was opened.
struct BottomTabRow {
    int tab = 0;
    BottomTabKind kind = BottomTabKind::ImportReview;
    bool active = false;
};

// One item of the menu offered on a selection of register lines.
struct MenuEntry {
    std::string item;
    bool enabled = true;
};

// The question a new book is asked, as it stands. Shown is false before a book
// is made and after the question is answered either way. See the new-book
// section of UserInterface.spectable.
struct NewBookSetup {
    bool shown = false;
    std::string book_name;
    std::string history_file;
};

// What came of answering it. A refusal is the history being unreadable, which
// leaves the question where it was rather than a book with nothing in it.
struct NewBookStarted {
    bool refused = false;
    std::string refusal;
    int transactions = 0;
};

// The File menu, in the order shown. Separators are not named: they group what
// is here and change nothing. There is one import on it, because what the file
// happens to be is a detail of the file, settled by reading it, and not a
// question to put to somebody who only wants last month in the register. See
// the file-menu section of UserInterface.spectable.
inline std::vector<MenuEntry> file_menu_items() {
    return {MenuEntry{"New book...", true}, MenuEntry{"Open book...", true},
            MenuEntry{"Open recent", true},
            MenuEntry{"Import transactions...", true}, MenuEntry{"Exit", true}};
}

// What came of recategorising a selection, counted, because a selection of
// twenty is too many to check by eye. See the register-menu section of
// UserInterface.spectable.
struct Recategorised {
    int changed = 0;
    int refused = 0;
    std::string reason;
};

class Workspace {
public:
    Workspace(const chart::Chart& accounts, const ledger::Ledger& book,
              const std::vector<ledger::Transaction>& transactions,
              const std::vector<ledger::TransactionSplit>& splits = {})
        : accounts_(accounts), book_(book), transactions_(transactions), splits_(splits) {}

    // Accepting an import can create a category the chart handed in at
    // construction never had, so a caller that wants to show a ref the
    // workspace made afterwards has to read it from here, not from its own
    // copy of the chart.
    const chart::Chart& accounts() const { return accounts_; }

    // The book has changed under the workspace. What is being read stays as it
    // is -- which registers are open, which report is on which tab, which of
    // them is in front -- and everything that was about the book as it was
    // goes, because the book it was about is not there any more. The count of
    // changes moves on, which is what tells a report tab that the figures on
    // it were totalled from a book that no longer exists.
    void book_is_now(const chart::Chart& accounts, const ledger::Ledger& book,
                     const std::vector<ledger::Transaction>& transactions,
                     const std::vector<ledger::TransactionSplit>& splits) {
        accounts_ = accounts;
        book_ = book;
        transactions_ = transactions;
        splits_ = splits;
        close_review();
        review_.clear();
        accepted_.clear();
        committed_ = false;
        results_.clear();
        selected_.clear();
        listing_ = Listing::None;
        editor_ = Editor();
        ++changes_;
    }

    // How many times the book has changed under this workspace. It is not a
    // version and nothing is stored against it: the only thing it is ever
    // compared with is the reading a report tab kept.
    long long changes() const { return changes_; }

    // --------------------------------------------------------------- tabs

    // Opens the register of this account in a tab, or returns to it if one is
    // already open -- two tabs on the same register would show the same
    // lines and leave which of them to believe an open question. A new tab
    // goes at the end and becomes active, because selecting an account is
    // asking to read it.
    void select(const std::string& account) {
        for (std::size_t i = 0; i < tabs_.size(); ++i)
            if (tabs_[i].kind == TabKind::Register && tabs_[i].account == account) {
                active_tab_ = i;
                return;
            }
        Tab t;
        t.kind = TabKind::Register;
        t.account = account;
        t.name = types::name_of(types::AccountPath(account));
        tabs_.push_back(t);
        active_tab_ = tabs_.size() - 1;
    }

    // A report opens another tab every time: reading two periods side by
    // side is the reason to have two, and a report that replaced the one
    // already open could not do that. A second tab of the same report gets
    // a 2 after it, because the tab is narrow and cannot say which period it
    // is itself; that is on the controls inside it.
    void open_report(const std::string& title) {
        int count = 0;
        for (const Tab& t : tabs_)
            if (t.kind == TabKind::Report && t.base == title) ++count;
        Tab t;
        t.kind = TabKind::Report;
        t.base = title;
        t.name = count == 0 ? title : title + " " + std::to_string(count + 1);
        tabs_.push_back(t);
        active_tab_ = tabs_.size() - 1;
    }

    // Makes the tab at this position active, whatever kind it is. This is the
    // step a click on the tab bar itself produces: it names a position, not an
    // account, because a click on a report tab has no account to name.
    // select() above is a different thing -- it is how a tab gets opened from
    // the account tree in the first place, and finds its way to an existing
    // one by what is on it rather than where it sits.
    void select_tab(int position_one_based) {
        const std::size_t index = static_cast<std::size_t>(position_one_based - 1);
        if (index < tabs_.size()) active_tab_ = index;
    }

    // Closes the tab at this position. The one to its right becomes active
    // if the one closed was active, or the one to its left if it was last --
    // closing a tab always leaves something showing (or nothing, if it was
    // the only one). Closing a tab that was not active changes nothing about
    // what is being read.
    void close_tab(int position_one_based) {
        const std::size_t index = static_cast<std::size_t>(position_one_based - 1);
        if (index >= tabs_.size()) return;
        // A review belongs to the tab it sits beside; closing that tab leaves
        // nothing for it to sit beside.
        if (review_open_ && tabs_[index].kind == TabKind::Register &&
            tabs_[index].account == review_account_)
            close_review();
        const bool was_active = (index == active_tab_);
        tabs_.erase(tabs_.begin() + static_cast<long>(index));
        selected_.clear();
        if (tabs_.empty()) { active_tab_ = 0; return; }
        if (was_active) active_tab_ = index < tabs_.size() ? index : tabs_.size() - 1;
        else if (index < active_tab_) --active_tab_;
    }

    std::vector<TabRow> open_tabs() const {
        std::vector<TabRow> out;
        for (std::size_t i = 0; i < tabs_.size(); ++i) {
            TabRow row;
            row.position = static_cast<int>(i) + 1;
            row.kind = tabs_[i].kind;
            row.account = tabs_[i].account;
            row.name = tabs_[i].name;
            row.active = (i == active_tab_);
            out.push_back(row);
        }
        return out;
    }

    // ------------------------------------------- working a report out again

    // Whether the report on this tab has to be worked out: it is the tab being
    // shown, and the figures on it were totalled from a book that has changed
    // since. Asked on every redraw -- a tab click, an account selected, a
    // column resized -- which is why the answer is two numbers compared and
    // not a walk of anything.
    bool report_wants_working_out(int position_one_based) const {
        const std::size_t index = static_cast<std::size_t>(position_one_based - 1);
        if (index >= tabs_.size()) return false;
        if (tabs_[index].kind != TabKind::Report) return false;
        // A report nobody is looking at has no figure on screen to be wrong,
        // so the work waits for the click that brings it forward.
        if (index != active_tab_) return false;
        return tabs_[index].worked_out_at != changes_;
    }

    // Said by whoever worked it out, once it has. The count it was worked out
    // at is the book the figures on it are now of.
    void report_worked_out(int position_one_based) {
        const std::size_t index = static_cast<std::size_t>(position_one_based - 1);
        if (index >= tabs_.size()) return;
        if (tabs_[index].kind != TabKind::Report) return;
        tabs_[index].worked_out_at = changes_;
        ++tabs_[index].times;
    }

    std::vector<ReportWorkRow> reports_worked_out() const {
        std::vector<ReportWorkRow> out;
        for (std::size_t i = 0; i < tabs_.size(); ++i) {
            if (tabs_[i].kind != TabKind::Report) continue;
            ReportWorkRow row;
            row.position = static_cast<int>(i) + 1;
            row.times = tabs_[i].times;
            out.push_back(row);
        }
        return out;
    }

    // --------------------------------------------------------- import review

    // Whether there is a register to import into -- the active tab, if it is
    // one -- and which account it is. Refused when the active tab is a
    // report: there is then no register to judge a proposed row against, and
    // importing into whichever one happened to be open last is how a
    // statement ends up in the wrong account.
    struct ActiveRegister {
        bool refused = false;
        std::string reason;
        std::string account;
    };

    ActiveRegister active_register() const {
        if (tabs_.empty() || tabs_[active_tab_].kind != TabKind::Register)
            return {true, "Open the register this statement is for, then import into it.",
                    std::string()};
        return {false, std::string(), tabs_[active_tab_].account};
    }

    // ------------------------------------------- where an import is going

    // What the dialog in front of an import says: the file it is about, the
    // account it proposes, and where that proposal came from. Shown is false
    // when there is nothing to confirm, which is the refusal below.
    struct ImportConfirmation {
        bool shown = false;
        std::string file_name;
        std::string account;
        importing::TargetSource source = importing::TargetSource::NoneOffered;
        // As the file wrote them. They are the only part of the dialog that did
        // not come from this program, which is why they are shown at all; a
        // download that states neither leaves both lines out rather than
        // showing an empty one.
        std::string bank_id;
        std::string account_number;
        // What the bank says the balance is, and the date it is the balance
        // on. A QFX arrives with LEDGERBAL in it and the dialog shows that; a
        // CSV or a QIF arrives with nothing, and the field is there to be
        // typed into off the bank's own website. Text, and empty for no
        // answer, which is not the same as a balance of zero. See asking for
        // the on-line balance in UserInterface.spectable.
        std::string online_balance;
        std::string online_balance_at;
    };

    // A file has been chosen and nothing has been read out of it yet. What the
    // file said about its own account arrives as account_in_file, because what
    // a QFX can say and a CSV cannot is the import's business and not the
    // window's. Everything else is here: what the name of the file fits, and
    // the register in front if one is.
    //
    // Never refused. Where all three have nothing to say the dialog is shown
    // all the same, with an empty account line waiting to be filled in, and the
    // person who has the statement in front of them answers what the program
    // could not work out.
    ActiveRegister choose_import_file(const std::string& file_name,
                                      const std::string& account_in_file,
                                      const std::string& bank_id = std::string(),
                                      const std::string& account_number = std::string(),
                                      const std::string& online_balance = std::string(),
                                      const std::string& online_balance_at = std::string()) {
        const std::string from_name =
            importing::account_from_file_name(accounts_, file_name);
        std::string active;
        if (!tabs_.empty() && tabs_[active_tab_].kind == TabKind::Register)
            active = tabs_[active_tab_].account;

        const importing::Proposal p = importing::propose(account_in_file, from_name, active);
        // Nothing to propose is still a question worth asking, so NoneOffered is
        // now a state of the dialog rather than the reason it never opened.
        // Importing into whichever register happened to be open last is how a
        // statement ends up in the wrong account; asking is not.
        confirming_ = ImportConfirmation{true, file_name, p.account, p.source,
                                         bank_id, account_number,
                                         online_balance, online_balance_at};
        return {false, std::string(), p.account};
    }

    const ImportConfirmation& import_confirmation() const { return confirming_; }

    // The rows of the file, now that the account is settled. Into the account
    // the dialog proposed, or into another one chosen in it; either way the
    // rows are stamped with it, that register becomes the active tab, and the
    // review opens in the panel beneath it.
    void confirm_import(const std::vector<ReviewRow>& rows) {
        // By value: the dialog is put away below, and the account it proposed
        // goes with it.
        const std::string account = confirming_.account;
        confirm_import_into(account, rows);
    }

    void confirm_import_into(const std::string& account,
                             const std::vector<ReviewRow>& rows) {
        confirm_import_with(account, confirming_.online_balance,
                            confirming_.online_balance_at, rows);
    }

    // The dialog, answered: the account, and the on-line balance as it was
    // left -- the file's own figure, corrected, or typed where the file had
    // none. Empty is the answer that there is no figure.
    //
    // Two things happen here and nowhere else. The account is taught the
    // bank's numbers, because answering this dialog is the only moment at
    // which anybody has said that this file and this account belong together,
    // and a learned number kept only in memory would have to be given again
    // after the program next closed. And the balance is put aside until the
    // import is accepted, which is when there is something for it to be a
    // check on. See confirming a download in UserInterface.spectable.
    void confirm_import_with(const std::string& account, const std::string& online_balance,
                             const std::string& online_balance_at,
                             const std::vector<ReviewRow>& rows) {
        stated_balance_ = online_balance;
        stated_balance_at_ = online_balance_at;
        // The rule is ofx::teach_account's: what is blank is filled in, what
        // disagrees is left alone. Nothing learned is nothing written -- a
        // book written for no change is a second of the program not
        // responding, for nothing.
        const ofx::Teaching taught =
            ofx::teach_account(&accounts_, account, confirming_.bank_id,
                               confirming_.account_number);
        if (taught.learned) ++book_writes_;

        std::vector<ReviewRow> stamped = rows;
        for (ReviewRow& r : stamped) r.account = account;
        review_import(stamped, account);
        confirming_ = ImportConfirmation();
    }

    // Cancelled reads nothing: no tab is opened, no panel, no row. The only
    // thing that happened is that the dialog was up, and now it is not.
    void cancel_import_confirmation() { confirming_ = ImportConfirmation(); }

    // An import goes into the account confirmed for it.
    void review_import(const std::vector<ReviewRow>& rows, const std::string& into) {
        review_ = rows;
        review_line_ = 0;
        look_ = DuplicateLook();
        for (ReviewRow& r : review_)
            if (r.disposition != "New") r.accepted = false;
        committed_ = false;
        if (!review_open_) open_bottom_tab(BottomTabKind::ImportReview);
        review_open_ = true;
        review_account_ = into;
        select(into);
        ask_about_transfers(into);
    }

    // --------------------------------------- are these the same payment

    // One pairing put to the person: a row being reviewed and a half the book
    // already has, with both accounts, both dates, both payees as their own
    // files wrote them, and the gap in days -- which is the thing a person
    // most needs in order to disagree. Shown is false when there is nothing
    // left to ask.
    struct TransferQuestion {
        bool shown = false;
        std::string from_account;
        types::Date from_date;
        std::string from_payee;
        std::string to_account;
        types::Date to_date;
        std::string to_payee;
        Money amount;
        long long days_apart = 0;
        // What answering acts on, rather than anything the question shows.
        int line = 0;
        std::string already_here;
        std::string already_here_account;
    };

    const TransferQuestion& transfer_question() const { return asking_; }

    // Yes puts each half on the other's account: the row being reviewed takes
    // the account of the half already here as its category, and that half is
    // noted as needing its own other side rewritten when the review is
    // accepted. Doing one and not the other is the half-done state this
    // question exists to avoid.
    //
    // No changes nothing and is as final as yes, for this pairing: the
    // question does not come back during this review. Leaving it unanswered
    // says the same thing without being asked to say it, which is why an
    // unanswered question is not a refusal.
    void answer_transfer_question(bool same_payment) {
        if (!asking_.shown) return;
        if (same_payment) {
            for (ReviewRow& r : review_)
                if (r.line == asking_.line) r.category = asking_.already_here_account;
            pairings_.push_back({asking_.already_here, review_account_, asking_.line});
            paired_lines_.insert(asking_.line);
            paired_halves_.insert(asking_.already_here);
        }
        next_transfer_question();
    }

    void tick(int line) { set_accepted(line, true); }
    void untick(int line) { set_accepted(line, false); }

    // The accepted rows become transactions, and the pane goes back to being a
    // register of the same account so the result can be read where the import was.
    void accept_import() {
        accepted_.clear();
        // The ref counter is read once and carried, rather than counting the
        // book again for every row -- which was a scan of the whole book per
        // accepted transaction. See decisions.md.
        int next_ref = ledger::transaction_count(transactions_) + 1;
        std::map<int, types::TransactionId> made_for_line;
        for (const ReviewRow& r : review_) {
            if (!r.accepted) continue;
            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef("T" + std::to_string(next_ref));
            ++next_ref;
            t.date = r.date;
            t.payee = types::PayeeName(r.payee);
            t.raw_name = r.raw_name;
            t.memo = r.memo;
            t.check_no = types::CheckNumber(r.check_no);
            if (accounts_.find(r.account) == nullptr)
                accounts_.add(types::AccountPath(r.account), types::AccountType::Checking);
            t.source_account = accounts_.ref_for_path(r.account);
            t.amount = r.amount;
            t.cleared = r.cleared;
            // The identifier belongs to the side the statement is for. The bank
            // has never heard of the category, so the other side carries none.
            ledger::stamp(&t, r.source, r.identifier);
            const Money other_amount = -r.amount;
            if (accounts_.find(r.category) == nullptr)
                accounts_.add(types::AccountPath(r.category),
                              other_amount.cents() < 0 ? types::AccountType::Expense
                                                       : types::AccountType::Income);
            t.destination = accounts_.ref_for_path(r.category);
            transactions_.push_back(t);
            accepted_.push_back(t);
            made_for_line[r.line] = t.id;
            book_.add({r.date, types::AccountPath(r.account), t.amount});
            book_.add({r.date, types::AccountPath(r.category), other_amount});
        }
        // The halves already in the book that an answer paired with a row of
        // this import. Their other side stops being a category and names the
        // account the row was for, which is what keeps a payment out of the
        // spending on both sides rather than on one of them.
        bool paired_anything = false;
        if (!pairings_.empty()) {
            // Where the row was accepted there are two transactions to join,
            // and ledger::match_destinations is the one thing that joins them:
            // each one's destination becomes the other's account and each
            // records the other's id. Without that recorded match the register
            // cannot tell the pair from an ordinary pair of transfers in
            // opposite directions, and shows the money in both registers
            // twice. See a transfer is a line in each of the two registers it
            // names, in TransactionRegister.spectable.
            //
            // Where it was not accepted there is nothing to join, and the half
            // already here is left pointing at the account the answer named,
            // as it was before the match was recorded anywhere.
            std::map<std::string, const Pairing*> by_id;
            for (const Pairing& pairing : pairings_) {
                const auto made = made_for_line.find(pairing.line);
                if (made != made_for_line.end()) {
                    if (ledger::match_destinations(&transactions_,
                                                   types::TransactionId(pairing.transaction_id),
                                                   made->second))
                        paired_anything = true;
                    continue;
                }
                by_id[pairing.transaction_id] = &pairing;
            }
            for (ledger::Transaction& t : transactions_) {
                const auto at = by_id.find(t.id.value());
                if (at == by_id.end()) continue;
                if (accounts_.find(at->second->becomes) == nullptr) continue;
                t.destination = accounts_.ref_for_path(at->second->becomes);
                paired_anything = true;
            }
        }
        // Two transactions that name each other are one movement, and the one
        // place that knows it is ledger::add_to_book -- which posts a matched
        // transfer once rather than twice. The postings added row by row above
        // cannot know, since the half they pair with is only now being
        // rewritten, so a pairing makes the book be built again instead.
        if (paired_anything) rebuild_book();
        pairings_.clear();
        paired_lines_.clear();
        paired_halves_.clear();
        asking_ = TransferQuestion();
        questions_.clear();
        // The balance the dialog was answered with, now that the rows it was
        // an answer about are in the book. Left empty it does nothing: an
        // import nobody has a balance for is the ordinary case for twenty
        // years of history and is not an error.
        if (!stated_balance_.empty() && Money::is_valid(stated_balance_)) {
            const std::optional<types::Date> at = types::Date::from_iso(stated_balance_at_);
            if (at.has_value())
                ofx::record_stated_balance(&accounts_, review_account_,
                                           Money(stated_balance_), *at);
        }
        stated_balance_.clear();
        stated_balance_at_.clear();
        committed_ = true;
        ++changes_;
        // The review was there to compare the import against the register
        // above it. There is nothing left to compare, so its tab closes, and
        // the panel with it if that was the only tab -- which leaves the
        // register, where the result wants reading. See the bottom-panel
        // section of UserInterface.spectable.
        close_review();
    }

    void cancel_import() {
        review_.clear();
        accepted_.clear();
        committed_ = false;
        pairings_.clear();
        paired_lines_.clear();
        paired_halves_.clear();
        asking_ = TransferQuestion();
        questions_.clear();
        close_review();
    }

    // --------------------------------------------- the width of a column

    // A width is clamped to this. A column dragged to no width at all would be
    // invisible, and a column that cannot be seen cannot be dragged back.
    static int minimum_column_width() { return 20; }

    // The width belongs to the column and not to the register: every register
    // is the same columns in the same order, so a width learned in one of them
    // is right in all of them. See the width rule in UserInterface.spectable.
    // Every pixel of a drag is reported, so this records and nothing more.
    // It used to write the book, which on thirty thousand transactions is
    // about a second, forty times over for a divider moved forty pixels --
    // which is what made widening a column look like a program that had hung.
    void resize_column(const std::string& column, int width) {
        widths_[column] = width < minimum_column_width() ? minimum_column_width() : width;
        resize_pending_ = true;
    }

    // The drag has stopped, so the width is now an answer rather than a
    // position on the way to one. True when there is something to write, which
    // is once per drag however many pixels it was. See a drag writes the width
    // once in UserInterface.spectable.
    bool finish_resize() {
        if (!resize_pending_) return false;
        resize_pending_ = false;
        ++book_writes_;
        return true;
    }

    // How many times the book has been asked to be written. Counted because
    // writing it is the expensive thing this program does, so how often is a
    // fact about the program rather than about the machine it is on.
    int book_writes() const { return book_writes_; }

    // Only the columns somebody resized. A column nobody touched keeps whatever
    // width the program gives it, and that number is deliberately not one this
    // program promises anybody.
    const std::map<std::string, int>& column_widths() const { return widths_; }

    void set_column_widths(const std::map<std::string, int>& widths) { widths_ = widths; }

    // ------------------------------------------------- the two listings

    std::vector<ListingRow> account_listing_rows() const {
        return account_listing(accounts_, book_);
    }

    std::vector<ListingRow> category_listing_rows() const {
        return category_listing(accounts_);
    }

    // Which listing is in front. Clicking a line opens the editor of the thing
    // on it, and the two listings hold different things.
    enum class Listing { None, Accounts, Categories };

    void open_account_listing() { listing_ = Listing::Accounts; }
    void open_category_listing() { listing_ = Listing::Categories; }

    // A category added from the listing it will appear in. The listing stays
    // open and the new line is in it, which is the whole point of the button:
    // the way in before was the account dialog, which put the category
    // somewhere the listing had to be reopened to see. See a category is added
    // from the listing in UserInterface.spectable.
    chart::Rejection add_category_from_listing(const types::AccountName& name,
                                               const std::string& under,
                                               types::AccountType type,
                                               const std::string& alias) {
        const chart::Rejection no =
            accounts_.add_category_under(name, under, type, alias);
        if (no.refused) return no;
        ++book_writes_;
        rebuild_book();
        return {};
    }

    // Which editor came up and what it came up on. What the account settings
    // window holds is in Accounts.spectable; what is recorded here is only that
    // a click opened it on the right node.
    struct Editor {
        std::string window;
        std::string target;
    };

    void click_listing_row(const std::string& path) {
        editor_.window = listing_ == Listing::Categories ? "Category settings"
                                                         : "Account settings";
        editor_.target = path;
    }

    // Edit on the tree opens the same window the listing opens: there would be
    // no sense in one account having two settings windows depending on where it
    // was clicked.
    void edit_tree_node(const std::string& node) {
        const bool category = accounts_.find(node) == nullptr &&
                              accounts_.find_category(node) != nullptr;
        editor_.window = category ? "Category settings" : "Account settings";
        editor_.target = node;
    }

    const Editor& editor_opened() const { return editor_; }

    // The menu on a line of the tree. A group heading is not a thing in the
    // book -- it is the kind of account written on the page -- so the only item
    // there is another account of that kind. Delete is offered and refused
    // rather than left off: an account that cannot be deleted raises the
    // question why not, and a greyed item with the reason to hand answers it.
    std::vector<MenuEntry> tree_menu(const std::string& node) const {
        std::vector<MenuEntry> out;
        out.push_back({"New account...", true});
        if (accounts_.find(node) == nullptr) return out;
        out.push_back({"Edit...", true});
        // Reconciling is done to an account that holds money. A category has no
        // statement and nothing to clear, so the item is greyed rather than left
        // off, for the reason Delete is.
        out.push_back({"Reconcile...", recon::reconcilable(accounts_, node)});
        out.push_back({"Hide", true});
        out.push_back({"Delete...", !has_entries(node)});
        return out;
    }

    // Whether anything in the book names this account. The chart holds no
    // transactions, so the question is asked of the transactions themselves
    // rather than of a count somebody has to remember to hand it.
    bool has_entries(const std::string& path) const {
        for (const ledger::Transaction& t : transactions_)
            if (accounts_.path_for_ref(t.source_account) == path ||
                accounts_.path_for_ref(t.destination) == path)
                return true;
        for (const ledger::TransactionSplit& s : splits_)
            if (accounts_.path_for_ref(s.destination) == path) return true;
        return false;
    }

    // What one account's own dialog holds, which the listing shows beside every
    // other account's -- the only place they are read together.
    void change_account_settings(const std::string& path, const std::string& bank,
                                 const std::string& number) {
        const chart::Account* existing = accounts_.find(path);
        if (existing == nullptr) return;
        chart::Account a = *existing;
        a.bank = bank;
        a.account_number = number;
        accounts_.put(a);
    }

    // ------------------------------------------- lines chosen in a register

    // The lines selected, counting from one in the order the register shows.
    // How the selection was made is the table's business: a click takes one,
    // shift extends, control adds, and all three arrive here as a set.
    void select_lines(const std::vector<int>& lines) { selected_ = lines; }

    const std::vector<int>& selected_lines() const { return selected_; }

    // The line of the import review that is selected, counting from one in the
    // order the review is showing. Nought is none.
    void select_review_line(int line) { review_line_ = line; }

    // The item reads as the disposition does: a duplicate names one
    // transaction, a possible names several, and a completion names the
    // payment whose other half was waiting. On a row that claims nothing the
    // item is still there and disabled, because a menu that changes shape from
    // line to line is harder to learn than one item that is sometimes grey.
    // See the menu rule in UserInterface.spectable.
    std::vector<MenuEntry> review_menu_items() const {
        const ReviewRow* row = review_line(review_line_);
        const std::string what = row == nullptr ? "New" : row->disposition;
        if (what == "Possible")
            return {MenuEntry{"Show possible duplicates...", true}};
        if (what == "Completed")
            return {MenuEntry{"Show the payment this completes...", true}};
        const bool anything = row != nullptr && !row->claimed_ids.empty();
        return {MenuEntry{"Show duplicate transaction...", anything}};
    }

    // Asking on a row that claims nothing is not an error: there is simply
    // nothing to show, which is what keeps a double click or a shortcut from
    // opening an empty dialog.
    const DuplicateLook& show_duplicate() {
        look_ = DuplicateLook();
        const ReviewRow* row = review_line(review_line_);
        if (row == nullptr || row->claimed_ids.empty()) return look_;
        for (const std::string& id : row->claimed_ids) {
            const ledger::Transaction* found = transaction_with(id);
            if (found == nullptr) continue;
            DuplicateRow out;
            out.ref = found->ref.value();
            out.date = found->date;
            out.payee = found->payee.value();
            out.account = accounts_.path_for_ref(found->source_account);
            out.category = accounts_.path_for_ref(found->destination);
            out.amount = found->amount;
            out.check_no = found->check_no.value();
            out.cleared = found->cleared;
            look_.rows.push_back(out);
        }
        // Nothing found means every transaction it claimed has since gone, and
        // an empty dialog says less than no dialog at all.
        if (look_.rows.empty()) return look_;
        look_.shown = true;
        look_.heading = row->disposition == "Possible"
                            ? "Could be any of these"
                            : (row->disposition == "Completed"
                                   ? "The payment this completes"
                                   : "Already in the book");
        return look_;
    }

    const DuplicateLook& duplicate_look() const { return look_; }

    // Closing it changes nothing else: the dialog is a look, not a decision.
    // The tick is where the decision is made.
    void close_duplicate_look() { look_ = DuplicateLook(); }

    std::vector<MenuEntry> menu_items() const {
        const bool anything = !selected_.empty() && !tabs_.empty() &&
                              tabs_[active_tab_].kind == TabKind::Register;
        return {MenuEntry{"Recategorize...", anything},
                // One transaction at a time, because the window checks its
                // lines against that transaction's amount and two of them have
                // no one amount between them. A transaction that is already a
                // split is shown rather than reopened, so the item is greyed
                // instead of left off: the answer to "can this be split" is
                // then in the same place either way.
                MenuEntry{"Split...", anything && selection_can_be_split()},
                MenuEntry{"Add payee rule...", anything},
                // The rules a person has already written, run over the lines in
                // front of them. The same rules the import applies, so a month
                // categorised by hand and a month imported come out the same.
                MenuEntry{"Apply payee rules...", anything},
                MenuEntry{"Delete...", anything}};
    }

    // Removes every transaction in the selection entirely -- both of its
    // postings, never one side of it, because deleting one side would leave
    // the book out of balance. Confirming is the window's business; this is
    // what runs once that has already happened. See the deleting section of
    // UserInterface.spectable.
    int delete_selected() {
        const std::vector<reg::Line> lines = register_lines(active_pane());
        std::vector<std::string> refs;
        for (const int line : selected_) {
            const ledger::Transaction* found = transaction_on(lines, line);
            if (found != nullptr) refs.push_back(found->ref.value());
        }
        int removed = 0;
        for (const std::string& ref : refs)
            if (edit::delete_transaction(&transactions_, ref, &splits_).changed) ++removed;
        if (removed > 0) rebuild_book();
        selected_.clear();
        return removed;
    }

    bool selection_can_be_split() const {
        if (selected_.size() != 1) return false;
        const std::vector<reg::Line> lines = register_lines(active_pane());
        const ledger::Transaction* found = transaction_on(lines, selected_.front());
        return found != nullptr && !chart::is_split(found->destination);
    }

    // The lines of the split on the selected register line, which is what the
    // split window shows when it is opened on a transaction already in the
    // book. Empty where that transaction is not a split.
    std::vector<reg::SplitLine> selected_split() const {
        if (selected_.empty()) return {};
        const std::vector<reg::Line> lines = register_lines(active_pane());
        const ledger::Transaction* found = transaction_on(lines, selected_.front());
        if (found == nullptr) return {};
        return reg::split_lines_of(accounts_, transactions_, splits_, found->ref.value());
    }

    struct SplitOutcome {
        bool saved = false;
        std::string reason;
    };

    // The split window saved over a transaction that is already in the
    // register. Nothing about the transaction's own side moves -- the amount
    // it was opened on is the total its lines are checked against. See
    // edit::split_transaction.
    SplitOutcome split_selected(const std::vector<reg::SplitLine>& lines) {
        if (selected_.size() != 1) return {false, "one transaction at a time"};
        const std::vector<reg::Line> shown = register_lines(active_pane());
        const ledger::Transaction* found = transaction_on(shown, selected_.front());
        if (found == nullptr) return {false, "no transaction is selected"};
        ledger::Transaction* writable = by_ref(found->ref.value());
        const edit::SplitWritten done =
            edit::split_transaction(&accounts_, writable, lines);
        if (!done.saved) return {false, done.reason};
        splits_.insert(splits_.end(), done.splits.begin(), done.splits.end());
        rebuild_book();
        return {true, std::string()};
    }

    // Every line in the selection, given the same category. The amounts do not
    // move: a category says where money went and cannot change how much of it
    // there was, which is why a report total that moves afterwards means a
    // posting has been lost.
    //
    // Only an account already in the book may be chosen, and only one that can
    // take a posting. Inventing a category here would make a typo into a new
    // heading on every report from now on.
    Recategorised recategorise_selection(const std::string& category) {
        const std::vector<reg::Line> lines = register_lines(active_pane());
        std::vector<std::string> refs;
        for (const int line : selected_) {
            const ledger::Transaction* found = transaction_on(lines, line);
            if (found != nullptr) refs.push_back(found->ref.value());
        }
        return recategorise(refs, category);
    }

    // The rules, over the selected lines only. Applying them to everything is
    // the right thing after a long import and the wrong thing while working
    // through one statement: a rule written a minute ago is meant for the
    // twenty lines in front of somebody. The pass itself is payees' and is the
    // same one the whole book gets. See applying the payee rules to a
    // selection in UserInterface.spectable.
    payees::BookApplication apply_payee_rules_to_selection(
            const std::vector<payees::Rule>& rules) {
        const std::vector<reg::Line> lines = register_lines(active_pane());
        std::vector<types::TransactionId> ids;
        for (const int line : selected_) {
            const ledger::Transaction* found = transaction_on(lines, line);
            if (found != nullptr) ids.push_back(found->id);
        }
        const payees::BookApplication out =
            payees::apply_to_selection(rules, accounts_, &transactions_, ids);
        if (out.matched > 0) rebuild_book();
        return out;
    }

    // The same by name rather than by line, which is how the report reaches it:
    // a report line is not a register line and the two cannot share a number.
    Recategorised recategorise(const std::vector<std::string>& refs,
                               const std::string& category) {
        Recategorised out;
        const chart::Account* into = accounts_.find(category);
        if (into == nullptr) {
            out.refused = static_cast<int>(refs.size());
            out.reason = category.empty()
                             ? std::string("no category was chosen")
                             : category + " is not an account in this book";
            return out;
        }
        if (into->placeholder) {
            out.refused = static_cast<int>(refs.size());
            out.reason = category + " is a placeholder and takes no postings";
            return out;
        }

        for (const std::string& ref : refs) {
            ledger::Transaction* writable = by_ref(ref);
            if (writable == nullptr) continue;
            const edit::Outcome done =
                edit::assign_category(writable, accounts_, category);
            if (done.changed) {
                ++out.changed;
                continue;
            }
            ++out.refused;
            // The first refusal is the one reported: a selection of twenty with
            // two splits in it needs a sentence, not two.
            if (out.reason.empty()) out.reason = why(*writable, done.reason);
        }
        if (out.changed > 0) rebuild_book();
        return out;
    }

    // --- reconciling an account against what the bank said ------------------
    //
    // The window is opened on an account and then asks for the figures every
    // time it draws, rather than being handed them once: a tick changes a
    // transaction, and the difference closing as lines are ticked is the whole
    // of the work. See "Reconciling an account against what the bank said" in
    // UserInterface.spectable and decision 29 in questions.md.
    void open_reconcile(const std::string& account) {
        recon_account_ =
            recon::reconcilable(accounts_, account) ? account : std::string();
    }

    void close_reconcile() { recon_account_.clear(); }

    recon::Figures reconcile_state() const {
        if (recon_account_.empty()) return {};
        return recon::figures_for(reg::BookIndex(accounts_, transactions_, splits_),
                                  recon_account_);
    }

    std::vector<recon::Item> uncleared_offered() const {
        if (recon_account_.empty()) return {};
        return recon::uncleared_in(reg::BookIndex(accounts_, transactions_, splits_),
                                   recon_account_);
    }

    // A tick is a cleared status and nothing else: no amount moves, and what
    // was already cleared is left alone rather than counted again.
    int clear_in_reconcile(const std::vector<std::string>& refs) {
        int changed = 0;
        for (const std::string& ref : refs) {
            ledger::Transaction* writable = by_ref(ref);
            if (writable == nullptr) continue;
            if (writable->cleared == types::ClearedStatus::Cleared) continue;
            writable->cleared = types::ClearedStatus::Cleared;
            ++changed;
        }
        if (changed > 0) rebuild_book();
        return changed;
    }

    recon::Adjustment balance_adjustment_offered() const {
        return recon::offer_for(reconcile_state());
    }

    // Accepting writes one transaction, uncleared, as asked for. The balance
    // then agrees with the bank and the cleared figure does not -- which is
    // what an uncleared adjustment means, and why it is worth writing rather
    // than quietly moving a figure.
    bool accept_balance_adjustment() {
        const recon::Adjustment offer = balance_adjustment_offered();
        if (!offer.shown) return false;
        const std::string ref =
            "T" + std::to_string(ledger::transaction_count(transactions_) + 1);
        const ledger::Transaction written =
            recon::balance_adjustment(&accounts_, recon_account_, offer, ref);
        if (written.ref.value().empty()) return false;
        transactions_.push_back(written);
        rebuild_book();
        return true;
    }

    // The registers a transaction could be read in: the real accounts it
    // touches, in the order its postings name them. A category is not among
    // them -- categories are reached through a report, which is where this is
    // asked from -- so an ordinary expense offers exactly one.
    std::vector<std::string> registers_offered(const std::string& ref) const {
        std::vector<std::string> out;
        const ledger::Transaction* found = by_ref_const(ref);
        if (found == nullptr) return out;
        for (const ledger::Side& s : ledger::sides_of(*found, splits_)) {
            const chart::Account* a = accounts_.find_account_by_ref(s.account);
            if (a == nullptr) continue;
            if (types::class_of(a->type) != types::AccountClass::Real) continue;
            out.push_back(accounts_.path_for_ref(s.account));
        }
        return out;
    }

    // Opens the one register a transaction can be read in, and selects its
    // line. A transfer touches two and this refuses: neither is more right than
    // the other, so the reader chooses and go_to_transaction_in opens it.
    // Choosing for them would be wrong half the time and silent about it.
    bool go_to_transaction(const std::string& ref) {
        const std::vector<std::string> offered = registers_offered(ref);
        if (offered.size() != 1) return false;
        return go_to_transaction_in(ref, offered.front());
    }

    bool go_to_transaction_in(const std::string& ref, const std::string& account) {
        const std::vector<std::string> offered = registers_offered(ref);
        if (std::find(offered.begin(), offered.end(), account) == offered.end())
            return false;
        select(account);
        // And the line it is, so the window has something to scroll to.
        const std::vector<reg::Line> lines = register_lines(active_pane());
        selected_.clear();
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (lines[i].ref == ref) {
                selected_.push_back(static_cast<int>(i) + 1);
                break;
            }
        return !selected_.empty();
    }

    // The rule the first selected line suggests: the name gives the pattern and
    // the line gives the category. Uncategorized is not offered as one -- a
    // rule carrying it would put every future visit back in the bucket this
    // program exists to empty.
    payees::Rule rule_offered() const {
        const std::vector<reg::Line> lines = register_lines(active_pane());
        if (selected_.empty()) return payees::Rule{};
        const ledger::Transaction* found = transaction_on(lines, selected_.front());
        if (found == nullptr) return payees::Rule{};
        const std::string raw =
            found->raw_name.empty() ? found->payee.value() : found->raw_name;
        payees::Rule out =
            payees::suggest(raw, category_worth_offering(lines, selected_.front()));
        // A category chosen in the dialog's own picker is the answer; what the
        // line suggested was only an opening offer.
        if (!rule_category_.empty()) out.category = rule_category_;
        return out;
    }

    // The rule dialog's category, chosen from the same list the register's
    // recategorise offers, on the same rule -- a category that cannot be
    // chosen there cannot be chosen here either. It was a line to type a path
    // into, which meant a rule could name a category that did not exist and
    // nothing said so until the rule failed to do anything.
    std::vector<chart::Account> rule_categories_offered(const std::string& typed,
                                                        bool include_unused,
                                                        const types::Date& today) const {
        return chart::offered_for_typed(accounts_, book_, typed, include_unused, today);
    }

    // The path, which is what every other part of the program looks a category
    // up by -- not the words on the list, which read Auto:Fuel (Expense).
    void choose_rule_category(const std::string& path) { rule_category_ = path; }

    void forget_rule_category() { rule_category_.clear(); }

    // ---------------------------------------------------------------- asking

    // There is one pane. It is the tab area, and its account is the active
    // tab's. An import used to be a second pane beside it and is now a tab of
    // the panel below, which is a different thing entirely -- so a register's
    // own selection (for the menu, for recategorising) always targets pane 1,
    // and the review has its own tick and untick.
    int pane_count() const { return static_cast<int>(panes().size()); }
    int active_pane() const { return pane_count() == 2 ? 2 : 1; }

    std::vector<Pane> panes() const {
        std::vector<Pane> out;
        Pane first;
        if (tabs_.empty()) {
            first.showing = PaneContent::Empty;
        } else if (tabs_[active_tab_].kind == TabKind::Register) {
            first.showing = PaneContent::Register;
            first.account = tabs_[active_tab_].account;
        } else {
            first.showing = PaneContent::Report;
        }
        out.push_back(first);
        return out;
    }

    // ------------------------------------------------- the bottom panel

    // The panel below the register, open exactly when it has a tab in it.
    BottomPanelState bottom_panel() const {
        BottomPanelState state;
        state.open = !bottom_.empty();
        state.tab_count = static_cast<int>(bottom_.size());
        state.active_tab = bottom_.empty() ? 0 : static_cast<int>(active_bottom_) + 1;
        return state;
    }

    std::vector<BottomTabRow> bottom_panel_tabs() const {
        std::vector<BottomTabRow> out;
        for (std::size_t i = 0; i < bottom_.size(); ++i) {
            BottomTabRow row;
            row.tab = static_cast<int>(i) + 1;
            row.kind = bottom_[i];
            row.active = (i == active_bottom_);
            out.push_back(row);
        }
        return out;
    }

    // Switching which tab of the panel is in front changes nothing else: the
    // register above it is not touched, and neither is what is ticked in the
    // import, because a tab is a thing to look at rather than a mode.
    void select_bottom_tab(int position_one_based) {
        const std::size_t index = static_cast<std::size_t>(position_one_based - 1);
        if (index < bottom_.size()) active_bottom_ = index;
    }

    // Closing a tab closes that tab only. For the results of a find there is
    // nothing else to it. For the import review there is: the panel is the
    // only place the import can be accepted from, so closing it has to mean
    // no, the same as the panel's own Cancel button. See the bottom-panel
    // scenarios in UserInterface.spectable.
    void close_bottom_tab_at(int position_one_based) {
        const std::size_t index = static_cast<std::size_t>(position_one_based - 1);
        if (index >= bottom_.size()) return;
        switch (bottom_[index]) {
            case BottomTabKind::ImportReview:
                cancel_import();
                break;
            case BottomTabKind::FindResults:
                close_bottom_tab(BottomTabKind::FindResults);
                break;
        }
    }

    // ------------------------------------------------------------- find

    // There is one Find dialog, so there is one results tab: performing a
    // find again replaces what is in it rather than opening a second one,
    // and brings it to the front, since looking at the results is the whole
    // reason for having asked.
    void perform_find(const FindCriteria& criteria) {
        const ActiveRegister where = active_register();
        results_ = ui::find(accounts_, transactions_, splits_, criteria,
                            where.refused ? std::string() : where.account);
        for (std::size_t i = 0; i < bottom_.size(); ++i)
            if (bottom_[i] == BottomTabKind::FindResults) {
                active_bottom_ = i;
                return;
            }
        open_bottom_tab(BottomTabKind::FindResults);
    }

    const std::vector<FindResultRow>& find_results() const { return results_; }

    // Going to a result is going to the transaction, in the register the
    // result is read under rather than whichever one is open. The result names
    // that register, so unlike the report's version nothing has to be chosen.
    bool go_to_find_result(int line_one_based) {
        if (line_one_based < 1 || line_one_based > static_cast<int>(results_.size()))
            return false;
        const FindResultRow& chosen = results_[static_cast<std::size_t>(line_one_based - 1)];
        return go_to_transaction_in(chosen.ref, chosen.account);
    }

    // The lines under the split a result names, which is the answer to "which
    // line was that" -- the question a split result raises and the row cannot
    // answer, because the row reads --Split-- and nothing more. Empty where
    // the transaction is not a split, which is also what makes it the test of
    // whether to offer the item at all.
    std::vector<reg::SplitLine> find_result_split(int line_one_based) const {
        if (line_one_based < 1 || line_one_based > static_cast<int>(results_.size()))
            return {};
        const FindResultRow& chosen = results_[static_cast<std::size_t>(line_one_based - 1)];
        return reg::split_lines_of(accounts_, transactions_, splits_, chosen.ref);
    }

    // The menu on a find result. Going to the transaction is on it because a
    // menu that cannot do the obvious thing is a surprise, and Show split is
    // on it where there is a split to show. A transaction with one category
    // raises no question about its split, so the item is left off rather than
    // offered and refused -- unlike the tree's Delete, where the reason a
    // thing cannot be done is worth saying.
    std::vector<MenuEntry> find_result_menu(int line_one_based) const {
        std::vector<MenuEntry> items{MenuEntry{"Go to the transaction...", true}};
        if (!find_result_split(line_one_based).empty())
            items.push_back(MenuEntry{"Show split...", true});
        return items;
    }

    // ----------------------------------------------------- a new book

    // A book has been made and named. The question arrives on its own, after
    // the name and before anything is in the book.
    void make_new_book(const std::string& name) {
        new_book_ = NewBookSetup();
        new_book_.shown = true;
        new_book_.book_name = name;
    }

    const NewBookSetup& new_book_question() const { return new_book_; }

    // A blank history file is the answer "this is an entirely new book", not a
    // cancellation, and it gives the standard chart. A history gives the chart
    // the history has: the categories twenty years of Quicken used are the ones
    // its transactions name, and the standard list beside them would be forty
    // empty categories in every picker for the rest of the book's life. Either
    // way the book ends with a chart in it, because a book with no accounts
    // cannot be typed into.
    //
    // The contents are handed in rather than read here: the window reads the
    // file, the same as it does for every other import.
    NewBookStarted answer_new_book_question(const std::string& history_file,
                                            const std::string& contents = {},
                                            const std::string& into_account = {}) {
        NewBookStarted out;
        new_book_.history_file = history_file;
        if (history_file.empty()) {
            new_book_.shown = false;
            book_is_now(chart::standard_chart(), ledger::Ledger(), {}, {});
            return out;
        }
        qif::Importer importer;
        if (!into_account.empty()) importer.import_into(into_account);
        const qif::Imported imported = importer.run(contents);
        if (imported.refused) {
            // Nothing is put in the book and the question stays: a file that
            // could not be read has not been answered with.
            out.refused = true;
            out.refusal = imported.refusal;
            return out;
        }
        new_book_.shown = false;
        book_is_now(imported.accounts, imported.book, imported.transactions,
                    imported.splits);
        out.transactions = static_cast<int>(imported.transactions.size());
        return out;
    }

    // Whether an import is waiting in the panel, and which account it is for.
    // The window needs both to label the review; see refreshReviewPanel.
    bool review_showing() const { return review_open_; }
    const std::string& review_account() const { return review_account_; }

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
        const std::vector<Pane> rows = panes();
        const std::size_t index = static_cast<std::size_t>(pane_one_based - 1);
        if (index >= rows.size() || rows[index].account.empty()) return {};
        return reg::lines_for(accounts_, transactions_, splits_, rows[index].account);
    }

    std::vector<AccountRow> account_rows(bool show_hidden) const {
        return account_list(accounts_, book_, show_hidden);
    }

    const ledger::Ledger& book() const { return book_; }
    const std::vector<ledger::Transaction>& transactions() const { return transactions_; }
    const std::vector<ledger::TransactionSplit>& splits() const { return splits_; }

private:
    void close_review() {
        review_open_ = false;
        review_account_.clear();
        close_bottom_tab(BottomTabKind::ImportReview);
    }

    // A new tab goes at the end and becomes active, the same rule a register
    // tab follows: what was just opened is what wanted looking at.
    void open_bottom_tab(BottomTabKind kind) {
        bottom_.push_back(kind);
        active_bottom_ = bottom_.size() - 1;
    }

    // Closes the panel's tab of this kind, and the panel itself if it was the
    // last one. The tab to its right takes over, or the one to its left if it
    // was last -- the rule the tab bar above already follows.
    void close_bottom_tab(BottomTabKind kind) {
        for (std::size_t i = 0; i < bottom_.size(); ++i) {
            if (bottom_[i] != kind) continue;
            const bool was_active = (i == active_bottom_);
            bottom_.erase(bottom_.begin() + static_cast<long>(i));
            if (bottom_.empty()) { active_bottom_ = 0; return; }
            if (was_active) active_bottom_ = i < bottom_.size() ? i : bottom_.size() - 1;
            else if (i < active_bottom_) --active_bottom_;
            return;
        }
    }

    // What a line of the register is a transaction of. The line carries the ref,
    // which is what names a transaction in a register.
    const ledger::Transaction* transaction_on(const std::vector<reg::Line>& lines,
                                              int line_one_based) const {
        if (line_one_based < 1 ||
            line_one_based > static_cast<int>(lines.size()))
            return nullptr;
        const std::string& ref =
            lines[static_cast<std::size_t>(line_one_based - 1)].ref;
        for (const ledger::Transaction& t : transactions_)
            if (t.ref.value() == ref) return &t;
        return nullptr;
    }

    const ledger::Transaction* by_ref_const(const std::string& ref) const {
        for (const ledger::Transaction& t : transactions_)
            if (t.ref.value() == ref) return &t;
        return nullptr;
    }

    ledger::Transaction* by_ref(const std::string& ref) {
        for (ledger::Transaction& t : transactions_)
            if (t.ref.value() == ref) return &t;
        return nullptr;
    }

    // Said the way it would be said on screen, naming the transaction rather
    // than the posting, because a person is looking at a line and not at a
    // pair of postings.
    static std::string why(const ledger::Transaction& t, const std::string& reason) {
        if (reason.find("split") != std::string::npos)
            return t.payee.value() + " is a split; open it to change a line";
        return t.payee.value() + ": " + reason;
    }

    // The category of one line, where there is a single one worth repeating in
    // a rule. A split has no one category, and Uncategorized is not a category
    // anybody chose.
    std::string category_worth_offering(const std::vector<reg::Line>& lines,
                                        int line_one_based) const {
        if (line_one_based < 1 ||
            line_one_based > static_cast<int>(lines.size()))
            return {};
        const std::string& category =
            lines[static_cast<std::size_t>(line_one_based - 1)].category;
        if (category == "--Split--") return {};
        if (category == "Expenses:Uncategorized" ||
            category == "Income:Uncategorized")
            return {};
        return category;
    }

    // The book is a second copy of the postings, so it follows them whenever
    // they move. A category change moves one posting to another account and
    // every balance under both of them changes with it.
    void rebuild_book() {
        ledger::Ledger fresh;
        ledger::add_to_book(&fresh, accounts_, transactions_, splits_);
        book_ = fresh;
        ++changes_;
    }

    // The account the reconciling window is open on, empty when it is not.
    std::string recon_account_;

    chart::Chart accounts_;
    ledger::Ledger book_;
    std::vector<ledger::Transaction> transactions_;
    std::vector<ledger::TransactionSplit> splits_;
    std::vector<Tab> tabs_;
    std::size_t active_tab_ = 0;
    ImportConfirmation confirming_;

    // A half already in the book whose other side an accepted review will
    // rewrite, because somebody said the two were one payment.
    struct Pairing {
        std::string transaction_id;
        std::string becomes;
        // Which row of the review the half already here was paired with. The
        // register hides the far side of a matched pair only when the match is
        // recorded on both transactions, so accepting has to know which of the
        // transactions it just made is this half's partner.
        int line = 0;
    };
    std::vector<Pairing> pairings_;
    std::vector<TransferQuestion> questions_;
    std::set<int> paired_lines_;
    std::set<std::string> paired_halves_;
    TransferQuestion asking_;

    // Every pairing this import raises, asked about one at a time. A row can
    // pair with several halves already here and nothing in the file says
    // which, so each pairing is offered in its own right.
    void ask_about_transfers(const std::string& into) {
        questions_.clear();
        pairings_.clear();
        paired_lines_.clear();
        paired_halves_.clear();
        asking_ = TransferQuestion();
        // One index of the book for the whole review rather than a scan of it
        // per row; see the speed note in decisions.md.
        const transfers::Book book(transactions_);
        for (const ReviewRow& r : review_) {
            // Only a row that will be added. A duplicate adds nothing, so
            // there is no half of anything for it to be.
            if (r.disposition != "New") continue;
            for (const transfers::Half& half : transfers::halves_for(
                     into, r.date, r.amount, book, accounts_)) {
                TransferQuestion q;
                // From is the account the money left, whichever side of the
                // pairing that is, and the amount is stated the way a person
                // would say it rather than signed.
                const bool row_is_from = r.amount.cents() < 0;
                q.from_account = row_is_from ? into : half.account;
                q.from_date = row_is_from ? r.date : half.date;
                q.from_payee = row_is_from ? r.payee : half.payee;
                q.to_account = row_is_from ? half.account : into;
                q.to_date = row_is_from ? half.date : r.date;
                q.to_payee = row_is_from ? half.payee : r.payee;
                q.amount = row_is_from ? -r.amount : r.amount;
                q.days_apart = half.days_apart;
                q.line = r.line;
                q.already_here = half.transaction_id;
                q.already_here_account = half.account;
                questions_.push_back(q);
            }
        }
        next_transfer_question();
    }

    // The next pairing worth asking about. A row that has been paired is
    // accounted for and so is the half it was paired with, so the rest of
    // their pairings come off the table rather than being asked.
    void next_transfer_question() {
        asking_ = TransferQuestion();
        while (!questions_.empty()) {
            const TransferQuestion q = questions_.front();
            questions_.erase(questions_.begin());
            if (paired_lines_.count(q.line) != 0) continue;
            if (paired_halves_.count(q.already_here) != 0) continue;
            asking_ = q;
            asking_.shown = true;
            return;
        }
    }
    bool review_open_ = false;
    std::string review_account_;
    Listing listing_ = Listing::None;
    Editor editor_;
    std::map<std::string, int> widths_;
    bool resize_pending_ = false;
    int book_writes_ = 0;
    std::string stated_balance_;
    std::string stated_balance_at_;
    std::string rule_category_;
    std::vector<BottomTabKind> bottom_;
    std::size_t active_bottom_ = 0;
    std::vector<ReviewRow> review_;
    std::vector<FindResultRow> results_;
    std::vector<ledger::Transaction> accepted_;
    std::vector<int> selected_;
    int review_line_ = 0;
    DuplicateLook look_;
    bool committed_ = false;
    long long changes_ = 0;
    NewBookSetup new_book_;

    const ReviewRow* review_line(int line) const {
        for (const ReviewRow& r : review_)
            if (r.line == line) return &r;
        return nullptr;
    }

    const ledger::Transaction* transaction_with(const std::string& id) const {
        for (const ledger::Transaction& t : transactions_)
            if (t.id.value() == id) return &t;
        return nullptr;
    }

    void set_accepted(int line, bool accepted) {
        for (ReviewRow& r : review_)
            if (r.line == line) r.accepted = accepted;
    }
};

}  // namespace ui
