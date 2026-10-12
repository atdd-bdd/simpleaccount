#pragma once
#include <gtest/gtest.h>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include "common/common.h"
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "holding.h"
#include "investing.h"
#include "ledger.h"
#include "money.h"
#include "report.h"
#include "text_types.h"
#include "transaction_id.h"

// Glue for Investments.spectable.
//
// One in-memory chart, ledger and holdings list per scenario -- there is no
// book on disk here, unlike Books.spectable, because nothing in this file is
// about persistence. A "When" step builds a transaction through investing.h
// and applies it with apply_transaction(), which both adds it to the ledger
// (so a later balance or report is correct) and records its postings in
// last_created_ -- what "Then postings are" and "Then postings created are"
// both check. The two step names mean the same thing in this file: every
// instance of either lists only what the last "When" created, never what a
// Given already established, so there is one notion of "created" to get
// right rather than two.
//
// given_holdings_are_as_previous() and given_postings_are_as_previous() are
// no-ops, same as given_the_chart_of_accounts_is_as_previous() elsewhere in
// this project: the real data, when there is any, arrives through the
// Background's own inlined call earlier in the same test. This file's
// Background establishes only the chart, so a scenario that writes "Given
// holdings are as previous" or "Given postings are as previous" with nothing
// of that kind earlier in the same test is asking for something that was
// never there -- see the $WIP comments left on the affected scenarios rather
// than guessed around here.
class InvestmentsGlue {
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
            a.bank = blank(value.bank);
            a.account_number = blank(value.accountnumber);
            chart_.put(a);
        }
    }

    // The Background's chart was already loaded moments ago by its own real
    // call, inlined ahead of this one in the same test, so there is nothing
    // left to do.
    void given_the_chart_of_accounts_is_as_previous() {}

    void given_holdings_are(const std::vector<HoldingString>& values) {
        holdings_.clear();
        for (const auto& value : values) {
            holdings::Holding h;
            h.account = types::AccountPath(value.account);
            const std::string sn = blank(value.securityname);
            if (!sn.empty()) h.security_name = types::SecurityName(sn);
            h.symbol = types::Symbol(blank(value.symbol));
            h.shares = std::stod(value.shares);
            h.price = std::stod(value.price);
            const auto as_of = types::Date::from_iso(value.priceasof);
            ASSERT_TRUE(as_of.has_value()) << value.priceasof;
            h.price_as_of = *as_of;
            holdings_.push_back(h);
        }
    }

    void given_holdings_are_as_previous() {}

    void given_postings_are(const std::vector<PostingRowString>& values) {
        transactions_.clear();
        ledger_ = ledger::Ledger();
        for (const auto& value : values) {
            ledger::Transaction* into = nullptr;
            for (ledger::Transaction& t : transactions_)
                if (t.ref.value() == value.ref) into = &t;
            if (into == nullptr) {
                ledger::Transaction made;
                made.id = ledger::new_id();
                made.ref = types::TransactionRef(value.ref);
                const auto on = types::Date::from_iso(value.date);
                ASSERT_TRUE(on.has_value()) << value.date;
                made.date = *on;
                made.payee = types::PayeeName(blank(value.payee));
                transactions_.push_back(made);
                into = &transactions_.back();
            }
            ledger::Posting posting;
            posting.account = chart_.ref_for_path(value.account);
            posting.amount = Money(value.amount);
            posting.memo = blank(value.memo);
            posting.cleared = types::cleared_status_from_string(value.cleared);
            into->postings.push_back(posting);
            ledger_.add({into->date, types::AccountPath(value.account), posting.amount,
                        into->ref.value()});
        }
    }

    void given_postings_are_as_previous() {}

    void given_today_is(const std::vector<TodayString>& values) {
        ASSERT_FALSE(values.empty());
        const auto d = types::Date::from_iso(values.front().date);
        ASSERT_TRUE(d.has_value()) << values.front().date;
        today_ = *d;
    }

    // ----------------------------------------------------------------- whens

    void when_shares_bought(const std::vector<TradeEntryString>& values) {
        last_created_.clear();
        for (const auto& value : values) {
            const types::AccountPath account(value.account);
            const auto date = types::Date::from_iso(value.date);
            ASSERT_TRUE(date.has_value()) << value.date;
            const types::Symbol symbol(blank(value.symbol));
            const double shares = std::stod(value.shares);
            const double price = std::stod(value.price);
            const Money fee(value.fee);

            holdings::Holding* existing = holdings::find_by_account(holdings_, account.value());
            const double held_shares = existing != nullptr ? existing->shares : 0.0;
            const Money held_value = ledger_.raw_balance(account.value());

            const investing::BuyResult result = investing::buy_shares(
                chart_, account, held_shares, held_value, shares, price, fee, *date, next_ref());
            apply_transaction(result.transaction);

            if (existing != nullptr) {
                existing->shares = result.new_shares;
                existing->price = result.new_price;
                existing->price_as_of = *date;
            } else {
                holdings::Holding h;
                h.account = account;
                h.symbol = symbol;
                h.shares = result.new_shares;
                h.price = result.new_price;
                h.price_as_of = *date;
                holdings_.push_back(h);
            }
        }
    }

    void when_shares_sold(const std::vector<TradeEntryString>& values) {
        last_created_.clear();
        rejection_ = chart::Rejection();
        for (const auto& value : values) {
            const types::AccountPath account(value.account);
            const auto date = types::Date::from_iso(value.date);
            ASSERT_TRUE(date.has_value()) << value.date;
            const double shares = std::stod(value.shares);
            const double price = std::stod(value.price);

            holdings::Holding* existing = holdings::find_by_account(holdings_, account.value());
            ASSERT_NE(nullptr, existing) << "no holding for " << account.value();
            const Money held_value = ledger_.raw_balance(account.value());

            const investing::SellResult result = investing::sell_shares(
                chart_, account, existing->symbol, existing->shares, existing->price, held_value,
                shares, price, *date, next_ref());
            rejection_ = result.rejection;
            if (rejection_.refused) continue;
            apply_transaction(result.transaction);
            existing->shares = result.new_shares;
            existing->price = result.new_price;
            existing->price_as_of = *date;
        }
    }

    void when_price_recorded(const std::vector<PriceEntryString>& values) {
        last_created_.clear();
        for (const auto& value : values) {
            const types::Symbol symbol(blank(value.symbol));
            const double new_price = std::stod(value.price);
            const auto as_of = types::Date::from_iso(value.asof);
            ASSERT_TRUE(as_of.has_value()) << value.asof;
            // Every account holding the symbol is revalued, each in its own
            // transaction: a posting belongs to one holding, because each is
            // its own account.
            for (holdings::Holding& h : holdings_) {
                if (h.symbol.value() != symbol.value()) continue;
                const Money held_value = ledger_.raw_balance(h.account.value());
                const investing::RevalueResult result = investing::revalue(
                    chart_, h.account, held_value, h.shares, new_price, *as_of, next_ref());
                if (result.posted) apply_transaction(result.transaction);
                // The date is brought forward even where nothing posted: the
                // price was confirmed, whether or not it changed.
                h.price = new_price;
                h.price_as_of = *as_of;
            }
        }
    }

    void when_dividend_recorded(const std::vector<IncomeEntryString>& values) {
        last_created_.clear();
        for (const auto& value : values) {
            const types::AccountPath account(value.account);
            const types::AccountPath category(value.category);
            const Money amount(value.amount);
            const bool reinvest = parse_bool_cell(value.reinvest);
            const auto date = types::Date::from_iso(value.date);
            ASSERT_TRUE(date.has_value()) << value.date;

            holdings::Holding* existing = holdings::find_by_account(holdings_, account.value());
            const double held_shares = existing != nullptr ? existing->shares : 0.0;
            const Money held_value = ledger_.raw_balance(account.value());
            const double bought_shares = std::stod(value.shares);
            const double bought_price = std::stod(value.price);

            const investing::DividendResult result = investing::record_dividend(
                chart_, account, category, amount, reinvest, held_shares, held_value, bought_shares,
                bought_price, *date, next_ref());
            apply_transaction(result.transaction);

            if (reinvest && existing != nullptr) {
                existing->shares = result.new_shares;
                existing->price = result.new_price;
                existing->price_as_of = *date;
            }
        }
    }

    void when_value_stated(const std::vector<ValueEntryString>& values) {
        last_created_.clear();
        rejection_ = chart::Rejection();
        for (const auto& value : values) {
            const types::AccountPath account(value.account);
            const Money new_value(value.value);
            const auto as_of = types::Date::from_iso(value.asof);
            ASSERT_TRUE(as_of.has_value()) << value.asof;

            const holdings::Holding* existing =
                holdings::find_by_account(holdings_, account.value());
            const Money current_value = ledger_.raw_balance(account.value());
            const types::Symbol symbol = existing != nullptr ? existing->symbol : types::Symbol();

            const investing::ValueResult result = investing::state_value(
                chart_, account, existing != nullptr, symbol, current_value, new_value, *as_of,
                next_ref());
            rejection_ = result.rejection;
            if (rejection_.refused) continue;
            apply_transaction(result.transaction);
        }
    }

    void when_gain_report_requested() { notice_ = investing::gain_report_notice(); }

    void when_holdings_report_run() {
        holdings_report_ = investing::holdings_report_rows(holdings_, today_);
        holdings_securities_ = Money();
        for (const auto& row : holdings_report_) holdings_securities_ += row.value;
        // Cash is whatever the brokerage's own accounts hold that is not one
        // of the holdings above -- the plain cash leg beside them.
        holdings_cash_ = Money();
        for (const chart::Account& a : chart_.all()) {
            if (a.placeholder) continue;
            if (types::class_of(a.type) != types::AccountClass::Real) continue;
            if (types::group_of(a.type) != types::AccountGroup::Investments) continue;
            if (holdings::find_by_account(holdings_, a.path.value()) != nullptr) continue;
            holdings_cash_ += ledger_.raw_balance(a.path.value());
        }
        holdings_total_ = holdings_cash_ + holdings_securities_;
    }

    void when_report_run(const std::vector<ReportSpecString>& values) {
        for (const auto& value : values) {
            reports::Spec spec;
            const auto from = types::Date::from_iso(value.from);
            const auto to = types::Date::from_iso(value.to);
            if (from) spec.from = *from;
            if (to) spec.to = *to;
            report_ = reports::category_report(chart_, ledger_, spec);
        }
    }

    // ----------------------------------------------------------------- thens

    void then_rolled_up_balances_are(const std::vector<RolledUpBalanceString>& values) {
        for (const auto& value : values) {
            EXPECT_EQ(Money(value.ownbalance).cents(), ledger_.own_balance(value.account).cents())
                << value.account << " own -- " << value.notes;
            EXPECT_EQ(Money(value.totalbalance).cents(),
                      ledger_.total_balance(value.account).cents())
                << value.account << " total -- " << value.notes;
        }
    }

    void then_balances_are(const std::vector<AccountBalanceString>& values) {
        for (const auto& value : values) {
            const chart::Account* a = chart_.find(value.account);
            ASSERT_NE(nullptr, a) << "no account " << value.account;
            if (value.rawbalance != DNCString)
                EXPECT_EQ(Money(value.rawbalance).cents(), ledger_.raw_balance(value.account).cents())
                    << value.account << " raw -- " << value.notes;
            if (value.displaybalance != DNCString)
                EXPECT_EQ(Money(value.displaybalance).cents(),
                          ledger_.display_balance(value.account, a->type).cents())
                    << value.account << " display -- " << value.notes;
        }
    }

    // "Then postings are" and "Then postings created are" ask the same
    // question in this file: what did the last "When" create.
    void then_postings_are(const std::vector<PostingRowString>& values) {
        check_created_postings(values);
    }

    void then_postings_created_are(const std::vector<PostingRowString>& values) {
        check_created_postings(values);
    }

    void then_holdings_are(const std::vector<HoldingString>& values) {
        ASSERT_EQ(values.size(), holdings_.size()) << "holdings";
        for (std::size_t i = 0; i < values.size(); ++i)
            EXPECT_EQ(values[i], as_holding_string(holdings_[i])) << "holding " << i;
    }

    void then_rejected_because(const std::vector<RejectionString>& values) {
        for (const auto& value : values) {
            EXPECT_TRUE(rejection_.refused) << "expected a refusal: " << value.reason;
            EXPECT_EQ(value.reason, rejection_.reason);
        }
    }

    void then_told_that(const std::vector<NoticeString>& values) {
        for (const auto& value : values) EXPECT_EQ(value.message, notice_);
    }

    void then_holdings_report_rows_are(const std::vector<HoldingsReportRowString>& values) {
        ASSERT_EQ(values.size(), holdings_report_.size()) << "holdings report rows";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const investing::HoldingsReportRow& row = holdings_report_[i];
            HoldingsReportRowString got;
            got.securityname = row.security_name.empty() ? "none" : row.security_name;
            got.symbol = row.symbol.empty() ? "none" : row.symbol;
            got.shares = investing::format_shares(row.shares);
            got.price = format_price(row.price);
            got.value = row.value.in_register();
            got.priceasof = row.price_as_of.iso();
            got.daysold = std::to_string(row.days_old);
            got.stale = row.stale ? "true" : "false";
            EXPECT_EQ(values[i], got) << "holdings report row " << i;
        }
    }

    void then_holdings_report_total_is(const std::vector<HoldingsTotalString>& values) {
        for (const auto& value : values) {
            EXPECT_EQ(Money(value.cash).cents(), holdings_cash_.cents()) << "holdings cash";
            EXPECT_EQ(Money(value.securities).cents(), holdings_securities_.cents())
                << "holdings securities";
            EXPECT_EQ(Money(value.total).cents(), holdings_total_.cents()) << "holdings total";
        }
    }

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

    // ------------------------------------------------------------ the rules

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

