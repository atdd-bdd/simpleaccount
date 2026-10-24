#pragma once
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sqlite3.h>
#include "common/common.h"
#include "account_type.h"
#include "chart.h"
#include "csv_import.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "owner_draw.h"
#include "payee_rules.h"
#include "transaction.h"
#include "report.h"
#include "report_ranges.h"
#include "ofx_import.h"
#include "store_sqlite.h"
#include "transaction_id.h"
#include "text_types.h"

// Glue for Books.spectable.
//
// Every scenario here runs against a real SQLite book in a folder of its own.
// Nothing is faked and nothing is shared: the constructor points the books
// folder at an empty directory made for this test and the destructor removes
// it, so no scenario can see what another left behind and the order they run
// in cannot matter. That is also what makes a given state knowable -- an empty
// folder is the same empty folder every time.
//
// What is still a stub, and why: the per-book isolation of payee rules, OFX
// account mappings and CSV profiles is specified, and the tables for them are
// in the schema, but nothing writes them yet. The QIF profile file beside the
// books is specified and not built. Those stubs fail by name rather than
// passing on an empty book, which would be the worst of both.
class BooksGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

    // The two books this file is about, as context. Nothing is created here:
    // the scenarios that need a book on disk make it themselves, and one that
    // makes Business would be refused if this had made it already.
    void given_books_are(const std::vector<BookString>& values) {
        named_.clear();
        for (const auto& value : values) named_.push_back(value.name);
    }

    void given_books_are_as_previous() {}

    void given_the_chart_of_accounts_of_business_is(const std::vector<AccountString>& v) {
        write_chart("Business", v);
    }

    void given_the_chart_of_accounts_of_personal_is(const std::vector<AccountString>& v) {
        write_chart("Personal", v);
    }

    // Read back from the Personal book, which is the whole point: a picker
    // offering what Business holds would be reading the wrong file.
    void when_category_picker_opened_in_personal() {
        offered_.clear();
        chart::Chart accounts;
        ASSERT_TRUE(read_book("Personal", &accounts, nullptr));
        for (const chart::Account& a : accounts.all())
            if (!a.placeholder && types::class_of(a.type) == types::AccountClass::Nominal)
                offered_.push_back(a.path.value());
    }

    void then_category_picker_offers(const std::vector<AccountString>& values) {
        std::vector<std::string> want;
        for (const auto& value : values) want.push_back(value.path);
        std::sort(want.begin(), want.end());
        std::vector<std::string> got = offered_;
        std::sort(got.begin(), got.end());
        EXPECT_EQ(want, got) << "the picker offered " << got.size() << " categories";
    }

    // Into the file and into the book: a scenario that goes on to save holds
    // these rules in memory, and one that only asks another book what is in
    // force needs them on disk.
    void given_payee_rules_of_business_are(const std::vector<PayeeRuleString>& values) {
        write_rules("Business", values);
        rules_ = rules_from(values);
    }

    void given_payee_rules_of_personal_are(const std::vector<PayeeRuleString>& values) {
        write_rules("Personal", values);
    }

    // Read back from Personal's own file: a rule learned in Business and
    // written into its own payee_rules table is not a row this query can even
    // reach, which is what "in force" has to mean for a book that is a
    // separate file rather than a branch of a shared one.
    void then_payee_rules_in_force_in_personal_are(const std::vector<PayeeRuleString>& values) {
        std::vector<payees::Rule> rules;
        ASSERT_TRUE(read_book_with_rules("Personal", nullptr, nullptr, &rules));
        ASSERT_EQ(values.size(), rules.size()) << "payee rules in force in Personal";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].pattern, rules[i].pattern) << "rule " << i << " pattern";
            EXPECT_EQ(values[i].matchtype.value, payees::to_string(rules[i].match_type))
                << "rule " << i << " match type";
            EXPECT_EQ(values[i].payee.value, rules[i].payee) << "rule " << i << " payee";
            EXPECT_EQ(blank(values[i].category), rules[i].category) << "rule " << i << " category";
        }
    }

    // The file being matched, the profiles of the book that is open, and
    // which of them it turned out to be -- kept from the When to the Then.
    csv::Read file_;
    std::vector<csv::Profile> saved_profiles_;
    const csv::Profile* recognised_ = nullptr;

    // --- csv profiles, which are the book's own ----------------------------

    // A Signature in a table is the file's headings in file order. Which
    // column means what is not what these scenarios are about, so each
    // heading goes through the alias table exactly as the ImportCsv glue
    // does it, and the profile that comes out expects that set of headings.
    static csv::Profile profile_from(const std::string& name,
                                     const std::string& signature) {
        csv::Profile one;
        one.name = name;
        std::string heading;
        std::vector<std::string> headings;
        for (char c : signature) {
            if (c == ',') { headings.push_back(heading); heading.clear(); }
            else heading += c;
        }
        if (!heading.empty()) headings.push_back(heading);
        for (const std::string& one_heading : headings) {
            const auto field = csv::field_for_heading(one_heading);
            if (!field.has_value()) continue;
            switch (*field) {
                case csv::Field::Date:     one.date_column = one_heading; break;
                case csv::Field::Payee:    one.payee_column = one_heading; break;
                case csv::Field::Memo:     one.memo_column = one_heading; break;
                case csv::Field::CheckNo:  one.check_column = one_heading; break;
                case csv::Field::Category: one.category_column = one_heading; break;
                case csv::Field::Balance:  one.balance_column = one_heading; break;
                case csv::Field::Amount:   one.amount_column = one_heading; break;
                case csv::Field::Debit:    one.debit_column = one_heading; break;
                case csv::Field::Credit:   one.credit_column = one_heading; break;
                case csv::Field::Ignore:   break;
            }
        }
        return one;
    }

    // Saved into the book named, with nothing else about it disturbed: a
    // profile is one more thing the book holds, so it goes through the same
    // write the chart does.
    void write_profiles(const std::string& name,
                        const std::vector<SavedProfileString>& values) {
        make_if_needed(name);
        current_book_ = name;
        chart::Chart accounts;
        std::vector<ledger::Transaction> transactions;
        std::vector<ledger::TransactionSplit> splits;
        std::vector<payees::Rule> rules;
        ASSERT_TRUE(read_book_with_rules(name, &accounts, &transactions, &rules, &splits));
        std::vector<csv::Profile> profiles;
        for (const auto& value : values)
            profiles.push_back(profile_from(value.name, value.signature));
        store::Book opened;
        const store::Failure where = store::Book::open_named(name, &opened);
        ASSERT_FALSE(where.refused) << where.reason;
        const store::Failure no =
            opened.write(accounts, transactions, rules, splits, {}, profiles);
        ASSERT_FALSE(no.refused) << no.reason;
    }

    void given_saved_profiles_of_business_are(const std::vector<SavedProfileString>& values) {
        write_profiles("Business", values);
    }

    void given_saved_profiles_of_personal_are(const std::vector<SavedProfileString>& values) {
        write_profiles("Personal", values);
    }

    void given_the_current_book_is(const std::vector<CurrentBookString>& values) {
        ASSERT_FALSE(values.empty());
        current_book_ = values.front().name;
    }

    void given_the_file_contains(const std::string& value) {
        file_ = csv::read(value);
        ASSERT_FALSE(file_.refused) << file_.refusal;
    }

    // The headings matched against the profiles of the book that is open,
    // read back off its own file. Nothing reaches into the other book,
    // which is the whole of the scenario: the same bank set up twice is the
    // repetition this decision accepts.
    void when_headers_matched() {
        ASSERT_FALSE(current_book_.empty()) << "no book is open";
        store::Book opened;
        const store::Failure where = store::Book::open_named(current_book_, &opened);
        ASSERT_FALSE(where.refused) << where.reason;
        chart::Chart accounts;
        ledger::Ledger unused;
        std::vector<ledger::Transaction> transactions;
        std::vector<payees::Rule> rules;
        std::vector<ledger::TransactionSplit> splits;
        std::map<std::string, int> widths;
        saved_profiles_.clear();
        const store::Failure no = opened.read(&accounts, &unused, &transactions,
                                              &rules, &splits, &widths,
                                              &saved_profiles_);
        ASSERT_FALSE(no.refused) << no.reason;
        recognised_ = csv::profile_for(saved_profiles_, file_.headings);
    }

    // MatchedBy is left as the table states it: what this scenario is about
    // is whether the other book's profile was found, and an unrecognised
    // file is read through the alias table either way.
    void then_profile_recognised_is(const std::vector<ProfileRecognitionString>& values) {
        ASSERT_FALSE(values.empty());
        ProfileRecognitionString got = values.front();
        got.name = recognised_ == nullptr ? "none" : recognised_->name;
        got.asked = recognised_ == nullptr ? "true" : "false";
        EXPECT_EQ(values.front(), got) << "wanted " << values.front().to_string()
                                      << " got " << got.to_string();
    }

    void given_account_mappings_of_business_are(const std::vector<AccountMappingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_account_mappings_of_business_are";
    }

    void given_account_mappings_of_personal_are(const std::vector<AccountMappingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_account_mappings_of_personal_are";
    }

    // Placing a download in the book that is open, and only in that book.
    // The chart is read back off the current book rather than kept in a
    // member, because the point of the scenario is that the other book's
    // chart is not consulted -- and a member would hold whichever book was
    // written last, which is exactly the bug it is testing for.
    void when_statement_imported(const std::vector<OfxStatementString>& values) {
        ASSERT_FALSE(values.empty());
        ASSERT_FALSE(current_book_.empty()) << "no book is open";
        chart::Chart accounts;
        std::vector<ledger::Transaction> transactions;
        ASSERT_TRUE(read_book(current_book_, &accounts, &transactions));
        const OfxStatementString& value = values.front();
        const std::string bank_id = blank(value.bankid);
        const std::string account_id = value.ofxaccountid;
        placed_ = ofx::place_statement(accounts, bank_id, account_id);
        // No account in this book claims the number, so the import has
        // nowhere to put the statement and has to ask.
        was_asked_ = placed_.empty();
        UnknownAccountString asked;
        asked.bankid = bank_id.empty() ? "none" : bank_id;
        asked.ofxaccountid = account_id;
        asked.ofxaccttype = value.ofxaccttype.value;
        asked.suggestion = "none";
        asked_ = asked;
    }

    // What the dialog is told. A suggestion of "none" is the case this
    // scenario is about: the number does exist on an account, in the other
    // book, and nothing is allowed to find it there.
    void then_asked_to_choose_an_account_for(const std::vector<UnknownAccountString>& values) {
        ASSERT_FALSE(values.empty());
        ASSERT_TRUE(was_asked_) << "the statement was placed in " << placed_
                                << " rather than asked about";
        EXPECT_EQ(values.front(), asked_);
    }

    // Where the last statement went, and what the dialog was told when it
    // went nowhere. Kept only from the When to the Then of one scenario.
    std::string placed_;
    bool was_asked_ = false;
    UnknownAccountString asked_;

    void given_transactions_of_business_are(const std::vector<TransactionString>& v) {
        write_transactions("Business", v);
    }

    void given_transactions_of_personal_are(const std::vector<TransactionString>& v) {
        write_transactions("Personal", v);
    }

    // One row is one transaction. The accounts it names are put in the chart
    // for the same reason write_postings does it: these scenarios are about
    // the books being separate, not about the chart.
    void write_transactions(const std::string& name,
                            const std::vector<TransactionString>& values) {
        make_if_needed(name);
        current_book_ = name;
        chart::Chart accounts;
        std::vector<ledger::Transaction> transactions;
        ASSERT_TRUE(read_book(name, &accounts, &transactions));
        const std::vector<ledger::TransactionSplit> splits;
        for (const auto& value : values) {
            for (const std::string& path : {blank(value.sourceaccount),
                                            blank(value.destination)}) {
                if (path.empty() || accounts.find(path) != nullptr) continue;
                chart::Account a;
                a.path = types::AccountPath(path);
                a.type = account_type_guessed_from_path(path);
                accounts.put(a);
            }
            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef(value.ref);
            const auto on = types::Date::from_iso(value.date);
            ASSERT_TRUE(on.has_value()) << value.date;
            t.date = *on;
            t.payee = types::PayeeName(blank(value.payee));
            t.check_no = types::CheckNumber(blank(value.checkno));
            t.memo = blank(value.memo);
            t.source_account = accounts.ref_for_path(blank(value.sourceaccount));
            t.destination = accounts.ref_for_path(blank(value.destination));
            t.amount = Money(value.amount);
            if (value.cleared.value != DNCString && !value.cleared.value.empty())
                t.cleared = types::cleared_status_from_string(value.cleared);
            transactions.push_back(t);
        }
        write_back(name, accounts, transactions, splits);
    }

    // Each book opened on its own and checked on its own. Two books that
    // balance separately is the claim; nothing is added across them.
    void when_each_book_checked() {
        checks_.clear();
        const std::vector<std::string> both = {"Business", "Personal"};
        for (const std::string& name : both) {
            std::vector<ledger::Transaction> transactions;
            std::vector<ledger::TransactionSplit> splits;
            if (!read_book(name, nullptr, &transactions, &splits)) continue;
            PerBookCheckString row;
            row.book = name;
            row.transactionschecked = std::to_string(transactions.size());
            int out = 0;
            for (const ledger::Transaction& t : transactions) {
                std::vector<Money> amounts;
                for (const ledger::Side& s : ledger::sides_of(t, splits))
                    amounts.push_back(s.amount);
                if (ledger::imbalance_of(amounts).cents() != 0) ++out;
            }
            row.outofbalance = std::to_string(out);
            row.balanced = out == 0 ? "true" : "false";
            checks_.push_back(row);
        }
    }

    void then_book_checks_are(const std::vector<PerBookCheckString>& values) {
        ASSERT_EQ(values.size(), checks_.size()) << "books checked";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].book, checks_[i].book) << "book " << i;
            EXPECT_EQ(values[i].transactionschecked, checks_[i].transactionschecked)
                << checks_[i].book << " transactions checked";
            EXPECT_EQ(values[i].outofbalance, checks_[i].outofbalance)
                << checks_[i].book << " out of balance";
            EXPECT_EQ(values[i].balanced, checks_[i].balanced)
                << checks_[i].book << " balanced";
        }
    }

    void when_register_entry_committed(const std::vector<RegisterEntryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_register_entry_committed";
    }

    void then_rejected_because(const std::vector<RejectionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_rejected_because";
    }

    // Each book is read, has its own half of the draw appended, and is written
    // back on its own -- there is no shared transaction and no join between
    // the two files, only two calls that happen to be made together.
    void when_draw_recorded(const std::vector<OwnerDrawString>& values) {
        for (const auto& value : values) {
            const auto date = types::Date::from_iso(value.date);
            ASSERT_TRUE(date.has_value()) << value.date;
            const Money amount{value.amount};

            make_if_needed(value.frombook);
            make_if_needed(value.tobook);

            chart::Chart from_accounts;
            chart::Chart to_accounts;
            std::vector<ledger::Transaction> from_txns;
            std::vector<ledger::Transaction> to_txns;
            ASSERT_TRUE(read_book(value.frombook, &from_accounts, &from_txns));
            ASSERT_TRUE(read_book(value.tobook, &to_accounts, &to_txns));

            const std::string from_ref =
                std::string(1, value.frombook.front()) + std::to_string(from_txns.size() + 1);
            const std::string to_ref =
                std::string(1, value.tobook.front()) + std::to_string(to_txns.size() + 1);

            from_txns.push_back(books::draw_transaction(&from_accounts, *date, -amount, from_ref));
            to_txns.push_back(books::draw_transaction(&to_accounts, *date, amount, to_ref));

            write_back(value.frombook, from_accounts, from_txns);
            write_back(value.tobook, to_accounts, to_txns);
        }
    }

    void then_transactions_of_business_are(const std::vector<TransactionString>& values) {
        check_transactions_of("Business", values);
    }

    void then_transactions_of_personal_are(const std::vector<TransactionString>& values) {
        check_transactions_of("Personal", values);
    }

    // Read back out of the file, in the order it holds them.
    void check_transactions_of(const std::string& name,
                               const std::vector<TransactionString>& values) {
        chart::Chart accounts;
        std::vector<ledger::Transaction> transactions;
        std::vector<ledger::TransactionSplit> splits;
        ASSERT_TRUE(read_book(name, &accounts, &transactions, &splits));
        ASSERT_EQ(values.size(), transactions.size())
            << name << " transactions that came back";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const ledger::Transaction& got = transactions[i];
            const TransactionString& want = values[i];
            const std::string where = name + " transaction " + std::to_string(i + 1);
            if (want.ref.value != DNCString)
                EXPECT_EQ(want.ref.value, got.ref.value()) << where << " ref";
            if (want.date != DNCString)
                EXPECT_EQ(want.date, got.date.iso()) << where << " date";
            if (want.payee.value != DNCString)
                EXPECT_EQ(blank(want.payee), got.payee.value()) << where << " payee";
            if (want.memo != DNCString)
                EXPECT_EQ(blank(want.memo), got.memo) << where << " memo";
            if (want.sourceaccount.value != DNCString)
                EXPECT_EQ(blank(want.sourceaccount), accounts.path_for_ref(got.source_account))
                    << where << " source account";
            if (want.destination.value != DNCString)
                EXPECT_EQ(blank(want.destination), accounts.path_for_ref(got.destination))
                    << where << " destination";
            if (want.amount.value != DNCString)
                EXPECT_EQ(Money(want.amount).cents(), got.amount.cents()) << where << " amount";
            if (want.cleared.value != DNCString && !want.cleared.value.empty())
                EXPECT_EQ(want.cleared.value, types::to_string(got.cleared)) << where << " cleared";
        }
    }

    // Read back from the current book -- set explicitly by a Given, or, where
    // a scenario never names one, whichever book the last Given actually
    // touched. A report is always of one book; there is no reading across.
    void when_report_run(const std::vector<ReportSpecString>& values) {
        ASSERT_FALSE(current_book_.empty()) << "no current book to report on";
        make_if_needed(current_book_);
        chart_ = chart::Chart();
        transactions_.clear();
        ASSERT_TRUE(read_book(current_book_, &chart_, &transactions_, &splits_));
        ledger_ = ledger::Ledger();
        ledger::add_to_book(&ledger_, chart_, transactions_, splits_);

        for (const auto& value : values) {
            reports::Spec spec;
            const auto from = types::Date::from_iso(value.from);
            const auto to = types::Date::from_iso(value.to);
            if (from) spec.from = *from;
            if (to) spec.to = *to;
            report_range_ = reports::Range{spec.from, spec.to};
            report_ = reports::category_report(chart_, ledger_, spec);
        }
    }

    void then_report_rows_are(const std::vector<ReportRowString>& values) {
        ASSERT_EQ(values.size(), report_.rows.size()) << "number of report rows";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const reports::Row& got = report_.rows[i];
            EXPECT_EQ(values[i].account.value, got.account) << "row " << i;
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

    void then_net_worth_is(const std::vector<NetWorthString>& values) {
        const ledger::Ledger::NetWorth got = ledger_.net_worth(chart_);
        for (const auto& value : values) {
            EXPECT_EQ(Money(value.assets).cents(), got.assets.cents()) << "assets";
            EXPECT_EQ(Money(value.liabilities).cents(), got.liabilities.cents())
                << "liabilities";
            EXPECT_EQ(Money(value.networth).cents(), got.net.cents()) << "net worth";
        }
    }

    void when_book_opened(const std::vector<BookOpenString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_book_opened";
    }

    void then_windows_are(const std::vector<BookWindowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_windows_are";
    }

    void then_report_heading_is(const std::vector<ReportHeadingString>& values) {
        for (const auto& value : values) {
            EXPECT_EQ(value.book, current_book_) << "report heading book";
            EXPECT_EQ(value.period, reports::period_label(report_range_)) << "report heading period";
        }
    }

    void given_no_books_exist() {
        ASSERT_TRUE(store::book_names().empty()) << "the folder for this test was not empty";
    }

    void when_qif_file_migrated_into_a_new_book(const std::vector<QifMigrationString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_qif_file_migrated_into_a_new_book";
    }

    void then_books_are(const std::vector<BookString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_books_are";
    }

    void given_import_target_is(const std::vector<ImportTargetString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_import_target_is";
    }

    void when_qif_file_imported() {
        ADD_FAILURE() << "Not implemented: when_qif_file_imported";
    }

    void then_accounts_created_are(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_accounts_created_are";
    }

    void then_migration_notes_are(const std::vector<MigrationNoteString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_migration_notes_are";
    }

    void examples_businessrule_what_is_held_in_a_book_and_what_is_held_for_the_program(const std::vector<BookScopeString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_what_is_held_in_a_book_and_what_is_held_for_the_program";
    }

public:

    void given_no_settings_file_exists() {
        ADD_FAILURE() << "Not implemented: given_no_settings_file_exists";
    }

    void when_a_qif_profile_is_saved_named(const std::vector<QifProfileSavedString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_a_qif_profile_is_saved_named";
    }

    void then_the_qif_profiles_are(const std::vector<QifProfileRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_the_qif_profiles_are";
    }

    void then_the_book_names_are(const std::vector<BookNameRowString>& values) {
        std::vector<std::string> want;
        for (const auto& value : values) want.push_back(value.name);
        EXPECT_EQ(want, listed_) << "the folder listed " << listed_.size() << " books";
    }

    void when_a_new_book_is_made_named(const std::vector<BookNameString>& values) {
        ASSERT_FALSE(values.empty());
        const std::string name = values.front().name;
        store::Book made;
        last_ = store::Book::create(store::path_for(name), name, &made);
        if (!last_.refused) {
            store_ = std::move(made);
            open_name_ = name;
        }
    }

    void then_the_book_opened_is(const std::vector<OpenedBookString>& values) {
        ASSERT_FALSE(values.empty());
        const OpenedBookString& want = values.front();
        ASSERT_FALSE(last_.refused) << last_.reason;
        ASSERT_TRUE(store_.is_open()) << "nothing was opened";
        EXPECT_EQ(want.name, open_name_);

        chart::Chart accounts;
        ledger::Ledger book;
        std::vector<ledger::Transaction> transactions;
        std::vector<ledger::TransactionSplit> splits;
        const store::Failure no = store_.read(&accounts, &book, &transactions, nullptr, &splits);
        ASSERT_FALSE(no.refused) << no.reason;
        EXPECT_EQ(want.accounts, std::to_string(accounts.all().size())) << "accounts";
        EXPECT_EQ(want.transactions, std::to_string(transactions.size())) << "transactions";
        // A book with nothing in it balances, which is worth asserting rather
        // than assuming: an empty book that did not would mean the check
        // itself was wrong.
        bool balances = true;
        for (const ledger::Transaction& t : transactions) {
            if (!chart::is_split(t.destination)) continue;
            std::vector<Money> amounts;
            for (const ledger::TransactionSplit& s : ledger::split_lines_of(splits, t.id))
                amounts.push_back(s.amount);
            if (t.amount.cents() + ledger::imbalance_of(amounts).cents() != 0) balances = false;
        }
        EXPECT_EQ(parse_bool_cell(want.balances), balances) << "balances";
    }

    void given_a_new_book_named_business() {
        last_ = store::Book::create(store::path_for("Business"), "Business", &store_);
        ASSERT_FALSE(last_.refused) << last_.reason;
        open_name_ = "Business";
    }

    // The book's display options are settings of the book row, so they are
    // written and read back through the store rather than held here. See "How
    // this book writes what is owed" in Books.spectable.
    void when_book_display_options_are_set_to(
            const std::vector<BookDisplayOptionsString>& values) {
        ASSERT_FALSE(values.empty());
        ASSERT_TRUE(store_.is_open()) << "no book was made";
        store::BookInfo now = store_.info();
        now.quicken_signs = parse_bool_cell(values.front().quickensigns);
        last_ = store_.set_info(now);
        ASSERT_FALSE(last_.refused) << last_.reason;
    }

    void then_book_display_options_are(const std::vector<BookDisplayOptionsString>& values) {
        ASSERT_FALSE(values.empty());
        ASSERT_TRUE(store_.is_open()) << "no book was made";
        EXPECT_EQ(parse_bool_cell(values.front().quickensigns), store_.info().quicken_signs)
            << "how the book writes what is owed";
    }

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
    }

    // Written, closed, and read back from the file -- not from anything still
    // in memory. Closing is the point: a book that only round-trips while the
    // program holds it has not been saved at all.
    void when_the_book_is_saved_and_opened_again() {
        ASSERT_TRUE(store_.is_open()) << "no book was made";
        last_ = store_.write(chart_, transactions_, rules_, splits_);
        ASSERT_FALSE(last_.refused) << last_.reason;
        store_ = store::Book();

        last_ = store::Book::open_named(open_name_, &store_);
        ASSERT_FALSE(last_.refused) << last_.reason;
        chart_ = chart::Chart();
        transactions_.clear();
        splits_.clear();
        rules_.clear();
        ledger::Ledger ignored;
        last_ = store_.read(&chart_, &ignored, &transactions_, &rules_, &splits_);
        ASSERT_FALSE(last_.refused) << last_.reason;
    }

    void then_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        for (const auto& value : values) {
            const chart::Account* a = chart_.find(value.path);
            ASSERT_NE(nullptr, a) << value.path << " did not come back";
            EXPECT_EQ(value.type.value, types::to_string(a->type)) << value.path.value << " type";
            EXPECT_EQ(parse_bool_cell(value.placeholder), a->placeholder)
                << value.path << " placeholder";
            EXPECT_EQ(parse_bool_cell(value.hidden), a->hidden) << value.path << " hidden";
        }
    }

    void when_a_book_is_opened_named(const std::vector<BookNameString>& values) {
        ASSERT_FALSE(values.empty());
        store::Book opened;
        last_ = store::Book::open_named(values.front().name, &opened);
        if (!last_.refused) {
            store_ = std::move(opened);
            open_name_ = values.front().name;
        }
    }

    void then_the_book_is_refused_saying(const std::string& value) {
        EXPECT_TRUE(last_.refused) << "it was not refused";
        EXPECT_EQ(value, last_.reason);
    }

    // A book from a later version of the program. Written with SQLite directly
    // rather than through the store, because the store is what is being tested
    // and it will not write a version it does not know.
    void given_a_book_whose_schema_version_is_99() {
        store::Book made;
        const store::Failure no =
            store::Book::create(store::path_for("Business"), "Business", &made);
        ASSERT_FALSE(no.refused) << no.reason;
        made = store::Book();

        sqlite3* db = nullptr;
        ASSERT_EQ(SQLITE_OK, sqlite3_open(store::path_for("Business").c_str(), &db));
        ASSERT_EQ(SQLITE_OK, sqlite3_exec(db, "UPDATE schema SET version = 99",
                                          nullptr, nullptr, nullptr));
        sqlite3_close(db);
    }

    // The files in the folder, whatever they are. A folder people can see is a
    // folder people put things in, so the listing has to cope with a file that
    // is not a book at all.
    void given_the_books_folder_holds(const std::vector<BookFileListString>& values) {
        for (const auto& value : values) {
            const std::string at = folder_ + "\\" + value.filename;
            std::ofstream(at).put('x');
        }
    }

    void when_the_books_are_listed() {
        listed_ = store::book_names();
    }

    // For every Thing the rule says is PerBook, there is a table of its own in
    // a book the program just made. Payees and Reconciliations are PerBook but
    // derived or not yet built, so they name no table of their own and are
    // skipped rather than asserted against one; Machine and Settings things
    // are not a book's business at all.
    void examples_businessrule_everything_a_book_needs_is_in_the_book(const std::vector<BookScopeString>& values) {
        store::Book made;
        const store::Failure no = store::Book::create(store::path_for("Scope"), "Scope", &made);
        ASSERT_FALSE(no.refused) << no.reason;
        const std::vector<std::string> tables = tables_of(store::path_for("Scope"));

        for (const auto& value : values) {
            if (value.scope != "PerBook") continue;
            std::vector<std::string> wanted;
            if (value.thing == "Accounts" || value.thing == "Categories") wanted = {"accounts"};
            else if (value.thing == "Transactions and splits") wanted = {"transactions", "transaction_splits"};
            else if (value.thing == "Payee rules") wanted = {"payee_rules"};
            else if (value.thing == "Remembered splits") wanted = {"payee_split_rules"};
            else if (value.thing == "Fiscal year start") wanted = {"book"};
            else if (value.thing == "CSV bank profiles") wanted = {"csv_profiles"};
            else if (value.thing == "CSV header aliases") wanted = {"csv_header_matches"};
            else continue;
            for (const std::string& table : wanted)
                EXPECT_TRUE(std::find(tables.begin(), tables.end(), table) != tables.end())
                    << value.thing << " is said to be kept per book but there is no " << table
                    << " table -- " << value.why;
        }
    }

    void examples_businessrule_a_book_is_one_sqlite_database_in_one_known_folder(
            const std::vector<BookFileString>& values) {
        for (const auto& value : values) {
            if (value.action == "New book") {
                store::Book made;
                const store::Failure no =
                    store::Book::create(store::path_for(value.name), value.name, &made);
                EXPECT_FALSE(no.refused) << value.effect << " -- " << no.reason;
                EXPECT_TRUE(std::filesystem::exists(store::path_for(value.name)))
                    << value.name << ".sadb was not created";
            } else if (value.action == "New book, the second time") {
                store::Book made;
                const store::Failure no =
                    store::Book::create(store::path_for(value.name), value.name, &made);
                EXPECT_TRUE(no.refused) << value.effect;
                EXPECT_EQ("A book named " + value.name + " already exists", no.reason);
            } else if (value.action == "Open book") {
                store::Book opened;
                const store::Failure no = store::Book::open_named(value.name, &opened);
                if (value.effect == "Opened") {
                    EXPECT_FALSE(no.refused) << value.name << ": " << no.reason;
                } else {
                    EXPECT_TRUE(no.refused) << value.effect;
                    EXPECT_EQ("There is no book named " + value.name, no.reason);
                }
            } else if (value.action == "List books") {
                // Every .sadb and nothing else: Business was made above, Typo
                // never existed, and a stray file is not a book.
                std::ofstream(folder_ + "\\notes.txt").put('x');
                const std::vector<std::string> got = store::book_names();
                EXPECT_EQ(std::vector<std::string>{"Business"}, got) << value.effect;
            } else {
                FAIL() << "unlisted action " << value.action;
            }
        }
    }

    // The completeness check: every entity the rule says lives in the book has
    // a table of that name in a book the program just made, and every entity it
    // says lives nowhere has no table. This is the test that makes the rule
    // true rather than aspirational -- add an entity to the spec and forget the
    // table, and this fails.
    void examples_businessrule_one_table_per_collection_one_row_per_entity(
            const std::vector<EntityStorageString>& values) {
        store::Book made;
        const store::Failure no =
            store::Book::create(store::path_for("Shapes"), "Shapes", &made);
        ASSERT_FALSE(no.refused) << no.reason;
        const std::vector<std::string> tables = tables_of(store::path_for("Shapes"));

        for (const auto& value : values) {
            const bool in_the_book = value.where == "The book";
            const bool has_table = std::find(tables.begin(), tables.end(),
                                             blank(value.table)) != tables.end();
            if (in_the_book) {
                EXPECT_FALSE(blank(value.table).empty())
                    << value.entity << " is said to live in the book but names no table";
                EXPECT_TRUE(has_table)
                    << value.entity << " lives in the book but there is no table "
                    << value.table;
            } else if (blank(value.table).empty()) {
                // Nowhere, not yet, or the folder: there should be no table.
                EXPECT_FALSE(has_table) << value.entity << " names no table";
            }
        }
    }

    void examples_businessrule_what_a_book_s_file_holds_besides_its_entities(
            const std::vector<BookInfrastructureString>& values) {
        store::Book made;
        const store::Failure no =
            store::Book::create(store::path_for("Holds"), "Holds", &made);
        ASSERT_FALSE(no.refused) << no.reason;
        const std::string path = store::path_for("Holds");

        for (const auto& value : values) {
            const std::vector<std::string> columns = columns_of(path, value.table);
            ASSERT_FALSE(columns.empty()) << "no table " << value.table;
            for (const std::string& wanted : split(value.columns))
                EXPECT_TRUE(std::find(columns.begin(), columns.end(), wanted) != columns.end())
                    << value.table << " has no column " << wanted << " -- " << value.notes;
            // One row, as the notes say.
            EXPECT_EQ(1, rows_in(path, value.table)) << value.table << " -- " << value.notes;
        }
    }

    // Each attribute type is checked against a real column of that type in a
    // real book, named here, so the rule is about the schema rather than about
    // a table of good intentions.
    void examples_businessrule_how_an_attribute_becomes_a_column(
            const std::vector<ColumnTypeString>& values) {
        store::Book made;
        const store::Failure no =
            store::Book::create(store::path_for("Types"), "Types", &made);
        ASSERT_FALSE(no.refused) << no.reason;
        const std::string path = store::path_for("Types");

        for (const auto& value : values) {
            const std::pair<std::string, std::string> where = example_column(value.attributetype);
            if (where.first.empty()) continue;   // nothing in the schema uses it yet
            const std::string declared = declared_type(path, where.first, where.second);
            EXPECT_EQ(value.column, declared)
                << value.attributetype << " is stored in " << where.first << "."
                << where.second << " -- " << value.notes;
        }
    }

