#pragma once
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "common/common.h"
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "posting.h"
#include "register_lines.h"
#include "report.h"
#include "text_types.h"
#include "payee_rules.h"
#include "transaction_edit.h"
#include "ui_model.h"

// Glue for UserInterface.spectable.
//
// The workspace is built the first time something is asked of it, so that the
// Givens can arrive in any order before the first When.
class UserInterfaceGlue {
public:
    // ---------------------------------------------------------------- givens

    void given_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        chart_ = chart::Chart();
        for (const auto& value : values) {
            chart::Account a;
            a.path = types::AccountPath(value.path);
            a.type = types::account_type_from_string(value.type);
            a.placeholder = parse_bool_cell(value.placeholder);
            a.hidden = parse_bool_cell(value.hidden);
            chart_.put(a);
        }
        workspace_.reset();
    }

    void group_into(std::vector<ledger::Transaction>* into, const PostingRowString& value,
                    int total_for_ref) {
        ledger::Transaction* first = nullptr;
        for (ledger::Transaction& t : *into) {
            if (t.ref.value() != value.ref) continue;
            first = &t;
            break;
        }
        const auto on = types::Date::from_iso(value.date);
        const chart::Ref account_ref = chart_.ref_for_path(value.account);
        const Money amount{value.amount};
        const std::string memo = blank(value.memo);
        types::ClearedStatus cleared = types::ClearedStatus::Uncleared;
        if (value.cleared != DNCString && !value.cleared.empty())
            cleared = types::cleared_status_from_string(value.cleared);

        if (first == nullptr) {
            ledger::Transaction made;
            made.id = ledger::new_id();
            made.ref = types::TransactionRef(value.ref);
            if (on) made.date = *on;
            made.payee = types::PayeeName(blank(value.payee));
            made.memo = memo;
            made.source_account = account_ref;
            made.amount = amount;
            made.cleared = cleared;
            into->push_back(made);
            return;
        }

        // Which row this is for its transaction is read from the
        // transaction's own state, not from how many rows have been seen:
        // a third or later row collapses into the same one object as the
        // second once its Destination is set, so counting objects sharing
        // a Ref stops distinguishing a third row from a second.
        if (first->destination.is_none()) {
            ledger::Transaction& t = *first;
            const bool transfer = total_for_ref == 2 && account_ref != t.source_account &&
                                  chart_.is_real(t.source_account) &&
                                  chart_.is_real(account_ref) &&
                                  !ledger::is_unassigned(chart_, t.source_account) &&
                                  !ledger::is_unassigned(chart_, account_ref);
            if (transfer) {
                ledger::Transaction made;
                made.id = ledger::new_id();
                made.ref = t.ref;
                if (on) made.date = *on;
                made.payee = types::PayeeName(blank(value.payee));
                made.memo = memo;
                made.source_account = account_ref;
                made.destination = t.source_account;
                made.amount = amount;
                made.cleared = cleared;
                t.destination = account_ref;
                into->push_back(made);
            } else {
                t.destination = account_ref;
                if (t.memo.empty() && !memo.empty()) t.memo = memo;
                second_row_amount_[t.id.value()] = {amount, memo};
            }
            return;
        }

        ledger::Transaction& t = *first;
        if (!chart::is_split(t.destination)) {
            if (!t.destination.is_none()) {
                ledger::TransactionSplit promoted;
                promoted.transaction_id = t.id;
                promoted.destination = t.destination;
                const auto at = second_row_amount_.find(t.id.value());
                if (at != second_row_amount_.end()) {
                    promoted.amount = at->second.amount;
                    promoted.memo = at->second.memo;
                } else {
                    promoted.amount = -t.amount;
                }
                splits_.push_back(promoted);
            }
            t.destination = chart::split_ref();
        }
        ledger::TransactionSplit s;
        s.transaction_id = t.id;
        s.destination = account_ref;
        s.amount = amount;
        s.memo = memo;
        splits_.push_back(s);
    }

    void given_the_chart_of_accounts_is_as_previous() {}
    void given_transactions_are_as_previous() {}

    // ----------------------------------------------------------------- whens

    void when_account_list_shown() { workspace(); }

    void when_hidden_accounts_shown() { show_hidden_ = true; }

    void when_account_selected(const std::vector<AccountSelectString>& values) {
        for (const auto& value : values) workspace().select(value.path);
    }

    void when_import_reviewed(const std::vector<ImportReviewRowString>& values) {
        // The account is not asked for: whichever register is in front of you
        // is the one the statement is for. See the active-tab rule.
        const ui::Workspace::ActiveRegister active = workspace().active_register();
        ASSERT_FALSE(active.refused) << active.reason;
        last_review_.clear();
        for (const auto& value : values) {
            ui::ReviewRow row;
            row.line = std::stoi(value.line);
            const auto date = types::Date::from_iso(value.date);
            ASSERT_TRUE(date.has_value()) << value.date;
            row.date = *date;
            row.payee = blank(value.payee);
            row.account = value.account;
            row.category = value.category;
            row.amount = Money(value.amount);
            row.disposition = value.disposition;
            // What the file called the row, which the review carries through to
            // the posting rather than losing on the way.
            row.identifier = blank(value.identifier);
            row.source = types::import_source_from_string(value.source);
            row.raw_name = blank(value.rawname);
            row.memo = blank(value.memo);
            row.check_no = blank(value.checkno);
            row.cleared = types::cleared_status_from_string(value.cleared);
            last_review_.push_back(row);
        }
        last_into_ = active.account;
        workspace().review_import(last_review_, last_into_);
    }

    void when_import_reviewed_as_previous() {
        workspace().review_import(last_review_, last_into_);
    }

    void when_review_row_ticked(const std::vector<ReviewRowSelectString>& values) {
        for (const auto& value : values) workspace().tick(std::stoi(value.line));
    }

    void when_review_row_unticked(const std::vector<ReviewRowSelectString>& values) {
        for (const auto& value : values) workspace().untick(std::stoi(value.line));
    }

    void when_import_accepted() { workspace().accept_import(); }
    void when_import_cancelled() { workspace().cancel_import(); }

    // ----------------------------------------------------------------- thens

    void then_account_list_rows_are(const std::vector<AccountListRowString>& values) {
        const std::vector<ui::AccountRow> got = workspace().account_rows(show_hidden_);
        ASSERT_EQ(values.size(), got.size()) << "number of account list rows";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(blank(values[i].group), got[i].group) << "row " << i << " group";
            EXPECT_EQ(blank(values[i].account), got[i].account) << "row " << i << " account";
            EXPECT_EQ(blank(values[i].name), got[i].name) << "row " << i << " name";
            EXPECT_EQ(std::stoi(values[i].indent), got[i].indent) << "row " << i << " indent";
            EXPECT_EQ(Money(values[i].balance).cents(), got[i].balance.cents())
                << "row " << i << " balance";
            EXPECT_EQ(parse_bool_cell(values[i].isheading), got[i].is_heading)
                << "row " << i << " heading";
        }
    }

    void then_account_list_groups_shown_are(const std::vector<GroupShownString>& values) {
        std::vector<std::string> got;
        for (const ui::AccountRow& r : workspace().account_rows(show_hidden_))
            if (r.is_heading) got.push_back(r.group);
        ASSERT_EQ(values.size(), got.size()) << "number of groups shown";
        for (std::size_t i = 0; i < values.size(); ++i)
            EXPECT_EQ(values[i].group, got[i]) << "group " << i;
    }

    void then_panes_are(const std::vector<PaneStateString>& values) {
        const std::vector<ui::Pane>& got = workspace().panes();
        ASSERT_EQ(values.size(), got.size()) << "number of panes";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const int pane = std::stoi(values[i].pane);
            ASSERT_EQ(static_cast<int>(i) + 1, pane) << "panes are listed in order";
            EXPECT_EQ(values[i].showing, ui::to_string(got[i].showing))
                << "pane " << pane << " showing";
            EXPECT_EQ(blank(values[i].account), got[i].account) << "pane " << pane;
            EXPECT_EQ(parse_bool_cell(values[i].active), workspace().active_pane() == pane)
                << "pane " << pane << " active";
        }
    }

    // The register of whichever pane is showing one. Where both are, the active
    // pane is the one being read about.
    void then_register_lines_are(const std::vector<RegisterLineString>& values) {
        const std::vector<reg::Line> got = register_of_interest();
        ASSERT_EQ(values.size(), got.size()) << "number of register lines";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const RegisterLineString& want = values[i];
            if (!dnc(want.ref)) EXPECT_EQ(want.ref, got[i].ref) << "line " << i << " ref";
            if (!dnc(want.date)) EXPECT_EQ(want.date, got[i].date.iso()) << "line " << i;
            if (!dnc(want.payee)) EXPECT_EQ(blank(want.payee), got[i].payee) << "line " << i;
            if (!dnc(want.category))
                EXPECT_EQ(blank(want.category), got[i].category) << "line " << i << " category";
            if (!dnc(want.payment))
                EXPECT_EQ(Money(want.payment).cents(), got[i].payment.cents())
                    << "line " << i << " payment";
            if (!dnc(want.deposit))
                EXPECT_EQ(Money(want.deposit).cents(), got[i].deposit.cents())
                    << "line " << i << " deposit";
            if (!dnc(want.balance))
                EXPECT_EQ(Money(want.balance).cents(), got[i].balance.cents())
                    << "line " << i << " balance";
        }
    }

    void then_import_review_rows_are(const std::vector<ImportReviewRowString>& values) {
        const std::vector<ui::ReviewRow>& got = workspace().review();
        ASSERT_EQ(values.size(), got.size()) << "number of review rows";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(std::stoi(values[i].line), got[i].line) << "row " << i << " line";
            if (!dnc(values[i].payee))
                EXPECT_EQ(blank(values[i].payee), got[i].payee) << "row " << i;
            if (!dnc(values[i].disposition))
                EXPECT_EQ(values[i].disposition, got[i].disposition) << "row " << i;
            if (!dnc(values[i].accepted))
                EXPECT_EQ(parse_bool_cell(values[i].accepted), got[i].accepted)
                    << "row " << i << " accepted";
        }
    }

    void then_import_review_summary_is(const std::vector<ImportReviewSummaryString>& values) {
        for (const auto& value : values) {
            EXPECT_EQ(std::stoi(value.rows),
                      static_cast<int>(workspace().review().size())) << "rows";
            EXPECT_EQ(std::stoi(value.newrows), workspace().count_with("New")) << "new";
            EXPECT_EQ(std::stoi(value.duplicates), workspace().count_with("Duplicate"))
                << "duplicates";
            EXPECT_EQ(std::stoi(value.accepted), workspace().accepted_count()) << "accepted";
            EXPECT_EQ(parse_bool_cell(value.committed), workspace().committed())
                << "committed";
        }
    }

    // ------------------------------------------------------------ the rules

    void examples_datatype_accountgroup(const std::vector<EnumerationValuesString>& values) {
        // Every value is one the list can show, and the seven of them are the
        // whole set, so the rule below has to account for each.
        int many = 0;
        const types::AccountGroup* order = types::account_groups_in_order(&many);
        ASSERT_EQ(static_cast<std::size_t>(many), values.size())
            << "the table and the code do not list the same number of groups";
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            bool known = false;
            for (int at = 0; at < many; ++at)
                if (types::to_string(order[at]) == v.value) known = true;
            EXPECT_TRUE(known) << "unknown group: " << v.value;
        }
        // And in the order the table declares them, which is the order the
        // headings come out in.
        for (int at = 0; at < many && at < static_cast<int>(values.size()); ++at) {
            const EnumerationValuesTyped v =
                EnumerationValuesTyped::from_string_struct(values[static_cast<std::size_t>(at)]);
            EXPECT_EQ(v.value, types::to_string(order[at])) << "group " << at + 1;
        }
    }

    void examples_datatype_panecontent(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            bool known = false;
            for (ui::PaneContent c : {ui::PaneContent::Register, ui::PaneContent::Report,
                                      ui::PaneContent::ImportReview, ui::PaneContent::Empty})
                if (ui::to_string(c) == v.value) known = true;
            EXPECT_TRUE(known) << "unknown pane content: " << v.value;
        }
    }

    void examples_businessrule_which_group_an_account_is_listed_under_and_how_the_heading_reads(
            const std::vector<GroupOfTypeString>& values) {
        for (const auto& value : values) {
            const GroupOfTypeTyped t = GroupOfTypeTyped::from_string_struct(value);
            const auto got = ui::group_of(types::account_type_from_string(t.accounttype));
            EXPECT_EQ(t.listed, got.has_value()) << t.accounttype << " listed";
            // The group and its heading only mean anything for a type that is
            // listed; the table carries the default in the other rows, and an
            // empty heading, which is what a category has.
            if (!t.listed || !got) continue;
            EXPECT_EQ(t.accountgroup, types::to_string(*got)) << t.accounttype;
            EXPECT_EQ(t.heading, types::heading_of(*got)) << t.accounttype << " heading";
        }
    }

