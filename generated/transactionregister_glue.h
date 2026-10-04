#pragma once
#include <gtest/gtest.h>
#include <algorithm>
#include <utility>
#include <iostream>
#include <string>
#include <vector>
#include "common/common.h"
#include "date.h"
#include "register_view.h"
#include "money.h"
#include "account_type.h"

class TransactionRegisterGlue {
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

    void when_register_opened_on(const std::vector<AccountSelectString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_register_opened_on";
    }

    void then_register_lines_are(const std::vector<RegisterLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_register_lines_are";
    }

    void given_the_chart_of_accounts_is_as_previous() {
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is_as_previous";
    }

    void given_postings_are_as_previous() {
        ADD_FAILURE() << "Not implemented: given_postings_are_as_previous";
    }

    void given_postings_in_entry_order_are(const std::vector<SequencedPostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_postings_in_entry_order_are";
    }

    void given_postings_in_entry_order_are_as_previous() {
        ADD_FAILURE() << "Not implemented: given_postings_in_entry_order_are_as_previous";
    }

    void when_register_sorted_by(const std::vector<SortByString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_register_sorted_by";
    }

    void then_running_balance_is_shown(const std::vector<BalanceColumnStateString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_running_balance_is_shown";
    }

    void when_line_expanded(const std::vector<TransactionSelectString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_line_expanded";
    }

    void then_split_lines_shown_are(const std::vector<SplitLineShownString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_split_lines_shown_are";
    }

    void given_today_is(const std::vector<TodayString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_today_is";
    }

    void then_today_marker_is(const std::vector<TodayMarkerString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_today_marker_is";
    }

    void given_the_register_holds(const std::vector<RegisterExtentString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_register_holds";
    }

    void then_register_view_is(const std::vector<RegisterViewString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_register_view_is";
    }

    void when_register_filtered_to_dates(const std::vector<DateRangeString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_register_filtered_to_dates";
    }

    void then_opening_balance_line_is(const std::vector<OpeningLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_opening_balance_line_is";
    }

    void when_register_searched_for(const std::vector<SearchTermString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_register_searched_for";
    }

    void then_blank_line_is(const std::vector<RegisterLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_blank_line_is";
    }

    void given_today_is_as_previous() {
        ADD_FAILURE() << "Not implemented: given_today_is_as_previous";
    }

    void when_blank_line_filled(const std::vector<RegisterLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_blank_line_filled";
    }

    void when_blank_line_left() {
        ADD_FAILURE() << "Not implemented: when_blank_line_left";
    }

    void examples_businessrule_what_the_two_amount_columns_are_called(const std::vector<ColumnHeadingsString>& values) {
        for (const auto& value : values) {
            const ColumnHeadingsTyped t = ColumnHeadingsTyped::from_string_struct(value);
            const reg::ColumnHeadings got =
                reg::headings_for(types::account_type_from_string(t.accounttype));
            EXPECT_EQ(t.outcolumn, got.out_column) << t.accounttype;
            EXPECT_EQ(t.incolumn, got.in_column) << t.accounttype;
        }
    }

    void examples_businessrule_the_running_balance_uses_the_display_sign_of_the_account(const std::vector<RunningBalanceSignString>& values) {
        for (const auto& value : values) {
            const RunningBalanceSignTyped t =
                RunningBalanceSignTyped::from_string_struct(value);
            EXPECT_EQ(Money(t.balanceshown).cents(),
                      reg::balance_shown(types::account_type_from_string(t.accounttype),
                                         Money(t.postingamount)).cents())
                << t.accounttype << " -- " << t.notes;
        }
    }

    void examples_businessrule_what_order_the_register_is_in(const std::vector<RegisterOrderString>& values) {
        // The table gives each line its position, so sorting the lines by the
        // rule must reproduce that order.
        std::vector<std::pair<reg::OrderKey, int>> lines;
        for (const auto& value : values) {
            const RegisterOrderTyped t = RegisterOrderTyped::from_string_struct(value);
            const auto date = types::Date::from_iso(t.date);
            ASSERT_TRUE(date.has_value()) << t.date;
            reg::OrderKey key;
            key.date_key = types::Date::day_number(*date);
            key.entry_seq = t.entryseq;
            lines.emplace_back(key, t.position);
        }
        std::stable_sort(lines.begin(), lines.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        for (std::size_t i = 0; i < lines.size(); ++i)
            EXPECT_EQ(static_cast<int>(i) + 1, lines[i].second)
                << "line " << (i + 1) << " of the register";
    }

    void examples_datatype_sortcolumn(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_sortcolumn";
    }

    void examples_datatype_searchtext(const std::vector<ValidValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_searchtext";
    }

};
