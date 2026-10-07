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
#include "text_types.h"
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

    void given_postings_are(const std::vector<PostingRowString>& values) {
        book_ = ledger::Ledger();
        transactions_.clear();
        std::map<std::string, std::size_t> by_ref;
        for (const auto& value : values) {
            const auto date = types::Date::from_iso(value.date);
            ASSERT_TRUE(date.has_value()) << value.date;
            const Money amount{value.amount};
            book_.add({*date, types::AccountPath(value.account), amount});

            auto at = by_ref.find(value.ref);
            if (at == by_ref.end()) {
                ledger::Transaction t;
                t.ref = types::TransactionRef(value.ref);
                t.date = *date;
                t.payee = types::PayeeName(value.payee);
                transactions_.push_back(t);
                at = by_ref.emplace(value.ref, transactions_.size() - 1).first;
            }
            ledger::Posting p;
            p.account = types::AccountPath(value.account);
            p.amount = amount;
            p.memo = blank(value.memo);
            p.cleared = types::cleared_status_from_string(
                value.cleared.empty() || value.cleared == DNCString ? "Uncleared"
                                                                    : value.cleared);
            transactions_[at->second].postings.push_back(p);
        }
        workspace_.reset();
    }

    void given_the_chart_of_accounts_is_as_previous() {}
    void given_postings_are_as_previous() {}

    // ----------------------------------------------------------------- whens

    void when_account_list_shown() { workspace(); }

    void when_hidden_accounts_shown() { show_hidden_ = true; }

    void when_account_selected(const std::vector<AccountSelectString>& values) {
        for (const auto& value : values) workspace().select(value.path);
    }

    void when_view_split() { workspace().split(); }
    void when_split_closed() { workspace().close_split(); }

    void when_pane_made_active(const std::vector<PaneSelectString>& values) {
        for (const auto& value : values) workspace().make_active(std::stoi(value.pane));
    }

    void when_import_reviewed(const std::vector<ImportReviewRowString>& values) {
        last_review_.clear();
        std::string into;
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
            if (into.empty()) into = value.account;
        }
        last_into_ = into;
        workspace().review_import(last_review_, into);
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

    void then_split_state_is(const std::vector<SplitStateString>& values) {
        for (const auto& value : values) {
            EXPECT_EQ(parse_bool_cell(value.open), workspace().split_open()) << "split open";
            EXPECT_EQ(std::stoi(value.panecount), workspace().pane_count()) << "pane count";
            EXPECT_EQ(std::stoi(value.activepane), workspace().active_pane()) << "active pane";
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
        // Every value is one the list can show, and the five of them are the
        // whole set, so the rule below has to account for each.
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            bool known = false;
            for (ui::Group g : {ui::Group::Banking, ui::Group::CreditCards,
                                ui::Group::Investments, ui::Group::Assets,
                                ui::Group::Liabilities})
                if (ui::to_string(g) == v.value) known = true;
            EXPECT_TRUE(known) << "unknown group: " << v.value;
        }
    }

    void examples_datatype_panecontent(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            bool known = false;
            for (ui::PaneContent c : {ui::PaneContent::Register, ui::PaneContent::ImportReview,
                                      ui::PaneContent::Empty})
                if (ui::to_string(c) == v.value) known = true;
            EXPECT_TRUE(known) << "unknown pane content: " << v.value;
        }
    }

    void examples_businessrule_which_group_an_account_is_listed_under(
            const std::vector<GroupOfTypeString>& values) {
        for (const auto& value : values) {
            const GroupOfTypeTyped t = GroupOfTypeTyped::from_string_struct(value);
            const auto got = ui::group_of(types::account_type_from_string(t.accounttype));
            EXPECT_EQ(t.listed, got.has_value()) << t.accounttype << " listed";
            // The group only means anything for a type that is listed; the table
            // carries the default in the other rows.
            if (t.listed && got) EXPECT_EQ(t.accountgroup, ui::to_string(*got))
                << t.accounttype;
        }
    }

private:
    chart::Chart chart_;
    ledger::Ledger book_;
    std::vector<ledger::Transaction> transactions_;
    std::unique_ptr<ui::Workspace> workspace_;
    bool show_hidden_ = false;
    std::vector<ui::ReviewRow> last_review_;
    std::string last_into_;

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

    // The sides of the transactions the accept just added, in order, with the
    // identifier each was stored under. Only those: what the book already held
    // is not an outcome of the import.
    void then_accepted_postings_are(const std::vector<AcceptedPostingString>& values) {
        std::vector<AcceptedPostingString> got;
        for (const ledger::Transaction& t : workspace().accepted()) {
            for (const ledger::Posting& p : t.postings) {
                AcceptedPostingString row;
                row.account = p.account.value();
                row.amount = p.amount.in_register();
                // A posting carries at most one identifier per source, and an
                // imported row has one source, so the first is the one.
                row.identifier = p.import_ids.empty() ? "none" : p.import_ids.front().id;
                row.source = types::to_string(p.import_ids.empty()
                                                  ? types::ImportSource::Ofx
                                                  : p.import_ids.front().source);
                row.rawname = t.raw_name.empty() ? "none" : t.raw_name;
                row.memo = t.memo.empty() ? "none" : t.memo;
                row.checkno = t.check_no.value().empty() ? "none" : t.check_no.value();
                row.cleared = types::to_string(p.cleared);
                got.push_back(row);
            }
        }
        ASSERT_EQ(values.size(), got.size()) << "accepted postings:\n" << listing(got);
        for (std::size_t i = 0; i < values.size(); ++i) {
            AcceptedPostingString back = got[i];
            if (values[i].amount != DNCString)
                EXPECT_EQ(Money(values[i].amount), Money(got[i].amount))
                    << "accepted posting " << i << " amount";
            back.amount = values[i].amount;
            // A posting carrying no identifier has no source to report either,
            // so the source of such a row is not compared.
            if (got[i].identifier == "none") back.source = values[i].source;
            EXPECT_EQ(values[i], back)
                << "accepted posting " << i << ": wanted " << values[i].to_string()
                << " got " << got[i].to_string();
        }
    }

private:
    static std::string listing(const std::vector<AcceptedPostingString>& rows) {
        std::string out;
        for (const auto& row : rows) out += "  " + row.to_string() + "\n";
        return out;
    }

};