private:
    struct PendingSecondRow { Money amount; std::string memo; };
    std::map<std::string, PendingSecondRow> second_row_amount_;
    std::string opened_;
    ui::Workspace::ActiveRegister import_refusal_;
    ui::Recategorised recategorised_;
    payees::Rule offered_;
    chart::Chart chart_;
    ledger::Ledger book_;
    std::vector<ledger::Transaction> transactions_;
    std::vector<ledger::TransactionSplit> splits_;
    std::unique_ptr<ui::Workspace> workspace_;
    bool show_hidden_ = false;
    std::vector<ui::ReviewRow> last_review_;
    std::string last_into_;

    // The report side of things -- clicking a line, selecting several,
    // recategorising from one -- is glue-level and reads chart_/book_/
    // transactions_ directly rather than through the workspace, which only
    // tracks which tab a report opened in. last_report_spec_ defaults wide
    // open rather than to Spec's own 2024 default, so a scenario that never
    // runs an explicit report (recategorising straight from a book that is
    // already open) still sees every posting in it.
    reports::Spec last_report_spec_ = wide_open_spec();
    std::vector<std::string> report_selected_refs_;
    bool report_selection_all_transactions_ = true;
    bool report_mode_ = false;
    int deletion_count_ = 0;

    static reports::Spec wide_open_spec() {
        reports::Spec s;
        s.from = types::Date(1900, 1, 1);
        s.to = types::Date(2999, 12, 31);
        return s;
    }

    ledger::Transaction* find_by_ref(const std::string& ref) {
        for (ledger::Transaction& t : transactions_)
            if (t.ref.value() == ref) return &t;
        return nullptr;
    }

    void rebuild_book() {
        book_ = ledger::Ledger();
        ledger::add_to_book(&book_, chart_, transactions_, splits_);
    }

    ui::Workspace& workspace() {
        if (!workspace_)
            workspace_ = std::make_unique<ui::Workspace>(chart_, book_, transactions_);
        return *workspace_;
    }

    static bool dnc(const std::string& s) { return s == DNCString || s.empty(); }

    static std::string blank(const std::string& s) {
        return (s == "none" || s == DNCString) ? std::string() : s;
    }

    // A Then about the register means the pane that is showing one: the active
    // pane where it is a register, and otherwise the other.
    std::vector<reg::Line> register_of_interest() {
        const std::vector<ui::Pane>& panes = workspace().panes();
        const int active = workspace().active_pane();
        if (panes[static_cast<std::size_t>(active - 1)].showing == ui::PaneContent::Register)
            return workspace().register_lines(active);
        for (int pane = 1; pane <= workspace().pane_count(); ++pane)
            if (panes[static_cast<std::size_t>(pane - 1)].showing == ui::PaneContent::Register)
                return workspace().register_lines(pane);
        return {};
    }
