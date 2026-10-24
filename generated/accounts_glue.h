#pragma once
#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <vector>
#include "common/common.h"
#include "account_type.h"
#include "chart.h"
#include "transaction_edit.h"
#include "category_search.h"
#include "date.h"
#include "ledger.h"
#include "balance_sheet.h"
#include "reconcile.h"
#include "register_lines.h"
#include "report.h"
#include "ui_model.h"
#include "money.h"
#include "store_sqlite.h"
#include "text_types.h"
#include <cctype>
#include <filesystem>
#include <system_error>

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
            a.alias = blank(value.alias);
            chart_.put(a);
        }
        apply_counts();
    }

    // The background chart is passed in full first and this says "the same
    // again", so there is nothing to do.
    void given_the_chart_of_accounts_is_as_previous() {}

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

            // Both are optional and both belong to the account, so the dialog
            // that opens it writes them rather than leaving them to a second
            // visit to the settings dialog. The number is kept as typed, less
            // the blanks at either end, which is the account-number rule.
            const chart::Account* made = chart_.find(path.value());
            if (made != nullptr) {
                chart::Account a = *made;
                a.bank = blank(value.bank);
                a.account_number = trimmed(blank(value.accountnumber));
                chart_.put(a);
            }

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
                [&](const chart::Account& a) { return a.path.value() == value.path.value; });
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
            if (value.rawbalance.value != DNCString)
                EXPECT_EQ(Money(value.rawbalance).cents(),
                          ledger_.raw_balance(value.account).cents())
                    << value.account << " raw -- " << value.notes;
            if (value.displaybalance.value != DNCString)
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

    // ------------------------------------------------------------ the rules

    void examples_businessrule_the_name_of_an_account_is_the_last_segment_of_its_path(
            const std::vector<AccountNameDerivationString>& values) {
        for (const auto& value : values) {
            const AccountNameDerivationTyped t =
                AccountNameDerivationTyped::from_string_struct(value);
            const types::AccountPath path{t.path.text};
            EXPECT_EQ(t.name.text, types::name_of(path)) << t.path.text << " name";
            EXPECT_EQ(t.parent, types::parent_of(path)) << t.path.text << " parent";
        }
    }

    void examples_businessrule_display_sign_follows_from_account_type(
            const std::vector<DisplaySignOfString>& values) {
        for (const auto& value : values) {
            const DisplaySignOfTyped t = DisplaySignOfTyped::from_string_struct(value);
            const types::AccountType type = types::account_type_from_string(t.accounttype.text);
            EXPECT_EQ(t.displaysign, types::display_sign(type)) << t.accounttype.text;
        }
    }

private:
    // A Default of "none" arrives as the literal word.
    static std::string blank(const std::string& s) {
        return (s == "none" || s == DNCString) ? std::string() : s;
    }

    static std::string trimmed(const std::string& s) {
        std::size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
        std::size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
        return s.substr(start, end - start);
    }

    std::string opened_;
    // Arbitrary until a scenario says otherwise: nothing is offered-while-
    // typing unless a posting is also given, and that never happens without
    // "Given today is" alongside it.
    types::Date today_{2024, 1, 1};
    std::vector<chart::Account> offered_;

    chart::Chart chart_;
    ledger::Ledger ledger_;
    chart::Rejection rejection_;
    std::vector<chart::Account> created_;
    std::vector<ledger::DatedPosting> created_postings_;
    chart::Chart::PostingCounts counts_;

    static AccountString as_string(const chart::Account& a) {
        AccountString s;
        // Deliberately never read back as anything but "none": see the
        // identity rule's own Details on why no table ever states one.
        s.id = "none";
        s.path = a.path.value();
        s.type = types::to_string(a.type);
        s.placeholder = a.placeholder ? "true" : "false";
        s.hidden = a.hidden ? "true" : "false";
        s.alias = a.alias.empty() ? "none" : a.alias;
        s.bank = a.bank.empty() ? "none" : a.bank;
        s.bankid = a.bank_id.empty() ? "none" : a.bank_id;
        s.accountnumber = a.account_number.empty() ? "none" : a.account_number;
        // Plain and ungrouped, which is how every table here writes an amount.
        // No date is "never" rather than a date nobody stated -- an account no
        // download has ever been placed into is the ordinary case.
        s.downloadedbalance = a.downloaded_balance.in_csv();
        s.downloadedbalanceat = a.downloaded_balance_at.has_value()
                                    ? a.downloaded_balance_at->iso()
                                    : std::string("never");
        return s;
    }

    void expect_present(const std::vector<AccountString>& values) {
        for (const auto& value : values) {
            const chart::Account* a = chart_.find(value.path);
            ASSERT_NE(nullptr, a) << "no account " << value.path;
            EXPECT_EQ(value, as_string(*a)) << value.path;
        }
    }


    void apply_counts() { chart_.set_posting_counts(counts_); }
