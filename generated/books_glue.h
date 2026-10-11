#pragma once
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sqlite3.h>
#include "common/common.h"
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "owner_draw.h"
#include "payee_rules.h"
#include "posting.h"
#include "report.h"
#include "report_ranges.h"
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

    void given_payee_rules_of_business_are(const std::vector<PayeeRuleString>& values) {
        write_rules("Business", values);
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
            EXPECT_EQ(values[i].matchtype, payees::to_string(rules[i].match_type))
                << "rule " << i << " match type";
            EXPECT_EQ(values[i].payee, rules[i].payee) << "rule " << i << " payee";
            EXPECT_EQ(blank(values[i].category), rules[i].category) << "rule " << i << " category";
        }
    }

    void given_saved_profiles_are(const std::vector<SavedProfileString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_saved_profiles_are";
    }

    void given_the_current_book_is(const std::vector<CurrentBookString>& values) {
        ASSERT_FALSE(values.empty());
        current_book_ = values.front().name;
    }

    void given_the_file_contains(const std::string& value) {
        std::cout << value << "\n";
        ADD_FAILURE() << "Not implemented: given_the_file_contains";
    }

    void when_headers_matched() {
        ADD_FAILURE() << "Not implemented: when_headers_matched";
    }

    void then_profile_recognised_is(const std::vector<ProfileRecognitionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_profile_recognised_is";
    }

    void given_account_mappings_of_business_are(const std::vector<AccountMappingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_account_mappings_of_business_are";
    }

    void given_account_mappings_of_personal_are(const std::vector<AccountMappingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_account_mappings_of_personal_are";
    }

    void when_statement_imported(const std::vector<OfxStatementString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_statement_imported";
    }

    void then_asked_to_choose_an_account_for(const std::vector<UnmappedAccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_asked_to_choose_an_account_for";
    }

    void given_postings_of_business_are(const std::vector<PostingRowString>& v) {
        write_postings("Business", v);
    }

    void given_postings_of_personal_are(const std::vector<PostingRowString>& v) {
        write_postings("Personal", v);
    }

    // Each book opened on its own and checked on its own. Two books that
    // balance separately is the claim; nothing is added across them.
    void when_each_book_checked() {
        checks_.clear();
        const std::vector<std::string> both = {"Business", "Personal"};
        for (const std::string& name : both) {
            std::vector<ledger::Transaction> transactions;
            if (!read_book(name, nullptr, &transactions)) continue;
            PerBookCheckString row;
            row.book = name;
            row.transactionschecked = std::to_string(transactions.size());
            int out = 0;
            for (const ledger::Transaction& t : transactions) {
                std::vector<Money> amounts;
                for (const ledger::Posting& posting : t.postings)
                    amounts.push_back(posting.amount);
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

            from_txns.push_back(books::draw_transaction(*date, -amount, from_ref));
            to_txns.push_back(books::draw_transaction(*date, amount, to_ref));

            write_back(value.frombook, from_accounts, from_txns);
            write_back(value.tobook, to_accounts, to_txns);
        }
    }

    void then_postings_of_business_are(const std::vector<PostingRowString>& values) {
        check_postings_of("Business", values);
    }

    void then_postings_of_personal_are(const std::vector<PostingRowString>& values) {
        check_postings_of("Personal", values);
    }

    // Read back from the current book -- set explicitly by a Given, or, where
    // a scenario never names one, whichever book the last Given actually
    // touched. A report is always of one book; there is no reading across.
    void when_report_run(const std::vector<ReportSpecString>& values) {
        ASSERT_FALSE(current_book_.empty()) << "no current book to report on";
        make_if_needed(current_book_);
        chart_ = chart::Chart();
        transactions_.clear();
        ASSERT_TRUE(read_book(current_book_, &chart_, &transactions_));
        ledger_ = ledger::Ledger();
        for (const ledger::Transaction& t : transactions_)
            for (const ledger::Posting& p : t.postings)
                ledger_.add({t.date, p.account, p.amount, t.ref.value()});

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
        const store::Failure no = store_.read(&accounts, &book, &transactions);
        ASSERT_FALSE(no.refused) << no.reason;
        EXPECT_EQ(want.accounts, std::to_string(accounts.all().size())) << "accounts";
        EXPECT_EQ(want.transactions, std::to_string(transactions.size())) << "transactions";
        // A book with nothing in it balances, which is worth asserting rather
        // than assuming: an empty book that did not would mean the check
        // itself was wrong.
        bool balances = true;
        for (const ledger::Transaction& t : transactions) {
            std::vector<Money> amounts;
            for (const ledger::Posting& posting : t.postings) amounts.push_back(posting.amount);
            if (ledger::imbalance_of(amounts).cents() != 0) balances = false;
        }
        EXPECT_EQ(parse_bool_cell(want.balances), balances) << "balances";
    }

    void given_a_new_book_named_business() {
        last_ = store::Book::create(store::path_for("Business"), "Business", &store_);
        ASSERT_FALSE(last_.refused) << last_.reason;
        open_name_ = "Business";
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
            a.payment_payee = blank(value.paymentpayee);
            chart_.put(a);
        }
    }

    void given_postings_are(const std::vector<PostingRowString>& values) {
        transactions_.clear();
        for (const auto& value : values) {
            ledger::Transaction* into = nullptr;
            for (ledger::Transaction& t : transactions_)
                if (t.ref.value() == value.ref) into = &t;
            if (into == nullptr) {
                ledger::Transaction made;
                // Named here as any creator must name one: the store refuses a
                // transaction with no id rather than inventing one.
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
            posting.account = types::AccountPath(value.account);
            posting.amount = Money(value.amount);
            posting.memo = blank(value.memo);
            posting.cleared = types::cleared_status_from_string(value.cleared);
            into->postings.push_back(posting);
        }
    }

    // Written, closed, and read back from the file -- not from anything still
    // in memory. Closing is the point: a book that only round-trips while the
    // program holds it has not been saved at all.
    void when_the_book_is_saved_and_opened_again() {
        ASSERT_TRUE(store_.is_open()) << "no book was made";
        last_ = store_.write(chart_, transactions_);
        ASSERT_FALSE(last_.refused) << last_.reason;
        store_ = store::Book();

        last_ = store::Book::open_named(open_name_, &store_);
        ASSERT_FALSE(last_.refused) << last_.reason;
        chart_ = chart::Chart();
        transactions_.clear();
        ledger::Ledger ignored;
        last_ = store_.read(&chart_, &ignored, &transactions_);
        ASSERT_FALSE(last_.refused) << last_.reason;
    }

    void then_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        for (const auto& value : values) {
            const chart::Account* a = chart_.find(value.path);
            ASSERT_NE(nullptr, a) << value.path << " did not come back";
            EXPECT_EQ(value.type, types::to_string(a->type)) << value.path << " type";
            EXPECT_EQ(parse_bool_cell(value.placeholder), a->placeholder)
                << value.path << " placeholder";
            EXPECT_EQ(parse_bool_cell(value.hidden), a->hidden) << value.path << " hidden";
        }
    }

    void then_postings_are(const std::vector<PostingRowString>& values) {
        std::vector<PostingRowString> got;
        for (const ledger::Transaction& t : transactions_) {
            for (const ledger::Posting& posting : t.postings) {
                PostingRowString row;
                row.ref = t.ref.value();
                row.date = t.date.iso();
                row.payee = t.payee.value().empty() ? "none" : t.payee.value();
                row.account = posting.account.value();
                row.amount = posting.amount.in_register();
                row.memo = posting.memo.empty() ? "none" : posting.memo;
                row.cleared = types::to_string(posting.cleared);
                got.push_back(row);
            }
        }
        ASSERT_EQ(values.size(), got.size()) << "postings that came back";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].ref, got[i].ref) << "posting " << i << " ref";
            EXPECT_EQ(values[i].date, got[i].date) << "posting " << i << " date";
            EXPECT_EQ(blank(values[i].payee), blank(got[i].payee)) << "posting " << i << " payee";
            EXPECT_EQ(values[i].account, got[i].account) << "posting " << i << " account";
            EXPECT_EQ(Money(values[i].amount).cents(), Money(got[i].amount).cents())
                << "posting " << i << " amount";
            EXPECT_EQ(blank(values[i].memo), blank(got[i].memo)) << "posting " << i << " memo";
            EXPECT_EQ(values[i].cleared, got[i].cleared) << "posting " << i << " cleared";
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
            else if (value.thing == "Transactions and postings") wanted = {"transactions", "postings"};
            else if (value.thing == "Payee rules") wanted = {"payee_rules"};
            else if (value.thing == "Remembered splits") wanted = {"payee_split_rules"};
            else if (value.thing == "Account mappings for OFX") wanted = {"ofx_account_mappings"};
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
    store::Book store_;
    chart::Chart chart_;
    std::vector<ledger::Transaction> transactions_;
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
                   std::vector<ledger::Transaction>* transactions) {
        return read_book_with_rules(name, accounts, transactions, nullptr);
    }

    bool read_book_with_rules(const std::string& name, chart::Chart* accounts,
                              std::vector<ledger::Transaction>* transactions,
                              std::vector<payees::Rule>* rules) {
        store::Book opened;
        store::Failure no = store::Book::open_named(name, &opened);
        if (no.refused) { ADD_FAILURE() << no.reason; return false; }
        chart::Chart mine;
        ledger::Ledger unused;
        std::vector<ledger::Transaction> theirs;
        std::vector<payees::Rule> their_rules;
        no = opened.read(&mine, &unused, &theirs, &their_rules);
        if (no.refused) { ADD_FAILURE() << no.reason; return false; }
        if (accounts != nullptr) *accounts = mine;
        if (transactions != nullptr) *transactions = theirs;
        if (rules != nullptr) *rules = their_rules;
        return true;
    }

    void write_back(const std::string& name, const chart::Chart& accounts,
                    const std::vector<ledger::Transaction>& transactions) {
        store::Book opened;
        const store::Failure where = store::Book::open_named(name, &opened);
        ASSERT_FALSE(where.refused) << where.reason;
        const store::Failure no = opened.write(accounts, transactions);
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
            a.payment_payee = blank(value.paymentpayee);
            accounts.put(a);
        }
        write_back(name, accounts, transactions);
    }

    void write_postings(const std::string& name, const std::vector<PostingRowString>& values) {
        make_if_needed(name);
        current_book_ = name;
        chart::Chart accounts;
        std::vector<ledger::Transaction> transactions;
        ASSERT_TRUE(read_book(name, &accounts, &transactions));
        for (const auto& value : values) {
            ledger::Transaction* into = nullptr;
            for (ledger::Transaction& t : transactions)
                if (t.ref.value() == value.ref) into = &t;
            if (into == nullptr) {
                ledger::Transaction made;
                // Named here as any creator must name one: the store refuses a
                // transaction with no id rather than inventing one.
                made.id = ledger::new_id();
                made.ref = types::TransactionRef(value.ref);
                const auto on = types::Date::from_iso(value.date);
                ASSERT_TRUE(on.has_value()) << value.date;
                made.date = *on;
                made.payee = types::PayeeName(blank(value.payee));
                transactions.push_back(made);
                into = &transactions.back();
            }
            ledger::Posting posting;
            posting.account = types::AccountPath(value.account);
            posting.amount = Money(value.amount);
            posting.memo = blank(value.memo);
            posting.cleared = types::cleared_status_from_string(value.cleared);
            into->postings.push_back(posting);
            // An account a posting names has to be in the chart for the book to
            // read back, and these scenarios are about the books being separate
            // rather than about the chart.
            if (accounts.find(value.account) == nullptr) {
                chart::Account a;
                a.path = types::AccountPath(value.account);
                a.type = account_type_guessed_from_path(value.account);
                accounts.put(a);
            }
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
        rules.clear();
        for (const auto& value : values) {
            payees::Rule r;
            r.pattern = value.pattern;
            r.match_type = payees::match_type_from_string(value.matchtype);
            r.payee = value.payee;
            r.category = blank(value.category);
            r.enabled = parse_bool_cell(value.enabled);
            rules.push_back(r);
        }
        store::Book opened;
        const store::Failure where = store::Book::open_named(name, &opened);
        ASSERT_FALSE(where.refused) << where.reason;
        const store::Failure no = opened.write(accounts, transactions, rules);
        ASSERT_FALSE(no.refused) << no.reason;
    }

    void check_postings_of(const std::string& name, const std::vector<PostingRowString>& values) {
        chart::Chart accounts;
        std::vector<ledger::Transaction> transactions;
        ASSERT_TRUE(read_book(name, &accounts, &transactions));
        std::vector<PostingRowString> got;
        for (const ledger::Transaction& t : transactions) {
            for (const ledger::Posting& posting : t.postings) {
                PostingRowString row;
                row.ref = t.ref.value();
                row.date = t.date.iso();
                row.payee = t.payee.value().empty() ? "none" : t.payee.value();
                row.account = posting.account.value();
                row.amount = posting.amount.in_register();
                row.memo = posting.memo.empty() ? "none" : posting.memo;
                row.cleared = types::to_string(posting.cleared);
                got.push_back(row);
            }
        }
        ASSERT_EQ(values.size(), got.size()) << name << " postings that came back";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].ref, got[i].ref) << name << " posting " << i << " ref";
            EXPECT_EQ(values[i].date, got[i].date) << name << " posting " << i << " date";
            EXPECT_EQ(values[i].account, got[i].account) << name << " posting " << i << " account";
            EXPECT_EQ(Money(values[i].amount).cents(), Money(got[i].amount).cents())
                << name << " posting " << i << " amount";
        }
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
        if (type == "Money")          return {"postings", "amount_cents"};
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
            t.source_account = path_or_empty(value.sourceaccount);
            t.destination = path_or_empty(value.destination);
            t.amount = Money(value.amount);
            if (value.cleared != DNCString && !value.cleared.empty())
                t.cleared = types::cleared_status_from_string(value.cleared);
            transactions_.push_back(t);
        }
    }

    void then_transaction_is(const std::vector<TransactionString>& values) {
        for (const auto& value : values) {
            const ledger::Transaction* t = nullptr;
            for (const ledger::Transaction& one : transactions_)
                if (one.ref.value() == value.ref) t = &one;
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            if (value.payee != DNCString)
                EXPECT_EQ(blank(value.payee), t->payee.value()) << value.ref << " payee";
            if (value.sourceaccount != DNCString)
                EXPECT_EQ(blank(value.sourceaccount), t->source_account.value())
                    << value.ref << " source account";
            if (value.destination != DNCString)
                EXPECT_EQ(blank(value.destination), t->destination.value())
                    << value.ref << " destination";
            if (value.amount != DNCString)
                EXPECT_EQ(Money(value.amount).cents(), t->amount.cents())
                    << value.ref << " amount";
            if (value.cleared != DNCString)
                EXPECT_EQ(value.cleared, types::to_string(t->cleared)) << value.ref << " cleared";
        }
    }

    static types::AccountPath path_or_empty(const std::string& text) {
        const std::string b = blank(text);
        return b.empty() ? types::AccountPath() : types::AccountPath(b);
    }

};
