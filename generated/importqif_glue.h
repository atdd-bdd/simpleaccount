#pragma once
#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <vector>
#include "common/common.h"
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "date_parse.h"
#include "ledger.h"
#include "money.h"
#include "posting.h"
#include "qif_import.h"
#include "qif_lexer.h"
#include "qif_reader.h"
#include "report.h"
#include "text_types.h"

// Glue for ImportQif.spectable.
//
// Two conventions of the generated tables are worth stating once. An attribute
// whose Default is "none" arrives as the literal word, so a comparison has to
// read that as nothing. And a docstring arrives as one string, which is the file
// to be read.
class ImportQifGlue {
public:
    // ---------------------------------------------------------------- givens

    void given_import_target_is(const std::vector<ImportTargetString>& values) {
        for (const auto& value : values) target_ = value.accountpath;
    }
    void given_import_target_is_as_previous() {}

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
    }
    void given_the_chart_of_accounts_is_as_previous() {}

    void given_the_file_contains(const std::string& value) { file_ = value; }
    void given_the_second_file_contains(const std::string& value) { second_file_ = value; }

    // A file whose leading byte order mark would otherwise become part of the
    // first field's name.
    void given_the_file_begins_with_a_byte_order_mark_and_contains(const std::string& value) {
        file_ = "\xEF\xBB\xBF" + value;
    }

    void given_the_qif_profile_is(const std::vector<QifProfileString>& values) {
        for (const auto& value : values)
            order_ = types::date_order_from_string(value.dateorder);
    }
    void given_the_qif_profile_is_as_previous() {}

    // ----------------------------------------------------------------- whens

    void when_qif_file_imported() {
        qif::Importer importer(order_);
        importer.start_from(chart_);
        importer.import_into(target_);
        result_ = importer.run(file_);
        if (!second_file_.empty()) {
            qif::Importer again(order_);
            again.start_from(result_.accounts);
            again.import_into(target_);
            const qif::Imported more = again.run(second_file_);
            for (const auto& p : more.postings) result_.book.add(p);
            for (const auto& t : more.transactions) result_.transactions.push_back(t);
            for (const auto& a : more.accounts_created)
                result_.accounts_created.push_back(a);
            result_.accounts = more.accounts;
        }
    }

    void when_book_checked() {}

    void when_report_run(const std::vector<ReportSpecString>& values) {
        for (const auto& value : values) {
            reports::Spec spec;
            const auto from = types::Date::from_iso(value.from);
            const auto to = types::Date::from_iso(value.to);
            if (from) spec.from = *from;
            if (to) spec.to = *to;
            report_ = reports::category_report(result_.accounts, result_.book, spec);
        }
    }

    // ----------------------------------------------------------------- thens

    void then_rejected_because(const std::vector<RejectionString>& values) {
        for (const auto& value : values) {
            EXPECT_TRUE(result_.refused) << "expected a refusal: " << value.reason;
            EXPECT_EQ(value.reason, result_.refusal);
        }
    }

    void then_qif_records_read_are(const std::vector<QifRecordString>& values) {
        ASSERT_EQ(values.size(), result_.records.size()) << "number of records read";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const qif::ReadRecord& got = result_.records[i];
            EXPECT_EQ(values[i].account, got.account) << "record " << i << " account";
            EXPECT_EQ(values[i].date, got.date.iso()) << "record " << i << " date";
            EXPECT_EQ(Money(values[i].amount).cents(), got.amount.cents())
                << "record " << i << " amount";
            EXPECT_EQ(blank(values[i].payee), got.payee) << "record " << i << " payee";
            EXPECT_EQ(blank(values[i].categorytext), got.category_text)
                << "record " << i << " category";
        }
    }

    void then_accounts_created_are(const std::vector<AccountString>& values) {
        ASSERT_EQ(values.size(), result_.accounts_created.size())
            << "number of accounts created";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const chart::Account& a = result_.accounts_created[i];
            EXPECT_EQ(values[i].path, a.path.value()) << "account " << i;
            EXPECT_EQ(values[i].type, types::to_string(a.type)) << a.path.value() << " type";
            EXPECT_EQ(parse_bool_cell(values[i].placeholder), a.placeholder)
                << a.path.value() << " placeholder";
        }
    }

    void then_account_limits_are(const std::vector<AccountLimitString>& values) {
        for (const auto& value : values) {
            const auto at = result_.credit_limits.find(value.account);
            ASSERT_NE(result_.credit_limits.end(), at) << "no limit for " << value.account;
            EXPECT_EQ(Money(value.creditlimit).cents(), at->second.cents()) << value.account;
        }
    }

    void then_migration_notes_are(const std::vector<MigrationNoteString>& values) {
        // A table with a header and no rows says nothing was reported.
        if (values.empty()) {
            EXPECT_TRUE(result_.notes.empty()) << first_note();
            return;
        }
        for (const auto& value : values) {
            const bool found = std::any_of(
                result_.notes.begin(), result_.notes.end(), [&](const qif::Note& n) {
                    return n.kind == value.kind && n.detail == value.detail;
                });
            EXPECT_TRUE(found) << "no note [" << value.kind << "] " << value.detail
                               << "\n  reported instead: " << first_note();
        }
    }

    void then_postings_are(const std::vector<PostingRowString>& values) {
        const std::vector<PostingRowString> got = flatten();
        ASSERT_EQ(values.size(), got.size()) << "number of postings";
        for (std::size_t i = 0; i < values.size(); ++i) compare_posting(values[i], got[i], i);
    }

    void then_postings_created_are(const std::vector<PostingRowString>& values) {
        then_postings_are(values);
    }

    void then_import_summary_is(const std::vector<ImportSummaryString>& values) {
        for (const auto& value : values) {
            EXPECT_EQ(std::stoi(value.new_), result_.summary.created) << "new";
            EXPECT_EQ(std::stoi(value.duplicate), result_.summary.duplicate) << "duplicate";
            EXPECT_EQ(std::stoi(value.matched), result_.summary.matched) << "matched";
            EXPECT_EQ(std::stoi(value.possible), result_.summary.possible) << "possible";
        }
    }

    void then_transaction_is(const std::vector<TransactionString>& values) {
        ASSERT_FALSE(result_.transactions.empty()) << "no transaction was made";
        for (const auto& value : values) {
            const ledger::Transaction& t = result_.transactions.front();
            if (blank(value.date) != "") EXPECT_EQ(value.date, t.date.iso()) << "date";
            EXPECT_EQ(blank(value.payee), t.payee.value()) << "payee";
            EXPECT_EQ(blank(value.checkno), t.check_no.value()) << "check number";
            EXPECT_EQ(blank(value.tag), t.tag) << "tag";
        }
    }

    void then_book_check_is(const std::vector<BookCheckString>& values) {
        int out_of_balance = 0;
        for (const ledger::Transaction& t : result_.transactions) {
            std::vector<Money> amounts;
            for (const ledger::Posting& p : t.postings) amounts.push_back(p.amount);
            if (!ledger::balances(amounts)) ++out_of_balance;
        }
        for (const auto& value : values) {
            EXPECT_EQ(std::stoi(value.transactionschecked),
                      static_cast<int>(result_.transactions.size()))
                << "transactions checked";
            EXPECT_EQ(std::stoi(value.outofbalance), out_of_balance) << "out of balance";
            EXPECT_EQ(parse_bool_cell(value.balanced), out_of_balance == 0) << "balanced";
        }
    }

    void then_report_rows_are(const std::vector<ReportRowString>& values) {
        ASSERT_EQ(values.size(), report_.rows.size()) << "number of report rows";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].account, report_.rows[i].account) << "row " << i;
            EXPECT_EQ(Money(values[i].amount).cents(), report_.rows[i].amount.cents())
                << report_.rows[i].account;
        }
    }

    void then_date_order_evidence_is(const std::vector<DateOrderEvidenceString>& values) {
        const dates::OrderEvidence got = evidence();
        for (const auto& value : values) {
            EXPECT_EQ(std::stoi(value.firstpartover12), got.first_part_over_12) << "first";
            EXPECT_EQ(std::stoi(value.secondpartover12), got.second_part_over_12) << "second";
            const auto proven = got.proven();
            ASSERT_TRUE(proven.has_value()) << "the file proves no order";
            EXPECT_EQ(value.proven, types::to_string(*proven));
        }
    }

    void then_date_order_is_ambiguous(const std::vector<DateOrderWarningString>& values) {
        const dates::OrderEvidence got = evidence();
        for (const auto& value : values)
            EXPECT_EQ(parse_bool_cell(value.ambiguous), got.ambiguous()) << "ambiguous";
    }

    // ------------------------------------------------------------ the rules

    void examples_businessrule_how_a_header_line_is_read(
            const std::vector<SectionHeaderString>& values) {
        for (const auto& value : values) {
            const SectionHeaderTyped t = SectionHeaderTyped::from_string_struct(value);
            EXPECT_EQ(t.section, qif::to_string(qif::section_of(t.line)))
                << "[" << t.line << "] -- " << t.notes;
        }
    }

    void examples_businessrule_how_the_c_field_becomes_a_cleared_status(
            const std::vector<ClearedMappingString>& values) {
        for (const auto& value : values) {
            const ClearedMappingTyped t = ClearedMappingTyped::from_string_struct(value);
            EXPECT_EQ(t.cleared, types::to_string(qif::cleared_of(t.text)))
                << "[" << t.text << "] -- " << t.notes;
        }
    }

    void examples_businessrule_how_a_qif_date_is_read(
            const std::vector<QifDateParseString>& values) {
        for (const auto& value : values) {
            const QifDateParseTyped t = QifDateParseTyped::from_string_struct(value);
            const auto got = dates::parse_qif(t.text,
                                              types::date_order_from_string(t.dateorder));
            EXPECT_EQ(t.valid, got.has_value())
                << "[" << t.text << "] read as " << t.dateorder
                << (t.notes.empty() ? "" : " -- " + t.notes);
            if (t.valid && got) EXPECT_EQ(t.date, got->iso()) << "[" << t.text << "]";
        }
    }

    void examples_businessrule_how_a_qif_amount_is_read(
            const std::vector<QifAmountParseString>& values) {
        for (const auto& value : values) {
            const QifAmountParseTyped t = QifAmountParseTyped::from_string_struct(value);
            const auto got = qif::parse_amount(t.text, t.decimalcomma);
            EXPECT_EQ(t.valid, got.has_value()) << "[" << t.text << "] -- " << t.notes;
            if (t.valid && got)
                EXPECT_EQ(Money(t.amount).cents(), got->cents()) << "[" << t.text << "]";
        }
    }

    void examples_businessrule_how_a_split_line_amount_becomes_a_posting_amount(
            const std::vector<SplitLineSignString>& values) {
        for (const auto& value : values) {
            const SplitLineSignTyped t = SplitLineSignTyped::from_string_struct(value);
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

    void examples_businessrule_how_a_qif_account_type_becomes_an_account_type_and_a_path(
            const std::vector<QifAccountMappingString>& values) {
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

    void examples_businessrule_how_a_stock_split_ratio_is_read(
            const std::vector<SplitRatioString>& values) {
        for (const auto& value : values) {
            const SplitRatioTyped t = SplitRatioTyped::from_string_struct(value);
            const double ratio = qif::split_ratio_of(t.qfield);
            EXPECT_DOUBLE_EQ(t.ratio, ratio) << "Q of " << t.qfield << " -- " << t.notes;
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

private:
    chart::Chart chart_;
    std::string target_;
    std::string file_;
    std::string second_file_;
    types::DateOrder order_ = types::DateOrder::MDY;
    qif::Imported result_;
    reports::CategoryReport report_;

    // A Default of "none" arrives as the literal word.
    static std::string blank(const std::string& s) {
        return (s == "none" || s == DNCString) ? std::string() : s;
    }

    std::string first_note() const {
        if (result_.notes.empty()) return "(no notes)";
        std::string out;
        for (const qif::Note& n : result_.notes) out += "[" + n.kind + "] " + n.detail + "; ";
        return out;
    }

    std::vector<PostingRowString> flatten() const {
        std::vector<PostingRowString> out;
        for (const ledger::Transaction& t : result_.transactions) {
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

    static void compare_posting(const PostingRowString& want, const PostingRowString& got,
                                std::size_t i) {
        EXPECT_EQ(want.ref, got.ref) << "posting " << i << " ref";
        EXPECT_EQ(want.date, got.date) << "posting " << i << " date";
        EXPECT_EQ(blank(want.payee), blank(got.payee)) << "posting " << i << " payee";
        EXPECT_EQ(want.account, got.account) << "posting " << i << " account";
        EXPECT_EQ(Money(want.amount).cents(), Money(got.amount).cents())
            << "posting " << i << " on " << got.account;
        EXPECT_EQ(blank(want.memo), blank(got.memo)) << "posting " << i << " memo";
        if (want.cleared != DNCString && !want.cleared.empty())
            EXPECT_EQ(want.cleared, got.cleared) << "posting " << i << " cleared";
    }

    // What the file's own dates prove about their order.
    dates::OrderEvidence evidence() const {
        dates::OrderEvidence out;
        const qif::File file = qif::read(qif::decode(file_), target_);
        for (const qif::Record& r : file.records) {
            const std::string d = r.first('D');
            const std::size_t first = d.find('/');
            if (first == std::string::npos) continue;
            std::size_t second = std::string::npos;
            for (std::size_t i = first + 1; i < d.size(); ++i)
                if (d[i] == '/' || d[i] == '\'') { second = i; break; }
            if (second == std::string::npos) continue;
            const int a = std::atoi(d.substr(0, first).c_str());
            const int b = std::atoi(d.substr(first + 1, second - first - 1).c_str());
            if (a > 12) ++out.first_part_over_12;
            if (b > 12) ++out.second_part_over_12;
        }
        return out;
    }
public:

    void then_statement_balance_to_check_is(const std::vector<QifStatementBalanceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_statement_balance_to_check_is";
    }

    void then_holdings_are(const std::vector<HoldingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_holdings_are";
    }

    void given_holdings_are(const std::vector<HoldingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_holdings_are";
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

    void then_unpaid_invoices_are(const std::vector<UnpaidInvoiceString>& values) {
        ASSERT_EQ(values.size(), result_.uncollected.size())
            << "number of uncollected invoices";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const UnpaidInvoiceString& want = values[i];
            const qif::UncollectedInvoice& got = result_.uncollected[i];
            EXPECT_EQ(want.date, got.date.iso()) << "date " << i;
            EXPECT_EQ(want.payee, got.payee) << "payee " << i;
            EXPECT_EQ(Money(want.amount).cents(), got.amount.cents()) << "amount " << i;
            EXPECT_EQ(want.category, got.category)
                << "category " << i;
            EXPECT_EQ(want.note, got.note) << "note " << i;
        }
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

    void examples_businessrule_what_an_account_block_means_depends_on_autoswitch(const std::vector<AccountBlockMeaningString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_what_an_account_block_means_depends_on_autoswitch";
    }

    void examples_businessrule_how_a_category_name_becomes_an_account_path(const std::vector<CategoryMappingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_how_a_category_name_becomes_an_account_path";
    }

    void examples_businessrule_the_split_lines_decide_the_categories_not_the_l_field(const std::vector<SplitCategorySourceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_the_split_lines_decide_the_categories_not_the_l_field";
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

public:



public:

    void examples_businessrule_the_payment_names_the_invoice_it_paid_in_the_n_field(const std::vector<InvoiceLinkEvidenceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_the_payment_names_the_invoice_it_paid_in_the_n_field";
    }

    void examples_businessrule_what_the_cash_basis_is_worth_measured_against_the_all_dates_report(const std::vector<CashBasisTargetString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_what_the_cash_basis_is_worth_measured_against_the_all_dates_report";
    }

public:

    void examples_businessrule_an_invoice_carries_a_receivable_and_the_income_waits_for_the_payment(const std::vector<InvoiceHoldingAccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_an_invoice_carries_a_receivable_and_the_income_waits_for_the_payment";
    }

public:

    void examples_businessrule_what_the_n_field_is_and_why_nothing_is_posted_from_it(const std::vector<InvoiceLinkEvidenceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_what_the_n_field_is_and_why_nothing_is_posted_from_it";
    }

    // Each row is put through the importer as a one-invoice file, so the table
    // says what the import does rather than what a helper believes.
    void examples_businessrule_the_cleared_marker_says_whether_an_invoice_was_collected(
            const std::vector<CollectedTestString>& values) {
        for (const auto& value : values) {
            const bool cleared = value.cfield.find('*') != std::string::npos;
            const bool income = value.bills.find("ncome") != std::string::npos;
            const std::string category = income ? "Consulting" : "Office";
            std::string text = "!Type:Cat\nN" + category + "\n" +
                               (income ? "I" : "E") + "\n^\n";
            text += "!Account\nNCustomer Invoices\nTInvoice\n^\n";
            text += "!Type:Invoice\nD3/15'25\nT500.00\nU500.00\n";
            if (cleared) text += "C*\n";
            text += "PA Client\nL--Split--\nS" + category + "\n$500.00\nXI1\n^\n";

            qif::Importer importer(types::DateOrder::MDY);
            importer.import_into("Assets:Customer Invoices");
            const qif::Imported out = importer.run(text);
            ASSERT_FALSE(out.refused) << out.refusal;

            const std::string path = (income ? "Income:" : "Expenses:") + category;
            const Money posted = out.book.raw_balance(path);
            const bool recognised = posted.cents() != 0;
            const bool want = value.recognised.rfind("Yes", 0) == 0;
            EXPECT_EQ(want, recognised)
                << value.cfield << " / " << value.bills << " -> " << path;
        }
    }

    void examples_businessrule_where_an_invoice_that_was_never_collected_is_parked(
            const std::vector<InvoiceHoldingAccountString>& values) {
        for (const auto& value : values) {
            const bool bill = value.document == "Bill";
            // A bill whose lines are income is the symmetric case; neither file
            // in testdata has one, so it is built here to say what it would do.
            std::string text = "!Type:Cat\nNConsulting\nI\n^\n";
            text += std::string("!Account\nN") +
                    (bill ? "Business Bills\nTBill\n" : "Customer Invoices\nTInvoice\n") +
                    "^\n";
            text += std::string("!Type:") + (bill ? "Bill" : "Invoice") +
                    "\nD3/15'25\nT500.00\nU500.00\nPA Client\nL--Split--\n"
                    "SConsulting\n$500.00\nXI1\n^\n";

            qif::Importer importer(types::DateOrder::MDY);
            importer.import_into(value.documentaccount);
            const qif::Imported out = importer.run(text);
            ASSERT_FALSE(out.refused) << out.refusal;

            const std::string holding = value.holdingaccount;
            EXPECT_NE(0, out.book.raw_balance(holding).cents())
                << value.document << " should park against " << holding;
            EXPECT_EQ(0, out.book.raw_balance("Income:Consulting").cents())
                << value.document << " should recognise no income";
        }
    }

    // Built as a whole case each time: an invoice in one year, a payment in
    // another, and the question of which year the income lands in.
    void examples_businessrule_which_period_a_collected_invoice_is_reported_in(
            const std::vector<RecognitionPeriodString>& values) {
        for (const auto& value : values) {
            const bool never = value.case_.find("never") != std::string::npos;
            const bool crosses = value.case_.find("the next") != std::string::npos;

            std::string text = "!Type:Cat\nNConsulting\nI\n^\n";
            text += "!Account\nNCustomer Invoices\nTInvoice\n^\n";
            text += "!Account\nNChecking\nTBank\n^\n";
            text += "!Type:Invoice\nD11/15'25\nT500.00\nU500.00\n";
            if (!never) text += "C*\n";
            text += "PA Client\nL--Split--\nSConsulting\n$500.00\nXI1\n^\n";
            if (!never) {
                text += std::string("D") + (crosses ? "1/20'26" : "11/20'25") +
                        "\nT-500.00\nU-500.00\nC*\nPA Client\nL[Checking]\nXI3\n^\n";
            }

            qif::Importer importer(types::DateOrder::MDY);
            importer.import_into("Assets:Customer Invoices");
            const qif::Imported out = importer.run(text);
            ASSERT_FALSE(out.refused) << out.refusal;

            const Money in_2025 = out.book.balance_as_at("Income:Consulting",
                                                         types::Date{2025, 12, 31});
            const Money ever = out.book.raw_balance("Income:Consulting");
            if (value.reportedin.find("not reported") != std::string::npos ||
                value.reportedin.find("Not reported") != std::string::npos) {
                EXPECT_EQ(0, ever.cents()) << value.case_;
            } else {
                // Both the "same year" and the "year invoiced" rows put it in
                // 2025, which is the year of the invoice.
                EXPECT_EQ(-50000, in_2025.cents()) << value.case_;
                EXPECT_EQ(-50000, ever.cents()) << value.case_;
            }
        }
    }

};