public:

private:
    static std::string listing(const std::vector<AcceptedPostingString>& rows) {
        std::string out;
        for (const auto& row : rows) out += "  " + row.to_string() + "\n";
        return out;
    }

public:

    void when_register_lines_selected(const std::vector<RegisterSelectString>& values) {
        report_mode_ = false;
        std::vector<int> lines;
        for (const auto& value : values) lines.push_back(std::stoi(value.line));
        workspace().select_lines(lines);
    }

    // One transaction selected offers to go to it as well as to recategorise
    // it; several offer only to recategorise, because there is no longer one
    // transaction to go to. A selection with a category line in it, or none
    // at all, offers nothing -- the empty-table case the converter does not
    // generate a step for, so nothing here is reached for it in a test, but
    // the behaviour is still right if it ever is.
    std::vector<ui::MenuEntry> report_menu_items() const {
        std::vector<ui::MenuEntry> out;
        if (!report_selection_all_transactions_ || report_selected_refs_.empty()) return out;
        if (report_selected_refs_.size() == 1) out.push_back({"Go to the transaction...", true});
        out.push_back({"Recategorize...", true});
        return out;
    }

    void then_menu_items_are(const std::vector<MenuItemString>& values) {
        const std::vector<ui::MenuEntry> got =
            report_mode_ ? report_menu_items() : workspace().menu_items();
        ASSERT_EQ(values.size(), got.size()) << "number of menu items";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].item, got[i].item) << "item " << i + 1;
            EXPECT_EQ(parse_bool_cell(values[i].enabled), got[i].enabled)
                << got[i].item << " enabled";
        }
    }

    void when_selection_recategorised(const std::vector<CategoryChoiceString>& values) {
        ASSERT_FALSE(values.empty());
        recategorised_ = workspace().recategorise_selection(values.front().category);
    }

    void then_recategorising_reported_is(const std::vector<RecategorisedString>& values) {
        ASSERT_FALSE(values.empty());
        const RecategorisedString& want = values.front();
        EXPECT_EQ(std::stoi(want.changed), recategorised_.changed) << "changed";
        EXPECT_EQ(std::stoi(want.refused), recategorised_.refused) << "refused";
        // The reason is only checked where the table names one: a scenario
        // about the count should not have to write the sentence out.
        if (want.reason != "none" && want.reason != DNCString)
            EXPECT_EQ(want.reason, recategorised_.reason) << "reason";
    }

    void when_payee_rule_asked_for() { offered_ = workspace().rule_offered(); }

    void then_rule_offered_is(const std::vector<PayeeRuleString>& values) {
        ASSERT_FALSE(values.empty());
        const PayeeRuleString& want = values.front();
        EXPECT_EQ(want.pattern, offered_.pattern) << "pattern";
        EXPECT_EQ(want.matchtype, payees::to_string(offered_.match_type)) << "match type";
        EXPECT_EQ(want.payee, offered_.payee) << "payee";
        // An offer with no category reads as none, which is what the table says
        // when there is nothing worth repeating in a rule.
        EXPECT_EQ(want.category,
                  offered_.category.empty() ? std::string("none") : offered_.category)
            << "category";
        EXPECT_EQ(parse_bool_cell(want.enabled), offered_.enabled) << "enabled";
    }

    // The report runs in the window, not in the workspace -- see the Reports
    // suite for the figures themselves. What the workspace tracks is only
    // which tab it opened in and under what title. The spec and detail are
    // kept here too, though: a click on a report line needs to know what the
    // report was actually showing to work out which transaction a line
    // number landed on.
    void when_report_run(const std::vector<ReportSpecString>& values) {
        for (const auto& value : values) {
            const std::string title = value.groupby == "Account"
                                          ? "Balance sheet"
                                          : "Spending by category";
            workspace().open_report(title);
            reports::Spec s;
            if (const auto f = types::Date::from_iso(value.from)) s.from = *f;
            if (const auto t = types::Date::from_iso(value.to)) s.to = *t;
            if (!value.detail.empty() && value.detail != DNCString)
                s.detail = reports::detail_from_string(value.detail);
            last_report_spec_ = s;
        }
    }

    // A transaction line opens its one register, the way the menu's
    // "Go to the transaction..." does; a category line -- a subtotal or an
    // Other row, neither of which is one transaction -- opens nothing.
    void when_report_line_clicked(const std::vector<ReportLineSelectString>& values) {
        ASSERT_FALSE(values.empty());
        const int line = std::stoi(values.front().line);
        const std::vector<reports::DetailLine> lines =
            reports::detail_report(chart_, book_, transactions_, splits_, last_report_spec_);
        if (line < 1 || line > static_cast<int>(lines.size())) return;
        const reports::DetailLine& chosen = lines[static_cast<std::size_t>(line - 1)];
        if (chosen.kind != reports::LineKind::Transaction) return;
        workspace().go_to_transaction(chosen.ref);
    }

    void then_the_register_line_selected_is(const std::vector<RegisterSelectString>& values) {
        const std::vector<int>& got = workspace().selected_lines();
        ASSERT_EQ(values.size(), got.size()) << "number of lines selected";
        for (std::size_t i = 0; i < values.size(); ++i)
            EXPECT_EQ(std::stoi(values[i].line), got[i]) << "line " << i + 1;
    }

    // The id is never written in a table -- it is a clock reading -- so what
    // is checked is that the transaction has one and that the report's line
    // for it carries the same one the register would.
    void then_the_transaction_id_shown_is(const std::vector<IdShownString>& values) {
        const std::vector<reports::DetailLine> lines =
            reports::detail_report(chart_, book_, transactions_, splits_, last_report_spec_);
        for (const auto& value : values) {
            const ledger::Transaction* t = nullptr;
            for (const ledger::Transaction& one : transactions_)
                if (one.date.iso() == value.date && one.payee.value() == value.payee) {
                    t = &one;
                    break;
                }
            ASSERT_NE(nullptr, t) << value.date << " " << value.payee;
            const reports::DetailLine* on_report = nullptr;
            for (const reports::DetailLine& l : lines)
                if (l.kind == reports::LineKind::Transaction && l.date.iso() == value.date &&
                    l.payee == value.payee) {
                    on_report = &l;
                    break;
                }
            ASSERT_NE(nullptr, on_report) << value.date << " " << value.payee;
            EXPECT_EQ(parse_bool_cell(value.blank), t->id.value().empty())
                << value.date << " " << value.payee;
            EXPECT_EQ(parse_bool_cell(value.thesame), t->id.value() == on_report->id)
                << value.date << " " << value.payee;
        }
    }