public:
    // The postings were passed in full by the step before this one, so "the same
    // again" has nothing to do. Appended as a stub by the converter when the step
    // was added to the specification, which is how it adds a new step to glue
    // that is already written by hand.
    void given_postings_are_as_previous() {}
public:

    void when_account_moved_under(const std::vector<AccountMoveString>& values) {
        for (const auto& value : values) {
            const std::string to = value.newparent.value + ":" + types::name_of(
                types::AccountPath(value.path));
            // The ledger first, while "from" still names the account it is
            // moving postings away from; chart_.move() then does the same
            // move on the chart side. Two structures, one change.
            ledger_ = edit::move_postings(ledger_, value.path, to);
            const chart::Rejection no =
                chart_.move(types::AccountPath(value.path), types::AccountPath(value.newparent));
            EXPECT_FALSE(no.refused) << no.reason;
        }
    }

    void then_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        expect_present(values);
    }

    void given_today_is(const std::vector<TodayString>& values) {
        ASSERT_FALSE(values.empty());
        const auto d = types::Date::from_iso(values.front().date);
        ASSERT_TRUE(d.has_value()) << values.front().date;
        today_ = *d;
    }

    void when_category_typed(const std::vector<CategorySearchString>& values) {
        ASSERT_FALSE(values.empty());
        offered_ = chart::offered_for_typed(chart_, ledger_, values.front().typed,
                                            parse_bool_cell(values.front().includeunused),
                                            today_);
    }

    void then_categories_offered_are(const std::vector<CategoryOfferedString>& values) {
        std::vector<std::string> got;
        for (const chart::Account& a : offered_) got.push_back(a.path.value());
        std::sort(got.begin(), got.end());
        std::vector<std::string> want;
        for (const auto& value : values) want.push_back(value.path);
        std::sort(want.begin(), want.end());
        EXPECT_EQ(want, got);
    }

    // The identity a path is not is deliberately absent from production code
    // too (see the Details on this rule), so there is nothing to read it back
    // from. What is checked instead is the one thing every row promises:
    // whichever operation ran, the postings already on the account are found
    // under it afterwards exactly as they were before -- never rewritten,
    // never lost, whether or not the path itself moved.
    void examples_businessrule_an_account_is_identified_by_something_that_is_not_its_path(
            const std::vector<AccountRearrangedString>& values) {
        for (const auto& value : values) {
            chart::Chart fixture;
            fixture.add(types::AccountPath("Expenses"), types::AccountType::Expense);
            fixture.add(types::AccountPath("Expenses:Auto"), types::AccountType::Expense);
            fixture.add(types::AccountPath("Expenses:Auto:Fuel"), types::AccountType::Expense);
            ledger::Ledger before;
            before.add({types::Date(2024, 1, 1), types::AccountPath("Expenses:Auto:Fuel"),
                       Money("40.00")});
            const Money original = before.raw_balance("Expenses:Auto:Fuel");

            std::string moved_to = "Expenses:Auto:Fuel";
            chart::Rejection no;
            if (value.whatchanges == "Renaming a category") {
                no = fixture.rename(types::AccountPath("Expenses:Auto:Fuel"),
                                    types::AccountName("Petrol"));
                moved_to = "Expenses:Auto:Petrol";
            } else if (value.whatchanges == "Moving it under another") {
                no = fixture.move(types::AccountPath("Expenses:Auto:Fuel"),
                                  types::AccountPath("Expenses"));
                moved_to = "Expenses:Fuel";
            } else if (value.whatchanges == "Hiding it") {
                no = fixture.hide(types::AccountPath("Expenses:Auto:Fuel"));
            } else if (value.whatchanges == "Changing its type") {
                no = fixture.change_type(types::AccountPath("Expenses:Auto:Fuel"),
                                         types::AccountType::Expense);
            } else {
                FAIL() << "unknown change: " << value.whatchanges;
                continue;
            }
            ASSERT_FALSE(no.refused) << value.whatchanges << ": " << no.reason;

            const ledger::Ledger after =
                edit::move_postings(before, "Expenses:Auto:Fuel", moved_to);
            EXPECT_EQ(parse_bool_cell(value.thepath), moved_to != "Expenses:Auto:Fuel")
                << value.whatchanges << " path";
            EXPECT_EQ(original.cents(), after.raw_balance(moved_to).cents())
                << value.whatchanges << " postings";
        }
    }

    void examples_businessrule_typing_part_of_a_category_offers_it_and_everything_under_it(
            const std::vector<CategoryMatchString>& values) {
        chart::Chart fixture;
        fixture.add(types::AccountPath("Expenses"), types::AccountType::Expense);
        fixture.add(types::AccountPath("Expenses:Auto"), types::AccountType::Expense);
        fixture.add(types::AccountPath("Expenses:Auto:Insurance"), types::AccountType::Expense);
        chart::Account fuel;
        fuel.path = types::AccountPath("Expenses:Auto:Fuel");
        fuel.type = types::AccountType::Expense;
        fuel.alias = "@FU";
        fixture.put(fuel);
        const ledger::Ledger empty;
        static const std::string suffix = ", and every category under it";

        for (const auto& value : values) {
            const std::vector<chart::Account> got = chart::offered_for_typed(
                fixture, empty, value.typed, false, types::Date(2026, 10, 5));
            std::vector<std::string> paths;
            for (const chart::Account& a : got) paths.push_back(a.path.value());
            std::sort(paths.begin(), paths.end());

            std::vector<std::string> want;
            if (value.offered != "Nothing") {
                if (value.offered.size() > suffix.size() &&
                    value.offered.compare(value.offered.size() - suffix.size(), suffix.size(),
                                          suffix) == 0) {
                    const std::string base =
                        value.offered.substr(0, value.offered.size() - suffix.size());
                    for (const chart::Account& a : fixture.all())
                        if (chart::Chart::is_descendant_or_self(a.path.value(), base))
                            want.push_back(a.path.value());
                } else {
                    want.push_back(value.offered);
                }
            }
            std::sort(want.begin(), want.end());
            EXPECT_EQ(want, paths) << value.typed << " -- " << value.notes;
        }
    }

