#pragma once
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>
#include "common/common.h"
#include "date.h"
#include "report_ranges.h"

class ReportsGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

    void given_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is";
    }

    void given_postings_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_postings_are";
    }

    void given_the_chart_of_accounts_is_as_previous() {
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is_as_previous";
    }

    void given_postings_are_as_previous() {
        ADD_FAILURE() << "Not implemented: given_postings_are_as_previous";
    }

    void when_report_run(const std::vector<ReportSpecString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_report_run";
    }

    void then_report_rows_are(const std::vector<ReportRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_report_rows_are";
    }

    void then_report_total_is(const std::vector<ReportTotalString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_report_total_is";
    }

    void then_payee_report_rows_are(const std::vector<PayeeReportRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_payee_report_rows_are";
    }

    void when_report_run_for_category(const std::vector<CategoryFilterString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_report_run_for_category";
    }

    void then_year_column_rows_are(const std::vector<YearColumnRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_year_column_rows_are";
    }

    void then_report_columns_are(const std::vector<ReportColumnString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_report_columns_are";
    }

    void when_report_run_with_average(const std::vector<ReportSpecString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_report_run_with_average";
    }

    void then_average_column_is(const std::vector<AverageRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_average_column_is";
    }

    void when_report_figure_opened(const std::vector<ReportCellString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_report_figure_opened";
    }

    void then_transactions_behind_it_are(const std::vector<DrillDownRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_transactions_behind_it_are";
    }

    void when_net_worth_report_run(const std::vector<ReportSpecString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_net_worth_report_run";
    }

    void then_net_worth_columns_are(const std::vector<NetWorthColumnString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_net_worth_columns_are";
    }

    void when_report_exported_as_csv(const std::vector<ReportSpecString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_report_exported_as_csv";
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

    void examples_businessrule_what_the_named_date_ranges_mean(const std::vector<NamedRangeString>& values) {
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

    void examples_businessrule_how_a_fiscal_year_is_named_and_bounded(const std::vector<FiscalYearString>& values) {
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

};