private:
    chart::Chart chart_;
    ledger::Ledger ledger_;
    std::vector<ledger::Transaction> transactions_;
    std::vector<holdings::Holding> holdings_;
    types::Date today_{2024, 1, 1};

    chart::Rejection rejection_;
    std::vector<PostingRowString> last_created_;
    std::string notice_;
    reports::CategoryReport report_;

    std::vector<investing::HoldingsReportRow> holdings_report_;
    Money holdings_cash_;
    Money holdings_securities_;
    Money holdings_total_;

    // A Default of "none" arrives as the literal word.
    static std::string blank(const std::string& s) {
        return (s == "none" || s == DNCString) ? std::string() : s;
    }

    static std::string format_price(double price) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.4f", price);
        return std::string(buf);
    }

    static HoldingString as_holding_string(const holdings::Holding& h) {
        HoldingString s;
        s.account = h.account.value();
        s.securityname = h.security_name.value();
        s.symbol = h.symbol.value();
        s.shares = investing::format_shares(h.shares);
        s.price = format_price(h.price);
        s.priceasof = h.price_as_of.iso();
        return s;
    }

    std::string next_ref() const { return "T" + std::to_string(transactions_.size() + 1); }

    void apply_transaction(const ledger::Transaction& t) {
        transactions_.push_back(t);
        for (const ledger::Posting& p : t.postings) {
            const std::string path = chart_.path_for_ref(p.account);
            ledger_.add({t.date, types::AccountPath(path), p.amount, t.ref.value()});
            PostingRowString row;
            row.ref = t.ref.value();
            row.date = t.date.iso();
            row.payee = "none";
            row.account = path;
            row.amount = p.amount.in_register();
            row.memo = "none";
            row.cleared = "Uncleared";
            last_created_.push_back(row);
        }
    }

    void check_created_postings(const std::vector<PostingRowString>& values) {
        ASSERT_EQ(values.size(), last_created_.size()) << "postings created";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].ref, last_created_[i].ref) << "posting " << i << " ref";
            EXPECT_EQ(values[i].date, last_created_[i].date) << "posting " << i << " date";
            EXPECT_EQ(values[i].account, last_created_[i].account) << "posting " << i << " account";
            EXPECT_EQ(Money(values[i].amount).cents(), Money(last_created_[i].amount).cents())
                << "posting " << i << " amount";
        }
    }
};