public:

    // Opening the dialog does not need a Qt dialog to test: the account it was
    // opened on is all there is to remember, and the Then reads straight off
    // chart_ either way.
    void when_account_settings_opened(const std::vector<AccountSelectString>& values) {
        ASSERT_FALSE(values.empty());
        opened_ = values.front().path;
    }

    void then_account_settings_are(const std::vector<AccountSettingsString>& values) {
        for (const auto& value : values) {
            const chart::Account* a = chart_.find(value.path);
            ASSERT_NE(nullptr, a) << "no account " << value.path;
            EXPECT_EQ(blank(value.bank), a->bank) << value.path << " bank";
            EXPECT_EQ(blank(value.bankid), a->bank_id)
                << value.path << " bank id";
            EXPECT_EQ(blank(value.accountnumber), a->account_number)
                << value.path << " account number";

            // Read off the transaction rather than out of a field, because the
            // transaction is where it is. An account with none shows zero, and
            // the date beside a zero says nothing -- which is why the scenario
            // that has no balance writes ?DNC? for the date.
            const edit::Starting starting =
                edit::starting_balance_of(chart_, transactions_, value.path);
            if (value.startingbalance.value != DNCString)
                EXPECT_EQ(Money(value.startingbalance).cents(), starting.stated.cents())
                    << value.path << " starting balance";
            if (value.startingdate != DNCString)
                EXPECT_EQ(value.startingdate, starting.on.iso())
                    << value.path << " starting date";
        }
    }

    // A number on a statement is a label, kept exactly as typed; only the
    // blanks at either end are not part of it. See the account-number rule.
    void when_account_settings_changed(const std::vector<AccountSettingsString>& values) {
        for (const auto& value : values) {
            const chart::Account* existing = chart_.find(value.path);
            ASSERT_NE(nullptr, existing) << "no account " << value.path;
            chart::Account a = *existing;
            a.bank = blank(value.bank);
            a.bank_id = trimmed(blank(value.bankid));
            a.account_number = trimmed(blank(value.accountnumber));
            chart_.put(a);

            // The starting balance is the transaction against opening equity,
            // so changing it in the dialog writes, changes or removes that
            // transaction. The ledger is rebuilt from the transactions
            // afterwards, because the balances a Then reads come off it.
            if (value.startingbalance.value == DNCString) continue;
            types::Date on(2005, 1, 1);
            if (value.startingdate != DNCString) {
                const auto parsed = types::Date::from_iso(value.startingdate);
                if (parsed) on = *parsed;
            }
            edit::set_starting_balance(&chart_, &transactions_, value.path,
                                       Money(value.startingbalance), on);
            rebuild_ledger();
        }
    }

    // Every transaction as the pair of amounts it is, which is what the ledger
    // here holds. Thrown away and built again rather than adjusted: a
    // half-updated balance is how a figure comes to disagree with the register
    // beside it.
    void rebuild_ledger() {
        ledger_ = ledger::Ledger();
        for (const ledger::Transaction& t : transactions_) {
            const std::string source = chart_.path_for_ref(t.source_account);
            const std::string destination = chart_.path_for_ref(t.destination);
            if (!source.empty())
                ledger_.add({t.date, types::AccountPath(source), t.amount});
            if (!destination.empty())
                ledger_.add({t.date, types::AccountPath(destination), -t.amount});
        }
    }

    // The transactions of the book, kept beside the ledger of amounts because
    // the starting balance is one of them.
    std::vector<ledger::Transaction> transactions_;

    // Writes the chart to a book of its own and reads it straight back,
    // replacing chart_ -- the same round trip the window makes when it saves
    // and the next launch opens the book again. A fixed name, cleared first,
    // so the file from a previous run of this test is never what is read.
    void when_the_book_is_reopened() {
        static const char* const kName = "AccountSettingsRoundTrip";
        const std::string path = store::path_for(kName);
        std::error_code ignored;
        std::filesystem::remove(path, ignored);

        store::Book made;
        const store::Failure no_create = store::Book::create(path, kName, &made);
        ASSERT_FALSE(no_create.refused) << no_create.reason;
        const store::Failure no_write = made.write(chart_, {});
        ASSERT_FALSE(no_write.refused) << no_write.reason;

        store::Book reopened;
        const store::Failure no_open = store::Book::open_named(kName, &reopened);
        ASSERT_FALSE(no_open.refused) << no_open.reason;
        ledger::Ledger book;
        std::vector<ledger::Transaction> transactions;
        const store::Failure no_read = reopened.read(&chart_, &book, &transactions);
        ASSERT_FALSE(no_read.refused) << no_read.reason;
    }

