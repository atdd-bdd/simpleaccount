#pragma once
#include <gtest/gtest.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "common/common.h"
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "posting.h"
#include "report.h"
#include "report_ranges.h"
#include "text_types.h"

// Glue for Reports.spectable.
class ReportsGlue {
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
    }

    void given_the_chart_of_accounts_is_as_previous() {}
    void given_postings_are_as_previous() {}

    // Postings grouped into transactions by their Ref, which is how these tables
    // say that two rows belong together.
    void given_postings_are(const std::vector<PostingRowString>& values) {
        book_ = ledger::Ledger();
        transactions_.clear();
        std::map<std::string, std::size_t> by_ref;
        for (const auto& value : values) {
            const auto date = types::Date::from_iso(value.date);
            ASSERT_TRUE(date.has_value()) << value.date;
            const Money amount{value.amount};
            book_.add({*date, types::AccountPath(value.account), amount, value.ref});

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
            p.memo = value.memo == "none" ? std::string() : value.memo;
            transactions_[at->second].postings.push_back(p);
        }
    }

    // ----------------------------------------------------------------- whens

    void when_report_run(const std::vector<ReportSpecString>& values) {
        for (const auto& value : values) run(value);
    }

    void when_report_run_with_average(const std::vector<ReportSpecString>& values) {
        when_report_run(values);
    }

    void when_net_worth_report_run(const std::vector<ReportSpecString>& values) {
        when_report_run(values);
    }

    void when_report_exported_as_csv(const std::vector<ReportSpecString>& values) {
        when_report_run(values);
    }

    void when_report_run_for_category(const std::vector<CategoryFilterString>& values) {
        for (const auto& value : values) {
            spec_ = range_of(value.from, value.to);
            category_filter_ = value.category;
            report_ = reports::category_report(chart_, book_, spec_);
        }
    }

    void when_report_figure_opened(const std::vector<ReportCellString>& values) {
        for (const auto& value : values) {
            spec_ = range_of(value.from, value.to);
            opened_ = value.account;
        }
    }

    // ----------------------------------------------------------------- thens

    void then_report_rows_are(const std::vector<ReportRowString>& values) {
        ASSERT_EQ(values.size(), report_.rows.size()) << "number of report rows";
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

    void then_payee_report_rows_are(const std::vector<PayeeReportRowString>& values) {
        const std::optional<std::string> only =
            category_filter_.empty() ? std::nullopt
                                     : std::optional<std::string>(category_filter_);
        const std::vector<reports::PayeeRow> got =
            reports::payee_report(chart_, transactions_, spec_, only);
        ASSERT_EQ(values.size(), got.size()) << "number of payee rows";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].payee, got[i].payee) << "row " << i;
            EXPECT_EQ(Money(values[i].amount).cents(), got[i].amount.cents())
                << got[i].payee << " amount";
            EXPECT_EQ(std::stoi(values[i].count), got[i].count) << got[i].payee << " count";
        }
    }

    void then_transactions_behind_it_are(const std::vector<DrillDownRowString>& values) {
        // Any figure can be taken apart, a subtotal included: opening one reaches
        // through the placeholder to the leaves beneath it.
        std::vector<DrillDownRowString> got;
        for (const ledger::Transaction& t : transactions_) {
            if (t.date < spec_.from || spec_.to < t.date) continue;
            for (const ledger::Posting& p : t.postings) {
                if (!chart::Chart::is_descendant_or_self(p.account.value(), opened_)) continue;
                DrillDownRowString row;
                row.ref = t.ref.value();
                row.date = t.date.iso();
                row.payee = t.payee.value();
                row.account = p.account.value();
                row.amount = p.amount.in_register();
                got.push_back(row);
            }
        }
        ASSERT_EQ(values.size(), got.size()) << "number of transactions behind the figure";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].ref, got[i].ref) << "row " << i;
            EXPECT_EQ(values[i].date, got[i].date) << "row " << i;
            EXPECT_EQ(values[i].account, got[i].account) << "row " << i;
            EXPECT_EQ(Money(values[i].amount).cents(), Money(got[i].amount).cents())
                << "row " << i;
        }
    }

    // ------------------------------------------------------------ the rules

    void examples_businessrule_what_the_named_date_ranges_mean(
            const std::vector<NamedRangeString>& values) {
        // Taken relative to today, which the rule states as 2024-05-15, and to
        // the first date in the book, which its All row states as 2005-01-01.
        const types::Date today(2024, 5, 15);
        const types::Date book_start(2005, 1, 1);
        for (const auto& value : values) {
            const NamedRangeTyped t = NamedRangeTyped::from_string_struct(value);
            const auto got = reports::named_range(t.name, today, book_start);
            ASSERT_TRUE(got.has_value()) << "unknown range: " << t.name;
            EXPECT_EQ(t.from, got->from.iso()) << t.name << " from -- " << t.notes;
            EXPECT_EQ(t.to, got->to.iso()) << t.name << " to -- " << t.notes;
        }
    }

    void examples_businessrule_how_a_fiscal_year_is_named_and_bounded(
            const std::vector<FiscalYearString>& values) {
        for (const auto& value : values) {
            const FiscalYearTyped t = FiscalYearTyped::from_string_struct(value);
            // The label is written FY2024; the number is what bounds the year.
            const std::string digits = t.fiscalyear.substr(t.fiscalyear.size() - 4);
            const reports::Range got =
                reports::fiscal_year(t.startmonth, std::stoi(digits));
            EXPECT_EQ(t.from, got.from.iso()) << t.fiscalyear << " from -- " << t.notes;
            EXPECT_EQ(t.to, got.to.iso()) << t.fiscalyear << " to -- " << t.notes;
        }
    }

