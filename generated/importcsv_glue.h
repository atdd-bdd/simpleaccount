#pragma once
#include <gtest/gtest.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "common/common.h"
#include "account_type.h"
#include "chart.h"
#include "csv_headers.h"
#include "csv_import.h"
#include "csv_reader.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "posting.h"
#include "text_types.h"
#include "transaction_id.h"

// Glue for ImportCsv.spectable.
//
// Every step drives src/csv_reader.h, src/csv_headers.h or src/csv_import.h.
// Nothing is decided here: this file turns a table into the arguments those
// functions take and turns what they return back into a table.
//
// Two conventions of the generated tables, stated once. An attribute whose
// Default is "none" arrives as the literal word, so a comparison reads that as
// nothing. And a value a CompareOnly table does not name arrives as "?DNC?",
// which the string structs already treat as equal to anything -- so a whole-row
// comparison is the right way to check a subset, and an amount is compared as
// Money so that 1705.00 and $1,705.00 are the same amount.
class ImportCsvGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

    // ---------------------------------------------------------------- givens

    void given_the_file_contains(const std::string& value) {
        text_ = value;
        reread();
    }

    // The mark three bytes long that a spreadsheet writes before the first
    // heading. Given separately because a table cannot hold the bytes.
    void given_the_file_begins_with_a_byte_order_mark_and_contains(const std::string& value) {
        text_ = std::string("\xEF\xBB\xBF") + value;
        reread();
    }

    void given_the_csv_profile_is(const std::vector<CsvProfileString>& values) {
        ASSERT_FALSE(values.empty());
        profile_ = profile_from(values.front());
        have_profile_ = true;
        reread();
    }

    // No scenario in this file reaches this: there is no Background here, so
    // "as previous" would establish nothing and the tables are spelled out.
    void given_the_csv_profile_is_as_previous() {}

    void given_saved_profiles_are(const std::vector<SavedProfileString>& values) {
        saved_.clear();
        for (const auto& value : values) {
            csv::Profile one;
            one.name = value.name;
            // A signature in the table is the headings in file order. Which
            // column means what is not the point of these scenarios, so each
            // heading is matched by the alias table to build the profile.
            for (const std::string& heading : split(value.signature)) {
                const auto field = csv::field_for_heading(heading);
                if (!field.has_value()) continue;
                switch (*field) {
                    case csv::Field::Date:     one.date_column = heading; break;
                    case csv::Field::Payee:    one.payee_column = heading; break;
                    case csv::Field::Memo:     one.memo_column = heading; break;
                    case csv::Field::CheckNo:  one.check_column = heading; break;
                    case csv::Field::Category: one.category_column = heading; break;
                    case csv::Field::Balance:  one.balance_column = heading; break;
                    case csv::Field::Amount:   one.amount_column = heading; break;
                    case csv::Field::Debit:    one.debit_column = heading; break;
                    case csv::Field::Credit:   one.credit_column = heading; break;
                    default: break;
                }
            }
            if (!one.debit_column.empty() && !one.credit_column.empty())
                one.amount_style = csv::AmountStyle::DebitCredit;
            saved_.push_back(one);
        }
    }

    void given_saved_header_matches_are(const std::vector<HeaderMatchString>& values) {
        for (const auto& value : values)
            from_profile_[csv::normalise_heading(value.sourceheader)] =
                csv::field_from_string(value.field);
        rematch();
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
                made.id = ledger::new_id();
                made.ref = types::TransactionRef(value.ref);
                const auto on = types::Date::from_iso(value.date);
                ASSERT_TRUE(on.has_value()) << value.date;
                made.date = *on;
                made.payee = types::PayeeName(blank(value.payee));
                transactions_.push_back(made);
                into = &transactions_.back();
            }
            ledger::Posting p;
            p.account = types::AccountPath(value.account);
            p.amount = Money(value.amount);
            p.memo = blank(value.memo);
            if (value.cleared != DNCString && !value.cleared.empty())
                p.cleared = types::cleared_status_from_string(value.cleared);
            into->postings.push_back(p);
        }
    }

    void given_import_target_is(const std::vector<ImportTargetString>& values) {
        ASSERT_FALSE(values.empty());
        target_ = values.front().accountpath;
    }

    // ----------------------------------------------------------------- whens

    void when_headers_matched() { rematch(); }

    void when_header_mapped_by_user(const std::vector<HeaderMatchString>& values) {
        for (const auto& value : values)
            from_profile_[csv::normalise_heading(value.sourceheader)] =
                csv::field_from_string(value.field);
        rematch();
        // A column the user mapped was matched by the user, whatever the alias
        // table would have said.
        for (const auto& value : values)
            for (csv::HeaderMatch& match : matching_.matches)
                if (match.source_header == value.sourceheader)
                    match.matched_by = csv::MatchedBy::User;
    }

    void when_profile_saved_as(const std::vector<ProfileLabelString>& values) {
        ASSERT_FALSE(values.empty());
        csv::Profile one = profile_;
        one.name = values.front().name;
        // The signature is what the file carried, through whatever the matching
        // settled on -- including anything the user mapped by hand.
        one.date_column = header_for(csv::Field::Date);
        one.payee_column = header_for(csv::Field::Payee);
        one.memo_column = header_for(csv::Field::Memo);
        one.check_column = header_for(csv::Field::CheckNo);
        one.category_column = header_for(csv::Field::Category);
        one.balance_column = header_for(csv::Field::Balance);
        one.amount_column = header_for(csv::Field::Amount);
        one.debit_column = header_for(csv::Field::Debit);
        one.credit_column = header_for(csv::Field::Credit);
        saved_.push_back(one);
    }

    void when_rows_imported() {
        rematch();
        rows_ = csv::rows_of(file_, matching_, order(), style(), outward());
        decided_ = csv::decide(rows_.rows, target_, transactions_);
        const std::vector<ledger::Transaction> made =
            csv::transactions_for(decided_, target_, transactions_.size(), &chart_);
        created_.clear();
        for (const ledger::Transaction& t : made) created_.push_back(t);
        transactions_.insert(transactions_.end(), made.begin(), made.end());
        checks_ = csv::balance_checks(rows_.rows, as_ledger(), target_);
    }

    // ----------------------------------------------------------------- thens

    void then_header_matches_are(const std::vector<HeaderMatchString>& values) {
        ASSERT_EQ(values.size(), matching_.matches.size()) << "columns matched";
        for (std::size_t i = 0; i < values.size(); ++i) {
            HeaderMatchString got;
            got.position = std::to_string(matching_.matches[i].position);
            got.sourceheader = matching_.matches[i].source_header;
            got.field = csv::to_string(matching_.matches[i].field);
            got.matchedby = csv::to_string(matching_.matches[i].matched_by);
            EXPECT_EQ(values[i], got) << "column " << i + 1 << ": wanted "
                                      << values[i].to_string() << " got " << got.to_string();
        }
    }

    void then_header_matching_is_complete(const std::vector<HeaderMatchResultString>& values) {
        ASSERT_FALSE(values.empty());
        HeaderMatchResultString got;
        got.complete = matching_.complete ? "true" : "false";
        got.missingfields = std::to_string(matching_.missing.size());
        got.unmatchedcount = std::to_string(matching_.unmatched.size());
        got.ambiguouscount = std::to_string(matching_.ambiguous.size());
        EXPECT_EQ(values.front(), got) << "wanted " << values.front().to_string()
                                      << " got " << got.to_string();
    }

    void then_amount_style_follows_from_the_matches(
            const std::vector<AmountStyleChoiceString>& values) {
        ASSERT_FALSE(values.empty());
        AmountStyleChoiceString got;
        got.amountstyle = csv::to_string(style_choice().style);
        EXPECT_EQ(values.front(), got) << "wanted " << values.front().to_string()
                                      << " got " << got.to_string();
    }

    void then_user_asked_about_headers_are(const std::vector<UnmatchedHeaderString>& values) {
        ASSERT_EQ(values.size(), matching_.unmatched.size()) << "columns asked about";
        for (std::size_t i = 0; i < values.size(); ++i) {
            UnmatchedHeaderString got;
            got.position = std::to_string(matching_.unmatched[i].position);
            got.sourceheader = matching_.unmatched[i].source_header;
            got.suggestion = csv::to_string(matching_.unmatched[i].field);
            EXPECT_EQ(values[i], got) << "asked " << i << ": wanted "
                                      << values[i].to_string() << " got " << got.to_string();
        }
    }

    void then_rejected_because(const std::vector<RejectionString>& values) {
        ASSERT_FALSE(values.empty());
        const bool refused = matching_.refused || file_.refused;
        EXPECT_TRUE(refused) << "it was not refused";
        EXPECT_EQ(values.front().reason,
                  matching_.refused ? matching_.refusal : file_.refusal);
    }

    void then_ambiguous_fields_are(const std::vector<AmbiguousFieldString>& values) {
        ASSERT_EQ(values.size(), matching_.ambiguous.size()) << "fields claimed twice";
        for (std::size_t i = 0; i < values.size(); ++i) {
            AmbiguousFieldString got;
            got.field = csv::to_string(matching_.ambiguous[i].field);
            got.positions = csv::detail::list_of(matching_.ambiguous[i].positions);
            got.headers = csv::detail::joined(matching_.ambiguous[i].headers);
            EXPECT_EQ(values[i], got) << "wanted " << values[i].to_string()
                                      << " got " << got.to_string();
        }
    }

    void then_profile_recognised_is(const std::vector<ProfileRecognitionString>& values) {
        ASSERT_FALSE(values.empty());
        const csv::Profile* found = csv::profile_for(saved_, file_.headings);
        ProfileRecognitionString got = values.front();
        got.name = found == nullptr ? "none" : found->name;
        got.asked = found == nullptr ? "true" : "false";
        EXPECT_EQ(values.front(), got) << "wanted " << values.front().to_string()
                                      << " got " << got.to_string();
    }

    void then_saved_profiles_are(const std::vector<SavedProfileString>& values) {
        ASSERT_EQ(values.size(), saved_.size()) << "profiles saved";
        for (std::size_t i = 0; i < values.size(); ++i) {
            SavedProfileString got;
            got.name = saved_[i].name;
            got.signature = signature_text(saved_[i]);
            EXPECT_EQ(values[i], got) << "profile " << i << ": wanted "
                                      << values[i].to_string() << " got " << got.to_string();
        }
    }

    void then_rows_read_are(const std::vector<CsvRowReadString>& values) {
        if (rows_.rows.empty()) read_rows();
        ASSERT_EQ(values.size(), rows_.rows.size()) << "rows read";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const csv::RowRead& row = rows_.rows[i];
            EXPECT_EQ(values[i].date, row.date.iso()) << "row " << i << " date";
            EXPECT_EQ(blank(values[i].payee), row.payee) << "row " << i << " payee";
            EXPECT_EQ(blank(values[i].memo), row.memo) << "row " << i << " memo";
            EXPECT_EQ(blank(values[i].checkno), row.check_no) << "row " << i << " check";
            EXPECT_EQ(blank(values[i].category), row.category) << "row " << i << " category";
            if (values[i].amount != DNCString)
                EXPECT_EQ(Money(values[i].amount), row.amount) << "row " << i << " amount";
        }
    }

    void then_rows_rejected_are(const std::vector<CsvRowErrorString>& values) {
        if (rows_.rows.empty() && rows_.rejected.empty()) read_rows();
        ASSERT_EQ(values.size(), rows_.rejected.size()) << "rows rejected";
        for (std::size_t i = 0; i < values.size(); ++i) {
            CsvRowErrorString got;
            got.line = std::to_string(rows_.rejected[i].line);
            got.reason = rows_.rejected[i].reason;
            EXPECT_EQ(values[i], got) << "wanted " << values[i].to_string()
                                      << " got " << got.to_string();
        }
    }

    void then_date_order_is_ambiguous(const std::vector<DateOrderWarningString>& values) {
        ASSERT_FALSE(values.empty());
        std::vector<std::string> cells;
        const csv::HeaderMatch* where = matching_.for_field(csv::Field::Date);
        if (where != nullptr)
            for (const csv::Row& row : file_.rows) {
                const auto at = static_cast<std::size_t>(where->position) - 1;
                if (at < row.cells.size()) cells.push_back(csv::detail::trim(row.cells[at]));
            }
        const csv::DateAmbiguity found = csv::date_ambiguity(cells, order());

        DateOrderWarningString got = values.front();
        got.ambiguous = found.ambiguous ? "true" : "false";
        if (found.ambiguous) {
            // Only where it is in doubt is there anything to show: the order it
            // will use, and what the first date would be read the other way.
            got.usingorder = types::to_string(found.reading);
            if (!cells.empty()) {
                const auto as_mdy = csv::date_of(cells.front(), types::DateOrder::MDY);
                const auto as_dmy = csv::date_of(cells.front(), types::DateOrder::DMY);
                got.firstdateasmdy = as_mdy.has_value() ? as_mdy->iso() : "none";
                got.firstdateasdmy = as_dmy.has_value() ? as_dmy->iso() : "none";
            }
        }
        EXPECT_EQ(values.front(), got) << "wanted " << values.front().to_string()
                                      << " got " << got.to_string();
    }

    void then_headings_read_are(const std::vector<HeadingReadString>& values) {
        ASSERT_EQ(values.size(), file_.headings.size()) << "headings read";
        for (std::size_t i = 0; i < values.size(); ++i)
            EXPECT_EQ(blank(values[i].heading), file_.headings[i]) << "heading " << i;
    }

    void then_balance_column_check_is(const std::vector<BalanceColumnCheckString>& values) {
        ASSERT_EQ(values.size(), checks_.size()) << "rows carrying a balance";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].line, std::to_string(checks_[i].line)) << "check " << i;
            EXPECT_EQ(Money(values[i].rowbalance), checks_[i].row_balance)
                << "check " << i << " row balance";
            EXPECT_EQ(Money(values[i].computedbalance), checks_[i].computed)
                << "check " << i << " computed balance";
            EXPECT_EQ(parse_bool_cell(values[i].agrees), checks_[i].agrees)
                << "check " << i << " agrees";
        }
    }

    void then_postings_are(const std::vector<PostingRowString>& values) {
        const std::vector<PostingRowString> got = flatten();
        ASSERT_EQ(values.size(), got.size()) << "postings in the book:\n" << listing(got);
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].ref, got[i].ref) << "posting " << i << " ref";
            EXPECT_EQ(values[i].date, got[i].date) << "posting " << i << " date";
            EXPECT_EQ(blank(values[i].payee), blank(got[i].payee)) << "posting " << i << " payee";
            EXPECT_EQ(values[i].account, got[i].account) << "posting " << i << " account";
            EXPECT_EQ(Money(values[i].amount), Money(got[i].amount))
                << "posting " << i << " amount";
            EXPECT_EQ(blank(values[i].memo), blank(got[i].memo)) << "posting " << i << " memo";
            if (values[i].cleared != DNCString)
                EXPECT_EQ(values[i].cleared, got[i].cleared) << "posting " << i << " cleared";
        }
    }

    void then_accounts_created_are(const std::vector<AccountString>& values) {
        for (const auto& value : values) {
            const chart::Account* a = chart_.find(value.path);
            ASSERT_NE(nullptr, a) << value.path << " was not created";
            if (value.type != DNCString)
                EXPECT_EQ(value.type, types::to_string(a->type)) << value.path << " type";
        }
    }

    void then_import_summary_is(const std::vector<ImportSummaryString>& values) {
        ASSERT_FALSE(values.empty());
        ImportSummaryString got;
        got.new_ = std::to_string(decided_.summary.New);
        got.duplicate = std::to_string(decided_.summary.Duplicate);
        got.matched = std::to_string(decided_.summary.Matched);
        got.possible = std::to_string(decided_.summary.Possible);
        EXPECT_EQ(values.front(), got) << "wanted " << values.front().to_string()
                                      << " but the import decided " << got.to_string();
    }

    // ------------------------------------------------------- example tables

    void examples_datatype_csvdelimiter(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            // Each named delimiter has to produce two cells from one line.
            const std::string line = std::string("a") + delimiter_of(value.value) + "b";
            const csv::Read read = csv::read(line, true, value.value);
            ASSERT_EQ(2u, read.headings.size()) << value.value << " -- " << value.notes;
            EXPECT_EQ("a", read.headings[0]) << value.value;
            EXPECT_EQ("b", read.headings[1]) << value.value;
        }
    }

    void examples_datatype_dateorder(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.value,
                      types::to_string(types::date_order_from_string(value.value)))
                << value.value << " -- " << value.notes;
    }

    void examples_datatype_amountstyle(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.value,
                      csv::to_string(csv::amount_style_from_string(value.value)))
                << value.value << " -- " << value.notes;
    }

    void examples_datatype_outwardsign(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.value,
                      csv::to_string(csv::outward_sign_from_string(value.value)))
                << value.value << " -- " << value.notes;
    }

    void examples_datatype_canonicalfield(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.value, csv::to_string(csv::field_from_string(value.value)))
                << value.value << " -- " << value.notes;
    }

    void examples_datatype_matchedby(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.value,
                      csv::to_string(csv::matched_by_from_string(value.value)))
                << value.value << " -- " << value.notes;
    }

    void examples_datatype_profilename(const std::vector<ValidValuesString>& values) {
        for (const auto& value : values) {
            const bool valid = types::ProfileName::is_valid(value.value);
            EXPECT_EQ(parse_bool_cell(value.isvalid), valid)
                << "\"" << value.value << "\" -- " << value.notes;
        }
    }

    void examples_datatype_heading(const std::vector<ValidValuesString>& values) {
        for (const auto& value : values) {
            const bool valid = types::Heading::is_valid(value.value);
            EXPECT_EQ(parse_bool_cell(value.isvalid), valid)
                << "\"" << value.value << "\" -- " << value.notes;
        }
    }

    void examples_businessrule_which_fields_a_file_must_supply(
            const std::vector<FieldRequirementString>& values) {
        for (const auto& value : values) {
            const csv::Field field = csv::field_from_string(value.field);
            // Read against the two cases the requirements turn on.
            const bool with_amount = csv::is_required(field, true, false);
            const bool with_pair = csv::is_required(field, false, true);
            const bool with_neither = csv::is_required(field, false, false);
            if (value.requirement == "Always") {
                EXPECT_TRUE(with_amount && with_pair && with_neither) << value.field;
            } else if (value.requirement == "Never") {
                EXPECT_FALSE(with_amount || with_pair || with_neither) << value.field;
            } else if (value.requirement == "Unless both Debit and Credit are matched") {
                EXPECT_FALSE(with_pair) << value.field << " with a debit and a credit";
                EXPECT_TRUE(with_neither) << value.field << " with neither";
            } else if (value.requirement == "Unless Amount is matched") {
                EXPECT_FALSE(with_amount) << value.field << " with an amount";
                EXPECT_TRUE(with_neither) << value.field << " with neither";
            } else {
                FAIL() << "unlisted requirement " << value.requirement;
            }
        }
    }

    void examples_businessrule_how_a_header_is_normalised_before_it_is_matched(
            const std::vector<HeaderNormalisationString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(blank(value.normalised),
                      csv::normalise_heading(blank(value.sourceheader)))
                << "\"" << value.sourceheader << "\" -- " << value.notes;
    }

    void examples_businessrule_known_spellings_of_each_field(
            const std::vector<HeaderAliasString>& values) {
        for (const auto& value : values) {
            const auto found = csv::field_for_heading(value.normalised);
            ASSERT_TRUE(found.has_value())
                << value.normalised << " is in the rule and not in the table";
            EXPECT_EQ(value.field, csv::to_string(*found)) << value.normalised;
            // Already normalised in the rule, so normalising again changes
            // nothing -- which is what makes the table readable as data.
            EXPECT_EQ(value.normalised, csv::normalise_heading(value.normalised))
                << value.normalised << " is not in normalised form";
        }
    }

    void examples_businessrule_the_amount_style_follows_from_which_amount_fields_matched(
            const std::vector<AmountStyleFromFieldsString>& values) {
        for (const auto& value : values) {
            const csv::StyleChoice got =
                csv::style_for(parse_bool_cell(value.amountmatched),
                               parse_bool_cell(value.debitmatched),
                               parse_bool_cell(value.creditmatched));
            AmountStyleFromFieldsString back = value;
            back.amountstyle = csv::to_string(got.style);
            back.valid = got.valid ? "true" : "false";
            EXPECT_EQ(value, back) << "wanted " << value.to_string()
                                   << " got " << back.to_string();
        }
    }

    void examples_businessrule_how_a_row_becomes_an_amount(
            const std::vector<CsvAmountString>& values) {
        for (const auto& value : values) {
            const Money got = csv::amount_of_row(
                csv::amount_style_from_string(value.amountstyle),
                csv::outward_sign_from_string(value.outwardsign),
                blank(value.amountcell), blank(value.debitcell), blank(value.creditcell));
            EXPECT_EQ(Money(value.amount), got)
                << value.amountstyle << " " << value.outwardsign << " -- " << value.notes;
        }
    }

    void examples_businessrule_how_a_date_cell_is_read(
            const std::vector<CsvDateParseString>& values) {
        for (const auto& value : values) {
            const auto got = csv::date_of(blank(value.text),
                                          types::date_order_from_string(value.dateorder));
            EXPECT_EQ(parse_bool_cell(value.valid), got.has_value())
                << value.dateorder << " \"" << value.text << "\" -- " << value.notes;
            if (got.has_value() && parse_bool_cell(value.valid))
                EXPECT_EQ(value.date, got->iso())
                    << value.dateorder << " \"" << value.text << "\" -- " << value.notes;
        }
    }

    void examples_businessrule_how_a_two_digit_year_is_read(
            const std::vector<TwoDigitYearString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.year, std::to_string(csv::year_from_two_digits(
                                      std::stoi(value.text))))
                << value.text << " -- " << value.notes;
    }

    void examples_businessrule_how_a_csv_row_is_tested_against_what_is_already_there(
            const std::vector<CsvDispositionRuleString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.disposition,
                      ofx::to_string(disposition_reaching_test(value.test)))
                << "test " << value.test << ": " << value.condition;
    }

    void examples_businessrule_what_a_fingerprint_ignores(
            const std::vector<FingerprintMatchString>& values) {
        for (const auto& value : values) {
            const auto one_date = types::Date::from_iso(value.datea);
            const auto two_date = types::Date::from_iso(value.dateb);
            ASSERT_TRUE(one_date.has_value()) << value.datea;
            ASSERT_TRUE(two_date.has_value()) << value.dateb;
            const std::string one = ofx::detail::fingerprint(
                *one_date, Money(value.amounta), blank(value.payeea));
            const std::string two = ofx::detail::fingerprint(
                *two_date, Money(value.amountb), blank(value.payeeb));
            EXPECT_EQ(parse_bool_cell(value.same), one == two)
                << "\"" << value.payeea << "\" against \"" << value.payeeb << "\"";
        }
    }

    void examples_businessrule_a_row_claims_at_most_one_transaction_already_there(
            const std::vector<ClaimPairingString>& values) {
        for (const auto& value : values) {
            const int in_file = std::stoi(value.infile);
            const int in_book = std::stoi(value.alreadythere);
            a_book_of_identical_transactions(in_book);
            std::vector<csv::RowRead> rows;
            for (int i = 0; i < in_file; ++i) rows.push_back(one_identical_row());
            const csv::Imported got = csv::decide(rows, kAccount, transactions_);
            ClaimPairingString back = value;
            back.possible = std::to_string(got.summary.Possible);
            back.new_ = std::to_string(got.summary.New);
            EXPECT_EQ(value, back)
                << in_file << " rows against " << in_book << " already there -- "
                << value.notes;
        }
    }