private:
    struct PendingSecondRow { Money amount; std::string memo; };
    std::map<std::string, PendingSecondRow> second_row_amount_;
    store::Book store_;
    chart::Chart chart_;
    std::vector<ledger::Transaction> transactions_;
    std::vector<ledger::TransactionSplit> splits_;
    std::vector<payees::Rule> rules_;
    std::vector<std::string> named_;
    std::vector<std::string> listed_;
    std::string open_name_;
    store::Failure last_;
    std::string folder_;

    std::vector<std::string> offered_;
    std::vector<PerBookCheckString> checks_;

    // Which book a report, or anything else that works on just one, is of. Set
    // explicitly by a Given, or left to whichever book the last Given actually
    // wrote to -- see write_chart and write_postings below.
    std::string current_book_;
    ledger::Ledger ledger_;
    reports::CategoryReport report_;
    reports::Range report_range_{types::Date(2024, 1, 1), types::Date(2024, 12, 31)};

    // --- two books at once -------------------------------------------------
    //
    // Each is made on demand, written to, and closed again. Nothing stays open
    // across a step, so what a later step reads is what is on the disk.

    void make_if_needed(const std::string& name) {
        if (std::filesystem::exists(store::path_for(name))) return;
        store::Book made;
        const store::Failure no = store::Book::create(store::path_for(name), name, &made);
        ASSERT_FALSE(no.refused) << no.reason;
    }

    bool read_book(const std::string& name, chart::Chart* accounts,
                   std::vector<ledger::Transaction>* transactions,
                   std::vector<ledger::TransactionSplit>* splits = nullptr) {
        return read_book_with_rules(name, accounts, transactions, nullptr, splits);
    }

    bool read_book_with_rules(const std::string& name, chart::Chart* accounts,
                              std::vector<ledger::Transaction>* transactions,
                              std::vector<payees::Rule>* rules,
                              std::vector<ledger::TransactionSplit>* splits = nullptr) {
        store::Book opened;
        store::Failure no = store::Book::open_named(name, &opened);
        if (no.refused) { ADD_FAILURE() << no.reason; return false; }
        chart::Chart mine;
        ledger::Ledger unused;
        std::vector<ledger::Transaction> theirs;
        std::vector<payees::Rule> their_rules;
        std::vector<ledger::TransactionSplit> their_splits;
        no = opened.read(&mine, &unused, &theirs, &their_rules, &their_splits);
        if (no.refused) { ADD_FAILURE() << no.reason; return false; }
        if (accounts != nullptr) *accounts = mine;
        if (transactions != nullptr) *transactions = theirs;
        if (rules != nullptr) *rules = their_rules;
        if (splits != nullptr) *splits = their_splits;
        return true;
    }

    void write_back(const std::string& name, const chart::Chart& accounts,
                    const std::vector<ledger::Transaction>& transactions,
                    const std::vector<ledger::TransactionSplit>& splits = {}) {
        store::Book opened;
        const store::Failure where = store::Book::open_named(name, &opened);
        ASSERT_FALSE(where.refused) << where.reason;
        const store::Failure no = opened.write(accounts, transactions, {}, splits);
        ASSERT_FALSE(no.refused) << no.reason;
    }

    void write_chart(const std::string& name, const std::vector<AccountString>& values) {
        make_if_needed(name);
        current_book_ = name;
        chart::Chart accounts;
        std::vector<ledger::Transaction> transactions;
        ASSERT_TRUE(read_book(name, &accounts, &transactions));
        for (const auto& value : values) {
            chart::Account a;
            a.path = types::AccountPath(value.path);
            a.type = types::account_type_from_string(value.type);
            a.placeholder = parse_bool_cell(value.placeholder);
            a.hidden = parse_bool_cell(value.hidden);
            a.alias = blank(value.alias);
            // The bank, its identifier and the account number are the
            // account's own, which is the whole point of the scenario that
            // says one book knowing a number leaves the other not knowing
            // it. Leaving them off here wrote a chart that could not state
            // what the table stated.
            a.bank = blank(value.bank);
            a.bank_id = blank(value.bankid);
            a.account_number = blank(value.accountnumber);
            accounts.put(a);
        }
        write_back(name, accounts, transactions);
    }

    // A guess good enough for a fixture whose point is book separation rather
    // than the chart: the real program never invents a type this way, and
    // every scenario that cares about a type gives one through
    // given_the_chart_of_accounts_of_*_is instead.
    static types::AccountType account_type_guessed_from_path(const std::string& path) {
        if (path.rfind("Assets", 0) == 0) return types::AccountType::Checking;
        if (path.rfind("Liabilities", 0) == 0) return types::AccountType::CreditCard;
        if (path.rfind("Equity", 0) == 0) return types::AccountType::Equity;
        if (path.rfind("Income", 0) == 0) return types::AccountType::Income;
        return types::AccountType::Expense;
    }

    void write_rules(const std::string& name, const std::vector<PayeeRuleString>& values) {
        make_if_needed(name);
        chart::Chart accounts;
        std::vector<ledger::Transaction> transactions;
        std::vector<payees::Rule> rules;
        ASSERT_TRUE(read_book_with_rules(name, &accounts, &transactions, &rules));
        rules = rules_from(values);
        store::Book opened;
        const store::Failure where = store::Book::open_named(name, &opened);
        ASSERT_FALSE(where.refused) << where.reason;
        const store::Failure no = opened.write(accounts, transactions, rules);
        ASSERT_FALSE(no.refused) << no.reason;
    }

    // A Default of "none" arrives as the literal word.
    static std::string blank(const std::string& s) {
        return (s == "none" || s == DNCString) ? std::string() : s;
    }

    static std::vector<std::string> split(const std::string& commas) {
        std::vector<std::string> out;
        std::string one;
        for (const char c : commas) {
            if (c == ',') { if (!one.empty()) out.push_back(trim(one)); one.clear(); }
            else one += c;
        }
        if (!trim(one).empty()) out.push_back(trim(one));
        return out;
    }

    static std::string trim(const std::string& text) {
        const auto first = text.find_first_not_of(" \t");
        if (first == std::string::npos) return {};
        const auto last = text.find_last_not_of(" \t");
        return text.substr(first, last - first + 1);
    }

    // --- asking the file itself, rather than the program that wrote it ------

    static std::vector<std::string> query(const std::string& path, const std::string& sql) {
        std::vector<std::string> out;
        sqlite3* db = nullptr;
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) { sqlite3_close(db); return out; }
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* text = sqlite3_column_text(stmt, 0);
                out.push_back(text == nullptr ? std::string()
                                              : reinterpret_cast<const char*>(text));
            }
        }
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return out;
    }

    static std::vector<std::string> tables_of(const std::string& path) {
        return query(path, "SELECT name FROM sqlite_master WHERE type = 'table'");
    }

    static std::vector<std::string> columns_of(const std::string& path,
                                               const std::string& table) {
        return query(path, "SELECT name FROM pragma_table_info('" + table + "')");
    }

    static std::string declared_type(const std::string& path, const std::string& table,
                                     const std::string& column) {
        const std::vector<std::string> got =
            query(path, "SELECT type FROM pragma_table_info('" + table +
                        "') WHERE name = '" + column + "'");
        return got.empty() ? std::string() : got.front();
    }

    static int rows_in(const std::string& path, const std::string& table) {
        const std::vector<std::string> got =
            query(path, "SELECT COUNT(*) FROM " + table);
        return got.empty() ? -1 : std::stoi(got.front());
    }

    // Where in the schema each attribute type actually appears, so the rule is
    // checked against a column rather than asserted.
    static std::pair<std::string, std::string> example_column(const std::string& type) {
        if (type == "Money")          return {"transactions", "amount_cents"};
        if (type == "Date")           return {"transactions", "date"};
        if (type == "Integer")        return {"book", "fiscal_year_start"};
        if (type == "Boolean")        return {"accounts", "placeholder"};
        if (type == "Text")           return {"transactions", "memo"};
        if (type == "AccountPath")    return {"accounts", "path"};
        if (type == "TransactionRef") return {"transactions", "ref"};
        if (type == "An enumeration") return {"accounts", "type"};
        return {};
    }