private:
    chart::Chart chart_;
    ledger::Ledger book_;
    std::vector<ledger::Transaction> transactions_;
    reports::Spec spec_;
    reports::CategoryReport report_;
    std::string category_filter_;
    std::string opened_;

    static reports::Spec range_of(const std::string& from, const std::string& to) {
        reports::Spec s;
        const auto a = types::Date::from_iso(from);
        const auto b = types::Date::from_iso(to);
        if (a) s.from = *a;
        if (b) s.to = *b;
        return s;
    }

    void run(const ReportSpecString& value) {
        spec_ = range_of(value.from, value.to);
        spec_.depth = value.depth.empty() ? 0 : std::stoi(value.depth);
        spec_.zero_rows = parse_bool_cell(value.zerorows);
        spec_.include_investment_activity =
            parse_bool_cell(value.includeinvestmentactivity);
        spec_.quicken_signs = parse_bool_cell(value.quickensigns);
        category_filter_.clear();
        report_ = reports::category_report(chart_, book_, spec_);
    }
public:

    void then_year_column_rows_are(const std::vector<YearColumnRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_year_column_rows_are";
    }

    void then_report_columns_are(const std::vector<ReportColumnString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_report_columns_are";
    }

    void then_average_column_is(const std::vector<AverageRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_average_column_is";
    }

    void then_net_worth_columns_are(const std::vector<NetWorthColumnString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_net_worth_columns_are";
    }

    void then_csv_written_is(const std::string& value) {
        std::cout << value << "\n";
        ADD_FAILURE() << "Not implemented: then_csv_written_is";
    }

    void examples_datatype_reportgrouping(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_reportgrouping";
    }

    void examples_datatype_reportinterval(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_reportinterval";
    }

