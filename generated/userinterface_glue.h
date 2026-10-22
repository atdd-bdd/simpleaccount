#pragma once
#include <gtest/gtest.h>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <vector>
#include "common/common.h"
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "transaction.h"
#include "register_entry.h"
#include "register_lines.h"
#include "report.h"
#include "text_types.h"
#include "payee_rules.h"
#include "transaction_edit.h"
#include "store_sqlite.h"
#include "ui_model.h"
#include "progress.h"
#include "tags.h"
#include "find.h"
#include "transfer_match.h"

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
            a.path = types::AccountPath(value.path.value);
            a.type = types::account_type_from_string(value.type.value);
            a.placeholder = parse_bool_cell(value.placeholder);
            a.hidden = parse_bool_cell(value.hidden);
            chart_.put(a);
        }
        // A new chart is a new book. A transaction established against the one
        // before it names accounts that are not there any more, and the ids it
        // holds resolve to nothing -- which a save and reopen turns into an
        // empty account path. The Background's chart is inlined first, so a
        // scenario stating its own chart replaces the transactions with it.
        transactions_.clear();
        splits_.clear();
        book_ = ledger::Ledger();
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
        for (const auto& value : values) workspace().select(value.path.value);
    }

    void when_import_reviewed(const std::vector<ImportReviewRowString>& values) {
        // The account is not asked for: whichever register is in front of you
        // is the one the statement is for. See the active-tab rule.
        const ui::Workspace::ActiveRegister active = workspace().active_register();
        ASSERT_FALSE(active.refused) << active.reason;
        rows_from(values, &last_review_);
        claim_onto(&last_review_);
        last_into_ = active.account;
        workspace().review_import(last_review_, last_into_);
    }

    // The rows a table of them states, with no decision about where they go.
    void rows_from(const std::vector<ImportReviewRowString>& values,
                   std::vector<ui::ReviewRow>* into) {
        into->clear();
        for (const auto& value : values) {
            ui::ReviewRow row;
            row.line = std::stoi(value.line);
            const auto date = types::Date::from_iso(value.date);
            ASSERT_TRUE(date.has_value()) << value.date;
            row.date = *date;
            row.payee = blank(value.payee.value);
            row.account = value.account.value;
            row.category = value.category.value;
            row.amount = Money(value.amount.value);
            row.disposition = value.disposition.value;
            // What the file called the row, which the review carries through to
            // the posting rather than losing on the way.
            row.identifier = blank(value.identifier.value);
            row.source = types::import_source_from_string(value.source.value);
            row.raw_name = blank(value.rawname);
            row.memo = blank(value.memo);
            row.check_no = blank(value.checkno.value);
            row.cleared = types::cleared_status_from_string(value.cleared.value);
            into->push_back(row);
        }
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

    void when_import_accepted() {
        workspace().accept_import();
        redraw_reports();
    }

    void when_transfer_question_answered(const std::vector<TransferAnswerGivenString>& values) {
        for (const auto& value : values) {
            // Plain assignment rather than .value: a cell whose DataType is
            // tagged out of the run arrives as a bare string, and the wrapper
            // converts to one implicitly, so this reads either shape.
            const std::string answer = value.answer;
            ASSERT_TRUE(answer == "Yes" || answer == "No" || answer == "YesAndRemember")
                << "not an answer to the transfer question: " << answer;
            workspace().answer_transfer_question(answer != "No");
        }
    }
    void when_import_cancelled() { workspace().cancel_import(); }

    // ----------------------------------------------------------------- thens

    void then_account_list_rows_are(const std::vector<AccountListRowString>& values) {
        const std::vector<ui::AccountRow> got = workspace().account_rows(show_hidden_);
        ASSERT_EQ(values.size(), got.size()) << "number of account list rows";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(blank(values[i].group), got[i].group) << "row " << i << " group";
            EXPECT_EQ(blank(values[i].account.value), got[i].account) << "row " << i << " account";
            EXPECT_EQ(blank(values[i].name), got[i].name) << "row " << i << " name";
            EXPECT_EQ(std::stoi(values[i].indent), got[i].indent) << "row " << i << " indent";
            EXPECT_EQ(Money(values[i].balance.value).cents(), got[i].balance.cents())
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
            EXPECT_EQ(values[i].showing.value, ui::to_string(got[i].showing))
                << "pane " << pane << " showing";
            EXPECT_EQ(blank(values[i].account.value), got[i].account) << "pane " << pane;
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
            if (!dnc(want.ref.value)) EXPECT_EQ(want.ref.value, got[i].ref) << "line " << i << " ref";
            if (!dnc(want.date)) EXPECT_EQ(want.date, got[i].date.iso()) << "line " << i;
            if (!dnc(want.payee.value)) EXPECT_EQ(blank(want.payee.value), got[i].payee) << "line " << i;
            if (!dnc(want.category))
                EXPECT_EQ(blank(want.category), got[i].category) << "line " << i << " category";
            if (!dnc(want.payment.value))
                EXPECT_EQ(Money(want.payment.value).cents(), got[i].payment.cents())
                    << "line " << i << " payment";
            if (!dnc(want.deposit.value))
                EXPECT_EQ(Money(want.deposit.value).cents(), got[i].deposit.cents())
                    << "line " << i << " deposit";
            if (!dnc(want.balance.value))
                EXPECT_EQ(Money(want.balance.value).cents(), got[i].balance.cents())
                    << "line " << i << " balance";
        }
    }

    // A question that is not being asked says nothing else, so a table that
    // states Shown false is asking about that and no more. The full table is
    // compared field by field, because every one of them is something a person
    // reads before answering.
    void then_transfer_question_is(const std::vector<TransferQuestionString>& values) {
        ASSERT_FALSE(values.empty());
        const TransferQuestionString& want = values.front();
        const ui::Workspace::TransferQuestion& got = workspace().transfer_question();
        const bool shown = parse_bool_cell(want.shown);
        ASSERT_EQ(shown, got.shown) << "whether a transfer question is being asked";
        if (!shown) return;
        EXPECT_EQ(want.fromaccount.value, got.from_account) << "from account";
        EXPECT_EQ(want.fromdate, got.from_date.iso()) << "from date";
        EXPECT_EQ(blank(want.frompayee), got.from_payee) << "from payee";
        EXPECT_EQ(want.toaccount.value, got.to_account) << "to account";
        EXPECT_EQ(want.todate, got.to_date.iso()) << "to date";
        EXPECT_EQ(blank(want.topayee), got.to_payee) << "to payee";
        EXPECT_EQ(Money(want.amount.value).cents(), got.amount.cents()) << "amount";
        EXPECT_EQ(std::stoll(want.daysapart), got.days_apart) << "days apart";
    }

    void then_import_review_rows_are(const std::vector<ImportReviewRowString>& values) {
        const std::vector<ui::ReviewRow>& got = workspace().review();
        ASSERT_EQ(values.size(), got.size()) << "number of review rows";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(std::stoi(values[i].line), got[i].line) << "row " << i << " line";
            if (!dnc(values[i].payee.value))
                EXPECT_EQ(blank(values[i].payee.value), got[i].payee) << "row " << i;
            // Which account the row is for, which a confirmed import fills in
            // from the dialog rather than from the file. A blank states
            // nothing: no row is ever for no account, so there is nothing
            // there to compare against.
            if (!dnc(values[i].account.value) && !blank(values[i].account.value).empty())
                EXPECT_EQ(values[i].account.value, got[i].account) << "row " << i << " account";
            if (!dnc(values[i].disposition.value))
                EXPECT_EQ(values[i].disposition.value, got[i].disposition) << "row " << i;
            // Where the other side of the row goes. A blank states nothing:
            // the column is left out of most of these tables, and a row
            // waiting to be reviewed may have nothing there yet.
            if (!dnc(values[i].category.value) && !blank(values[i].category.value).empty())
                EXPECT_EQ(values[i].category.value, got[i].category) << "row " << i << " category";
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
                                      ui::PaneContent::Empty})
                if (ui::to_string(c) == v.value) known = true;
            EXPECT_TRUE(known) << "unknown pane content: " << v.value;
        }
    }

    void examples_businessrule_which_group_an_account_is_listed_under_and_how_the_heading_reads(
            const std::vector<GroupOfTypeString>& values) {
        for (const auto& value : values) {
            const GroupOfTypeTyped t = GroupOfTypeTyped::from_string_struct(value);
            const auto got = ui::group_of(types::account_type_from_string(t.accounttype.text));
            EXPECT_EQ(t.listed, got.has_value()) << t.accounttype.text << " listed";
            // The group and its heading only mean anything for a type that is
            // listed; the table carries the default in the other rows, and an
            // empty heading, which is what a category has.
            if (!t.listed || !got) continue;
            EXPECT_EQ(t.accountgroup.text, types::to_string(*got)) << t.accounttype.text;
            EXPECT_EQ(t.heading, types::heading_of(*got)) << t.accounttype.text << " heading";
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
    std::vector<ui::ReviewRow> file_rows_;
    std::string last_into_;

    // The report side of things -- clicking a line, selecting several,
    // recategorising from one -- is glue-level and reads chart_/book_/
    // transactions_ directly rather than through the workspace, which only
    // tracks which tab a report opened in. last_report_spec_ defaults wide
    // open rather than to Spec's own 2024 default, so a scenario that never
    // runs an explicit report (recategorising straight from a book that is
    // already open) still sees every posting in it.
    reports::Spec last_report_spec_ = wide_open_spec();

    // What each report tab is showing: the spec it was asked for, and the
    // figures it was last worked out with. The window keeps these on the tab
    // widget -- the controls and the table -- and this is the same thing. A
    // report is not totalled to answer a Then: the figures asserted are the
    // ones the tab is holding, which is the whole of what the rule is about.
    std::map<int, reports::Spec> report_spec_at_;
    std::map<int, reports::CategoryReport> report_rows_at_;
    int last_report_position_ = 0;
    std::vector<std::string> report_selected_refs_;
    bool report_selection_all_transactions_ = true;
    bool report_mode_ = false;
    int deletion_count_ = 0;

    // What the window does on every redraw: for each report tab, ask the
    // workspace whether the figures on it are the book's as it stands, and
    // work it out again only where they are not. See MainWindow::refreshPane.
    void redraw_reports() {
        for (const ui::TabRow& row : workspace().open_tabs()) {
            if (row.kind != ui::TabKind::Report) continue;
            if (!workspace().report_wants_working_out(row.position)) continue;
            report_rows_at_[row.position] =
                reports::category_report(workspace().accounts(), workspace().book(),
                                         report_spec_at_[row.position]);
            workspace().report_worked_out(row.position);
        }
    }

    // The book has moved. The workspace is handed it rather than built again,
    // which is what MainWindow::rebuildWorkspace does: a new workspace has no
    // tabs, and the report tab is the whole of what these scenarios watch.
    void book_changed() {
        rebuild_book();
        if (workspace_) workspace_->book_is_now(chart_, book_, transactions_, splits_);
        redraw_reports();
    }

    // Which report tab a Then about report rows is about: the one in front, or
    // failing that the last one a report was run on -- a line clicked in a
    // report opens a register, which leaves no report tab active.
    int report_position_showing() {
        for (const ui::TabRow& row : workspace().open_tabs())
            if (row.kind == ui::TabKind::Report && row.active) return row.position;
        return last_report_position_;
    }

    reports::CategoryReport report_showing() {
        const auto found = report_rows_at_.find(report_position_showing());
        if (found != report_rows_at_.end()) return found->second;
        return reports::category_report(workspace().accounts(), workspace().book(),
                                        last_report_spec_);
    }

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

    // ----------------------------------------------- the width of a column

public:

    void when_register_column_resized(const std::vector<RegisterColumnWidthString>& values) {
        for (const RegisterColumnWidthString& value : values)
            workspace().resize_column(value.column, std::stoi(value.width));
    }

    // The width the register is showing, whichever register that is: the width
    // is the column's, so there is one answer rather than one per account.
    void then_register_column_widths_are(const std::vector<RegisterColumnWidthString>& values) {
        const std::map<std::string, int>& got = workspace().column_widths();
        for (const RegisterColumnWidthString& want : values) {
            const auto at = got.find(want.column);
            ASSERT_NE(got.end(), at) << want.column << " has no width";
            EXPECT_EQ(std::stoi(want.width), at->second) << want.column << " width";
        }
    }

    // Only what somebody resized, so the size of the set is part of the answer.
    void then_recorded_column_widths_are(const std::vector<RegisterColumnWidthString>& values) {
        const std::map<std::string, int>& got = workspace().column_widths();
        ASSERT_EQ(values.size(), got.size()) << "number of columns recorded";
        for (const RegisterColumnWidthString& want : values) {
            const auto at = got.find(want.column);
            ASSERT_NE(got.end(), at) << want.column << " was not recorded";
            EXPECT_EQ(std::stoi(want.width), at->second) << want.column << " width";
        }
    }

    void examples_businessrule_a_column_cannot_be_dragged_away_to_nothing(
            const std::vector<WidthRequestedString>& values) {
        for (const WidthRequestedString& value : values) {
            ui::Workspace fixture(chart_, book_, transactions_);
            fixture.resize_column("Payee", std::stoi(value.requested));
            const auto at = fixture.column_widths().find("Payee");
            ASSERT_NE(fixture.column_widths().end(), at);
            EXPECT_EQ(std::stoi(value.stored), at->second)
                << value.requested << " -- " << value.notes;
        }
    }

    // Written to a real file, closed, and read back -- a width that only
    // survives while the program holds it has not been kept with the book.
    void when_the_book_is_saved_and_opened_again() {
        const std::string path =
            (std::filesystem::temp_directory_path() / "sa_ui_widths.sadb").string();
        std::error_code ignored;
        std::filesystem::remove(path, ignored);

        store::Book book;
        store::Failure no = store::Book::create(path, "Widths", &book);
        ASSERT_FALSE(no.refused) << no.reason;
        no = book.write(chart_, transactions_, {}, splits_, workspace().column_widths());
        ASSERT_FALSE(no.refused) << no.reason;

        book = store::Book();
        no = store::Book::open(path, &book);
        ASSERT_FALSE(no.refused) << no.reason;

        chart_ = chart::Chart();
        book_ = ledger::Ledger();
        transactions_.clear();
        splits_.clear();
        std::map<std::string, int> widths;
        no = book.read(&chart_, &book_, &transactions_, nullptr, &splits_, &widths);
        ASSERT_FALSE(no.refused) << no.reason;

        workspace_.reset();
        workspace().set_column_widths(widths);
    }

    // --------------------------------------------------------- the listings

public:

    void when_account_listing_opened() { workspace().open_account_listing(); }

    void when_category_listing_opened() { workspace().open_category_listing(); }

    void then_account_listing_rows_are(const std::vector<AccountListingRowString>& values) {
        const std::vector<ui::ListingRow> got = workspace().account_listing_rows();
        ASSERT_EQ(values.size(), got.size()) << "number of lines in the account listing";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const AccountListingRowString& want = values[i];
            EXPECT_EQ(want.path.value, got[i].path) << "line " << i + 1;
            if (!dnc(want.type.value))
                EXPECT_EQ(want.type.value, types::to_string(got[i].type)) << want.path.value << " type";
            if (!dnc(want.placeholder))
                EXPECT_EQ(parse_bool_cell(want.placeholder), got[i].placeholder)
                    << want.path.value << " placeholder";
            if (!dnc(want.hidden))
                EXPECT_EQ(parse_bool_cell(want.hidden), got[i].hidden)
                    << want.path.value << " hidden";
            if (!dnc(want.bank))
                EXPECT_EQ(blank(want.bank), got[i].bank) << want.path.value << " bank";
            if (!dnc(want.accountnumber))
                EXPECT_EQ(blank(want.accountnumber), got[i].account_number)
                    << want.path.value << " account number";
            if (!dnc(want.balance.value))
                EXPECT_EQ(Money(want.balance.value).cents(), got[i].balance.cents())
                    << want.path.value << " balance";
        }
    }

    void then_category_listing_rows_are(const std::vector<CategoryListingRowString>& values) {
        const std::vector<ui::ListingRow> got = workspace().category_listing_rows();
        ASSERT_EQ(values.size(), got.size()) << "number of lines in the category listing";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const CategoryListingRowString& want = values[i];
            EXPECT_EQ(want.path.value, got[i].path) << "line " << i + 1;
            if (!dnc(want.type.value))
                EXPECT_EQ(want.type.value, types::to_string(got[i].type)) << want.path.value << " type";
            if (!dnc(want.placeholder))
                EXPECT_EQ(parse_bool_cell(want.placeholder), got[i].placeholder)
                    << want.path.value << " placeholder";
            if (!dnc(want.hidden))
                EXPECT_EQ(parse_bool_cell(want.hidden), got[i].hidden)
                    << want.path.value << " hidden";
            if (!dnc(want.alias))
                EXPECT_EQ(blank(want.alias), got[i].alias) << want.path.value << " alias";
        }
    }

    void when_listing_row_clicked(const std::vector<ListingRowSelectString>& values) {
        ASSERT_FALSE(values.empty());
        workspace().click_listing_row(values.front().path.value);
    }

    void when_account_list_node_right_clicked(const std::vector<TreeNodeString>& values) {
        ASSERT_FALSE(values.empty());
        tree_node_ = values.front().node;
    }

    void when_node_edit_chosen(const std::vector<TreeNodeString>& values) {
        ASSERT_FALSE(values.empty());
        workspace().edit_tree_node(values.front().node);
    }

    void then_the_editor_opened_is(const std::vector<EditorOpenedString>& values) {
        const ui::Workspace::Editor& got = workspace().editor_opened();
        for (const EditorOpenedString& want : values) {
            EXPECT_EQ(want.window, got.window) << "which editor opened";
            EXPECT_EQ(blank(want.target), got.target) << "what it opened on";
        }
    }

    // The account's own dialog, the same as in Accounts.spectable -- here only
    // so that the listing has something to show beside the account.
    void when_account_settings_changed(const std::vector<AccountSettingsString>& values) {
        for (const AccountSettingsString& value : values)
            workspace().change_account_settings(value.path.value, blank(value.bank),
                                                blank(value.accountnumber));
    }