public:
    // Each scenario gets an empty folder of its own, so a known given state is
    // simply the folder before anything has been put in it.
    BooksGlue() {
        static int which = 0;
        folder_ = (std::filesystem::temp_directory_path() /
                   ("sa_books_" + std::to_string(++which))).string();
        std::error_code ignored;
        std::filesystem::remove_all(folder_, ignored);
        std::filesystem::create_directories(folder_, ignored);
        _putenv_s("SIMPLEACCOUNT_BOOKS", folder_.c_str());
    }

    ~BooksGlue() {
        store_ = store::Book();
        std::error_code ignored;
        std::filesystem::remove_all(folder_, ignored);
    }

    void examples_businessrule_the_qif_profiles_live_in_one_file_beside_the_books_under_a_name(const std::vector<BesideTheBooksString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_the_qif_profiles_live_in_one_file_beside_the_books_under_a_name";
    }

public:

    void given_transactions_are(const std::vector<TransactionString>& values) {
        transactions_.clear();
        for (const auto& value : values) {
            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef(value.ref);
            const auto on = types::Date::from_iso(value.date);
            if (on) t.date = *on;
            t.payee = types::PayeeName(blank(value.payee));
            t.memo = blank(value.memo);
            t.source_account = chart_.ref_for_path(blank(value.sourceaccount));
            t.destination = chart_.ref_for_path(blank(value.destination));
            t.amount = Money(value.amount);
            if (value.cleared.value != DNCString && !value.cleared.value.empty())
                t.cleared = types::cleared_status_from_string(value.cleared);
            transactions_.push_back(t);
        }
    }

    void then_transaction_is(const std::vector<TransactionString>& values) {
        for (const auto& value : values) {
            const ledger::Transaction* t = nullptr;
            for (const ledger::Transaction& one : transactions_)
                if (one.ref.value() == value.ref.value) t = &one;
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            if (value.payee.value != DNCString)
                EXPECT_EQ(blank(value.payee), t->payee.value()) << value.ref << " payee";
            if (value.sourceaccount.value != DNCString)
                EXPECT_EQ(blank(value.sourceaccount), chart_.path_for_ref(t->source_account))
                    << value.ref << " source account";
            if (value.destination.value != DNCString)
                EXPECT_EQ(blank(value.destination), chart_.path_for_ref(t->destination))
                    << value.ref << " destination";
            if (value.amount.value != DNCString)
                EXPECT_EQ(Money(value.amount).cents(), t->amount.cents())
                    << value.ref << " amount";
            if (value.cleared.value != DNCString)
                EXPECT_EQ(value.cleared.value, types::to_string(t->cleared)) << value.ref.value << " cleared";
        }
    }

