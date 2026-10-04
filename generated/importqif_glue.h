#pragma once
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>
#include "common/common.h"
#include "qif_lexer.h"
#include "posting.h"
#include "text_types.h"
#include "money.h"
#include "date_parse.h"
#include "date.h"

class ImportQifGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

    void given_import_target_is(const std::vector<ImportTargetString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_import_target_is";
    }

    void given_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is";
    }

    void given_the_file_contains(const std::string& value) {
        std::cout << value << "\n";
        ADD_FAILURE() << "Not implemented: given_the_file_contains";
    }

    void when_qif_file_imported() {
        ADD_FAILURE() << "Not implemented: when_qif_file_imported";
    }

    void then_qif_records_read_are(const std::vector<QifRecordString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_qif_records_read_are";
    }

    void then_accounts_created_are(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_accounts_created_are";
    }

    void then_account_limits_are(const std::vector<AccountLimitString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_account_limits_are";
    }

    void then_migration_notes_are(const std::vector<MigrationNoteString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_migration_notes_are";
    }

    void then_statement_balance_to_check_is(const std::vector<QifStatementBalanceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_statement_balance_to_check_is";
    }

    void given_the_qif_profile_is(const std::vector<QifProfileString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_qif_profile_is";
    }

    void then_date_order_is_ambiguous(const std::vector<DateOrderWarningString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_date_order_is_ambiguous";
    }

    void then_date_order_evidence_is(const std::vector<DateOrderEvidenceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_date_order_evidence_is";
    }

    void then_rejected_because(const std::vector<RejectionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_rejected_because";
    }

    void then_postings_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_postings_are";
    }

    void then_import_summary_is(const std::vector<ImportSummaryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_import_summary_is";
    }

    void given_the_chart_of_accounts_is_as_previous() {
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is_as_previous";
    }

    void then_transaction_is(const std::vector<TransactionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_transaction_is";
    }

    void then_holdings_are(const std::vector<HoldingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_holdings_are";
    }

    void given_holdings_are(const std::vector<HoldingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_holdings_are";
    }

    void then_postings_created_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_postings_created_are";
    }

    void then_payee_rules_created_are(const std::vector<PayeeRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_payee_rules_created_are";
    }

    void then_payee_split_rules_created_are(const std::vector<PayeeSplitRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_payee_split_rules_created_are";
    }

    void then_remembered_split_lines_created_are(const std::vector<RememberedSplitLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_remembered_split_lines_created_are";
    }

    void then_migration_report_is(const std::vector<MigrationReportString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_migration_report_is";
    }

    void then_balance_checks_are(const std::vector<LedgerBalanceResultString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_balance_checks_are";
    }

    void when_book_checked() {
        ADD_FAILURE() << "Not implemented: when_book_checked";
    }

    void then_book_check_is(const std::vector<BookCheckString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_book_check_is";
    }

    void when_report_run(const std::vector<ReportSpecString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_report_run";
    }

    void then_report_rows_are(const std::vector<ReportRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_report_rows_are";
    }

    void given_the_qif_profile_is_as_previous() {
        ADD_FAILURE() << "Not implemented: given_the_qif_profile_is_as_previous";
    }

    void then_accounts_with_incomplete_balances_are(const std::vector<IncompleteBalanceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_accounts_with_incomplete_balances_are";
    }

    void given_accounts_with_incomplete_balances_are(const std::vector<IncompleteBalanceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_accounts_with_incomplete_balances_are";
    }

    void then_balance_display_is(const std::vector<BalanceDisplayString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_balance_display_is";
    }

    void given_import_target_is_as_previous() {
        ADD_FAILURE() << "Not implemented: given_import_target_is_as_previous";
    }

    void given_the_second_file_contains(const std::string& value) {
        std::cout << value << "\n";
        ADD_FAILURE() << "Not implemented: given_the_second_file_contains";
    }

    void then_unpaid_invoices_are(const std::vector<UnpaidInvoiceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_unpaid_invoices_are";
    }

    void when_invoice_reconciliation_summarised() {
        ADD_FAILURE() << "Not implemented: when_invoice_reconciliation_summarised";
    }

    void then_invoice_reconciliation_is(const std::vector<InvoiceReconciliationString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_invoice_reconciliation_is";
    }

    void when_invoice_years_summarised() {
        ADD_FAILURE() << "Not implemented: when_invoice_years_summarised";
    }

    void then_invoice_years_are(const std::vector<InvoiceYearSpanString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_invoice_years_are";
    }

    void given_today_is(const std::vector<TodayString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_today_is";
    }

    void then_holdings_report_rows_are(const std::vector<HoldingsReportRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_holdings_report_rows_are";
    }

    void then_balance_check_is(const std::vector<LedgerBalanceResultString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_balance_check_is";
    }

    void then_transfer_pairing_report_is(const std::vector<TransferPairingReportString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_transfer_pairing_report_is";
    }

    void examples_datatype_qifsection(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_qifsection";
    }

    void examples_businessrule_how_a_header_line_is_read(const std::vector<SectionHeaderString>& values) {
        for (const auto& value : values) {
            const SectionHeaderTyped t = SectionHeaderTyped::from_string_struct(value);
            EXPECT_EQ(t.section, qif::to_string(qif::section_of(t.line)))
                << "[" << t.line << "] -- " << t.notes;
        }
    }

    void examples_businessrule_what_an_account_block_means_depends_on_autoswitch(const std::vector<AccountBlockMeaningString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_what_an_account_block_means_depends_on_autoswitch";
    }

    void examples_businessrule_how_a_qif_account_type_becomes_an_account_type_and_a_path(const std::vector<QifAccountMappingString>& values) {
        for (const auto& value : values) {
            const QifAccountMappingTyped t =
                QifAccountMappingTyped::from_string_struct(value);
            const auto mapping = qif::account_mapping_of(t.qiftype);
            ASSERT_TRUE(mapping.has_value()) << "unmapped QIF type: " << t.qiftype;
            EXPECT_EQ(t.accounttype, types::to_string(mapping->type)) << t.qiftype;
            EXPECT_EQ(t.root, mapping->root) << t.qiftype;
            EXPECT_EQ(t.examplepath, qif::account_path_of(t.qiftype, t.examplename))
                << t.qiftype;
        }
    }

    void examples_businessrule_how_a_category_name_becomes_an_account_path(const std::vector<CategoryMappingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_how_a_category_name_becomes_an_account_path";
    }

    void examples_businessrule_how_a_qif_date_is_read(const std::vector<QifDateParseString>& values) {
        for (const auto& value : values) {
            const QifDateParseTyped t = QifDateParseTyped::from_string_struct(value);
            const auto got = dates::parse_qif(t.text,
                                              types::date_order_from_string(t.dateorder));
            EXPECT_EQ(t.valid, got.has_value())
                << "[" << t.text << "] read as " << t.dateorder
                << (t.notes.empty() ? "" : " -- " + t.notes);
            // The Date column of a row that is not valid holds the attribute's
            // default, so there is nothing to compare it with.
            if (t.valid && got) EXPECT_EQ(t.date, got->iso()) << "[" << t.text << "]";
        }
    }

    void examples_businessrule_how_a_qif_amount_is_read(const std::vector<QifAmountParseString>& values) {
        for (const auto& value : values) {
            const QifAmountParseTyped t = QifAmountParseTyped::from_string_struct(value);
            const auto got = qif::parse_amount(t.text, t.decimalcomma);
            EXPECT_EQ(t.valid, got.has_value()) << "[" << t.text << "] -- " << t.notes;
            if (t.valid && got)
                EXPECT_EQ(Money(t.amount).cents(), got->cents()) << "[" << t.text << "]";
        }
    }

    void examples_businessrule_how_the_c_field_becomes_a_cleared_status(const std::vector<ClearedMappingString>& values) {
        for (const auto& value : values) {
            const ClearedMappingTyped t = ClearedMappingTyped::from_string_struct(value);
            EXPECT_EQ(t.cleared, types::to_string(qif::cleared_of(t.text)))
                << "[" << t.text << "] -- " << t.notes;
        }
    }

    void examples_businessrule_the_split_lines_decide_the_categories_not_the_l_field(const std::vector<SplitCategorySourceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_the_split_lines_decide_the_categories_not_the_l_field";
    }

    void examples_businessrule_how_a_split_line_amount_becomes_a_posting_amount(const std::vector<SplitLineSignString>& values) {
        for (const auto& value : values) {
            const SplitLineSignTyped t = SplitLineSignTyped::from_string_struct(value);
            // The posting to the account itself is T, unchanged.
            EXPECT_EQ(Money(t.t).cents(), Money(t.accountposting).cents()) << t.notes;
            const std::string dollars[3] = {value.dollar1, value.dollar2, value.dollar3};
            const std::string postings[3] = {value.posting1, value.posting2, value.posting3};
            for (int i = 0; i < 3; ++i) {
                if (dollars[i].empty()) continue;
                EXPECT_EQ(Money(postings[i]).cents(),
                          qif::posting_amount_of_split_line(Money(dollars[i])).cents())
                    << "line " << (i + 1) << " -- " << t.notes;
            }
        }
    }

    void examples_businessrule_when_two_transfer_halves_are_the_same_transfer(const std::vector<TransferPairingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_when_two_transfer_halves_are_the_same_transfer";
    }

    void examples_datatype_investmentmode(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_investmentmode";
    }

    void examples_datatype_qifaction(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_qifaction";
    }

    void examples_businessrule_what_each_investment_action_posts(const std::vector<ActionPostingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_what_each_investment_action_posts";
    }

    void examples_businessrule_how_a_stock_split_ratio_is_read(const std::vector<SplitRatioString>& values) {
        for (const auto& value : values) {
            const SplitRatioTyped t = SplitRatioTyped::from_string_struct(value);
            const double ratio = qif::split_ratio_of(t.qfield);
            EXPECT_DOUBLE_EQ(t.ratio, ratio) << "Q of " << t.qfield << " -- " << t.notes;
            // Shares and price change; the value does not, which is why a split
            // posts nothing.
            const double held_before = std::stod(t.heldbefore);
            const double price_before = std::stod(t.pricebefore);
            EXPECT_DOUBLE_EQ(std::stod(t.heldafter),
                             qif::shares_after_split(held_before, ratio)) << t.notes;
            EXPECT_DOUBLE_EQ(std::stod(t.priceafter),
                             qif::price_after_split(price_before, ratio)) << t.notes;
            EXPECT_DOUBLE_EQ(held_before * price_before,
                             qif::shares_after_split(held_before, ratio) *
                             qif::price_after_split(price_before, ratio))
                << "a split changes no value -- " << t.notes;
        }
    }

    void examples_businessrule_how_a_price_list_line_is_read(const std::vector<PriceLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_how_a_price_list_line_is_read";
    }

    void examples_businessrule_in_what_order_a_qif_file_is_read(const std::vector<MigrationPassString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_in_what_order_a_qif_file_is_read";
    }

    void examples_businessrule_which_sections_the_first_round_imports(const std::vector<RoundOneSectionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_which_sections_the_first_round_imports";
    }

    void examples_businessrule_which_investment_records_the_first_round_imports(const std::vector<RoundOneInvestmentActionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_which_investment_records_the_first_round_imports";
    }

    void examples_businessrule_how_the_invoice_section_is_shaped(const std::vector<InvoiceRecordKindString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_how_the_invoice_section_is_shaped";
    }

    void examples_businessrule_the_export_does_not_say_which_payment_paid_which_invoice(const std::vector<InvoiceLinkEvidenceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_the_export_does_not_say_which_payment_paid_which_invoice";
    }

    void examples_businessrule_how_a_payment_is_matched_to_the_invoice_it_paid(const std::vector<PaymentMatchPassString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_how_a_payment_is_matched_to_the_invoice_it_paid";
    }

    void examples_businessrule_what_encoding_a_qif_file_is_read_as(const std::vector<EncodingChoiceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_what_encoding_a_qif_file_is_read_as";
    }

    void examples_businessrule_a_field_whose_value_will_not_parse_is_a_continuation_of_the_one_before(const std::vector<ContinuationLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_a_field_whose_value_will_not_parse_is_a_continuation_of_the_one_before";
    }

    void examples_businessrule_what_a_field_code_means_depends_on_its_section(const std::vector<FieldCodeBySectionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_what_a_field_code_means_depends_on_its_section";
    }

    void examples_businessrule_what_the_two_files_settled_and_what_is_still_assumed(const std::vector<DialectRiskString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_what_the_two_files_settled_and_what_is_still_assumed";
    }

};