private:
    std::string tree_node_;
    // What a Given said the history file holds, and which account a QIF with
    // no account block of its own is read into.
    std::string file_text_;
    std::string import_target_;
    // The folder as found, and what the dialog made of it.
    std::vector<ui::FileOffered> in_folder_;
    std::vector<ui::FileOffered> offered_files_;
    // The find result whose menu a scenario asked for; zero until one is
    // right-clicked, lines being one-based.
    int find_result_line_ = 0;
    std::vector<reg::SplitLine> split_shown_;

    // The splits go in too. They are the fourth argument and it has a default,
    // so leaving them out compiled and read as a workspace over the same book
    // -- but a workspace with no split lines answers a find for the amount on
    // one of them with nothing, and shows a register line that says --Split--
    // and stands for nothing. The register reads the destination ref, which is
    // why this was invisible until a find looked past it.
    ui::Workspace& workspace() {
        if (!workspace_)
            workspace_ =
                std::make_unique<ui::Workspace>(chart_, book_, transactions_, splits_);
        return *workspace_;
    }

    static bool dnc(const std::string& s) { return s == DNCString || s.empty(); }

    static std::string blank(const std::string& s) {
        return (s == "none" || s == DNCString) ? std::string() : s;
    }

    // A table cell cannot hold a leading or trailing blank, so a value that is
    // one is written in double quotes in the spec and the quotes come off here.
    // Safe on every cell: one that is not quoted is returned as it stands.
    static std::string unquoted(const std::string& s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return s.substr(1, s.size() - 2);
        return s;
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
        // Four menus reached by one step: the tree's, a find result's, the
        // report's and the register's. A right-clicked tree node means the
        // tree's and a right-clicked result means that result's, no scenario
        // having any reason to ask for two of them at once.
        const std::vector<ui::MenuEntry> got =
            !tree_node_.empty()
                ? workspace().tree_menu(tree_node_)
                : (find_result_line_ != 0
                       ? workspace().find_result_menu(find_result_line_)
                       : (report_mode_ ? report_menu_items()
                                       : workspace().menu_items()));
        ASSERT_EQ(values.size(), got.size()) << "number of menu items";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].item, got[i].item) << "item " << i + 1;
            EXPECT_EQ(parse_bool_cell(values[i].enabled), got[i].enabled)
                << got[i].item << " enabled";
        }
    }

    void when_selection_recategorised(const std::vector<CategoryChoiceString>& values) {
        ASSERT_FALSE(values.empty());
        recategorised_ = workspace().recategorise_selection(values.front().category.value);
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
        EXPECT_EQ(want.matchtype.value, payees::to_string(offered_.match_type)) << "match type";
        EXPECT_EQ(want.payee.value, offered_.payee) << "payee";
        // An offer with no category reads as none, which is what the table says
        // when there is nothing worth repeating in a rule.
        EXPECT_EQ(want.category.value,
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
            const std::string title = value.groupby.value == "Account"
                                          ? "Balance sheet"
                                          : "Spending by category";
            workspace().open_report(title);
            reports::Spec s;
            if (const auto f = types::Date::from_iso(value.from)) s.from = *f;
            if (const auto t = types::Date::from_iso(value.to)) s.to = *t;
            if (!value.detail.value.empty() && value.detail.value != DNCString)
                s.detail = reports::detail_from_string(value.detail.value);
            last_report_spec_ = s;
            // The tab it opened in is the last one, and it is the one in
            // front. The report itself is worked out by the redraw that
            // follows, the same as every other time it is worked out.
            last_report_position_ = static_cast<int>(workspace().open_tabs().size());
            report_spec_at_[last_report_position_] = s;
        }
        redraw_reports();
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
                if (one.date.iso() == value.date && one.payee.value() == value.payee.value) {
                    t = &one;
                    break;
                }
            ASSERT_NE(nullptr, t) << value.date << " " << value.payee.value;
            const reports::DetailLine* on_report = nullptr;
            for (const reports::DetailLine& l : lines)
                if (l.kind == reports::LineKind::Transaction && l.date.iso() == value.date &&
                    l.payee == value.payee.value) {
                    on_report = &l;
                    break;
                }
            ASSERT_NE(nullptr, on_report) << value.date << " " << value.payee.value;
            EXPECT_EQ(parse_bool_cell(value.blank), t->id.value().empty())
                << value.date << " " << value.payee.value;
            EXPECT_EQ(parse_bool_cell(value.thesame), t->id.value() == on_report->id)
                << value.date << " " << value.payee.value;
        }
    }