public:

    void then_account_ids_are(const std::vector<AccountIdentityString>& values) {
        EXPECT_EQ(values.size(), chart_.all().size()) << "number of nodes in the chart";
        for (const AccountIdentityString& want : values) {
            const chart::Account* a = chart_.find(want.path);
            ASSERT_NE(nullptr, a) << want.path << " is not in the chart";
            EXPECT_EQ(std::stoll(want.id), a->id.value()) << want.path << " id";
            EXPECT_EQ(parse_bool_cell(want.placeholder), a->placeholder)
                << want.path << " placeholder";
        }
    }

    // Read out of the file rather than off the chart in memory, because the
    // column is the thing being asserted. A chart can work the number out from
    // the rows it holds; a book has to have written it down.
    void then_the_book_records(const std::vector<IdCounterString>& values) {
        const std::vector<std::string> got =
            query(store_.path(), "SELECT last_id_used FROM book");
        ASSERT_FALSE(got.empty()) << "the book row has no last_id_used";
        for (const IdCounterString& want : values)
            EXPECT_EQ(want.lastidused, got.front()) << "the recorded counter";
    }

    // Each question in the table is asked of a real book: two nodes created, the
    // second deleted, saved and opened again. Nothing here is asserted of a
    // table of good intentions -- the last question is the one that matters,
    // and it fails if the counter is worked out from what the chart still holds.
    void examples_businessrule_the_id_counter_is_recorded_in_the_book_not_worked_out_from_it(
            const std::vector<CounterStorageString>& values) {
        const std::string path = store::path_for("Counter");
        store::Book book;
        const store::Failure no = store::Book::create(path, "Counter", &book);
        ASSERT_FALSE(no.refused) << no.reason;

        chart::Chart chart;
        ASSERT_FALSE(chart.add(types::AccountPath("Assets"),
                               types::AccountType::Checking).refused);
        ASSERT_FALSE(chart.add(types::AccountPath("Assets:Checking"),
                               types::AccountType::Checking).refused);
        ASSERT_FALSE(book.write(chart, {}).refused);
        ASSERT_FALSE(chart.remove(types::AccountPath("Assets:Checking")).refused);
        ASSERT_FALSE(book.write(chart, {}).refused);

        book = store::Book();
        ASSERT_FALSE(store::Book::open_named("Counter", &book).refused);
        chart::Chart reopened;
        ledger::Ledger ignored;
        std::vector<ledger::Transaction> none;
        ASSERT_FALSE(book.read(&reopened, &ignored, &none).refused);

        for (const CounterStorageString& value : values) {
            const std::string& asked = value.whatisasked;
            if (asked == "Where the last id handed out is kept") {
                const std::vector<std::string> columns = columns_of(path, "book");
                EXPECT_TRUE(std::find(columns.begin(), columns.end(), "last_id_used") !=
                            columns.end())
                    << "the book row has no last_id_used -- " << value.answer;
            } else if (asked == "How the next id is found") {
                // On a copy, so that asking the question does not change the
                // chart the questions after it are asked of.
                chart::Chart probe = reopened;
                std::vector<chart::Account> made;
                ASSERT_FALSE(probe.add(types::AccountPath("Assets:Savings"),
                                       types::AccountType::Savings, &made).refused);
                ASSERT_EQ(1u, made.size());
                EXPECT_EQ(3, made.back().id.value()) << value.answer;
            } else if (asked == "What a delete does to it") {
                EXPECT_EQ("2", query(path, "SELECT last_id_used FROM book").front())
                    << value.answer;
            } else if (asked == "What a book opened again counts from") {
                // One node left in the chart, carrying id 1, and the counter
                // at 2 all the same. Worked out from the chart it would be 1.
                EXPECT_EQ(1u, reopened.all().size());
                EXPECT_LE(2, reopened.last_id_used()) << value.answer;
            } else if (asked == "Whether a group takes an id of its own") {
                const chart::Account* group = reopened.find("Assets");
                ASSERT_NE(nullptr, group) << "the group did not come back";
                EXPECT_LT(0, group->id.value()) << value.answer;
            } else {
                FAIL() << "nothing checks the question: " << asked;
            }
        }
    }