public:

    void when_the_display_names_are_rebuilt() { chart_.rebuild_display_names(); }

    void then_accounts_show_as(const std::vector<AccountDisplayString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.shownas, chart_.displayed(value.path)) << value.path;
    }

    void examples_datatype_accountid(const std::vector<ValidValuesString>& values) {
        for (const auto& value : values) {
            const ValidValuesTyped v = ValidValuesTyped::from_string_struct(value);
            bool accepted = true;
            try {
                if (v.value.empty()) throw std::invalid_argument("empty");
                std::size_t pos = 0;
                const long long n = std::stoll(v.value, &pos);
                if (pos != v.value.size()) throw std::invalid_argument("not a number");
                const chart::AccountId parsed(n);
                (void)parsed;
            } catch (const std::exception&) {
                accepted = false;
            }
            EXPECT_EQ(v.isvalid, accepted)
                << "AccountId given [" << v.value << "]"
                << (v.notes.empty() ? "" : " -- " + v.notes);
        }
    }

    // Each row is its own little chart of one account, because the rule is
    // about one node's own conversion and not about anything a shared fixture
    // would add.
    void examples_businessrule_converting_an_account_to_the_one_line_it_is_shown_as(
            const std::vector<AccountDisplayString>& values) {
        for (const auto& value : values) {
            chart::Chart fixture;
            fixture.add(types::AccountPath(value.path),
                       types::account_type_from_string(value.type));
            EXPECT_EQ(value.shownas, fixture.display_of(value.path)) << value.path;
        }
    }

public:

    void given_raw_category_rows_are(const std::vector<CategoryRowString>& values) {
        raw_category_rows_.clear();
        for (const auto& value : values) {
            chart::CategoryRawRow r;
            r.id = std::stoll(value.id);
            r.subcategory_of = std::stoll(value.subcategoryof);
            r.name = value.name;
            r.type = types::account_type_from_string(value.type);
            raw_category_rows_.push_back(r);
        }
    }

    void when_the_rows_are_read_as_categories() {
        categories_from_raw_ = chart::categories_from_raw(raw_category_rows_);
    }

    void then_categories_are(const std::vector<CategoryString>& values) {
        ASSERT_EQ(values.size(), categories_from_raw_.size()) << "number of categories";
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (values[i].path.value != DNCString)
                EXPECT_EQ(values[i].path.value, categories_from_raw_[i].path.value())
                    << "category " << i;
        }
    }

    void examples_datatype_categoryid(const std::vector<ValidValuesString>& values) {
        for (const auto& value : values) {
            const ValidValuesTyped v = ValidValuesTyped::from_string_struct(value);
            bool accepted = true;
            try {
                if (v.value.empty()) throw std::invalid_argument("empty");
                std::size_t pos = 0;
                const long long n = std::stoll(v.value, &pos);
                if (pos != v.value.size()) throw std::invalid_argument("not a number");
                const chart::CategoryId parsed(n);
                (void)parsed;
            } catch (const std::exception&) {
                accepted = false;
            }
            EXPECT_EQ(v.isvalid, accepted)
                << "CategoryId given [" << v.value << "]"
                << (v.notes.empty() ? "" : " -- " + v.notes);
        }
    }

    // Drives Chart's own add()/add_category() in the order the table states,
    // so what is checked is the production counter actually being shared --
    // not just that the numbers in the table happen not to collide.
    void examples_businessrule_an_account_id_and_a_category_id_never_collide(
            const std::vector<IdsGivenOutString>& values) {
        chart::Chart fixture;
        for (const auto& value : values) {
            long long got = 0;
            if (value.kind == "Account") {
                std::vector<chart::Account> created;
                fixture.add(types::AccountPath("Node" + value.id), types::AccountType::Checking,
                           &created);
                ASSERT_FALSE(created.empty()) << value.createdinorder;
                got = created.back().id.value();
            } else if (value.kind == "Category") {
                std::vector<chart::Category> created;
                fixture.add_category(types::AccountPath("Node" + value.id),
                                     types::AccountType::Expense, &created);
                ASSERT_FALSE(created.empty()) << value.createdinorder;
                got = created.back().id.value();
            } else {
                FAIL() << "unknown kind: " << value.kind;
                continue;
            }
            EXPECT_EQ(std::stoll(value.id), got) << value.createdinorder;
        }
    }