private:
    static constexpr const char* kAccount = "Assets:Checking";

    std::string text_;
    csv::Read file_;
    csv::Profile profile_;
    bool have_profile_ = false;
    std::vector<csv::Profile> saved_;
    std::map<std::string, csv::Field> from_profile_;
    csv::Matching matching_;
    csv::Rows rows_;
    csv::Imported decided_;
    std::vector<csv::BalanceAtRow> checks_;
    std::vector<ledger::Transaction> created_;
    chart::Chart chart_;
    std::vector<ledger::Transaction> transactions_;
    std::string target_ = kAccount;

    static std::string blank(const std::string& s) {
        return (s == "none" || s == DNCString) ? std::string() : s;
    }

    static char delimiter_of(const std::string& name) {
        if (name == "Tab") return '\t';
        if (name == "Semicolon") return ';';
        if (name == "Pipe") return '|';
        return ',';
    }

    static std::vector<std::string> split(const std::string& commas) {
        std::vector<std::string> out;
        std::string one;
        for (const char c : commas) {
            if (c == ',') {
                if (!csv::detail::trim(one).empty()) out.push_back(csv::detail::trim(one));
                one.clear();
            } else {
                one += c;
            }
        }
        if (!csv::detail::trim(one).empty()) out.push_back(csv::detail::trim(one));
        return out;
    }

    void reread() {
        file_ = csv::read(text_, have_profile_ ? profile_.has_header : true,
                          have_profile_ ? profile_.delimiter : std::string("Comma"),
                          have_profile_ ? profile_.skip_rows : 0);
        rematch();
    }

    void rematch() {
        // A saved profile recognised by the headings of this file drives the
        // matching. The positions come from this file, not from the saved ones,
        // so a bank that reorders its columns needs no setting up again.
        if (!have_profile_) {
            if (const csv::Profile* found = csv::profile_for(saved_, file_.headings)) {
                // What the user corrected comes first: a mapping they fixed once
                // is not undone by an alias that happens to match, which is the
                // whole point of having saved it.
                std::map<std::string, csv::Field> columns = from_profile_;
                for (const auto& entry : csv::columns_of(*found)) columns.insert(entry);
                matching_ = csv::match_headers(file_.headings, columns);
                return;
            }
            if (!from_profile_.empty()) {
                matching_ = csv::match_headers(file_.headings, from_profile_);
                return;
            }
        }
        // A file with no header row is described by column number, so there is
        // nothing to match a heading against and the profile has said it all.
        if (have_profile_ && !profile_.has_header) {
            const std::size_t columns = file_.rows.empty() ? 0 : file_.rows.front().cells.size();
            matching_ = csv::match_positions(columns, profile_);
            return;
        }
        std::map<std::string, csv::Field> columns = from_profile_;
        if (have_profile_)
            for (const auto& entry : csv::columns_of(profile_))
                columns.insert(entry);
        matching_ = csv::match_headers(file_.headings, columns);
    }

    void read_rows() {
        rows_ = csv::rows_of(file_, matching_, order(), style(), outward());
    }

    types::DateOrder order() const {
        return have_profile_ ? profile_.date_order : types::DateOrder::MDY;
    }

    csv::StyleChoice style_choice() const {
        return csv::style_for(matching_.for_field(csv::Field::Amount) != nullptr,
                              matching_.for_field(csv::Field::Debit) != nullptr,
                              matching_.for_field(csv::Field::Credit) != nullptr);
    }

    csv::AmountStyle style() const {
        if (have_profile_) return profile_.amount_style;
        return style_choice().style;
    }

    csv::OutwardSign outward() const {
        return have_profile_ ? profile_.outward_sign : csv::OutwardSign::Negative;
    }

    std::string header_for(csv::Field field) const {
        const csv::HeaderMatch* found = matching_.for_field(field);
        return found == nullptr ? std::string("none") : found->source_header;
    }

    std::string signature_text(const csv::Profile& one) const {
        // In file order, which is what the tables write.
        std::vector<std::string> out;
        for (const std::string& heading : file_.headings) {
            const std::set<std::string> wanted = one.signature();
            if (wanted.count(csv::normalise_heading(heading)) == 1) out.push_back(heading);
        }
        if (out.empty())
            for (const std::string* column : {&one.date_column, &one.payee_column,
                                              &one.amount_column, &one.debit_column,
                                              &one.credit_column})
                if (!column->empty() && *column != "none") out.push_back(*column);
        // Commas and nothing else: it is a signature, not a sentence.
        std::string text;
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (i > 0) text += ",";
            text += out[i];
        }
        return text;
    }

    static csv::Profile profile_from(const CsvProfileString& value) {
        csv::Profile one;
        one.name = value.name;
        if (value.hasheader != DNCString && !value.hasheader.empty())
            one.has_header = parse_bool_cell(value.hasheader);
        if (value.delimiter != DNCString && !value.delimiter.empty())
            one.delimiter = value.delimiter;
        if (value.skiprows != DNCString && !value.skiprows.empty())
            one.skip_rows = std::stoi(value.skiprows);
        one.date_column = blank(value.datecolumn);
        if (value.dateorder != DNCString && !value.dateorder.empty())
            one.date_order = types::date_order_from_string(value.dateorder);
        one.payee_column = blank(value.payeecolumn);
        one.memo_column = blank(value.memocolumn);
        one.check_column = blank(value.checkcolumn);
        one.category_column = blank(value.categorycolumn);
        // CsvProfile in the specification names no balance column, so a profile
        // stated in a table cannot say which column it is. One recognised by its
        // signature can, because the alias table places it. Worth raising: a
        // profile ought to be able to name every column of its file.
        if (value.amountstyle != DNCString && !value.amountstyle.empty())
            one.amount_style = csv::amount_style_from_string(value.amountstyle);
        one.amount_column = blank(value.amountcolumn);
        one.debit_column = blank(value.debitcolumn);
        one.credit_column = blank(value.creditcolumn);
        if (value.outwardsign != DNCString && !value.outwardsign.empty())
            one.outward_sign = csv::outward_sign_from_string(value.outwardsign);
        return one;
    }

    ledger::Ledger as_ledger() const {
        ledger::Ledger out;
        for (const ledger::Transaction& t : transactions_)
            for (const ledger::Posting& p : t.postings)
                out.add({t.date, p.account, p.amount, t.ref.value()});
        return out;
    }

    std::vector<PostingRowString> flatten() const {
        std::vector<PostingRowString> out;
        for (const ledger::Transaction& t : transactions_) {
            for (const ledger::Posting& p : t.postings) {
                PostingRowString row;
                row.ref = t.ref.value();
                row.date = t.date.iso();
                row.payee = t.payee.value().empty() ? "none" : t.payee.value();
                row.account = p.account.value();
                row.amount = p.amount.in_register();
                row.memo = p.memo.empty() ? "none" : p.memo;
                row.cleared = types::to_string(p.cleared);
                out.push_back(row);
            }
        }
        return out;
    }

    static std::string listing(const std::vector<PostingRowString>& rows) {
        std::string out;
        for (const auto& row : rows) out += "  " + row.to_string() + "\n";
        return out;
    }

    // --- the rule tables ---------------------------------------------------

    static csv::RowRead one_identical_row() {
        csv::RowRead row;
        row.date = types::Date(2024, 1, 15);
        row.payee = "STARBUCKS";
        row.amount = Money("-4.75");
        row.line = 2;
        return row;
    }

    void a_book_of_identical_transactions(int many) {
        chart_ = chart::Chart();
        chart::Account bank;
        bank.path = types::AccountPath(kAccount);
        bank.type = types::AccountType::Bank;
        chart_.put(bank);
        chart::Account other;
        other.path = types::AccountPath("Expenses:Uncategorized");
        other.type = types::AccountType::Expense;
        chart_.put(other);

        transactions_.clear();
        for (int i = 0; i < many; ++i) {
            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef("T" + std::to_string(i + 1));
            t.date = types::Date(2024, 1, 15);
            t.payee = types::PayeeName("STARBUCKS");
            ledger::Posting here;
            here.account = types::AccountPath(kAccount);
            here.amount = Money("-4.75");
            here.cleared = types::ClearedStatus::Cleared;
            ledger::Posting there;
            there.account = types::AccountPath("Expenses:Uncategorized");
            there.amount = Money("4.75");
            t.postings = {here, there};
            transactions_.push_back(t);
        }
        target_ = kAccount;
    }

    // The book that makes a row reach exactly the numbered test and stop there.
    ofx::Disposition disposition_reaching_test(const std::string& test) {
        a_book_of_identical_transactions(0);
        csv::RowRead row = one_identical_row();

        ledger::Transaction t;
        t.id = ledger::new_id();
        t.ref = types::TransactionRef("T1");
        t.payee = types::PayeeName("STARBUCKS");
        ledger::Posting here;
        here.account = types::AccountPath(kAccount);
        here.amount = Money("-4.75");
        ledger::Posting there;
        there.account = types::AccountPath("Expenses:Uncategorized");
        there.amount = Money("4.75");

        if (test == "1") {
            // The same fingerprint: same day, same amount, same payee.
            t.date = types::Date(2024, 1, 15);
            here.cleared = types::ClearedStatus::Cleared;
        } else if (test == "2") {
            // Uncleared, same amount and check number, and far enough away that
            // the five-day test could not have claimed it.
            t.date = types::Date(2023, 11, 1);
            t.check_no = types::CheckNumber("2041");
            row.check_no = "2041";
            row.payee = "COUNTY TAX OFFICE";    // so the fingerprint cannot match
        } else if (test == "3") {
            // Uncleared, same amount, five days earlier, a different payee.
            t.date = types::Date(2024, 1, 10);
            t.payee = types::PayeeName("SOMEWHERE ELSE");
        } else {
            // Test 4: an empty book.
            const csv::Imported got = csv::decide({row}, kAccount, transactions_);
            EXPECT_EQ(1u, got.decided.size());
            return got.decided.empty() ? ofx::Disposition::New
                                       : got.decided.front().disposition;
        }

        t.postings = {here, there};
        transactions_ = {t};
        const csv::Imported got = csv::decide({row}, kAccount, transactions_);
        EXPECT_EQ(1u, got.decided.size()) << "test " << test;
        return got.decided.empty() ? ofx::Disposition::New
                                   : got.decided.front().disposition;
    }
};
