#pragma once
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>
#include "common/common.h"

class WorkedExampleGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

    void given_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is";
    }

    void given_the_csv_profile_is(const std::vector<CsvProfileString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_csv_profile_is";
    }

    void given_import_target_is(const std::vector<ImportTargetString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_import_target_is";
    }

    void given_the_file_contains(const std::string& value) {
        std::cout << value << "\n";
        ADD_FAILURE() << "Not implemented: given_the_file_contains";
    }

    void given_rows_imported() {
        ADD_FAILURE() << "Not implemented: given_rows_imported";
    }

    void then_balances_are(const std::vector<AccountBalanceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_balances_are";
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

    void when_categories_assigned(const std::vector<CategoryAssignmentString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_categories_assigned";
    }

};
