#pragma once
#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <vector>
#include "common/common.h"
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "text_types.h"

// Glue for Accounts.spectable.
//
// A note on the subset assertions. A Then table marked CompareOnly arrives with
// "?DNC?" in the columns it does not name, and the generated string structs
// already treat that as equal to anything -- so comparing string structs is the
// right way round, not comparing fields by hand.
//
// Several of those tables also list fewer accounts than the chart holds: the
// rename scenario names the three paths it expects to have changed, not the ten
// it expects to be untouched. Those assertions are therefore containment: every
// account listed is present and matches. Where a scenario is about something
// being *absent*, that is asserted separately and explicitly.
class AccountsGlue {
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
        apply_counts();
    }

    // The background chart is passed in full first and this says "the same
    // again", so there is nothing to do.
    void given_the_chart_of_accounts_is_as_previous() {}

    void given_expenses_groceries_has_postings(const std::vector<PostingCountString>& v) {
        set_count("Expenses:Groceries", v);
    }
    void given_expenses_auto_repairs_has_postings(const std::vector<PostingCountString>& v) {
        set_count("Expenses:Auto:Repairs", v);
    }
    void given_assets_savings_has_postings(const std::vector<PostingCountString>& v) {
        set_count("Assets:Savings", v);
    }

    // Postings without a date: these scenarios are about balances, not ordering.
    void given_postings_are(const std::vector<PostingLineString>& values) {
        ledger_ = ledger::Ledger();
        for (const auto& value : values)
            ledger_.add({types::Date(2024, 1, 1), types::AccountPath(value.account),
                         Money(value.amount)});
    }

    void given_dated_postings_are(const std::vector<DatedPostingLineString>& values) {
        ledger_ = ledger::Ledger();
        for (const auto& value : values) {
            const auto date = types::Date::from_iso(value.date);
            ASSERT_TRUE(date.has_value()) << value.date;
            ledger_.add({*date, types::AccountPath(value.account), Money(value.amount)});
        }
    }

    // ----------------------------------------------------------------- whens

    void when_account_added(const std::vector<AccountString>& values) {
        created_.clear();
        for (const auto& value : values)
            rejection_ = chart_.add(types::AccountPath(value.path),
                                    types::account_type_from_string(value.type), &created_);
    }

    void when_account_added_with_an_opening_balance(
            const std::vector<AccountWithOpeningBalanceString>& values) {
        created_.clear();
        created_postings_.clear();
        for (const auto& value : values) {
            const types::AccountPath path{value.path};
            const types::AccountType type = types::account_type_from_string(value.type);
            rejection_ = chart_.add(path, type, &created_);
            const auto on = types::Date::from_iso(value.openingdate);
            ASSERT_TRUE(on.has_value()) << value.openingdate;
            for (const auto& p : ledger::Ledger::opening_balance_postings(
                     path, type, Money(value.openingbalance), *on)) {
                created_postings_.push_back(p);
                ledger_.add(p);
            }
        }
    }

    void when_account_renamed(const std::vector<AccountRenameString>& values) {
        for (const auto& value : values)
            rejection_ = chart_.rename(types::AccountPath(value.path),
                                       types::AccountName(value.newname));
    }

    void when_account_moved(const std::vector<AccountMoveString>& values) {
        for (const auto& value : values)
            rejection_ = chart_.move(types::AccountPath(value.path),
                                     types::AccountPath(value.newparent));
    }

    void when_account_deleted(const std::vector<AccountSelectString>& values) {
        for (const auto& value : values)
            rejection_ = chart_.remove(types::AccountPath(value.path));
    }

    void when_account_hidden(const std::vector<AccountSelectString>& values) {
        for (const auto& value : values)
            rejection_ = chart_.hide(types::AccountPath(value.path));
    }

    void when_account_type_changed(const std::vector<AccountTypeChangeString>& values) {
        for (const auto& value : values)
            rejection_ = chart_.change_type(types::AccountPath(value.path),
                                            types::account_type_from_string(value.newtype));
    }

    // ----------------------------------------------------------------- thens

    void then_rejected_because(const std::vector<RejectionString>& values) {
        for (const auto& value : values) {
            EXPECT_TRUE(rejection_.refused) << "expected a refusal: " << value.reason;
            EXPECT_EQ(value.reason, rejection_.reason);
        }
    }

    void then_account_is(const std::vector<AccountString>& values) {
        for (const auto& value : values) {
            const chart::Account* a = chart_.find(value.path);
            ASSERT_NE(nullptr, a) << "no account " << value.path;
            EXPECT_EQ(value, as_string(*a)) << value.path;
        }
    }

    void then_accounts_created_are(const std::vector<AccountString>& values) {
        ASSERT_EQ(values.size(), created_.size()) << "number of accounts created";
        for (std::size_t i = 0; i < values.size(); ++i)
            EXPECT_EQ(values[i], as_string(created_[i])) << "account " << i;
    }

    void then_accounts_are(const std::vector<AccountString>& values) {
        expect_present(values);
    }

    void then_accounts_named_insurance_are(const std::vector<AccountString>& values) {
        expect_present(values);
    }

    void then_accounts_under_expenses_auto_are(const std::vector<AccountString>& values) {
        // Here the list is the whole of that subtree, so it is exact.
        std::vector<AccountString> actual;
        for (const chart::Account& a : chart_.all())
            if (types::parent_of(a.path) == "Expenses:Auto") actual.push_back(as_string(a));
        ASSERT_EQ(values.size(), actual.size()) << "accounts under Expenses:Auto";
        for (std::size_t i = 0; i < values.size(); ++i) EXPECT_EQ(values[i], actual[i]);
    }

    void then_category_picker_offers(const std::vector<AccountString>& values) {
        const std::vector<chart::Account> offered = chart_.picker();
        for (const auto& value : values) {
            const bool found = std::any_of(
                offered.begin(), offered.end(),
                [&](const chart::Account& a) { return a.path.value() == value.path; });
            EXPECT_TRUE(found) << value.path << " should be offered";
        }
        // The scenario this belongs to is about a hidden account being kept out,
        // so that is asserted rather than left to the list above.
        for (const chart::Account& a : offered) {
            EXPECT_FALSE(a.hidden) << a.path.value() << " is hidden and was offered";
            EXPECT_FALSE(a.placeholder) << a.path.value() << " is a placeholder and was offered";
        }
    }

    void then_balances_are(const std::vector<AccountBalanceString>& values) {
        for (const auto& value : values) {
            const chart::Account* a = chart_.find(value.account);
            ASSERT_NE(nullptr, a) << "no account " << value.account;
            EXPECT_EQ(Money(value.rawbalance).cents(),
                      ledger_.raw_balance(value.account).cents())
                << value.account << " raw -- " << value.notes;
            EXPECT_EQ(Money(value.displaybalance).cents(),
                      ledger_.display_balance(value.account, a->type).cents())
                << value.account << " display -- " << value.notes;
        }
    }

    void then_rolled_up_balances_are(const std::vector<RolledUpBalanceString>& values) {
        for (const auto& value : values) {
            EXPECT_EQ(Money(value.ownbalance).cents(),
                      ledger_.own_balance(value.account).cents())
                << value.account << " own -- " << value.notes;
            EXPECT_EQ(Money(value.totalbalance).cents(),
                      ledger_.total_balance(value.account).cents())
                << value.account << " total -- " << value.notes;
        }
    }

    void then_net_worth_is(const std::vector<NetWorthString>& values) {
        const ledger::Ledger::NetWorth got = ledger_.net_worth(chart_);
        for (const auto& value : values) {
            EXPECT_EQ(Money(value.assets).cents(), got.assets.cents()) << "assets";
            EXPECT_EQ(Money(value.liabilities).cents(), got.liabilities.cents())
                << "liabilities";
            EXPECT_EQ(Money(value.networth).cents(), got.net.cents()) << "net worth";
        }
    }

    void then_balance_as_at_dates_are(const std::vector<BalanceAsAtString>& values) {
        for (const auto& value : values) {
            const auto as_at = types::Date::from_iso(value.asat);
            ASSERT_TRUE(as_at.has_value()) << value.asat;
            EXPECT_EQ(Money(value.balance).cents(),
                      ledger_.balance_as_at(value.account, *as_at).cents())
                << value.account << " as at " << value.asat << " -- " << value.notes;
        }
    }

    void then_postings_created_are(const std::vector<DatedPostingLineString>& values) {
        ASSERT_EQ(values.size(), created_postings_.size()) << "number of postings created";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].date, created_postings_[i].date.iso()) << "posting " << i;
            EXPECT_EQ(values[i].account, created_postings_[i].account.value())
                << "posting " << i;
            EXPECT_EQ(Money(values[i].amount).cents(), created_postings_[i].amount.cents())
                << "posting " << i;
        }
    }

    // ------------------------------------------------------------ the rules

    void examples_businessrule_the_name_of_an_account_is_the_last_segment_of_its_path(
            const std::vector<AccountNameDerivationString>& values) {
        for (const auto& value : values) {
            const AccountNameDerivationTyped t =
                AccountNameDerivationTyped::from_string_struct(value);
            const types::AccountPath path{t.path};
            EXPECT_EQ(t.name, types::name_of(path)) << t.path << " name";
            EXPECT_EQ(t.parent, types::parent_of(path)) << t.path << " parent";
        }
    }

    void examples_businessrule_display_sign_follows_from_account_type(
            const std::vector<DisplaySignOfString>& values) {
        for (const auto& value : values) {
            const DisplaySignOfTyped t = DisplaySignOfTyped::from_string_struct(value);
            const types::AccountType type = types::account_type_from_string(t.accounttype);
            EXPECT_EQ(t.displaysign, types::display_sign(type)) << t.accounttype;
        }
    }