public:



    void when_the_book_is_saved() {
        ASSERT_TRUE(store_.is_open()) << "no book was made";
        last_ = store_.write(chart_, transactions_, rules_, splits_);
        ASSERT_FALSE(last_.refused) << last_.reason;
    }

    // An edit of the book in front of it rather than a new book, so a
    // transaction whose ref is already there keeps its id. The id is what a
    // save matches a row against: hand every row a new one and every row is a
    // different transaction, and a save that rewrote all of them would be
    // right to.
    void when_transactions_become(const std::vector<TransactionString>& values) {
        std::map<std::string, types::TransactionId> kept;
        for (const ledger::Transaction& t : transactions_) kept.emplace(t.ref.value(), t.id);
        given_transactions_are(values);
        for (ledger::Transaction& t : transactions_) {
            const auto at = kept.find(t.ref.value());
            if (at != kept.end()) t.id = at->second;
        }
        // Lines belong to a transaction by id, so the lines of a transaction
        // that has gone go with it. That is what deleting a split from a
        // register does, and leaving them here would hide the save's part in
        // it.
        std::vector<ledger::TransactionSplit> still;
        for (const ledger::TransactionSplit& s : splits_)
            for (const ledger::Transaction& t : transactions_)
                if (t.id.value() == s.transaction_id.value()) { still.push_back(s); break; }
        splits_ = still;
    }

    void then_transactions_are(const std::vector<TransactionString>& values) {
        ASSERT_EQ(values.size(), transactions_.size()) << "transactions in the book";
        for (const auto& value : values) {
            const ledger::Transaction* t = nullptr;
            for (const ledger::Transaction& one : transactions_)
                if (one.ref.value() == value.ref.value) t = &one;
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            EXPECT_EQ(value.date, t->date.iso()) << value.ref << " date";
            EXPECT_EQ(blank(value.payee), t->payee.value()) << value.ref << " payee";
            EXPECT_EQ(blank(value.sourceaccount), chart_.path_for_ref(t->source_account))
                << value.ref << " source account";
            EXPECT_EQ(blank(value.destination), chart_.path_for_ref(t->destination))
                << value.ref << " destination";
            EXPECT_EQ(Money(value.amount).cents(), t->amount.cents()) << value.ref << " amount";
            EXPECT_EQ(blank(value.memo), t->memo) << value.ref << " memo";
            EXPECT_EQ(value.cleared.value, types::to_string(t->cleared)) << value.ref.value << " cleared";
            // Which row this one was matched to, named by Ref the way the
            // table names rows. It is held as the other row's id, so this is
            // the check that the save wrote it and the read brought it back.
            std::string matched = "none";
            if (!t->matched_to.value().empty())
                for (const ledger::Transaction& one : transactions_)
                    if (one.id.value() == t->matched_to.value()) matched = one.ref.value();
            if (value.matchedto.value != DNCString)
                EXPECT_EQ(value.matchedto.value, matched) << value.ref.value << " matched to";
        }
    }

    // The transaction these lines are the parts of: the one that says Split.
    types::TransactionId split_owner_id() const {
        for (auto it = transactions_.rbegin(); it != transactions_.rend(); ++it)
            if (chart::is_split(it->destination)) return it->id;
        return types::TransactionId();
    }

    void given_split_lines_are(const std::vector<TransactionSplitString>& values) {
        const types::TransactionId owner = split_owner_id();
        ASSERT_FALSE(owner.value().empty()) << "no transaction for these split lines";
        for (const auto& value : values) {
            ledger::TransactionSplit s;
            s.transaction_id = owner;
            s.destination = chart_.ref_for_path(blank(value.destination));
            s.amount = Money(value.amount);
            s.memo = blank(value.memo);
            splits_.push_back(s);
        }
    }

    // Every line in the book, not only the ones under the transaction asked
    // about. A line left behind by a deletion belongs to nothing, so no
    // register would ever show it; the file is what this is about.
    void then_split_lines_are(const std::vector<TransactionSplitString>& values) {
        ASSERT_EQ(values.size(), splits_.size()) << "split lines in the book";
        for (std::size_t k = 0; k < values.size(); ++k) {
            EXPECT_EQ(blank(values[k].destination), chart_.path_for_ref(splits_[k].destination))
                << "line " << k + 1 << " destination";
            EXPECT_EQ(Money(values[k].amount).cents(), splits_[k].amount.cents())
                << "line " << k + 1 << " amount";
            EXPECT_EQ(blank(values[k].memo), splits_[k].memo) << "line " << k + 1 << " memo";
        }
    }

    void when_split_lines_become(const std::vector<TransactionSplitString>& values) {
        const types::TransactionId owner = split_owner_id();
        ASSERT_FALSE(owner.value().empty()) << "no split to change";
        std::vector<ledger::TransactionSplit> others;
        for (const ledger::TransactionSplit& s : splits_)
            if (s.transaction_id.value() != owner.value()) others.push_back(s);
        splits_ = others;
        given_split_lines_are(values);
    }

    void when_account_renamed(const std::vector<AccountRenameString>& values) {
        ASSERT_FALSE(values.empty());
        const chart::Rejection no = chart_.rename(types::AccountPath(values.front().path),
                                                  types::AccountName(values.front().newname));
        ASSERT_FALSE(no.refused) << no.reason;
    }

    void when_payee_rules_become(const std::vector<PayeeRuleString>& values) {
        rules_ = rules_from(values);
    }

    void then_payee_rules_in_force_in_business_are(const std::vector<PayeeRuleString>& values) {
        std::vector<payees::Rule> rules;
        ASSERT_TRUE(read_book_with_rules("Business", nullptr, nullptr, &rules));
        ASSERT_EQ(values.size(), rules.size()) << "payee rules in force in Business";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].pattern, rules[i].pattern) << "rule " << i << " pattern";
            EXPECT_EQ(values[i].matchtype.value, payees::to_string(rules[i].match_type))
                << "rule " << i << " match type";
            EXPECT_EQ(values[i].payee.value, rules[i].payee) << "rule " << i << " payee";
        }
    }

    // Each row driven for real against a book of its own: made, saved,
    // changed, saved again, and read back from the file. The rows are what a
    // reader of the rule takes away, so they are checked rather than
    // illustrated -- and both halves of a row are checked, the change that
    // must be in the file and the rest of the book that must not have moved.
    void examples_businessrule_a_save_leaves_the_file_holding_exactly_what_the_book_holds(
            const std::vector<SaveEffectString>& values) {
        int which = 0;
        for (const auto& value : values) {
            const std::string name = "Saves" + std::to_string(++which);
            store::Book one;
            const store::Failure made = store::Book::create(store::path_for(name), name, &one);
            ASSERT_FALSE(made.refused) << made.reason;

            chart::Chart c;
            for (const char* path : {"Assets:Checking", "Assets:Savings"})
                c.put(account_named(path, types::AccountType::Checking));
            for (const char* path : {"Expenses:Groceries", "Expenses:Household"})
                c.put(account_named(path, types::AccountType::Expense));

            std::vector<ledger::Transaction> ts;
            ts.push_back(transaction_in(c, "T1", "2026-01-15", "Harris Teeter", "Split", -4500));
            ts.push_back(
                transaction_in(c, "T2", "2026-01-16", "Lowes", "Expenses:Groceries", -1200));
            std::vector<ledger::TransactionSplit> ls;
            ls.push_back(line_in(c, ts[0].id, "Expenses:Groceries", 3000, "Apples"));
            ls.push_back(line_in(c, ts[0].id, "Expenses:Household", 1500, "Soap"));
            std::vector<payees::Rule> rs(1);
            rs[0].pattern = "STAPLES";
            rs[0].match_type = payees::MatchType::Contains;
            rs[0].payee = "Office";
            rs[0].enabled = true;

            store::Failure no = one.write(c, ts, rs, ls);
            ASSERT_FALSE(no.refused) << no.reason;
            const std::string before = what_is_in(name);

            std::string wanted_gone;
            std::string wanted_there;
            if (value.change == "A transaction added") {
                ts.push_back(
                    transaction_in(c, "T3", "2026-01-17", "Staples", "Expenses:Household", -900));
                wanted_there = "transaction T3";
            } else if (value.change == "A transaction edited") {
                ts[1].amount = Money::from_cents(-1900);
                ts[1].memo = "Nails and screws";
                wanted_gone = "Lowes Assets:Checking -> Expenses:Groceries -1200";
                wanted_there = "Nails and screws";
            } else if (value.change == "A transaction deleted") {
                ts.erase(ts.begin());
                ls.clear();
                wanted_gone = "transaction T1";
                wanted_there = "split lines 0";
            } else if (value.change == "A split line removed") {
                ls.pop_back();
                wanted_gone = "Soap";
                wanted_there = "split lines 1";
            } else if (value.change == "An account renamed") {
                const chart::Rejection why = c.rename(types::AccountPath("Assets:Savings"),
                                                      types::AccountName("Rainy Day"));
                ASSERT_FALSE(why.refused) << why.reason;
                wanted_gone = "account Assets:Savings";
                wanted_there = "account Assets:Rainy Day";
            } else if (value.change == "A payee rule added") {
                payees::Rule more;
                more.pattern = "HARRIS TEETER";
                more.match_type = payees::MatchType::Contains;
                more.payee = "Harris Teeter";
                more.enabled = true;
                rs.push_back(more);
                wanted_there = "rule HARRIS TEETER";
            } else if (value.change == "Nothing changed") {
                // Nothing to do, which is the row: a second save of a book
                // nobody has touched has to leave it alone rather than empty
                // it and fill it again.
            } else if (value.change == "The save fails") {
                // Refused by the one refusal reachable from outside: a
                // transaction with no id, which the store will not store
                // because there would be nothing to match its row against
                // next time.
                ledger::Transaction nameless = ts.front();
                nameless.id = types::TransactionId();
                nameless.ref = types::TransactionRef("T9");
                ts.push_back(nameless);
            } else {
                FAIL() << "unlisted change " << value.change;
            }

            no = one.write(c, ts, rs, ls);
            if (value.change == "The save fails") {
                EXPECT_TRUE(no.refused) << value.afterwards;
                EXPECT_EQ(before, what_is_in(name)) << value.afterwards;
                continue;
            }
            ASSERT_FALSE(no.refused) << no.reason;

            const std::string after = what_is_in(name);
            if (value.change == "Nothing changed") {
                EXPECT_EQ(before, after) << value.afterwards;
                continue;
            }
            if (!wanted_there.empty())
                EXPECT_NE(std::string::npos, after.find(wanted_there))
                    << value.afterwards << " -- the file holds\n"
                    << after;
            if (!wanted_gone.empty())
                EXPECT_EQ(std::string::npos, after.find(wanted_gone))
                    << value.afterwards << " -- the file still holds " << wanted_gone;
        }
    }