public:

    void then_open_tabs_are(const std::vector<OpenTabString>& values) {
        const std::vector<ui::TabRow> got = workspace().open_tabs();
        ASSERT_EQ(values.size(), got.size()) << "number of open tabs";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(std::stoi(values[i].position), got[i].position)
                << "tab " << i + 1 << " position";
            EXPECT_EQ(values[i].kind.value, ui::to_string(got[i].kind))
                << "tab " << i + 1 << " kind";
            EXPECT_EQ(blank(values[i].account.value), got[i].account)
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
        opened_ = values.front().ref.value;
        workspace().go_to_transaction(opened_);
    }

    void when_the_transactions_are_recategorised(
            const std::vector<TransactionRecategoriseString>& values) {
        for (const auto& value : values) {
            ledger::Transaction* t = find_by_ref(value.ref.value);
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref.value;
            edit::assign_category(t, chart_, value.category.value);
        }
        // One step is one change, whether it names one transaction or twenty.
        book_changed();
    }

    void then_report_rows_are(const std::vector<ReportRowString>& values) {
        const reports::CategoryReport report = report_showing();
        ASSERT_EQ(values.size(), report.rows.size()) << "number of report rows";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const reports::Row& got = report.rows[i];
            EXPECT_EQ(values[i].account.value, got.account) << "row " << i;
            EXPECT_EQ(std::stoi(values[i].level), got.level) << got.account << " level";
            EXPECT_EQ(Money(values[i].amount.value).cents(), got.amount.cents())
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
            EXPECT_EQ(values[i].account.value, got[i]) << "offer " << i + 1;
    }

    void when_the_transaction_is_opened_in(const std::vector<RegisterChoiceString>& values) {
        ASSERT_FALSE(values.empty());
        opened_ = values.front().ref.value;
        std::string offered;
        for (const std::string& one : workspace().registers_offered(opened_))
            offered += (offered.empty() ? "" : ", ") + one;
        EXPECT_TRUE(workspace().go_to_transaction_in(opened_, values.front().account.value))
            << values.front().account.value << " does not hold " << opened_
            << "; offered: " << (offered.empty() ? "nothing" : offered);
    }

public:

    // The deletion happens in the workspace's own copies, so a report after it
    // is worked out from the workspace's book rather than this glue's -- which
    // is what redraw_reports() reads, and why it is right.
    void when_selection_deleted() {
        deletion_count_ = workspace().delete_selected();
        redraw_reports();
    }

    void then_deletion_count_is(const std::vector<DeletionCountString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(std::stoi(value.removed), deletion_count_) << "transactions removed";
    }

public:

    void when_tab_selected(const std::vector<TabSelectString>& values) {
        for (const auto& value : values) {
            workspace().select_tab(std::stoi(value.position));
            // A click is a redraw, which is where a report that was behind is
            // worked out -- and where one that was already right is not.
            redraw_reports();
        }
    }