public:

    void then_open_tabs_are(const std::vector<OpenTabString>& values) {
        const std::vector<ui::TabRow> got = workspace().open_tabs();
        ASSERT_EQ(values.size(), got.size()) << "number of open tabs";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(std::stoi(values[i].position), got[i].position)
                << "tab " << i + 1 << " position";
            EXPECT_EQ(values[i].kind, ui::to_string(got[i].kind))
                << "tab " << i + 1 << " kind";
            EXPECT_EQ(blank(values[i].account), got[i].account)
                << "tab " << i + 1 << " account";
            EXPECT_EQ(values[i].name, got[i].name) << "tab " << i + 1 << " name";
            EXPECT_EQ(parse_bool_cell(values[i].active), got[i].active)
                << "tab " << i + 1 << " active";
        }
    }

    void when_tab_closed(const std::vector<TabSelectString>& values) {
        for (const auto& value : values) workspace().close_tab(std::stoi(value.position));
    }

    void when_import_asked_for() {
        import_refusal_ = workspace().active_register();
    }

    void then_the_import_is_refused_saying(const std::string& value) {
        EXPECT_TRUE(import_refusal_.refused) << "it was not refused";
        EXPECT_EQ(value, import_refusal_.reason);
    }

    void examples_datatype_tabkind(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            bool known = false;
            for (ui::TabKind k : {ui::TabKind::Register, ui::TabKind::Report})
                if (ui::to_string(k) == v.value) known = true;
            EXPECT_TRUE(known) << "unknown tab kind: " << v.value;
        }
    }


