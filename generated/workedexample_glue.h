#pragma once
#include <gtest/gtest.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "common/common.h"
#include "account_type.h"
#include "chart.h"
#include "csv_headers.h"
#include "csv_import.h"
#include "csv_reader.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "report.h"
#include "text_types.h"
#include "transaction_edit.h"
#include "transaction_id.h"

// Glue for WorkedExample.spectable.
//
// A month end to end: four CSV files read into four accounts, the categories
// that matter assigned, the report read. Nothing is decided here -- every step
// drives src/csv_reader.h, src/csv_import.h, src/transaction_edit.h and
// src/report.h, and this file only turns tables into their arguments and what
// they return back into tables.
//
// One book throughout. Each file adds to it, which is what four statements
// arriving in the same month do, so the imports are Givens rather than Whens:
// in this file importing is the setup and not the behaviour under test.
class WorkedExampleGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

    // ---------------------------------------------------------------- givens

    void given_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        chart_ = chart::Chart();
        for (const auto& value : values) {
            chart::Account a;
            a.path = types::AccountPath(value.path);
            a.type = types::account_type_from_string(value.type);
            a.placeholder = parse_bool_cell(value.placeholder);
            a.hidden = parse_bool_cell(value.hidden);
            a.alias = blank(value.alias);
            a.payment_payee = blank(value.paymentpayee);
            chart_.put(a);
        }
    }

    void given_the_csv_profile_is(const std::vector<CsvProfileString>& values) {
        ASSERT_FALSE(values.empty());
        profile_ = profile_from(values.front());
    }

    void given_import_target_is(const std::vector<ImportTargetString>& values) {
        ASSERT_FALSE(values.empty());
        target_ = values.front().accountpath;
    }

    void given_the_file_contains(const std::string& value) { text_ = value; }

    // The file last given, read into the account last named, added to the book.
    // The columns are matched by their headings through the profile, which is
    // the whole reason two of these files can have different headings and still
    // be read by the same code.
    void given_rows_imported() {
        const csv::Read file = csv::read(text_, profile_.has_header,
                                         profile_.delimiter, profile_.skip_rows);
        const csv::Matching matching =
            csv::match_headers(file.headings, csv::columns_of(profile_));
        ASSERT_TRUE(matching.complete)
            << "the profile " << profile_.name << " does not read this file: "
            << matching.missing.size() << " field(s) unmatched";
        const csv::Rows rows = csv::rows_of(file, matching, profile_.date_order,
                                            profile_.amount_style,
                                            profile_.outward_sign);
        ASSERT_TRUE(rows.rejected.empty())
            << "row " << rows.rejected.front().line << " of the "
            << target_ << " file was rejected: " << rows.rejected.front().reason;
        const csv::Imported decided = csv::decide(rows.rows, target_, transactions_);
        const std::vector<ledger::Transaction> made =
            csv::transactions_for(decided, target_, transactions_.size(), &chart_);
        transactions_.insert(transactions_.end(), made.begin(), made.end());
    }

    // ----------------------------------------------------------------- whens

    // A transaction is named by its date and its payee, the way a person finds
    // it on the screen. Two matching would make the assignment a guess, so it
    // is an error here rather than a coin toss.
    void when_categories_assigned(const std::vector<CategoryAssignmentString>& values) {
        for (const auto& value : values) {
            const auto date = types::Date::from_iso(value.date);
            ASSERT_TRUE(date.has_value()) << value.date;
            ledger::Transaction* t = the_one_on(*date, value.payee);
            ASSERT_NE(nullptr, t) << "no single transaction on " << value.date
                                  << " to " << value.payee;
            const edit::Outcome done = edit::assign_category(t, chart_, value.category);
            EXPECT_TRUE(done.changed) << value.payee << ": " << done.reason;
        }
    }

    void when_report_run(const std::vector<ReportSpecString>& values) {
        ASSERT_FALSE(values.empty());
        const ReportSpecString& value = values.front();
        reports::Spec spec;
        const auto from = types::Date::from_iso(value.from);
        const auto to = types::Date::from_iso(value.to);
        if (from) spec.from = *from;
        if (to) spec.to = *to;
        spec.depth = value.depth.empty() ? 0 : std::stoi(value.depth);
        spec.detail = reports::detail_from_string(value.detail);
        spec.zero_rows = parse_bool_cell(value.zerorows);
        spec.include_investment_activity =
            parse_bool_cell(value.includeinvestmentactivity);
        spec.quicken_signs = parse_bool_cell(value.quickensigns);
        report_ = reports::category_report(chart_, book(), spec);
    }

    // ----------------------------------------------------------------- thens

    void then_balances_are(const std::vector<AccountBalanceString>& values) {
        const ledger::Ledger here = book();
        for (const auto& value : values) {
            const chart::Account* a = chart_.find(value.account);
            ASSERT_NE(nullptr, a) << "no account " << value.account;
            EXPECT_EQ(Money(value.rawbalance).cents(),
                      here.raw_balance(value.account).cents())
                << value.account << " raw -- " << value.notes;
            EXPECT_EQ(Money(value.displaybalance).cents(),
                      here.display_balance(value.account, a->type).cents())
                << value.account << " display -- " << value.notes;
        }
    }

    void then_report_rows_are(const std::vector<ReportRowString>& values) {
        ASSERT_EQ(values.size(), report_.rows.size())
            << "number of report rows. Got:" << "\n" << listing();
        for (std::size_t i = 0; i < values.size(); ++i) {
            const reports::Row& got = report_.rows[i];
            EXPECT_EQ(values[i].account, got.account) << "row " << i;
            EXPECT_EQ(std::stoi(values[i].level), got.level) << got.account << " level";
            EXPECT_EQ(Money(values[i].amount).cents(), got.amount.cents())
                << got.account << " amount";
            EXPECT_EQ(parse_bool_cell(values[i].issubtotal), got.is_subtotal)
                << got.account << " subtotal";
        }
    }

    void then_report_total_is(const std::vector<ReportTotalString>& values) {
        for (const auto& value : values) {
            EXPECT_EQ(Money(value.totalincome).cents(), report_.totals.income.cents())
                << "total income";
            EXPECT_EQ(Money(value.totalexpenses).cents(), report_.totals.expenses.cents())
                << "total expenses";
            EXPECT_EQ(Money(value.net).cents(), report_.totals.net.cents()) << "net";
        }
    }