public:

    void then_bottom_panel_is(const std::vector<BottomPanelString>& values) {
        const ui::BottomPanelState got = workspace().bottom_panel();
        for (const BottomPanelString& want : values) {
            EXPECT_EQ(parse_bool_cell(want.open), got.open) << "bottom panel open";
            EXPECT_EQ(std::stoi(want.tabcount), got.tab_count) << "bottom panel tab count";
            EXPECT_EQ(std::stoi(want.activetab), got.active_tab) << "bottom panel active tab";
        }
    }

    void then_bottom_panel_tabs_are(const std::vector<BottomPanelTabRowString>& values) {
        const std::vector<ui::BottomTabRow> got = workspace().bottom_panel_tabs();
        ASSERT_EQ(values.size(), got.size()) << "number of bottom panel tabs";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const int tab = std::stoi(values[i].tab);
            ASSERT_EQ(static_cast<int>(i) + 1, tab) << "tabs are listed in the order opened";
            EXPECT_EQ(tab, got[i].tab) << "tab " << tab;
            EXPECT_EQ(values[i].kind.value, ui::to_string(got[i].kind)) << "tab " << tab << " kind";
            EXPECT_EQ(parse_bool_cell(values[i].active), got[i].active)
                << "tab " << tab << " active";
        }
    }

    void when_find_performed(const std::vector<FindCriteriaString>& values) {
        for (const FindCriteriaString& value : values) {
            ui::FindCriteria criteria;
            criteria.text = blank(value.text);
            criteria.account = blank(value.account.value);
            if (!blank(value.datefrom).empty())
                criteria.date_from = types::Date::from_iso(blank(value.datefrom));
            if (!blank(value.dateto).empty())
                criteria.date_to = types::Date::from_iso(blank(value.dateto));
            const std::string match = blank(value.amountmatch.value);
            if (!match.empty()) {
                const auto which = ui::amount_match_from(match);
                ASSERT_TRUE(which.has_value()) << "unknown amount match: " << match;
                criteria.amount_match = *which;
            }
            if (!blank(value.amount.value).empty())
                criteria.amount = Money(blank(value.amount.value));
            if (!blank(value.amountto.value).empty())
                criteria.amount_to = Money(blank(value.amountto.value));
            const std::string scope = blank(value.scope.value);
            if (!scope.empty()) {
                const auto which = ui::find_scope_from(scope);
                ASSERT_TRUE(which.has_value()) << "unknown find scope: " << scope;
                criteria.scope = *which;
            }
            workspace().perform_find(criteria);
        }
    }

    // The result names its own register, so the choice is the line and
    // nothing else; a result that does not reach its transaction is a failure
    // rather than a quiet nothing.
    void when_find_result_chosen(const std::vector<FindResultChoiceString>& values) {
        for (const FindResultChoiceString& value : values) {
            const int line = std::stoi(value.line);
            EXPECT_TRUE(workspace().go_to_find_result(line))
                << "result " << line << " of " << workspace().find_results().size();
        }
    }

    void when_bottom_panel_tab_selected(const std::vector<BottomPanelTabSelectString>& values) {
        for (const BottomPanelTabSelectString& value : values)
            workspace().select_bottom_tab(std::stoi(value.tab));
    }

    void when_bottom_panel_tab_closed(const std::vector<BottomPanelTabSelectString>& values) {
        for (const BottomPanelTabSelectString& value : values)
            workspace().close_bottom_tab_at(std::stoi(value.tab));
    }

    void then_find_results_are(const std::vector<FindResultRowString>& values) {
        const std::vector<ui::FindResultRow>& got = workspace().find_results();
        ASSERT_EQ(values.size(), got.size()) << "number of find results";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const FindResultRowString& want = values[i];
            EXPECT_EQ(want.ref.value, got[i].ref) << "result " << i << " ref";
            EXPECT_EQ(want.date, got[i].date.iso()) << "result " << i << " date";
            EXPECT_EQ(blank(want.payee.value), got[i].payee) << "result " << i << " payee";
            EXPECT_EQ(want.account.value, got[i].account) << "result " << i << " account";
            EXPECT_EQ(blank(want.category), got[i].category) << "result " << i << " category";
            EXPECT_EQ(Money(want.amount.value).cents(), got[i].amount.cents())
                << "result " << i << " amount";
        }
    }

    void examples_datatype_bottompaneltabkind(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            bool known = false;
            for (ui::BottomTabKind k :
                 {ui::BottomTabKind::ImportReview, ui::BottomTabKind::FindResults})
                if (ui::to_string(k) == v.value) known = true;
            EXPECT_TRUE(known) << "unknown bottom panel tab kind: " << v.value;
        }
    }

    void examples_datatype_findscope(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            EXPECT_TRUE(ui::find_scope_from(v.value).has_value())
                << "unknown find scope: " << v.value;
        }
    }

    void examples_datatype_amountmatch(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            EXPECT_TRUE(ui::amount_match_from(v.value).has_value())
                << "unknown amount match: " << v.value;
        }
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
            t.ref = types::TransactionRef(value.ref.value);
            const auto on = types::Date::from_iso(value.date);
            if (on) t.date = *on;
            t.payee = types::PayeeName(blank(value.payee.value));
            t.check_no = types::CheckNumber(blank(value.checkno.value));
            t.memo = blank(value.memo);
            t.tag = blank(value.tag);
            t.source_account = chart_.ref_for_path(blank(value.sourceaccount.value));
            t.destination = chart_.ref_for_path(blank(value.destination.value));
            t.amount = Money(value.amount.value);
            if (value.cleared.value != DNCString && !value.cleared.value.empty())
                t.cleared = types::cleared_status_from_string(value.cleared.value);
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
            s.destination = chart_.ref_for_path(blank(value.destination.value));
            s.amount = Money(value.amount.value);
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
            if (want.sourceaccount.value != DNCString)
                EXPECT_EQ(blank(want.sourceaccount.value),
                          workspace().accounts().path_for_ref(made.source_account))
                    << where << " source account";
            if (want.destination.value != DNCString)
                EXPECT_EQ(blank(want.destination.value),
                          workspace().accounts().path_for_ref(made.destination))
                    << where << " destination";
            if (want.amount.value != DNCString)
                EXPECT_EQ(Money(want.amount.value).cents(), made.amount.cents()) << where << " amount";
            if (want.identifier.value != DNCString)
                EXPECT_EQ(blank(want.identifier.value),
                          made.import_ids.empty() ? std::string()
                                                  : made.import_ids.front().id)
                    << where << " identifier";
            if (want.source.value != DNCString && !made.import_ids.empty())
                EXPECT_EQ(want.source.value, types::to_string(made.import_ids.front().source))
                    << where << " source";
            if (want.rawname != DNCString)
                EXPECT_EQ(blank(want.rawname), made.raw_name) << where << " raw name";
            if (want.memo != DNCString)
                EXPECT_EQ(blank(want.memo), made.memo) << where << " memo";
            if (want.checkno.value != DNCString)
                EXPECT_EQ(blank(want.checkno.value), made.check_no.value()) << where << " check number";
            if (want.cleared.value != DNCString && !want.cleared.value.empty())
                EXPECT_EQ(want.cleared.value, types::to_string(made.cleared)) << where << " cleared";
        }
    }

public:

public:

    void then_reports_worked_out_are(const std::vector<ReportWorkString>& values) {
        const std::vector<ui::ReportWorkRow> got = workspace().reports_worked_out();
        ASSERT_EQ(values.size(), got.size()) << "number of report tabs";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(std::stoi(values[i].position), got[i].position)
                << "report tab " << i + 1;
            EXPECT_EQ(std::stoi(values[i].times), got[i].times)
                << "times the report on tab " << got[i].position << " was worked out";
        }
    }

    // A line typed into the register and left, which is reg::commit() exactly
    // as MainWindow::recordBlankLine does it -- and then the book has changed.
    void when_register_entry_committed(const std::vector<RegisterEntryString>& values) {
        for (const RegisterEntryString& value : values) {
            reg::BlankLine line;
            if (const auto on = types::Date::from_iso(value.date)) line.date = *on;
            line.payee = blank(value.payee.value);
            line.check_no = blank(value.checkno.value);
            line.category = blank(value.category.value);
            line.payment = Money(value.payment.value);
            line.deposit = Money(value.deposit.value);
            line.memo = blank(value.memo);
            const reg::Committed done = reg::commit(
                line, value.account.value, ledger::transaction_count(transactions_), &chart_);
            ASSERT_TRUE(done.committed) << done.reason;
            transactions_.push_back(done.transaction);
            if (done.is_transfer) transactions_.push_back(done.other_side);
            for (const ledger::TransactionSplit& one : done.splits)
                splits_.push_back(one);
        }
        book_changed();
    }