public:

    // A category line -- a subtotal or an Other row -- selected alongside or
    // instead of a transaction line means there is no single transaction and
    // no single category either, so the whole selection is marked unusable
    // rather than quietly dropping the line that did not fit.
    void when_report_lines_selected(const std::vector<ReportMenuSelectString>& values) {
        report_mode_ = true;
        report_selected_refs_.clear();
        report_selection_all_transactions_ = true;
        const std::vector<reports::DetailLine> lines =
            reports::detail_report(chart_, book_, transactions_, splits_, last_report_spec_);
        for (const auto& value : values) {
            const int line = std::stoi(value.line);
            if (line < 1 || line > static_cast<int>(lines.size()) ||
                lines[static_cast<std::size_t>(line - 1)].kind != reports::LineKind::Transaction) {
                report_selection_all_transactions_ = false;
                continue;
            }
            report_selected_refs_.push_back(lines[static_cast<std::size_t>(line - 1)].ref);
        }
    }

    // A transfer has two registers and this opens neither; the Thens say which
    // of the two outcomes was wanted.
    void when_the_transaction_is_opened(const std::vector<TransactionSelectString>& values) {
        ASSERT_FALSE(values.empty());
        opened_ = values.front().ref;
        workspace().go_to_transaction(opened_);
    }

    void when_the_transactions_are_recategorised(
            const std::vector<TransactionRecategoriseString>& values) {
        for (const auto& value : values) {
            ledger::Transaction* t = find_by_ref(value.ref);
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            edit::assign_category(t, chart_, value.category);
        }
        rebuild_book();
        workspace_.reset();   // the workspace's own copies are now stale
    }

    void then_report_rows_are(const std::vector<ReportRowString>& values) {
        const reports::CategoryReport report =
            reports::category_report(chart_, book_, last_report_spec_);
        ASSERT_EQ(values.size(), report.rows.size()) << "number of report rows";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const reports::Row& got = report.rows[i];
            EXPECT_EQ(values[i].account, got.account) << "row " << i;
            EXPECT_EQ(std::stoi(values[i].level), got.level) << got.account << " level";
            EXPECT_EQ(Money(values[i].amount).cents(), got.amount.cents())
                << got.account << " amount";
            EXPECT_EQ(parse_bool_cell(values[i].issubtotal), got.is_subtotal)
                << got.account << " subtotal";
        }
    }

