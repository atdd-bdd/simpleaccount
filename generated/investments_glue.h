#pragma once
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>
#include "common/common.h"
#include "date.h"
#include "money.h"
#include "holding.h"

class InvestmentsGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

    void given_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is";
    }

    void given_the_chart_of_accounts_is_as_previous() {
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is_as_previous";
    }

    void given_holdings_are(const std::vector<HoldingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_holdings_are";
    }

    void given_postings_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_postings_are";
    }

    void then_rolled_up_balances_are(const std::vector<RolledUpBalanceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_rolled_up_balances_are";
    }

    void when_shares_bought(const std::vector<TradeEntryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_shares_bought";
    }

    void then_postings_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_postings_are";
    }

    void then_holdings_are(const std::vector<HoldingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_holdings_are";
    }

    void given_postings_are_as_previous() {
        ADD_FAILURE() << "Not implemented: given_postings_are_as_previous";
    }

    void then_balances_are(const std::vector<AccountBalanceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_balances_are";
    }

    void when_shares_sold(const std::vector<TradeEntryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_shares_sold";
    }

    void then_rejected_because(const std::vector<RejectionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_rejected_because";
    }

    void when_price_recorded(const std::vector<PriceEntryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_price_recorded";
    }

    void given_holdings_are_as_previous() {
        ADD_FAILURE() << "Not implemented: given_holdings_are_as_previous";
    }

    void then_postings_created_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_postings_created_are";
    }

    void when_dividend_recorded(const std::vector<IncomeEntryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_dividend_recorded";
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

    void when_value_stated(const std::vector<ValueEntryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_value_stated";
    }

    void when_gain_report_requested() {
        ADD_FAILURE() << "Not implemented: when_gain_report_requested";
    }

    void then_told_that(const std::vector<NoticeString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_told_that";
    }

    void given_today_is(const std::vector<TodayString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_today_is";
    }

    void when_holdings_report_run() {
        ADD_FAILURE() << "Not implemented: when_holdings_report_run";
    }

    void then_holdings_report_rows_are(const std::vector<HoldingsReportRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_holdings_report_rows_are";
    }

    void then_holdings_report_total_is(const std::vector<HoldingsTotalString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_holdings_report_total_is";
    }

    void examples_businessrule_what_the_account_of_a_holding_is_called(const std::vector<HoldingAccountNameString>& values) {
        for (const auto& value : values) {
            const HoldingAccountNameTyped t =
                HoldingAccountNameTyped::from_string_struct(value);
            EXPECT_EQ(t.accountname,
                      holdings::account_name_for(t.securityname, t.symbol))
                << t.securityname;
        }
    }

    void examples_businessrule_the_value_of_a_holding_is_its_shares_times_its_price(const std::vector<HoldingValueString>& values) {
        for (const auto& value : values) {
            const HoldingValueTyped t = HoldingValueTyped::from_string_struct(value);
            EXPECT_EQ(Money(t.value).cents(),
                      holdings::value_of(std::stod(t.shares), std::stod(t.price)).cents())
                << t.shares << " at " << t.price << " -- " << t.notes;
        }
    }

    void examples_businessrule_how_the_carried_price_changes_when_more_shares_are_bought(const std::vector<AveragePriceString>& values) {
        for (const auto& value : values) {
            const AveragePriceTyped t = AveragePriceTyped::from_string_struct(value);
            const holdings::AveragedHolding got = holdings::after_buying(
                std::stod(t.heldshares), Money(t.heldvalue),
                std::stod(t.boughtshares), std::stod(t.boughtprice));
            EXPECT_DOUBLE_EQ(std::stod(t.newshares), got.shares) << t.notes;
            EXPECT_EQ(Money(t.newvalue).cents(), got.value.cents()) << t.notes;
            EXPECT_DOUBLE_EQ(std::stod(t.newprice), got.price) << t.notes;
        }
    }

    void examples_businessrule_when_a_price_is_called_stale(const std::vector<StalenessString>& values) {
        for (const auto& value : values) {
            const StalenessTyped t = StalenessTyped::from_string_struct(value);
            EXPECT_EQ(t.stale, holdings::is_stale(t.daysold))
                << t.daysold << " days old -- " << t.notes;
        }
    }

};