public:

    // Another book is the one change that is not a change to the book that is
    // open: MainWindow::openBook throws the workspace away rather than handing
    // the new book over to it, and a workspace with no tabs is the whole of
    // what the scenario watches. What is in the other book is beside the point
    // here, so it is opened empty.
    void when_another_book_is_opened() {
        chart_ = chart::Chart();
        book_ = ledger::Ledger();
        transactions_.clear();
        splits_.clear();
        report_spec_at_.clear();
        report_rows_at_.clear();
        last_report_position_ = 0;
        workspace_.reset();
    }

    // -------------------------------------- where an import is going

    // A file has been chosen and not a row of it read. The format is in the
    // table because the dialog says what kind of file it is reading; what the
    // file knew about its own account is already AccountInFile, which the
    // import works out and this specification takes as given.
    void when_import_file_chosen(const std::vector<ImportFileChosenString>& values) {
        for (const auto& value : values)
            import_refusal_ = workspace().choose_import_file(
                value.filename, blank(value.accountinfile.value), blank(value.bankid),
                blank(value.accountnumber));
    }

    void then_import_confirmation_is(const std::vector<ImportConfirmationString>& values) {
        const ui::Workspace::ImportConfirmation& got = workspace().import_confirmation();
        for (const auto& value : values) {
            EXPECT_EQ(parse_bool_cell(value.shown), got.shown) << "shown";
            if (!dnc(value.filename))
                EXPECT_EQ(blank(value.filename), got.file_name) << "file name";
            if (!dnc(value.account.value)) EXPECT_EQ(blank(value.account.value), got.account) << "account";
            if (!dnc(value.source.value))
                EXPECT_EQ(value.source.value, importing::to_string(got.source)) << "source";
            if (!dnc(value.bankid))
                EXPECT_EQ(blank(value.bankid), got.bank_id) << "bank id";
            if (!dnc(value.accountnumber))
                EXPECT_EQ(blank(value.accountnumber), got.account_number)
                    << "account number";
        }
    }

    // What the file turns out to hold, stated before it is opened so that
    // confirming the dialog can be the thing that reads it. The Account column
    // is left for the dialog to fill: which account these rows are for is the
    // whole question being settled.
    void given_the_file_holds(const std::vector<ImportReviewRowString>& values) {
        rows_from(values, &file_rows_);
    }

    void when_import_confirmed() { workspace().confirm_import(file_rows_); }

    void when_import_confirmed_into(const std::vector<AccountSelectString>& values) {
        for (const auto& value : values) workspace().confirm_import_into(value.path.value, file_rows_);
    }

    void when_import_confirmation_cancelled() { workspace().cancel_import_confirmation(); }

    void examples_datatype_importfileformat(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            bool known = false;
            for (importing::FileFormat f : {importing::FileFormat::Qfx,
                                            importing::FileFormat::Qif,
                                            importing::FileFormat::Csv})
                if (importing::to_string(f) == v.value) known = true;
            EXPECT_TRUE(known) << "unknown import file format: " << v.value;
        }
    }

    void examples_datatype_importtargetsource(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            bool known = false;
            for (importing::TargetSource t : {importing::TargetSource::File,
                                              importing::TargetSource::FileName,
                                              importing::TargetSource::ActiveTab,
                                              importing::TargetSource::NoneOffered})
                if (importing::to_string(t) == v.value) known = true;
            EXPECT_TRUE(known) << "unknown import target source: " << v.value;
        }
    }

    // The precedence on its own, with no window around it: three candidates in,
    // one answer out, and which of the three answered.
    void examples_businessrule_which_account_the_import_proposes(
            const std::vector<ImportTargetProposalString>& values) {
        for (const auto& value : values) {
            const importing::Proposal p = importing::propose(
                blank(value.accountinfile.value), blank(value.fromname.value), blank(value.activetab.value));
            EXPECT_EQ(blank(value.proposed.value), p.account)
                << value.format.value << " file proposing into " << value.activetab.value;
            EXPECT_EQ(value.source.value, importing::to_string(p.source))
                << "where " << blank(value.proposed.value) << " came from";
        }
    }

    // --------------------------------------- showing what an import claimed

    // The claim is stated as a transaction reference, because a reference is
    // what a scenario can write. The review carries ids, and the ids are made
    // when the Given builds the book, so the references are kept here and
    // turned into ids when the review opens.
    void given_import_claims_are(const std::vector<ImportReviewClaimString>& values) {
        claims_.clear();
        for (const auto& value : values)
            claims_.push_back(std::make_pair(std::stoi(value.line), value.ref.value));
    }

    void claim_onto(std::vector<ui::ReviewRow>* rows) {
        for (const auto& claim : claims_) {
            const ledger::Transaction* found = nullptr;
            for (const ledger::Transaction& t : transactions_)
                if (t.ref.value() == claim.second) found = &t;
            ASSERT_NE(nullptr, found) << "no transaction " << claim.second;
            for (ui::ReviewRow& r : *rows)
                if (r.line == claim.first) r.claimed_ids.push_back(found->id.value());
        }
    }

    void when_import_review_line_selected(const std::vector<ImportReviewSelectString>& values) {
        for (const auto& value : values)
            workspace().select_review_line(std::stoi(value.line));
    }

    void when_duplicate_transaction_asked() { look_ = workspace().show_duplicate(); }

    void when_duplicate_transaction_dialog_closed() {
        workspace().close_duplicate_look();
        look_ = workspace().duplicate_look();
    }

    void then_duplicate_transaction_dialog_is(
            const std::vector<DuplicateTransactionDialogString>& values) {
        ASSERT_EQ(1u, values.size());
        const auto& value = values.front();
        EXPECT_EQ(parse_bool_cell(value.shown), look_.shown) << "dialog shown";
        EXPECT_EQ(std::stoi(value.count), static_cast<int>(look_.rows.size()))
            << "transactions shown";
        EXPECT_EQ(blank(value.heading), look_.heading) << "heading";
    }

    void then_duplicate_transactions_shown_are(
            const std::vector<DuplicateTransactionRowString>& values) {
        ASSERT_EQ(values.size(), look_.rows.size()) << "number of transactions shown";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto& value = values[i];
            const ui::DuplicateRow& got = look_.rows[i];
            EXPECT_EQ(value.ref.value, got.ref) << "transaction " << i + 1;
            EXPECT_EQ(value.date, got.date.iso()) << got.ref << " date";
            EXPECT_EQ(blank(value.payee.value), got.payee) << got.ref << " payee";
            EXPECT_EQ(blank(value.account.value), got.account) << got.ref << " account";
            EXPECT_EQ(blank(value.category.value), got.category) << got.ref << " category";
            EXPECT_EQ(Money(value.amount.value).cents(), got.amount.cents()) << got.ref << " amount";
            EXPECT_EQ(blank(value.checkno.value), got.check_no) << got.ref << " check number";
            EXPECT_EQ(value.cleared.value, types::to_string(got.cleared)) << got.ref << " cleared";
        }
    }

    void examples_businessrule_what_the_menu_on_an_import_row_offers(
            const std::vector<DuplicateMenuRuleString>& values) {
        for (const auto& value : values) {
            a_review_of_one_row(value.disposition.value);
            const std::vector<ui::MenuEntry> got = workspace().review_menu_items();
            ASSERT_EQ(1u, got.size()) << value.disposition.value << " menu";
            EXPECT_EQ(blank(value.item), got.front().item) << value.disposition.value;
            EXPECT_EQ(parse_bool_cell(value.enabled), got.front().enabled)
                << got.front().item << " enabled";
        }
    }

    // A review of one row of the given disposition, over a book holding one
    // transaction it can claim. A New row claims nothing, which is what its own
    // row of the rule is about.
    void a_review_of_one_row(const std::string& disposition) {
        chart_ = chart::Chart();
        const auto put = [&](const std::string& path, types::AccountType type) {
            chart::Account a;
            a.path = types::AccountPath(path);
            a.type = type;
            chart_.put(a);
        };
        put("Assets:Checking", types::AccountType::Checking);
        put("Expenses:Groceries", types::AccountType::Expense);

        transactions_.clear();
        splits_.clear();
        ledger::Transaction t;
        t.id = ledger::new_id();
        t.ref = types::TransactionRef("T1");
        t.date = types::Date(2026, 1, 15);
        t.payee = types::PayeeName("Harris Teeter");
        t.source_account = chart_.ref_for_path("Assets:Checking");
        t.destination = chart_.ref_for_path("Expenses:Groceries");
        t.amount = Money("-45.00");
        transactions_.push_back(t);
        rebuild_book();
        workspace_.reset();
        workspace().select("Assets:Checking");

        ui::ReviewRow row;
        row.line = 1;
        row.date = t.date;
        row.payee = "HARRIS TEETER";
        row.account = "Assets:Checking";
        row.category = "Expenses:Groceries";
        row.amount = t.amount;
        row.disposition = disposition;
        row.accepted = disposition == "New";
        if (disposition != "New") row.claimed_ids.push_back(t.id.value());
        workspace().review_import({row}, "Assets:Checking");
        workspace().select_review_line(1);
    }

    std::vector<std::pair<int, std::string>> claims_;
    ui::DuplicateLook look_;

    void examples_businessrule_when_the_name_of_a_file_fits_an_account(
            const std::vector<FileNameMatchString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(parse_bool_cell(value.fits),
                      importing::name_fits(value.filename, value.accountname,
                                           blank(value.accountnumber)))
                << value.filename << " against " << value.accountname
                << " numbered " << blank(value.accountnumber);
    }