public:

    void then_the_registers_offered_are(const std::vector<RegisterOfferString>& values) {
        const std::vector<std::string> got = workspace().registers_offered(opened_);
        ASSERT_EQ(values.size(), got.size()) << "number of registers offered";
        for (std::size_t i = 0; i < values.size(); ++i)
            EXPECT_EQ(values[i].account, got[i]) << "offer " << i + 1;
    }

    void when_the_transaction_is_opened_in(const std::vector<RegisterChoiceString>& values) {
        ASSERT_FALSE(values.empty());
        opened_ = values.front().ref;
        std::string offered;
        for (const std::string& one : workspace().registers_offered(opened_))
            offered += (offered.empty() ? "" : ", ") + one;
        EXPECT_TRUE(workspace().go_to_transaction_in(opened_, values.front().account))
            << values.front().account << " does not hold " << opened_
            << "; offered: " << (offered.empty() ? "nothing" : offered);
    }

public:

    void when_selection_deleted() { deletion_count_ = workspace().delete_selected(); }

    void then_deletion_count_is(const std::vector<DeletionCountString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(std::stoi(value.removed), deletion_count_) << "transactions removed";
    }

public:

    void when_tab_selected(const std::vector<TabSelectString>& values) {
        for (const auto& value : values) workspace().select_tab(std::stoi(value.position));
    }