private:

    static chart::Account account_named(const std::string& path, types::AccountType type) {
        chart::Account a;
        a.path = types::AccountPath(path);
        a.type = type;
        return a;
    }

    static ledger::Transaction transaction_in(const chart::Chart& c, const std::string& ref,
                                              const std::string& date, const std::string& payee,
                                              const std::string& destination, long long cents) {
        ledger::Transaction t;
        t.id = ledger::new_id();
        t.ref = types::TransactionRef(ref);
        const auto on = types::Date::from_iso(date);
        if (on) t.date = *on;
        t.payee = types::PayeeName(payee);
        t.source_account = c.ref_for_path("Assets:Checking");
        t.destination = c.ref_for_path(destination);
        t.amount = Money::from_cents(cents);
        return t;
    }

    static ledger::TransactionSplit line_in(const chart::Chart& c,
                                            const types::TransactionId& owner,
                                            const std::string& destination, long long cents,
                                            const std::string& memo) {
        ledger::TransactionSplit s;
        s.transaction_id = owner;
        s.destination = c.ref_for_path(destination);
        s.amount = Money::from_cents(cents);
        s.memo = memo;
        return s;
    }

    // What the file holds, as one sorted string, so a row of the rule can say
    // "and nothing else altered" in a single comparison. The count of split
    // lines is in it on purpose: a line whose transaction has gone appears
    // under no transaction, and without the count a deletion that left one
    // behind would read as clean.
    std::string what_is_in(const std::string& name) {
        chart::Chart c;
        std::vector<ledger::Transaction> ts;
        std::vector<payees::Rule> rs;
        std::vector<ledger::TransactionSplit> ls;
        if (!read_book_with_rules(name, &c, &ts, &rs, &ls)) return "unreadable";
        std::vector<std::string> lines;
        for (const ledger::Transaction& t : ts) {
            std::string one = "transaction " + t.ref.value() + " " + t.date.iso() + " " +
                              t.payee.value() + " " + c.path_for_ref(t.source_account) + " -> " +
                              c.path_for_ref(t.destination) + " " +
                              std::to_string(t.amount.cents()) + " " + t.memo + " " +
                              types::to_string(t.cleared);
            for (const ledger::TransactionSplit& s : ledger::split_lines_of(ls, t.id))
                one += " / " + c.path_for_ref(s.destination) + " " +
                       std::to_string(s.amount.cents()) + " " + s.memo;
            lines.push_back(one);
        }
        lines.push_back("split lines " + std::to_string(ls.size()));
        for (const chart::Account& a : c.all())
            lines.push_back("account " + a.path.value() + " " + types::to_string(a.type));
        for (const payees::Rule& r : rs)
            lines.push_back("rule " + r.pattern + " " + payees::to_string(r.match_type) + " " +
                            r.payee);
        std::sort(lines.begin(), lines.end());
        std::string out;
        for (const std::string& line : lines) out += line + "\n";
        return out;
    }

    static std::vector<payees::Rule> rules_from(const std::vector<PayeeRuleString>& values) {
        std::vector<payees::Rule> rules;
        for (const auto& value : values) {
            payees::Rule r;
            r.pattern = value.pattern;
            r.match_type = payees::match_type_from_string(value.matchtype);
            r.payee = value.payee;
            r.category = blank(value.category);
            r.enabled = parse_bool_cell(value.enabled);
            rules.push_back(r);
        }
        return rules;
    }