public:

    // Which result the menu belongs to. The result names its own register, so
    // the line is the whole of the choice; the menu itself is read by the
    // common menu-items step, which is why this only records the line.
    void when_find_result_right_clicked(const std::vector<FindResultChoiceString>& values) {
        ASSERT_FALSE(values.empty());
        find_result_line_ = std::stoi(values.front().line);
    }

    void when_find_result_split_shown(const std::vector<FindResultChoiceString>& values) {
        ASSERT_FALSE(values.empty());
        split_shown_ = workspace().find_result_split(std::stoi(values.front().line));
    }

    void then_the_split_shown_is(const std::vector<SplitLineShownString>& values) {
        ASSERT_EQ(values.size(), split_shown_.size()) << "lines under the split";
        for (std::size_t i = 0; i < values.size(); ++i) {
            SplitLineShownString got = values[i];
            got.category = split_shown_[i].category;
            got.memo = split_shown_[i].memo.empty() ? "none" : split_shown_[i].memo;
            if (values[i].amount.value != DNCString)
                EXPECT_EQ(Money(values[i].amount), split_shown_[i].amount)
                    << "split line " << i + 1 << " amount";
            got.amount = values[i].amount;
            EXPECT_EQ(values[i], got) << "split line " << i + 1;
        }
    }

    // ------------------------------------------------------- a new book

    void when_a_new_book_is_made_named(const std::vector<BookNameString>& values) {
        ASSERT_FALSE(values.empty());
        workspace().make_new_book(values.front().name);
    }

    void then_the_new_book_question_is(const std::vector<NewBookSetupString>& values) {
        ASSERT_FALSE(values.empty());
        const ui::NewBookSetup& got = workspace().new_book_question();
        EXPECT_EQ(parse_bool_cell(values.front().shown), got.shown) << "shown";
        EXPECT_EQ(blank(values.front().bookname), got.book_name) << "book name";
        EXPECT_EQ(blank(values.front().historyfile), got.history_file)
            << "history file";
    }

    // The window reads the file; here the Given above holds what it says.
    void when_the_new_book_question_is_answered(
            const std::vector<NewBookAnswerString>& values) {
        ASSERT_FALSE(values.empty());
        const std::string file = blank(values.front().historyfile);
        const ui::NewBookStarted said =
            workspace().answer_new_book_question(file, file_text_, import_target_);
        EXPECT_FALSE(said.refused) << said.refusal;
    }

    // The whole chart rather than a subset: which categories a new book has is
    // what the two scenarios asking are about, and a category that should not
    // be there is as much of a defect as one that is missing.
    void then_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        const chart::Chart& got = workspace().accounts();
        EXPECT_EQ(values.size(), got.all().size()) << "accounts in the chart";
        for (const AccountString& want : values) {
            const chart::Account* a = got.find(want.path);
            ASSERT_NE(nullptr, a) << want.path.value << " is not in the chart";
            EXPECT_EQ(want.type.value, types::to_string(a->type))
                << want.path.value << " type";
            EXPECT_EQ(parse_bool_cell(want.placeholder), a->placeholder)
                << want.path.value << " placeholder";
            EXPECT_EQ(parse_bool_cell(want.hidden), a->hidden)
                << want.path.value << " hidden";
        }
    }

    void given_import_target_is(const std::vector<ImportTargetString>& values) {
        ASSERT_FALSE(values.empty());
        import_target_ = unquoted(blank(values.front().accountpath.value));
    }

    void given_the_file_contains(const std::string& value) { file_text_ = value; }

    // ------------------------------------------- the File menu and its dialog

    // Nothing to do: the menu is the same list every time it is shown, which
    // is what the Then reads.
    void when_the_file_menu_is_shown() {}

    void then_file_menu_items_are(const std::vector<FileMenuItemString>& values) {
        const std::vector<ui::MenuEntry> got = ui::file_menu_items();
        ASSERT_EQ(values.size(), got.size()) << "items on the File menu";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].item, got[i].item) << "item " << i + 1;
            EXPECT_EQ(parse_bool_cell(values[i].enabled), got[i].enabled)
                << got[i].item << " enabled";
        }
    }

    // The folder as the window found it, in the order it was written down,
    // which is not the order anything is offered in.
    void given_the_folder_contains(const std::vector<ImportFileOfferedString>& values) {
        in_folder_.clear();
        for (const ImportFileOfferedString& value : values) {
            ui::FileOffered one;
            one.name = value.name;
            const auto on = types::Date::from_iso(value.modified);
            ASSERT_TRUE(on.has_value()) << value.modified;
            one.modified = *on;
            in_folder_.push_back(one);
        }
    }

    void when_the_import_file_dialog_is_shown() {
        offered_files_ = ui::files_offered(in_folder_);
    }

    void then_the_files_offered_are(const std::vector<ImportFileOfferedString>& values) {
        ASSERT_EQ(values.size(), offered_files_.size()) << "files offered";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].name, offered_files_[i].name) << "file " << i + 1;
            EXPECT_EQ(values[i].modified, offered_files_[i].modified.iso())
                << values[i].name << " modified";
        }
    }

    // Nothing in the model to assert on: which dialog was opened is not a fact
    // about the book, and the rule is here to say why the program draws its
    // own. The rows are its reasoning, read by a person.
    void examples_businessrule_the_import_dialog_is_the_program_own_not_the_one_windows_provides(
            const std::vector<DialogChoiceString>& values) {
        EXPECT_FALSE(values.empty()) << "the rule states no reasons";
    }

    // The dropdown decides the fields and nothing else about the find, so
    // this is read straight off ui::amount_fields and the labels are
    // compared word for word.
    void examples_businessrule_the_second_amount_field_is_only_there_for_a_range(
            const std::vector<AmountFieldsShownString>& values) {
        for (const auto& value : values) {
            const auto match = ui::amount_match_from(value.match);
            ASSERT_TRUE(match.has_value()) << "not a match kind: " << value.match;
            const ui::AmountFields fields = ui::amount_fields(*match);
            EXPECT_EQ(std::stoi(value.fieldsshown), fields.shown)
                << "fields shown on " << value.match;
            EXPECT_EQ(blank(value.firstlabel), fields.first_label)
                << "first label on " << value.match;
            EXPECT_EQ(blank(value.secondlabel), fields.second_label)
                << "second label on " << value.match;
        }
    }

    // Which of the four places was clicked, read off the rule's own wording.
    static ui::ScrollBarPlace place_clicked(const std::string& where) {
        if (where == "Above the indicator") return ui::ScrollBarPlace::AboveIndicator;
        if (where == "Below the indicator") return ui::ScrollBarPlace::BelowIndicator;
        if (where == "On the indicator") return ui::ScrollBarPlace::OnIndicator;
        return ui::ScrollBarPlace::OnArrow;
    }

    // Whether the view moves, and how far, is the one part of this rule that is
    // code rather than window: the window asks ui::scroll_lines and moves by
    // what it says. How a click becomes a place is the window's, and a test
    // with no window in it cannot ask that.
    void examples_businessrule_clicking_the_scroll_bar_beside_the_indicator_pages_the_view(
            const std::vector<ScrollBarClickString>& values) {
        const int on_screen = 20;
        for (const ScrollBarClickString& value : values) {
            const std::string& what = value.whathappens;
            int wanted = 0;
            if (what.find("pages back") != std::string::npos)
                wanted = -(on_screen - 1);
            else if (what.find("pages on") != std::string::npos)
                wanted = on_screen - 1;
            else if (what.find("moves one line") != std::string::npos)
                wanted = 1;
            EXPECT_EQ(wanted, ui::scroll_lines(place_clicked(value.whereclicked), on_screen))
                << value.whereclicked << " -- " << what;
        }
    }

    // One row of the table is one disposition and one note, which is the
    // ordinary case; two notes on one row are covered by the check scenario in
    // ImportOfx, and the order they read in is the function's own business.
    void examples_businessrule_the_disposition_column_reads_the_disposition_and_then_the_note(
            const std::vector<DispositionReadingString>& values) {
        ASSERT_FALSE(values.empty());
        for (const DispositionReadingString& value : values) {
            std::vector<std::string> notes;
            if (value.note.value != "None") notes.push_back(value.note.value);
            EXPECT_EQ(unquoted(value.reads),
                      ui::disposition_reading(value.disposition.value, notes))
                << value.disposition.value << " with " << value.note.value;
        }
    }