public:

    void then_bottom_panel_is(const std::vector<BottomPanelString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_bottom_panel_is";
    }

    void then_bottom_panel_tabs_are(const std::vector<BottomPanelTabRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_bottom_panel_tabs_are";
    }

    void when_find_performed(const std::vector<FindCriteriaString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_find_performed";
    }

    void when_bottom_panel_tab_selected(const std::vector<BottomPanelTabSelectString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_bottom_panel_tab_selected";
    }

    void then_find_results_are(const std::vector<FindResultRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_find_results_are";
    }

    void examples_datatype_bottompaneltabkind(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_bottompaneltabkind";
    }

    void examples_datatype_findscope(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_findscope";
    }


public:

    // The book as the scenario found it, written the way a transaction is
    // stored: one row is one transaction, naming both its sides.
    void given_transactions_are(const std::vector<TransactionString>& values) {
        transactions_.clear();
        splits_.clear();
        for (const auto& value : values) {
            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef(value.ref);
            const auto on = types::Date::from_iso(value.date);
            if (on) t.date = *on;
            t.payee = types::PayeeName(blank(value.payee));
            t.check_no = types::CheckNumber(blank(value.checkno));
            t.memo = blank(value.memo);
            t.source_account = chart_.ref_for_path(blank(value.sourceaccount));
            t.destination = chart_.ref_for_path(blank(value.destination));
            t.amount = Money(value.amount);
            if (value.cleared != DNCString && !value.cleared.empty())
                t.cleared = types::cleared_status_from_string(value.cleared);
            transactions_.push_back(t);
        }
        rebuild_book();
        workspace_.reset();
    }

    // Which transaction a table of split lines belongs to: the one that says
    // Split, or failing that the last one the scenario established.
    types::TransactionId split_owner_id() const {
        for (auto it = transactions_.rbegin(); it != transactions_.rend(); ++it)
            if (chart::is_split(it->destination)) return it->id;
        return transactions_.empty() ? types::TransactionId()
                                     : transactions_.back().id;
    }

    void given_split_lines_are(const std::vector<TransactionSplitString>& values) {
        const types::TransactionId owner = split_owner_id();
        ASSERT_FALSE(owner.value().empty()) << "no transaction for these split lines";
        for (const auto& value : values) {
            ledger::TransactionSplit s;
            s.transaction_id = owner;
            s.destination = chart_.ref_for_path(blank(value.destination));
            s.amount = Money(value.amount);
            s.memo = blank(value.memo);
            splits_.push_back(s);
        }
        rebuild_book();
        workspace_.reset();
    }