private:
    std::string text_;
    csv::Profile profile_;
    std::string target_;
    chart::Chart chart_;
    std::vector<ledger::Transaction> transactions_;
    reports::CategoryReport report_;

    static std::string blank(const std::string& s) {
        return (s == "none" || s == DNCString) ? std::string() : s;
    }

    // The book is the transactions, flattened. Keeping one copy and deriving the
    // other means the two cannot disagree, which is the point of checking a
    // register against a report in the same file.
    ledger::Ledger book() const {
        ledger::Ledger out;
        for (const ledger::Transaction& t : transactions_)
            for (const ledger::Posting& p : t.postings)
                out.add({t.date, p.account, p.amount, t.ref.value()});
        return out;
    }

    ledger::Transaction* the_one_on(const types::Date& date, const std::string& payee) {
        ledger::Transaction* found = nullptr;
        for (ledger::Transaction& t : transactions_) {
            if (!(t.date == date)) continue;
            if (t.payee.value() != payee) continue;
            if (found != nullptr) return nullptr;   // two match; naming one is a guess
            found = &t;
        }
        return found;
    }

    std::string listing() const {
        std::string out;
        for (const reports::Row& row : report_.rows)
            out += "  " + row.account + " level " + std::to_string(row.level) +
                   " " + row.amount.in_register() + "\n";
        return out;
    }

    static csv::Profile profile_from(const CsvProfileString& value) {
        csv::Profile one;
        one.name = value.name;
        if (value.hasheader != DNCString && !value.hasheader.empty())
            one.has_header = parse_bool_cell(value.hasheader);
        if (value.delimiter != DNCString && !value.delimiter.empty())
            one.delimiter = value.delimiter;
        if (value.skiprows != DNCString && !value.skiprows.empty())
            one.skip_rows = std::stoi(value.skiprows);
        one.date_column = blank(value.datecolumn);
        if (value.dateorder != DNCString && !value.dateorder.empty())
            one.date_order = types::date_order_from_string(value.dateorder);
        one.payee_column = blank(value.payeecolumn);
        one.memo_column = blank(value.memocolumn);
        one.check_column = blank(value.checkcolumn);
        one.category_column = blank(value.categorycolumn);
        if (value.amountstyle != DNCString && !value.amountstyle.empty())
            one.amount_style = csv::amount_style_from_string(value.amountstyle);
        one.amount_column = blank(value.amountcolumn);
        one.debit_column = blank(value.debitcolumn);
        one.credit_column = blank(value.creditcolumn);
        if (value.outwardsign != DNCString && !value.outwardsign.empty())
            one.outward_sign = csv::outward_sign_from_string(value.outwardsign);
        return one;
    }
};
