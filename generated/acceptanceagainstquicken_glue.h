#pragma once
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>
#include "common/common.h"

class AcceptanceAgainstQuickenGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

    void given_the_quicken_report_states(const std::vector<QuickenFigureString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_quicken_report_states";
    }

    void when_reports_compared(const std::vector<QuickenReportString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_reports_compared";
    }

    void then_comparison_summary_is(const std::vector<ComparisonSummaryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_comparison_summary_is";
    }

    void given_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is";
    }

    void given_postings_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_postings_are";
    }

    void then_comparison_lines_are(const std::vector<ComparisonLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_comparison_lines_are";
    }

    void then_lines_only_in_the_quicken_report_are(const std::vector<ComparisonLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_lines_only_in_the_quicken_report_are";
    }

    void then_lines_only_in_our_report_are(const std::vector<ComparisonLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_lines_only_in_our_report_are";
    }

    void given_today_is(const std::vector<TodayString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_today_is";
    }

    void then_current_year_figures_are(const std::vector<PeriodBoundaryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_current_year_figures_are";
    }

    void given_migrated_books_are(const std::vector<MigratedBookString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_migrated_books_are";
    }

    void then_comparison_heading_is(const std::vector<ComparisonHeadingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_comparison_heading_is";
    }

    void given_the_quicken_reports_state(const std::vector<ImportDeltaString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_quicken_reports_state";
    }

    void then_import_delta_is(const std::vector<ImportDeltaString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_import_delta_is";
    }

    void given_the_quicken_reports_cross_check(const std::vector<TransferCrossCheckString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_quicken_reports_cross_check";
    }

    void then_transfer_cross_check_is(const std::vector<TransferCrossCheckString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_transfer_cross_check_is";
    }

    void when_balance_report_compared(const std::vector<QuickenReportString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_balance_report_compared";
    }

    void then_lines_not_compared_are(const std::vector<LineNotComparedString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_lines_not_compared_are";
    }

    void given_imported_postings_are(const std::vector<ImportedPostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_imported_postings_are";
    }

    void given_import_target_is(const std::vector<ImportTargetString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_import_target_is";
    }

    void when_transactions_imported(const std::vector<OfxTransactionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_transactions_imported";
    }

    void then_import_summary_is(const std::vector<ImportSummaryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_import_summary_is";
    }

    void given_the_chart_of_accounts_is_as_previous() {
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is_as_previous";
    }

    void then_report_rows_are(const std::vector<ReportRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_report_rows_are";
    }

    void then_comparisons_to_run_are(const std::vector<ComparisonPlanString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_comparisons_to_run_are";
    }

    void given_migrated_books_are_as_previous() {
        ADD_FAILURE() << "Not implemented: given_migrated_books_are_as_previous";
    }

    void then_book_extents_are(const std::vector<MigratedBookString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_book_extents_are";
    }

    void examples_datatype_comparisonrun(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_comparisonrun";
    }

    void examples_datatype_quickenreportkind(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_quickenreportkind";
    }

    void examples_businessrule_when_a_comparison_passes(const std::vector<PassConditionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_when_a_comparison_passes";
    }

    void examples_businessrule_how_a_quicken_category_name_becomes_an_account_path(const std::vector<ReportLineMappingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_how_a_quicken_category_name_becomes_an_account_path";
    }

    void examples_businessrule_how_the_signs_of_the_two_reports_are_lined_up(const std::vector<SignMappingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_how_the_signs_of_the_two_reports_are_lined_up";
    }

    void examples_businessrule_the_two_uncategorized_lines_map_one_to_one(const std::vector<UncategorizedComparisonString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_the_two_uncategorized_lines_map_one_to_one";
    }

    void examples_businessrule_how_a_quicken_report_writes_a_category_tree(const std::vector<ReportTreeShapeString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_how_a_quicken_report_writes_a_category_tree";
    }

    void examples_businessrule_what_each_report_kind_is_expected_to_include(const std::vector<InclusionRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_what_each_report_kind_is_expected_to_include";
    }

    void examples_businessrule_the_state_of_the_reconciliation(const std::vector<ReconciliationStatusString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_the_state_of_the_reconciliation";
    }

    void examples_businessrule_which_sections_have_to_be_included_for_a_year_to_reconcile(const std::vector<SectionNecessityString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_which_sections_have_to_be_included_for_a_year_to_reconcile";
    }

    void examples_businessrule_what_simpleaccount_computes_for_the_business_book_by_year(const std::vector<YearFigureString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_what_simpleaccount_computes_for_the_business_book_by_year";
    }

    void examples_businessrule_what_simpleaccount_computes_for_the_personal_book_by_year(const std::vector<YearFigureString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_what_simpleaccount_computes_for_the_personal_book_by_year";
    }

    void examples_businessrule_how_a_report_has_to_be_exported_to_be_comparable(const std::vector<ExportSettingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_how_a_report_has_to_be_exported_to_be_comparable";
    }

    void examples_businessrule_in_what_order_the_comparisons_are_run_and_what_stopping_means(const std::vector<RunOrderString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_in_what_order_the_comparisons_are_run_and_what_stopping_means";
    }

};