public:

    // What accepting the review added, one row per transaction. The identifier
    // is the one the file gave it: at most one per source, and an imported row
    // has one source, so the first is the one.
    void then_accepted_transactions_are(const std::vector<AcceptedTransactionString>& values) {
        const std::vector<ledger::Transaction>& got = workspace().accepted();
        ASSERT_EQ(values.size(), got.size()) << "accepted transactions";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const ledger::Transaction& made = got[i];
            const AcceptedTransactionString& want = values[i];
            const std::string where = "accepted transaction " + std::to_string(i + 1);
            if (want.sourceaccount != DNCString)
                EXPECT_EQ(blank(want.sourceaccount),
                          workspace().accounts().path_for_ref(made.source_account))
                    << where << " source account";
            if (want.destination != DNCString)
                EXPECT_EQ(blank(want.destination),
                          workspace().accounts().path_for_ref(made.destination))
                    << where << " destination";
            if (want.amount != DNCString)
                EXPECT_EQ(Money(want.amount).cents(), made.amount.cents()) << where << " amount";
            if (want.identifier != DNCString)
                EXPECT_EQ(blank(want.identifier),
                          made.import_ids.empty() ? std::string()
                                                  : made.import_ids.front().id)
                    << where << " identifier";
            if (want.source != DNCString && !made.import_ids.empty())
                EXPECT_EQ(want.source, types::to_string(made.import_ids.front().source))
                    << where << " source";
            if (want.rawname != DNCString)
                EXPECT_EQ(blank(want.rawname), made.raw_name) << where << " raw name";
            if (want.memo != DNCString)
                EXPECT_EQ(blank(want.memo), made.memo) << where << " memo";
            if (want.checkno != DNCString)
                EXPECT_EQ(blank(want.checkno), made.check_no.value()) << where << " check number";
            if (want.cleared != DNCString && !want.cleared.empty())
                EXPECT_EQ(want.cleared, types::to_string(made.cleared)) << where << " cleared";
        }
    }

};
