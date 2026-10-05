#pragma once
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>
#include "common/common.h"

class BooksGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

    void given_books_are(const std::vector<BookString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_books_are";
    }

    void given_books_are_as_previous() {
        ADD_FAILURE() << "Not implemented: given_books_are_as_previous";
    }

    void given_the_chart_of_accounts_of_business_is(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_of_business_is";
    }

    void given_the_chart_of_accounts_of_personal_is(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_of_personal_is";
    }

    void when_category_picker_opened_in_personal() {
        ADD_FAILURE() << "Not implemented: when_category_picker_opened_in_personal";
    }

    void then_category_picker_offers(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_category_picker_offers";
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

    void given_postings_of_business_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_postings_of_business_are";
    }

    void given_postings_of_personal_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_postings_of_personal_are";
    }

    void when_each_book_checked() {
        ADD_FAILURE() << "Not implemented: when_each_book_checked";
    }

    void then_book_checks_are(const std::vector<PerBookCheckString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_book_checks_are";
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
        ADD_FAILURE() << "Not implemented: given_no_books_exist";
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
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_the_book_names_are";
    }

    void when_a_new_book_is_made_named(const std::vector<BookNameString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_a_new_book_is_made_named";
    }

    void then_the_book_opened_is(const std::vector<OpenedBookString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_the_book_opened_is";
    }

    void given_a_new_book_named_business() {
        ADD_FAILURE() << "Not implemented: given_a_new_book_named_business";
    }

    void given_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is";
    }

    void given_postings_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_postings_are";
    }

    void when_the_book_is_saved_and_opened_again() {
        ADD_FAILURE() << "Not implemented: when_the_book_is_saved_and_opened_again";
    }

    void then_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_the_chart_of_accounts_is";
    }

    void then_postings_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_postings_are";
    }

    void when_a_book_is_opened_named(const std::vector<BookNameString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_a_book_is_opened_named";
    }

    void then_the_book_is_refused_saying(const std::string& value) {
        std::cout << value << "\n";
        ADD_FAILURE() << "Not implemented: then_the_book_is_refused_saying";
    }

    void given_a_book_whose_schema_version_is_99() {
        ADD_FAILURE() << "Not implemented: given_a_book_whose_schema_version_is_99";
    }

    void given_the_books_folder_holds(const std::vector<BookFileListString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_books_folder_holds";
    }

    void when_the_books_are_listed() {
        ADD_FAILURE() << "Not implemented: when_the_books_are_listed";
    }

    void examples_businessrule_everything_a_book_needs_is_in_the_book(const std::vector<BookScopeString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_everything_a_book_needs_is_in_the_book";
    }

    void examples_businessrule_a_book_is_one_sqlite_database_in_one_known_folder(const std::vector<BookFileString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_a_book_is_one_sqlite_database_in_one_known_folder";
    }

    void examples_businessrule_one_table_per_collection_one_row_per_entity(const std::vector<EntityStorageString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_one_table_per_collection_one_row_per_entity";
    }

    void examples_businessrule_what_a_book_s_file_holds_besides_its_entities(const std::vector<BookInfrastructureString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_what_a_book_s_file_holds_besides_its_entities";
    }

    void examples_businessrule_how_an_attribute_becomes_a_column(const std::vector<ColumnTypeString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_how_an_attribute_becomes_a_column";
    }

    void examples_businessrule_the_qif_profiles_live_in_one_file_beside_the_books_under_a_name(const std::vector<BesideTheBooksString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_the_qif_profiles_live_in_one_file_beside_the_books_under_a_name";
    }

};