public:

    void given_the_categories_are(const std::vector<CategoryString>& values) {
        for (const auto& value : values) {
            chart::Category c;
            c.path = types::AccountPath(value.path);
            c.type = types::account_type_from_string(value.type);
            c.placeholder = parse_bool_cell(value.placeholder);
            c.hidden = parse_bool_cell(value.hidden);
            c.alias = blank(value.alias);
            chart_.put_category(c);
        }
    }

    void then_categories_show_as(const std::vector<AccountDisplayString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.shownas, chart_.displayed(value.path)) << value.path;
    }

private:
    std::vector<chart::CategoryRawRow> raw_category_rows_;
    std::vector<chart::Category> categories_from_raw_;
public:

    void given_expenses_groceries_has_entries(const std::vector<EntryCountString>& values) {
        set_count("Expenses:Groceries", values);
    }

    void given_expenses_auto_repairs_has_entries(const std::vector<EntryCountString>& values) {
        set_count("Expenses:Auto:Repairs", values);
    }

    void given_assets_savings_has_entries(const std::vector<EntryCountString>& values) {
        set_count("Assets:Savings", values);
    }

    void set_count(const std::string& path, const std::vector<EntryCountString>& values) {
        for (const auto& value : values) counts_[path] = std::stoi(value.count);
        apply_counts();
    }

    // Amounts against accounts, with no date: these scenarios are about what an
    // account holds, not about when.
    void given_entries_are(const std::vector<AccountEntryString>& values) {
        ledger_ = ledger::Ledger();
        for (const auto& value : values)
            ledger_.add({types::Date(2024, 1, 1), types::AccountPath(value.account),
                         Money(value.amount)});
    }

    void given_dated_entries_are(const std::vector<DatedAccountEntryString>& values) {
        ledger_ = ledger::Ledger();
        for (const auto& value : values) {
            const auto date = types::Date::from_iso(value.date);
            ASSERT_TRUE(date.has_value()) << value.date;
            ledger_.add({*date, types::AccountPath(value.account), Money(value.amount)});
        }
    }

    // A transaction puts its amount against its source account and the opposite
    // against its destination. The ledger here is a set of amounts per account,
    // so both sides of the one transaction are added to it.
    void given_transactions_are(const std::vector<TransactionString>& values) {
        ledger_ = ledger::Ledger();
        transactions_.clear();
        for (const auto& value : values) {
            types::Date on(2024, 1, 1);
            const auto parsed = types::Date::from_iso(value.date);
            if (parsed) on = *parsed;
            const Money amount{value.amount};
            const std::string source = blank(value.sourceaccount);
            const std::string destination = blank(value.destination);
            if (!source.empty())
                ledger_.add({on, types::AccountPath(source), amount});
            if (!destination.empty())
                ledger_.add({on, types::AccountPath(destination), -amount});

            // The same rows again as transactions, because the starting balance
            // of an account is one of them rather than a field of its own, and
            // the dialog edits the transaction.
            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef(value.ref);
            t.date = on;
            t.payee = types::PayeeName(blank(value.payee));
            t.memo = blank(value.memo);
            t.source_account = chart_.ref_for_path(source);
            t.destination = chart_.ref_for_path(destination);
            t.amount = amount;
            transactions_.push_back(t);
        }
    }

    // Opening an account makes one transaction: the account against opening
    // equity. It reaches here as the pair of amounts the ledger was given, so
    // the first is the transaction's own side and the second its destination.
    void then_transactions_created_are(const std::vector<TransactionString>& values) {
        ASSERT_EQ(values.size() * 2, created_postings_.size())
            << "number of entries created";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const ledger::DatedPosting& own = created_postings_[i * 2];
            const ledger::DatedPosting& other = created_postings_[i * 2 + 1];
            const TransactionString& want = values[i];
            const std::string where = "transaction " + std::to_string(i + 1);
            if (want.date != DNCString)
                EXPECT_EQ(want.date, own.date.iso()) << where << " date";
            if (want.sourceaccount.value != DNCString)
                EXPECT_EQ(blank(want.sourceaccount), own.account.value())
                    << where << " source account";
            if (want.destination.value != DNCString)
                EXPECT_EQ(blank(want.destination), other.account.value())
                    << where << " destination";
            if (want.amount.value != DNCString)
                EXPECT_EQ(Money(want.amount).cents(), own.amount.cents())
                    << where << " amount";
        }
    }