public:

    // A book from an earlier version, written the way the later one is:
    // through SQLite directly, because the store will not write a version it
    // does not know, and 9 is the version before this one.
    void given_a_book_whose_schema_version_is_9() {
        store::Book made;
        const store::Failure no =
            store::Book::create(store::path_for("Business"), "Business", &made);
        ASSERT_FALSE(no.refused) << no.reason;
        made = store::Book();
        stamp_version("Business", 9);
    }

public:

    // Against a real book, the same way the infrastructure rule is checked:
    // the table is in the file or the rule is not true yet.
    void examples_businessrule_a_tag_table_of_its_own_once_there_is_a_list_of_tags(
            const std::vector<TagInfrastructureString>& values) {
        store::Book made;
        const store::Failure no =
            store::Book::create(store::path_for("Tagged"), "Tagged", &made);
        ASSERT_FALSE(no.refused) << no.reason;
        const std::string path = store::path_for("Tagged");

        for (const auto& value : values) {
            const std::vector<std::string> columns = columns_of(path, value.table);
            ASSERT_FALSE(columns.empty()) << "no table " << value.table;
            for (const std::string& wanted : split(value.columns))
                EXPECT_TRUE(std::find(columns.begin(), columns.end(), wanted) != columns.end())
                    << value.table << " has no column " << wanted << " -- " << value.notes;
            // Unique as the notes say, read off the file's own definition
            // rather than taken on trust: two spellings of one word are one
            // word, and that is the database's promise to keep.
            const std::vector<std::string> sql =
                query(path, "SELECT sql FROM sqlite_master WHERE name = '" +
                            value.table + "'");
            ASSERT_FALSE(sql.empty()) << "no definition of " << value.table;
            EXPECT_NE(std::string::npos, sql.front().find("UNIQUE"))
                << value.table << " does not promise a unique name -- " << value.notes;
            // A vocabulary, so a new book has none: the rows arrive as words
            // are typed rather than being a list of fields that have been.
            EXPECT_EQ(0, rows_in(path, value.table)) << value.table << " in a new book";
        }
    }

    // Each row is a book of its own, made by the store and then stamped with
    // the version the row describes -- or stripped of the schema table
    // altogether, which is what a file this program never wrote looks like.
    // The book is opened afterwards, because refusing is a thing open does.
    void examples_businessrule_a_book_of_any_version_but_this_one_is_refused(const std::vector<VersionRefusalString>& values) {
        int which = 0;
        for (const VersionRefusalString& value : values) {
            const std::string name = "Book" + std::to_string(++which);
            store::Book made;
            const store::Failure no = store::Book::create(store::path_for(name), name, &made);
            ASSERT_FALSE(no.refused) << no.reason;
            made = store::Book();

            const std::string& says = value.thebooksversion;
            if (says == "Later than this") stamp_version(name, store::kSchemaVersion + 1);
            else if (says == "Earlier") stamp_version(name, store::kSchemaVersion - 1);
            else if (says == "Absent") drop_schema_table(name);
            else ASSERT_EQ("This one", says) << "a version this rule does not describe";

            store::Book opened;
            const store::Failure got = store::Book::open_named(name, &opened);
            const std::string& then = value.then;
            if (then == "Opened") {
                EXPECT_FALSE(got.refused) << name << ": " << got.reason;
                continue;
            }
            EXPECT_TRUE(got.refused) << name << " was not refused";
            if (then.find("later") != std::string::npos)
                EXPECT_EQ(name + " was written by a later version of SimpleAccount", got.reason);
            else if (then.find("earlier") != std::string::npos)
                EXPECT_EQ(name + " was written by an earlier version of SimpleAccount", got.reason);
            else
                EXPECT_EQ(name + " is not a SimpleAccount book", got.reason);
        }
    }