private:
    chart::Chart chart_;
    ledger::Ledger ledger_;
    chart::Rejection rejection_;
    std::vector<chart::Account> created_;
    std::vector<ledger::DatedPosting> created_postings_;
    chart::Chart::PostingCounts counts_;

    static AccountString as_string(const chart::Account& a) {
        AccountString s;
        s.path = a.path.value();
        s.type = types::to_string(a.type);
        s.placeholder = a.placeholder ? "true" : "false";
        s.hidden = a.hidden ? "true" : "false";
        return s;
    }

    void expect_present(const std::vector<AccountString>& values) {
        for (const auto& value : values) {
            const chart::Account* a = chart_.find(value.path);
            ASSERT_NE(nullptr, a) << "no account " << value.path;
            EXPECT_EQ(value, as_string(*a)) << value.path;
        }
    }

    void set_count(const std::string& path, const std::vector<PostingCountString>& v) {
        for (const auto& value : v) counts_[path] = std::stoi(value.count);
        apply_counts();
    }

    void apply_counts() { chart_.set_posting_counts(counts_); }
public:
    // The postings were passed in full by the step before this one, so "the same
    // again" has nothing to do. Appended as a stub by the converter when the step
    // was added to the specification, which is how it adds a new step to glue
    // that is already written by hand.
    void given_postings_are_as_previous() {}
};