public:

    // One chart, walked through the table a row at a time. What each row asserts
    // is the id the node actually got, so a counter that dropped back after the
    // delete would hand out 2 again and the last row would fail.
    void examples_businessrule_an_id_is_never_given_out_a_second_time(
            const std::vector<CounterOnlyGoesForwardString>& values) {
        chart::Chart fixture;
        for (const CounterOnlyGoesForwardString& value : values) {
            const std::string& what = value.whathappens;
            // What was done is the last word, so it is looked for at the end:
            // "A new book, nothing created yet" has the word in the middle and
            // is not a row that creates anything.
            const auto ends_with = [&what](const std::string& tail) {
                return what.size() > tail.size() &&
                       what.compare(what.size() - tail.size(), tail.size(), tail) == 0;
            };
            const bool created = ends_with(" created");
            const bool deleted = ends_with(" deleted");
            if (created) {
                const types::AccountPath path(what.substr(0, what.size() - 8));
                std::vector<chart::Account> made;
                const chart::Rejection no =
                    fixture.add(path, types::AccountType::Checking, &made);
                ASSERT_FALSE(no.refused) << no.reason;
                ASSERT_EQ(1u, made.size()) << what << " made more than one node";
                EXPECT_EQ(std::stoll(value.lastidused), made.back().id.value())
                    << what << " got the wrong id";
            } else if (deleted) {
                const types::AccountPath path(what.substr(0, what.size() - 8));
                const chart::Rejection no = fixture.remove(path);
                ASSERT_FALSE(no.refused) << no.reason;
            }
            EXPECT_EQ(std::stoll(value.lastidused), fixture.last_id_used()) << what;
            EXPECT_EQ(std::stoll(value.nextidgivenout), fixture.last_id_used() + 1) << what;
        }
    }

public:

    // Opening it remembers the category and nothing else: what the dialog
    // comes up showing is read off the chart, and the Then reads it the same
    // way whether the dialog was opened or answered.
    void when_category_settings_opened(const std::vector<AccountSelectString>& values) {
        ASSERT_FALSE(values.empty());
        opened_ = values.front().path;
    }

    void then_category_settings_are(const std::vector<CategorySettingsString>& values) {
        for (const CategorySettingsString& want : values) {
            const std::optional<chart::CategorySettings> got =
                chart_.category_settings(want.path.value);
            ASSERT_TRUE(got.has_value()) << "no account " << want.path.value;
            EXPECT_EQ(want.name.value, got->name.value()) << want.path.value << " name";
            EXPECT_EQ(blank(want.under.value), got->under) << want.path.value << " under";
            EXPECT_EQ(want.type.value, types::to_string(got->type))
                << want.path.value << " type";
            EXPECT_EQ(blank(want.alias), got->alias) << want.path.value << " alias";
            EXPECT_EQ(parse_bool_cell(want.hidden), got->hidden)
                << want.path.value << " hidden";
        }
    }

    // The dialog answered. Each of the five is the chart's own operation, so
    // what this does is hand the five over and say it was not refused -- the
    // rules of a rename or a move are specified where they are.
    void when_category_settings_changed(const std::vector<CategorySettingsString>& values) {
        for (const CategorySettingsString& value : values) {
            chart::CategorySettings want;
            want.path = types::AccountPath(value.path.value);
            want.name = types::AccountName(value.name.value);
            want.under = blank(value.under.value);
            want.type = types::account_type_from_string(value.type.value);
            want.alias = blank(value.alias);
            want.hidden = parse_bool_cell(value.hidden);
            rejection_ = chart_.apply_category_settings(want);
            EXPECT_FALSE(rejection_.refused) << rejection_.reason;
        }
    }

    // Every account's date at once, which is the point: one pass over the
    // postings rather than one pass per account.
    void when_the_last_used_dates_are_read() { last_used_ = chart::last_used_dates(ledger_); }

    void then_last_used_dates_are(const std::vector<LastUsedString>& values) {
        for (const LastUsedString& want : values) {
            const auto at = last_used_.find(want.account.value);
            // Absent from the map is never used, which is a different answer
            // from a date and not a stand-in for one.
            const std::string got = at == last_used_.end() ? std::string("never") : at->second.iso();
            EXPECT_EQ(want.lastused, got) << want.account.value << " last used";
        }
    }

private:
    chart::LastUsedDates last_used_;
public:

// --- the Quicken reading of what is owed ------------------------------------