public:

    // One made-up question, and every line of it read back. The words are
    // the point of this rule: a dialog that says "match?" and nothing else
    // is a dialog nobody can answer, so each half is named by its account,
    // its date and the payee its own file gave it.
    void examples_businessrule_what_the_transfer_question_says(
            const std::vector<TransferQuestionLineString>& values) {
        transfers::Question question;
        question.from_account = "Assets:Checking";
        question.from_date = types::Date(2026, 2, 3);
        question.from_payee = "Shell";
        question.to_account = "Liabilities:Visa";
        question.to_date = types::Date(2026, 2, 5);
        question.to_payee = "SHELL OIL 4471";
        question.amount = Money("91.40");
        question.days_apart = 2;

        std::vector<std::string> in_order;
        for (const auto& value : values) {
            in_order.push_back(value.line);
            const std::string reads = transfers::line_reads(question, value.line);
            ASSERT_FALSE(reads.empty()) << "nothing is said on " << value.line;
            if (value.line == "Heading" || value.line == "Answers") {
                EXPECT_EQ(blank(value.reads), reads) << value.line;
                continue;
            }
            // The other three name things, so what is checked is that the
            // things are in them rather than the order somebody wrote them.
            if (value.line == "Gap") {
                EXPECT_NE(std::string::npos, reads.find("2")) << reads;
                continue;
            }
            const bool first = value.line == "FirstHalf";
            const std::string account =
                first ? question.from_account : question.to_account;
            const std::string when =
                (first ? question.from_date : question.to_date).iso();
            const std::string payee =
                first ? question.from_payee : question.to_payee;
            EXPECT_NE(std::string::npos, reads.find(account)) << reads;
            EXPECT_NE(std::string::npos, reads.find(when)) << reads;
            EXPECT_NE(std::string::npos, reads.find(payee)) << reads;
        }
        EXPECT_EQ(transfers::question_lines(), in_order)
            << "the lines of the question, in the order it reads";
    }

    void examples_datatype_transferanswer(
            const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.value,
                      transfers::to_string(
                          transfers::answer_from_string(value.value)))
                << "the answer " << value.value;
    }