public:

    // Each case is built as a real one-transaction book and put through the
    // report, rather than asking the rule in the abstract: the table is then a
    // statement about what the report does and not about what a helper thinks.
    void examples_businessrule_income_and_expense_arising_inside_an_investment_account_is_left_out(const std::vector<InvestmentActivityCaseString>& values) {
        for (const auto& value : values) {
            const std::string where = value.transactiontouches;
            const bool brokerage = where.find("brokerage") != std::string::npos;
            const bool retirement = where.find("retirement") != std::string::npos;
            const bool bank = where.find("bank") != std::string::npos;

            chart_ = chart::Chart();
            const auto put = [&](const std::string& path, const char* type) {
                chart::Account a;
                a.path = types::AccountPath(path);
                a.type = types::account_type_from_string(type);
                chart_.put(a);
            };
            put("Assets:Checking", "Bank");
            put("Assets:Brokerage", "Investment");
            put("Assets:Ken IRA", "Investment");
            put(value.category, value.category.rfind("Income", 0) == 0 ? "Income" : "Expense");

            // The category takes a credit for income and a debit for an expense,
            // and the real accounts take the other side.
            const bool is_income = value.category.rfind("Income", 0) == 0;
            const Money hundred{"100.00"};
            const types::Date on{2024, 5, 1};
            book_ = ledger::Ledger();
            transactions_.clear();
            ledger::Transaction t;
            t.ref = types::TransactionRef("T1");
            t.date = on;
            t.payee = types::PayeeName("Vanguard");

            std::vector<std::pair<std::string, Money>> legs;
            legs.push_back({value.category, is_income ? -hundred : hundred});
            // Where the money was. Both a brokerage and a bank means the one
            // transaction touched each, which is the case that decides whether
            // the investment side taints the whole of it.
            if (brokerage && bank) {
                legs.push_back({"Assets:Brokerage", is_income ? Money{"40.00"} : Money{"-40.00"}});
                legs.push_back({"Assets:Checking", is_income ? Money{"60.00"} : Money{"-60.00"}});
            } else if (brokerage) {
                legs.push_back({"Assets:Brokerage", is_income ? hundred : -hundred});
            } else if (retirement) {
                legs.push_back({"Assets:Ken IRA", is_income ? hundred : -hundred});
            } else {
                legs.push_back({"Assets:Checking", is_income ? hundred : -hundred});
            }

            for (const auto& leg : legs) {
                book_.add({on, types::AccountPath(leg.first), leg.second, t.ref.value()});
                ledger::Posting posting;
                posting.account = types::AccountPath(leg.first);
                posting.amount = leg.second;
                t.postings.push_back(posting);
            }
            transactions_.push_back(t);

            reports::Spec spec;
            spec.from = types::Date{2024, 1, 1};
            spec.to = types::Date{2024, 12, 31};
            const reports::CategoryReport r =
                reports::category_report(chart_, book_, spec);
            const bool counted = r.totals.income.cents() != 0 ||
                                 r.totals.expenses.cents() != 0;
            EXPECT_EQ(parse_bool_cell(value.counts), counted)
                << where << " / " << value.category << " -- " << value.notes;
        }
    }

public:

    void given_dated_postings_are(const std::vector<DatedPostingLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_dated_postings_are";
    }

    void examples_businessrule_a_category_with_its_own_transactions_shows_them_on_an_other_row(const std::vector<OtherRowNamingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_a_category_with_its_own_transactions_shows_them_on_an_other_row";
    }

public:

    void then_report_lines_are(const std::vector<ReportDetailLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_report_lines_are";
    }

    void examples_datatype_reportdetail(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_reportdetail";
    }

    void examples_datatype_reportlinekind(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_reportlinekind";
    }

public:

    void when_balance_sheet_run(const std::vector<ReportSpecString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_balance_sheet_run";
    }

    void then_balance_sheet_rows_are(const std::vector<BalanceSheetRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_balance_sheet_rows_are";
    }

    void then_balance_sheet_total_is(const std::vector<NetWorthString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_balance_sheet_total_is";
    }

    void then_net_worth_comes_from(const std::vector<NetWorthTieString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_net_worth_comes_from";
    }

    void examples_datatype_balancesheetsection(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_balancesheetsection";
    }

};