public:

    // The two readings of the same balance, side by side. Our reading is the
    // display sign -- what the account's own statement shows -- and the Quicken
    // reading negates it for a real account that reads as an amount owed. The
    // raw balance is asserted too, because the claim is that it does not move.
    void then_balances_read_both_ways_are(const std::vector<BalanceAsReadString>& values) {
        for (const auto& value : values) {
            const chart::Account* a = chart_.find(value.account);
            ASSERT_NE(nullptr, a) << "no account " << value.account;
            const Money raw = ledger_.raw_balance(value.account);
            const std::string where = value.account.value;
            if (value.rawbalance.value != DNCString)
                EXPECT_EQ(Money(value.rawbalance).cents(), raw.cents())
                    << where << " raw -- " << value.notes;
            const int ours = types::display_sign(a->type);
            const int theirs = types::print_sign(a->type, true);
            if (value.ourreading.value != DNCString)
                EXPECT_EQ(Money(value.ourreading).cents(), (ours < 0 ? -raw : raw).cents())
                    << where << " our reading -- " << value.notes;
            if (value.quickenreading.value != DNCString)
                EXPECT_EQ(Money(value.quickenreading).cents(), (theirs < 0 ? -raw : raw).cents())
                    << where << " the Quicken reading -- " << value.notes;
            // And the display balance the rest of the program asks for is our
            // reading, not something a third way round.
            EXPECT_EQ((ours < 0 ? -raw : raw).cents(),
                      ledger_.display_balance(value.account, a->type).cents())
                << where << " display balance";
        }
    }

    // One line of rule, checked against every account type there is: the sign
    // of the Quicken reading is the display sign, negated for a real account
    // that reads as an amount owed.
    void examples_businessrule_the_quicken_reading_negates_what_is_owed_and_nothing_else(
            const std::vector<QuickenReadingOfString>& values) {
        for (const auto& value : values) {
            const types::AccountType type = types::account_type_from_string(value.accounttype);
            EXPECT_EQ(std::stoi(value.ourreading), types::display_sign(type))
                << value.accounttype << " our reading -- " << value.notes;
            EXPECT_EQ(std::stoi(value.quickenreading), types::print_sign(type, true))
                << value.accounttype << " the Quicken reading -- " << value.notes;
            // With the option off the two programs' readings are the same
            // question, which is what makes this display only.
            EXPECT_EQ(types::display_sign(type), types::print_sign(type, false))
                << value.accounttype << " with the option off";
        }
    }

    // Every figure the option was checked against, worked out both ways over
    // one small book and compared. A row naming a figure no code path reads the
    // option for -- an amount as it is typed, a posting as it is stored -- has
    // nothing to work out, and is asserted not to change.
    void examples_businessrule_what_the_quicken_reading_leaves_alone(
            const std::vector<QuickenReadingTouchesString>& values) {
        const QuickenFixture book;
        for (const auto& value : values) {
            const std::optional<std::pair<std::string, std::string>> both =
                book.figure(value.figure);
            const bool wanted = parse_bool_cell(value.changes);
            if (!both.has_value()) {
                EXPECT_FALSE(wanted)
                    << value.figure << " is said to change, but nothing reads the "
                    << "option on the way to it -- " << value.notes;
                continue;
            }
            EXPECT_EQ(wanted, both->first != both->second)
                << value.figure << " reads " << both->first << " and "
                << both->second << " -- " << value.notes;
        }
    }

