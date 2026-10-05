#pragma once
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>
#include "common/common.h"
#include "csv_headers.h"
#include "text_types.h"
#include "money.h"
#include "date_parse.h"
#include "date.h"

class ImportCsvGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

    void given_the_file_contains(const std::string& value) {
        std::cout << value << "\n";
        ADD_FAILURE() << "Not implemented: given_the_file_contains";
    }

    void when_headers_matched() {
        ADD_FAILURE() << "Not implemented: when_headers_matched";
    }

    void then_header_matches_are(const std::vector<HeaderMatchString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_header_matches_are";
    }

    void then_header_matching_is_complete(const std::vector<HeaderMatchResultString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_header_matching_is_complete";
    }

    void then_amount_style_follows_from_the_matches(const std::vector<AmountStyleChoiceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_amount_style_follows_from_the_matches";
    }

    void then_user_asked_about_headers_are(const std::vector<UnmatchedHeaderString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_user_asked_about_headers_are";
    }

    void then_rejected_because(const std::vector<RejectionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_rejected_because";
    }

    void then_ambiguous_fields_are(const std::vector<AmbiguousFieldString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_ambiguous_fields_are";
    }

    void given_saved_profiles_are(const std::vector<SavedProfileString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_saved_profiles_are";
    }

    void then_profile_recognised_is(const std::vector<ProfileRecognitionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_profile_recognised_is";
    }

    void given_saved_header_matches_are(const std::vector<HeaderMatchString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_saved_header_matches_are";
    }

    void then_rows_read_are(const std::vector<CsvRowReadString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_rows_read_are";
    }

    void when_header_mapped_by_user(const std::vector<HeaderMatchString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_header_mapped_by_user";
    }

    void when_profile_saved_as(const std::vector<ProfileLabelString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_profile_saved_as";
    }

    void then_saved_profiles_are(const std::vector<SavedProfileString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_saved_profiles_are";
    }

    void given_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is";
    }

    void given_postings_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_postings_are";
    }

    void given_import_target_is(const std::vector<ImportTargetString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_import_target_is";
    }

    void when_rows_imported() {
        ADD_FAILURE() << "Not implemented: when_rows_imported";
    }

    void then_balance_column_check_is(const std::vector<BalanceColumnCheckString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_balance_column_check_is";
    }

    void given_the_csv_profile_is(const std::vector<CsvProfileString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_csv_profile_is";
    }

    void then_rows_rejected_are(const std::vector<CsvRowErrorString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_rows_rejected_are";
    }

    void then_date_order_is_ambiguous(const std::vector<DateOrderWarningString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_date_order_is_ambiguous";
    }

    void given_the_csv_profile_is_as_previous() {
        ADD_FAILURE() << "Not implemented: given_the_csv_profile_is_as_previous";
    }

    void given_the_file_begins_with_a_byte_order_mark_and_contains(const std::string& value) {
        std::cout << value << "\n";
        ADD_FAILURE() << "Not implemented: given_the_file_begins_with_a_byte_order_mark_and_contains";
    }

    void then_headings_read_are(const std::vector<HeadingReadString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_headings_read_are";
    }

    void then_postings_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_postings_are";
    }

    void then_accounts_created_are(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_accounts_created_are";
    }

    void then_import_summary_is(const std::vector<ImportSummaryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_import_summary_is";
    }

    void examples_datatype_csvdelimiter(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_csvdelimiter";
    }

    void examples_datatype_dateorder(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_dateorder";
    }

    void examples_datatype_amountstyle(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_amountstyle";
    }

    void examples_datatype_outwardsign(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_outwardsign";
    }

    void examples_datatype_canonicalfield(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_canonicalfield";
    }

    void examples_businessrule_which_fields_a_file_must_supply(const std::vector<FieldRequirementString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_which_fields_a_file_must_supply";
    }

    void examples_datatype_matchedby(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_matchedby";
    }

    void examples_businessrule_how_a_header_is_normalised_before_it_is_matched(const std::vector<HeaderNormalisationString>& values) {
        for (const auto& value : values) {
            const HeaderNormalisationTyped t =
                HeaderNormalisationTyped::from_string_struct(value);
            EXPECT_EQ(t.normalised, csv::normalise_heading(t.sourceheader))
                << "[" << t.sourceheader << "] -- " << t.notes;
        }
    }

    void examples_businessrule_known_spellings_of_each_field(const std::vector<HeaderAliasString>& values) {
        for (const auto& value : values) {
            const HeaderAliasTyped t = HeaderAliasTyped::from_string_struct(value);
            const auto field = csv::field_for_heading(t.normalised);
            ASSERT_TRUE(field.has_value()) << "no alias for [" << t.normalised << "]";
            EXPECT_EQ(t.field, csv::to_string(*field)) << t.normalised;
            // The table is already in normalised form, so normalising it again
            // must change nothing.
            EXPECT_EQ(t.normalised, csv::normalise_heading(t.normalised));
        }
    }

    void examples_businessrule_the_amount_style_follows_from_which_amount_fields_matched(const std::vector<AmountStyleFromFieldsString>& values) {
        for (const auto& value : values) {
            const AmountStyleFromFieldsTyped t =
                AmountStyleFromFieldsTyped::from_string_struct(value);
            const csv::StyleChoice got =
                csv::style_for(t.amountmatched, t.debitmatched, t.creditmatched);
            EXPECT_EQ(t.valid, got.valid) << t.notes;
            // The style only means anything where the headings gave a usable one.
            if (t.valid) EXPECT_EQ(t.amountstyle, csv::to_string(got.style)) << t.notes;
        }
    }

    void examples_datatype_profilename(const std::vector<ValidValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_profilename";
    }

    void examples_businessrule_how_a_row_becomes_an_amount(const std::vector<CsvAmountString>& values) {
        for (const auto& value : values) {
            const CsvAmountTyped t = CsvAmountTyped::from_string_struct(value);
            const Money got = csv::amount_of_row(
                csv::amount_style_from_string(t.amountstyle),
                csv::outward_sign_from_string(t.outwardsign),
                value.amountcell, value.debitcell, value.creditcell);
            EXPECT_EQ(Money(t.amount).cents(), got.cents())
                << t.amountstyle << "/" << t.outwardsign << " -- " << t.notes;
        }
    }

    void examples_businessrule_how_a_date_cell_is_read(const std::vector<CsvDateParseString>& values) {
        for (const auto& value : values) {
            const CsvDateParseTyped t = CsvDateParseTyped::from_string_struct(value);
            const auto got = dates::parse_csv(t.text,
                                              types::date_order_from_string(t.dateorder));
            EXPECT_EQ(t.valid, got.has_value())
                << "[" << t.text << "] read as " << t.dateorder
                << (t.notes.empty() ? "" : " -- " + t.notes);
            if (t.valid && got) EXPECT_EQ(t.date, got->iso()) << "[" << t.text << "]";
        }
    }

    void examples_businessrule_how_a_two_digit_year_is_read(const std::vector<TwoDigitYearString>& values) {
        for (const auto& value : values) {
            const TwoDigitYearTyped t = TwoDigitYearTyped::from_string_struct(value);
            EXPECT_EQ(t.year, types::year_from_two_digits(std::stoi(t.text)))
                << "two-digit year " << t.text;
        }
    }

    void examples_datatype_heading(const std::vector<ValidValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_heading";
    }

    void examples_businessrule_how_a_csv_row_is_tested_against_what_is_already_there(const std::vector<CsvDispositionRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_how_a_csv_row_is_tested_against_what_is_already_there";
    }

    void examples_businessrule_what_a_fingerprint_ignores(const std::vector<FingerprintMatchString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_what_a_fingerprint_ignores";
    }

public:

    void examples_businessrule_a_row_claims_at_most_one_transaction_already_there(const std::vector<ClaimPairingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_a_row_claims_at_most_one_transaction_already_there";
    }

};