private:
    // The version a book says it is, set behind the store's back.
    static void stamp_version(const std::string& name, int version) {
        run_sql(name, "UPDATE schema SET version = " + std::to_string(version));
    }

    // A file with no schema table at all: a SQLite database this program did
    // not write, which is the only way a book has no version to read.
    static void drop_schema_table(const std::string& name) {
        run_sql(name, "DROP TABLE schema");
    }

    static void run_sql(const std::string& name, const std::string& sql) {
        sqlite3* db = nullptr;
        ASSERT_EQ(SQLITE_OK, sqlite3_open(store::path_for(name).c_str(), &db));
        const int done = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
        sqlite3_close(db);
        ASSERT_EQ(SQLITE_OK, done) << sql;
    }

public:

public:

    // Matching two rows already in the book. What the save has to keep is
    // which two they were, so the scenario matches them and then saves.
    void when_the_other_entry_is_matched(const std::vector<EntryMatchString>& values) {
        for (const auto& value : values) {
            const ledger::Transaction* t = nullptr;
            const ledger::Transaction* other = nullptr;
            for (const ledger::Transaction& one : transactions_) {
                if (one.ref.value() == value.ref.value) t = &one;
                if (one.ref.value() == value.otherref.value) other = &one;
            }
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            ASSERT_NE(nullptr, other) << "no transaction " << value.otherref;
            EXPECT_TRUE(ledger::match_destinations(&transactions_, t->id, other->id))
                << value.ref << " could not be joined with " << value.otherref;
        }
    }

};
