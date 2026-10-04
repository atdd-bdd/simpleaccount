#pragma once
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>
#include "common/common.h"

class ImportOfxGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

    void given_the_file_contains(const std::string& value) {
        std::cout << value << "\n";
        ADD_FAILURE() << "Not implemented: given_the_file_contains";
    }

    void then_statement_is(const std::vector<OfxStatementString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_statement_is";
    }

    void then_transactions_read_are(const std::vector<OfxTransactionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_transactions_read_are";
    }

    void then_rejected_because(const std::vector<RejectionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_rejected_because";
    }

    void given_account_mappings_are(const std::vector<AccountMappingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_account_mappings_are";
    }

    void when_statement_imported(const std::vector<OfxStatementString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_statement_imported";
    }

    void then_asked_to_choose_an_account_for(const std::vector<UnmappedAccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_asked_to_choose_an_account_for";
    }

    void then_import_target_is(const std::vector<ImportTargetString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_import_target_is";
    }

    void given_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is";
    }

    void given_import_target_is(const std::vector<ImportTargetString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_import_target_is";
    }

    void when_transactions_imported(const std::vector<OfxTransactionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_transactions_imported";
    }

    void then_import_summary_is(const std::vector<ImportSummaryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_import_summary_is";
    }

    void then_balances_are(const std::vector<AccountBalanceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_balances_are";
    }

    void given_postings_are(const std::vector<ImportedPostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_postings_are";
    }

    void then_postings_are(const std::vector<ImportedPostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_postings_are";
    }

    void given_the_chart_of_accounts_is_as_previous() {
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is_as_previous";
    }

    void when_statements_imported(const std::vector<OfxImportFileString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_statements_imported";
    }

    void then_transfer_candidates_are(const std::vector<TransferCandidateString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_transfer_candidates_are";
    }

    void when_candidate_approved(const std::vector<TransferCandidateString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_candidate_approved";
    }

    void when_report_run(const std::vector<ReportSpecString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_report_run";
    }

    void then_report_rows_are(const std::vector<ReportRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_report_rows_are";
    }

    void given_account_mappings_are_as_previous() {
        ADD_FAILURE() << "Not implemented: given_account_mappings_are_as_previous";
    }

    void then_confirmation_dialog_shows(const std::vector<ConfirmationRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_confirmation_dialog_shows";
    }

    void then_import_preview_is(const std::vector<ImportPreviewString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_import_preview_is";
    }

    void when_candidate_rejected(const std::vector<TransferCandidateString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_candidate_rejected";
    }

    void then_rule_offered_is(const std::vector<TransferMatchRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_rule_offered_is";
    }

    void given_transfer_match_rules_are(const std::vector<TransferMatchRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_transfer_match_rules_are";
    }

    void given_transfer_match_rules_are_as_previous() {
        ADD_FAILURE() << "Not implemented: given_transfer_match_rules_are_as_previous";
    }

    void when_ledger_balance_checked(const std::vector<LedgerBalanceCheckString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_ledger_balance_checked";
    }

    void then_balance_check_is(const std::vector<LedgerBalanceResultString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_balance_check_is";
    }

    void examples_datatype_ofxaccttype(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_ofxaccttype";
    }

    void examples_datatype_ofxtrntype(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_ofxtrntype";
    }

    void examples_businessrule_an_ofx_date_is_read_to_the_day(const std::vector<OfxDateParseString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_an_ofx_date_is_read_to_the_day";
    }

    void examples_businessrule_the_amount_in_an_ofx_file_is_used_as_the_posting_amount(const std::vector<OfxAmountToPostingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_the_amount_in_an_ofx_file_is_used_as_the_posting_amount";
    }

    void examples_datatype_importdisposition(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_importdisposition";
    }

    void examples_businessrule_how_a_downloaded_transaction_is_identified(const std::vector<DispositionRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_how_a_downloaded_transaction_is_identified";
    }

    void examples_datatype_transfercandidatestate(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_transfercandidatestate";
    }

    void examples_businessrule_when_two_new_transactions_are_proposed_as_one_transfer(const std::vector<TransferCandidateRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_when_two_new_transactions_are_proposed_as_one_transfer";
    }

    void examples_businessrule_the_two_date_windows_and_why_they_differ(const std::vector<DateWindowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_the_two_date_windows_and_why_they_differ";
    }

    void examples_businessrule_which_half_pairs_with_which_when_several_match(const std::vector<CandidatePriorityString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_which_half_pairs_with_which_when_several_match";
    }

};