private:

    // A card with a statement to compare against, a bank account, and one
    // charge the bank has reported. Everything in the rule above is read off
    // this, each figure once under each reading.
    struct QuickenFixture {
        chart::Chart accounts;
        std::vector<ledger::Transaction> transactions;
        std::vector<ledger::TransactionSplit> splits;
        ledger::Ledger ledger;

        QuickenFixture() {
            accounts.add(types::AccountPath("Assets:Checking"), types::AccountType::Checking);
            accounts.add(types::AccountPath("Liabilities:Visa"), types::AccountType::CreditCard);
            accounts.add(types::AccountPath("Expenses:Groceries"), types::AccountType::Expense);
            accounts.add(types::AccountPath("Equity:Opening Balances"),
                         types::AccountType::Equity);
            // 400.00 owed where the book says 450.00, so the difference is a
            // figure rather than a zero that would read the same either way.
            chart::Account card = *accounts.find("Liabilities:Visa");
            card.downloaded_balance = Money("400.00");
            card.downloaded_balance_at = types::Date(2024, 1, 31);
            accounts.put(card);

            add("T1", types::Date(2024, 1, 2), "Opening", "Assets:Checking",
                "Equity:Opening Balances", Money("1000.00"), types::ClearedStatus::Cleared);
            add("T2", types::Date(2024, 1, 10), "Amazon", "Liabilities:Visa",
                "Expenses:Groceries", Money("-450.00"), types::ClearedStatus::Cleared);
            ledger::add_to_book(&ledger, accounts, transactions, splits);
        }

        void add(const std::string& ref, const types::Date& on, const std::string& payee,
                 const std::string& from, const std::string& to, const Money& amount,
                 types::ClearedStatus cleared) {
            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef(ref);
            t.date = on;
            t.payee = types::PayeeName(payee);
            t.source_account = accounts.ref_for_path(from);
            t.destination = accounts.ref_for_path(to);
            t.amount = amount;
            t.cleared = cleared;
            transactions.push_back(t);
        }

        static bool mentions(const std::string& figure, const std::string& what) {
            return figure.find(what) != std::string::npos;
        }

        static std::string money(const Money& m) { return std::to_string(m.cents()); }

        // The figure named, under our reading and under Quicken's. Nothing for a
        // figure the option cannot reach.
        std::optional<std::pair<std::string, std::string>> figure(
                const std::string& named) const {
            const std::string card = "Liabilities:Visa";
            const reg::BookIndex index(accounts, transactions, splits);

            if (mentions(named, "Charge and Payment")) {
                return both([&](bool q) {
                    const std::vector<reg::Line> lines = reg::lines_for(index, card, q);
                    std::string out;
                    for (const reg::Line& line : lines)
                        out += money(line.payment) + "/" + money(line.deposit) + " ";
                    return out;
                });
            }
            if (mentions(named, "running balance")) {
                return both([&](bool q) {
                    std::string out;
                    for (const reg::Line& line : reg::lines_for(index, card, q))
                        out += money(line.balance) + " ";
                    return out;
                });
            }
            if (mentions(named, "Balance, Cleared and Downloaded")) {
                return both([&](bool q) {
                    const reg::Balances b = reg::balances_for(index, card, q);
                    return money(b.balance) + " " + money(b.cleared) + " " + money(b.downloaded);
                });
            }
            if (mentions(named, "Difference")) {
                return both([&](bool q) {
                    return money(reg::balances_for(index, card, q).difference);
                });
            }
            if (mentions(named, "downloaded balance agrees")) {
                return both([&](bool q) {
                    return recon::figures_for(index, card, q).agrees ? "agrees" : "does not";
                });
            }
            if (mentions(named, "reconciling window")) {
                return both([&](bool q) {
                    const recon::Figures f = recon::figures_for(index, card, q);
                    return money(f.balance) + " " + money(f.cleared) + " " +
                           money(f.online) + " " + money(f.difference);
                });
            }
            if (mentions(named, "offered when reconciling")) {
                return both([&](bool) {
                    std::string out;
                    for (const recon::Item& item : recon::uncleared_in(index, card))
                        out += item.ref + " ";
                    return out;
                });
            }
            if (mentions(named, "liability on a balance sheet")) {
                return both([&](bool q) {
                    const reports::BalanceSheet sheet = reports::balance_sheet(
                        accounts, ledger, types::Date(2024, 12, 31), false, q);
                    std::string out = money(sheet.totals.liabilities);
                    for (const reports::BalanceSheetRow& row : sheet.rows)
                        if (row.account == card) out += " " + money(row.balance);
                    return out;
                });
            }
            if (named == "Net worth") {
                return both([&](bool q) {
                    return money(reports::balance_sheet(accounts, ledger,
                                                        types::Date(2024, 12, 31), false, q)
                                     .totals.net);
                });
            }
            if (mentions(named, "account list balance")) {
                return both([&](bool q) {
                    std::string out;
                    for (const ui::AccountRow& row : ui::account_list(accounts, ledger, false, q))
                        if (row.account == card || row.is_heading)
                            out += money(row.balance) + " ";
                    return out;
                });
            }
            if (mentions(named, "category report")) {
                return both([&](bool q) {
                    reports::Spec spec;
                    spec.from = types::Date(2024, 1, 1);
                    spec.to = types::Date(2024, 12, 31);
                    spec.quicken_signs = q;
                    std::string out;
                    for (const reports::Row& row : reports::category_report(accounts, ledger, spec).rows)
                        if (row.account == "Expenses:Groceries") out += money(row.amount);
                    return out;
                });
            }
            if (mentions(named, "payee report")) {
                return both([&](bool q) {
                    reports::Spec spec;
                    spec.from = types::Date(2024, 1, 1);
                    spec.to = types::Date(2024, 12, 31);
                    spec.quicken_signs = q;
                    std::string out;
                    for (const reports::PayeeRow& row :
                         reports::payee_report(accounts, transactions, splits, spec))
                        out += row.payee + " " + money(row.amount) + " ";
                    return out;
                });
            }
            if (mentions(named, "raw balance")) {
                return both([&](bool) { return money(ledger.raw_balance(card)); });
            }
            return std::nullopt;
        }

        template <typename F>
        static std::optional<std::pair<std::string, std::string>> both(F read) {
            return std::make_pair(std::string(read(false)), std::string(read(true)));
        }
    };

public:

};
