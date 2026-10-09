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
#include "posting.h"
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
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_payee_rules_of_business_are";
    }

    void given_payee_rules_of_personal_are(const std::vector<PayeeRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_payee_rules_of_personal_are";
    }

    void then_payee_rules_in_force_in_personal_are(const std::vector<PayeeRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_payee_rules_in_force_in_personal_are";
    }

    void given_saved_profiles_are(const std::vector<SavedProfileString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_saved_profiles_are";
    }

    void given_the_current_book_is(const std::vector<CurrentBookString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_current_book_is";
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

    void when_draw_recorded(const std::vector<OwnerDrawString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_draw_recorded";
    }

    void then_postings_of_business_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_postings_of_business_are";
    }

    void then_postings_of_personal_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_postings_of_personal_are";
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

    void then_net_worth_is(const std::vector<NetWorthString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_net_worth_is";
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
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_report_heading_is";
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

    void examples_businessrule_everything_a_book_needs_is_in_the_book(const std::vector<BookScopeString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_everything_a_book_needs_is_in_the_book";
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
        store::Book opened;
        store::Failure no = store::Book::open_named(name, &opened);
        if (no.refused) { ADD_FAILURE() << no.reason; return false; }
        chart::Chart mine;
        ledger::Ledger unused;
        std::vector<ledger::Transaction> theirs;
        no = opened.read(&mine, &unused, &theirs);
        if (no.refused) { ADD_FAILURE() << no.reason; return false; }
        if (accounts != nullptr) *accounts = mine;
        if (transactions != nullptr) *transactions = theirs;
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
                a.type = value.account.rfind("Assets", 0) == 0 ? types::AccountType::Checking
                                                               : types::AccountType::Expense;
                accounts.put(a);
            }
        }
        write_back(name, accounts, transactions);
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

};