public:

    // Work in hand. A scenario states the strip rather than starting a real
    // job, which is what lets the words be checked without a file and a clock.
    void given_work_in_progress_is(const std::vector<ProgressReportString>& values) {
        ASSERT_FALSE(values.empty());
        report_ = progress::Report();
        report_.what = blank(values.front().what);
        report_.done = count_cell(values.front().done);
        report_.total = count_cell(values.front().total);
        running_ = true;
    }

    void when_work_has_advanced_to(const std::vector<ProgressAdvanceString>& values) {
        ASSERT_FALSE(values.empty());
        report_ = progress::advanced(report_, count_cell(values.front().done));
        // Reaching the total ends the work, and the strip goes with it.
        if (report_.finished) running_ = false;
    }

    void then_work_in_progress_is(const std::vector<ProgressReportString>& values) {
        if (values.empty()) {
            // A table with no rows: nothing is in progress, so there is no
            // report at all rather than one reading finished.
            EXPECT_FALSE(running_) << "a report still reading "
                                   << progress::reads(report_);
            return;
        }
        ASSERT_TRUE(running_) << "no report at all";
        ProgressReportString got;
        got.what = report_.what.empty() ? "none" : report_.what;
        got.done = std::to_string(report_.done);
        got.total = std::to_string(report_.total);
        got.reads = progress::reads(report_);
        got.finished = report_.finished ? "true" : "false";
        EXPECT_EQ(values.front(), got) << "wanted " << values.front().to_string()
                                       << " got " << got.to_string();
    }

    // Stopping. The work computes and it is the writing at the end that would
    // have changed anything, so a stopped job leaves the book alone; the strip
    // goes away the same as a finished one's.
    void when_work_in_progress_is_stopped() {
        report_ = progress::stopped(report_);
        running_ = false;
    }

    void then_transactions_are(const std::vector<TransactionString>& values) {
        ASSERT_EQ(values.size(), transactions_.size()) << "transactions";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const ledger::Transaction& made = transactions_[i];
            const TransactionString& want = values[i];
            const std::string where = "transaction " + std::to_string(i + 1);
            EXPECT_EQ(want.ref.value, made.ref.value()) << where << " ref";
            EXPECT_EQ(want.date, made.date.iso()) << where << " date";
            EXPECT_EQ(blank(want.payee.value), made.payee.value()) << where << " payee";
            EXPECT_EQ(blank(want.sourceaccount.value),
                      chart_.path_for_ref(made.source_account))
                << where << " source account";
            EXPECT_EQ(blank(want.destination.value),
                      chart_.path_for_ref(made.destination))
                << where << " destination";
            EXPECT_EQ(Money(want.amount.value).cents(), made.amount.cents())
                << where << " amount";
            EXPECT_EQ(blank(want.tag), made.tag) << where << " tag";
        }
    }

    // A count written the way a spec cell writes one, with the separators a
    // reader puts in allowed: 24,000 and 24000 are the same number.
    static long long count_cell(const std::string& cell) {
        std::string digits;
        for (char c : cell)
            if (c != ',' && c != ' ') digits += c;
        if (digits.empty() || digits == "none" || digits == DNCString) return 0;
        return std::stoll(digits);
    }

    progress::Report report_;
    bool running_ = false;

    // The listing is read rather than held: the words on the transactions are
    // the use of the vocabulary, and asking twice cannot disagree with itself.
    // Opening the window is therefore nothing but asking.
    void when_tag_listing_opened() { tag_listing_opened_ = true; }

    void then_tag_listing_rows_are(const std::vector<TagListingRowString>& values) {
        const std::vector<tags::Row> got = tags::listing(vocabulary_, tag_fields());
        ASSERT_EQ(values.size(), got.size()) << "tag listing rows -- got "
                                            << tag_listing_text(got);
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].name, got[i].name) << "tag listing row "
                                                   << (i + 1) << " name";
            EXPECT_EQ(std::stoll(values[i].used), got[i].used)
                << "tag " << got[i].name << " used";
        }
    }

    // The words the book knows, whether or not anything carries them.
    void given_tags_are(const std::vector<TagListingRowString>& values) {
        vocabulary_.clear();
        for (const auto& value : values) vocabulary_.push_back(value.name);
    }

    void when_tag_renamed(const std::vector<TagEditString>& values) {
        ASSERT_FALSE(values.empty());
        const std::string was = blank(values.front().wasname);
        const std::string now = blank(values.front().name);
        for (ledger::Transaction& t : transactions_)
            t.tag = tags::renamed_field(t.tag, was, now);
        vocabulary_ = tags::renamed(vocabulary_, was, now);
        rebuild_book();
        workspace_.reset();
    }

    // Typing into the column: what is offered against it, what the field holds
    // when the typing is accepted, and the word being added to the vocabulary
    // where it is one nobody has used before.
    void when_tag_typed_is(const std::vector<TagTypedString>& values) {
        ASSERT_FALSE(values.empty());
        const std::string typed = blank(values.front().typed);
        EXPECT_EQ(blank(values.front().offered), tags::offer(vocabulary_, typed))
            << "offered against " << values.front().typed;
        EXPECT_EQ(blank(values.front().accepted), tags::joined(tags::words_of(typed)))
            << "accepted from " << values.front().typed;
        vocabulary_ = tags::accepted(vocabulary_, typed);
    }

    std::vector<std::string> tag_fields() const {
        std::vector<std::string> out;
        for (const ledger::Transaction& t : transactions_) out.push_back(t.tag);
        return out;
    }

    static std::string tag_listing_text(const std::vector<tags::Row>& rows) {
        std::string out;
        for (const tags::Row& row : rows) {
            if (!out.empty()) out += ", ";
            out += row.name + " " + std::to_string(row.used);
        }
        return out.empty() ? "nothing" : out;
    }

    std::vector<std::string> vocabulary_;
    bool tag_listing_opened_ = false;

    // Naming the second account is the first moment there is anything to ask
    // about, so the destination is written and the look is done in the same
    // breath.
    void when_destination_typed_is(const std::vector<DestinationTypedString>& values) {
        ASSERT_FALSE(values.empty());
        const std::string named = blank(values.front().destination.value);
        ledger::Transaction* editing = transaction_by_ref(values.front().ref.value);
        ASSERT_NE(nullptr, editing) << "no transaction " << values.front().ref.value;
        editing->destination = chart_.ref_for_path(named);
        editing_ = editing->id.value();
        candidates_ = transfers::halves_in(
            named, chart_.path_for_ref(editing->source_account), editing->date,
            editing->amount, transactions_, chart_);
        rebuild_book();
        workspace_.reset();
    }

    // The question names each candidate back to the person who entered it, so
    // the amount and the cleared status are read off the transaction rather
    // than carried around separately.
    void then_transfer_candidates_are(const std::vector<TransferCandidateRowString>& values) {
        ASSERT_EQ(values.size(), candidates_.size())
            << "transfer candidates -- got " << candidate_text();
        for (std::size_t i = 0; i < values.size(); ++i) {
            const transfers::Half& got = candidates_[i];
            const TransferCandidateRowString& want = values[i];
            const ledger::Transaction* t = transaction_by_id(got.transaction_id);
            ASSERT_NE(nullptr, t) << "candidate " << (i + 1) << " is in no book";
            const std::string where = "candidate " + std::to_string(i + 1);
            EXPECT_EQ(want.ref.value, t->ref.value()) << where << " ref";
            EXPECT_EQ(blank(want.account.value), got.account) << where << " account";
            EXPECT_EQ(want.date, got.date.iso()) << where << " date";
            EXPECT_EQ(blank(want.payee), got.payee) << where << " payee";
            EXPECT_EQ(Money(want.amount.value).cents(), t->amount.cents())
                << where << " amount";
            EXPECT_EQ(want.cleared.value, types::to_string(t->cleared))
                << where << " cleared";
            EXPECT_EQ(std::stoll(want.daysapart), got.days_apart) << where << " days apart";
        }
    }

    void when_transfer_candidate_chosen(const std::vector<TransferCandidateChoiceString>& values) {
        ASSERT_FALSE(values.empty());
        const std::string ref = blank(values.front().ref);
        if (ref.empty()) {
            // None is a real answer and not an unanswered question. It creates
            // nothing: the transaction already names both accounts, so it is
            // already a line in the other register, and what none rules out is
            // the merge rather than the transfer.
            candidates_.clear();
            return;
        }
        const ledger::Transaction* half = transaction_by_ref(ref);
        ASSERT_NE(nullptr, half) << "no transaction " << ref;
        const std::string half_id = half->id.value();
        EXPECT_TRUE(transfers::merge(editing_, half_id, &transactions_))
            << "nothing was merged";
        candidates_.clear();
        rebuild_book();
        workspace_.reset();
    }

    ledger::Transaction* transaction_by_ref(const std::string& ref) {
        for (ledger::Transaction& t : transactions_)
            if (t.ref.value() == ref) return &t;
        return nullptr;
    }

    const ledger::Transaction* transaction_by_id(const std::string& id) const {
        for (const ledger::Transaction& t : transactions_)
            if (t.id.value() == id) return &t;
        return nullptr;
    }

    std::string candidate_text() const {
        std::string out;
        for (const transfers::Half& half : candidates_) {
            if (!out.empty()) out += ", ";
            const ledger::Transaction* t = transaction_by_id(half.transaction_id);
            out += (t == nullptr ? half.transaction_id : t->ref.value());
            out += " in " + half.account;
        }
        return out.empty() ? "nothing" : out;
    }

    std::vector<transfers::Half> candidates_;
    std::string editing_;

    // Reading a register is selecting its account: the tab opens, or the one
    // already open comes forward, and it becomes the register a following
    // Then is about.
    void when_register_opened_on(const std::vector<AccountSelectString>& values) {
        for (const auto& value : values) workspace().select(value.path.value);
    }

    // Each row is a job: what it would say, and for the short one that it
    // says nothing at all. The rows are written with an em dash spelled --
    // where the scenarios write a single hyphen, so runs of hyphens are read
    // as one before comparing; the words either side are what is being checked.
    void examples_businessrule_work_that_takes_a_noticeable_time_says_so_and_does_not_take_the_window_away(
            const std::vector<ProgressShownString>& values) {
        for (const ProgressShownString& value : values) {
            progress::Report report;
            if (value.thework == "Reading a file of 40 rows") {
                report.what = "Reading the file";
                report.total = 40;
                report.done = 12;
                // Over within a second, so nothing is shown and nothing the
                // report would have read matters.
                EXPECT_FALSE(progress::shows(report, 0.4)) << value.thework;
                EXPECT_TRUE(progress::shows(report, 1.5)) << value.thework;
                continue;
            }
            if (value.thework == "Importing 24,000 rows") {
                report.what = "Importing";
                report.done = 8000;
                report.total = 24000;
            } else if (value.thework == "Recalculating every balance") {
                report.what = "Recalculating";
                report.done = 8000;
                report.total = 24000;
            } else if (value.thework == "Opening a book") {
                report.what = "Opening the book";
                report.done = 1200;
            } else {
                report.what = "Building the report";
                report.done = 3;
                report.total = 20;
                report.unit = "years";
            }
            EXPECT_EQ(report.total > 0, parse_bool_cell(value.hasatotal))
                << value.thework << " has a total";
            EXPECT_EQ(one_dash(value.whatitsays), one_dash(progress::reads(report)))
                << value.thework;
            EXPECT_TRUE(progress::shows(report, 1.5)) << value.thework;
        }
    }

    static std::string one_dash(const std::string& s) {
        std::string out;
        for (char c : s)
            if (c != '-' || out.empty() || out.back() != '-') out += c;
        return out;
    }

    // Every word in the field is a tag, which is the whole of what the rule
    // says about storage: the count is the reading of the field, and the two
    // paint rows carry the same category while differing in the field, which
    // is why the distinction is not a category.
    void examples_businessrule_a_tag_is_the_third_thing_a_transaction_says_about_itself(
            const std::vector<WhatATagIsForString>& values) {
        std::string paint_categories;
        for (const WhatATagIsForString& value : values) {
            const std::string field = blank(value.thefield);
            EXPECT_EQ(std::stoll(value.tagsitcarries),
                      static_cast<long long>(tags::words_of(field).size()))
                << value.example;
            if (value.example.rfind("Paint", 0) == 0) {
                if (!paint_categories.empty())
                    EXPECT_EQ(paint_categories, value.category)
                        << value.example << " is the same expense as the row above it";
                paint_categories = value.category;
            }
        }
    }

    // Against the author's own three words, which are the vocabulary the rows
    // were written over.
    void examples_businessrule_typing_a_tag_works_the_way_typing_a_category_works(
            const std::vector<TagCompletionString>& values) {
        const std::vector<std::string> vocabulary = {"Kitchen", "Rental", "WarpAndByte"};
        for (const TagCompletionString& value : values)
            EXPECT_EQ(blank(value.offered), tags::offer(vocabulary, blank(value.typed)))
                << value.typed << " -- " << value.notes;
    }

    // Each row is the same payment in the checking register with the card
    // holding however many halves the row says, and what is asked about it.
    void examples_businessrule_naming_an_account_in_the_destination_column_looks_for_the_other_half(
            const std::vector<DestinationNamedString>& values) {
        for (const DestinationNamedString& value : values) {
            std::string typed = "Liabilities:Visa";
            int halves = 1;
            if (value.whatistyped == "A category") {
                typed = "Expenses:Groceries";
            } else if (value.whatistyped == "Unassigned") {
                typed = ledger::kUnassigned;
            } else if (value.whatistyped ==
                       "An account with no equal and opposite transaction") {
                halves = 0;
            } else if (value.whatistyped == "An account with three") {
                halves = 3;
            }
            const std::vector<transfers::Half> got = offered_for(typed, halves);
            if (value.whathappens.rfind("Nothing", 0) == 0) {
                EXPECT_TRUE(got.empty())
                    << value.whatistyped << " -- " << value.whathappens;
                continue;
            }
            ASSERT_EQ(static_cast<std::size_t>(halves), got.size())
                << value.whatistyped << " -- " << value.whathappens;
            // Closest in days first, which is the order the question asks in.
            for (std::size_t i = 1; i < got.size(); ++i)
                EXPECT_LE(got[i - 1].days_apart, got[i].days_apart)
                    << value.whatistyped << " -- " << value.whathappens;
        }
    }

    // The payment in the checking register, and that many equal and opposite
    // transactions in the card, each a day further away.
    static std::vector<transfers::Half> offered_for(const std::string& typed, int halves) {
        chart::Chart accounts;
        accounts.add(types::AccountPath("Assets:Checking"), types::AccountType::Checking);
        accounts.add(types::AccountPath("Liabilities:Visa"), types::AccountType::CreditCard);
        accounts.add(types::AccountPath("Expenses:Groceries"), types::AccountType::Expense);
        accounts.add(types::AccountPath("Expenses:Uncategorized"), types::AccountType::Expense);
        accounts.add(types::AccountPath(ledger::kUnassigned), types::AccountType::OtherAsset);

        std::vector<ledger::Transaction> transactions;
        ledger::Transaction mine;
        mine.id = ledger::new_id();
        mine.ref = types::TransactionRef("T1");
        mine.date = types::Date(2026, 2, 10);
        mine.payee = types::PayeeName("Visa Payment");
        mine.source_account = accounts.ref_for_path("Assets:Checking");
        mine.destination = accounts.ref_for_path("Expenses:Uncategorized");
        mine.amount = Money("-100.00");
        transactions.push_back(mine);

        for (int i = 0; i < halves; ++i) {
            ledger::Transaction other;
            other.id = ledger::new_id();
            other.ref = types::TransactionRef("C" + std::to_string(i + 1));
            other.date = types::Date(2026, 2, 10 + i);
            other.payee = types::PayeeName("Payment");
            other.source_account = accounts.ref_for_path("Liabilities:Visa");
            other.destination = accounts.ref_for_path("Expenses:Uncategorized");
            other.amount = Money("100.00");
            transactions.push_back(other);
        }
        return transfers::halves_in(typed, "Assets:Checking", mine.date, mine.amount,
                                    transactions, accounts);
    }

};
