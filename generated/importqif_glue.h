#pragma once
#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
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
            EXPECT_EQ(values[i].account.value, got.account) << "record " << i << " account";
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
            EXPECT_EQ(values[i].path.value, a.path.value()) << "account " << i;
            if (values[i].type.value != DNCString)
                EXPECT_EQ(values[i].type.value, types::to_string(a.type)) << a.path.value() << " type";
            if (values[i].placeholder != DNCString)
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

    void then_import_summary_is(const std::vector<ImportSummaryString>& values) {
        for (const auto& value : values) {
            EXPECT_EQ(std::stoi(value.new_), result_.summary.created) << "new";
            EXPECT_EQ(std::stoi(value.duplicate), result_.summary.duplicate) << "duplicate";
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
            for (const ledger::Side& s : ledger::sides_of(t, result_.splits))
                amounts.push_back(s.amount);
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
            EXPECT_EQ(values[i].account.value, report_.rows[i].account) << "row " << i;
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
            EXPECT_EQ(value.proven.value, types::to_string(*proven));
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
            EXPECT_EQ(t.section.text, qif::to_string(qif::section_of(t.line)))
                << "[" << t.line << "] -- " << t.notes;
        }
    }

    void examples_businessrule_how_the_c_field_becomes_a_cleared_status(
            const std::vector<ClearedMappingString>& values) {
        for (const auto& value : values) {
            const ClearedMappingTyped t = ClearedMappingTyped::from_string_struct(value);
            EXPECT_EQ(t.cleared.text, types::to_string(qif::cleared_of(t.text)))
                << "[" << t.text << "] -- " << t.notes;
        }
    }

    void examples_businessrule_how_a_qif_date_is_read(
            const std::vector<QifDateParseString>& values) {
        for (const auto& value : values) {
            const QifDateParseTyped t = QifDateParseTyped::from_string_struct(value);
            const auto got = dates::parse_qif(t.text,
                                              types::date_order_from_string(t.dateorder.text));
            EXPECT_EQ(t.valid, got.has_value())
                << "[" << t.text << "] read as " << t.dateorder.text
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
                EXPECT_EQ(Money(t.amount.text).cents(), got->cents()) << "[" << t.text << "]";
        }
    }

    void examples_businessrule_how_a_split_line_amount_becomes_a_split_line_s_amount(
            const std::vector<SplitLineSignString>& values) {
        for (const auto& value : values) {
            const SplitLineSignTyped t = SplitLineSignTyped::from_string_struct(value);
            EXPECT_EQ(Money(t.t.text).cents(), Money(t.accountposting.text).cents()) << t.notes;
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
            EXPECT_EQ(t.accounttype.text, types::to_string(mapping->type)) << t.qiftype;
            EXPECT_EQ(t.root.text, mapping->root) << t.qiftype;
            EXPECT_EQ(t.examplepath.text, qif::account_path_of(t.qiftype, t.examplename.text))
                << t.qiftype;
        }
    }

    void examples_businessrule_how_a_stock_split_ratio_is_read(
            const std::vector<SplitRatioString>& values) {
        for (const auto& value : values) {
            const SplitRatioTyped t = SplitRatioTyped::from_string_struct(value);
            const double ratio = qif::split_ratio_of(t.qfield);
            EXPECT_DOUBLE_EQ(t.ratio, ratio) << "Q of " << t.qfield << " -- " << t.notes;
            const double held_before = std::stod(t.heldbefore.text);
            const double price_before = std::stod(t.pricebefore.text);
            EXPECT_DOUBLE_EQ(std::stod(t.heldafter.text),
                             qif::shares_after_split(held_before, ratio)) << t.notes;
            EXPECT_DOUBLE_EQ(std::stod(t.priceafter.text),
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
    std::map<std::string, qif::IncompleteBalance> incomplete_balances_given_;

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

    // The handful of BusinessRules in the "Things the two files in testdata
    // settled" section are pinned to the real business file rather than a
    // synthetic fixture -- this reads it from disk by a path relative to this
    // header's own location, which is stable regardless of the test binary's
    // working directory.
    static std::string read_testdata_raw(const std::string& name) {
        const std::filesystem::path here(__FILE__);
        const std::filesystem::path path =
            (here.parent_path() / ".." / "testdata" / name).lexically_normal();
        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    static qif::File read_testdata_file(const std::string& name) {
        return qif::read(qif::decode(read_testdata_raw(name)));
    }

    // Which real !Type:Invoice record is a document and which a payment, by
    // the same L-field test the importer itself uses (see is_document_record
    // in qif_import.h): a payment names an account in brackets, a document
    // names a category or --Split--.
    static bool is_real_invoice_document(const qif::Record& r) {
        const bool has_split_lines = r.has('$') && r.section != qif::Section::InvestmentTxns;
        const std::string l = r.first('L');
        const bool bracket = !l.empty() && l.front() == '[';
        return has_split_lines || (!l.empty() && !bracket);
    }

    std::map<int, int> invoice_years_by_year_;

public:

    void then_statement_balance_to_check_is(const std::vector<QifStatementBalanceString>& values) {
        for (const auto& value : values) {
            const auto at = result_.statement_balances.find(value.account);
            ASSERT_NE(result_.statement_balances.end(), at)
                << "no statement balance for " << value.account;
            EXPECT_EQ(Money(value.statedbal).cents(), at->second.balance.cents())
                << value.account;
            EXPECT_EQ(value.statedbalat, at->second.as_of.iso()) << value.account;
        }
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
        const qif::MigrationReport got = result_.migration_report();
        for (const auto& value : values) {
            EXPECT_EQ(std::stoi(value.accountscreated), got.accounts_created)
                << "accounts created";
            EXPECT_EQ(std::stoi(value.categoriescreated), got.categories_created)
                << "categories created";
            EXPECT_EQ(std::stoi(value.transactions), got.transactions) << "transactions";
            EXPECT_EQ(std::stoi(value.postings), got.postings) << "postings";
            EXPECT_EQ(std::stoi(value.transferspaired), got.transfers_paired)
                << "transfers paired";
            EXPECT_EQ(std::stoi(value.unsupported), got.unsupported) << "unsupported";
            EXPECT_EQ(std::stoi(value.notes), got.notes) << "notes: " << first_note();
            EXPECT_EQ(parse_bool_cell(value.balanced), got.balanced) << "balanced";
        }
    }

    void then_balance_checks_are(const std::vector<LedgerBalanceResultString>& values) {
        then_balance_check_is(values);
    }

    void then_accounts_with_incomplete_balances_are(
            const std::vector<IncompleteBalanceString>& values) {
        ASSERT_EQ(values.size(), result_.incomplete_balances.size())
            << "number of accounts with incomplete balances";
        for (const auto& value : values) {
            const auto at = result_.incomplete_balances.find(value.account);
            ASSERT_NE(result_.incomplete_balances.end(), at)
                << "no incomplete balance for " << value.account;
            EXPECT_EQ(value.reason, at->second.reason) << value.account;
            EXPECT_EQ(std::stoi(value.skippedrecords), at->second.skipped_records)
                << value.account;
        }
    }

    void given_accounts_with_incomplete_balances_are(
            const std::vector<IncompleteBalanceString>& values) {
        for (const auto& value : values)
            incomplete_balances_given_[value.account] =
                {value.reason, std::stoi(value.skippedrecords)};
    }

    void then_balance_display_is(const std::vector<BalanceDisplayString>& values) {
        for (const auto& value : values) {
            const bool incomplete = incomplete_balances_given_.count(value.account) != 0;
            const qif::BalanceDisplay got = qif::balance_display(incomplete);
            EXPECT_EQ(parse_bool_cell(value.complete), got.complete) << value.account;
            EXPECT_EQ(blank(value.shownas), got.shown_as) << value.account;
            EXPECT_EQ(blank(value.caution), got.caution) << value.account;
        }
    }

    void then_unpaid_invoices_are(const std::vector<UnpaidInvoiceString>& values) {
        ASSERT_EQ(values.size(), result_.uncollected.size())
            << "number of uncollected invoices";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const UnpaidInvoiceString& want = values[i];
            const qif::UncollectedInvoice& got = result_.uncollected[i];
            EXPECT_EQ(want.date, got.date.iso()) << "date " << i;
            EXPECT_EQ(want.payee.value, got.payee) << "payee " << i;
            EXPECT_EQ(Money(want.amount).cents(), got.amount.cents()) << "amount " << i;
            EXPECT_EQ(want.category.value, got.category)
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

    // Read straight from the real business file, not from anything a prior
    // step imported -- this is a standalone diagnostic over the invoice
    // section's own dates, independent of the importer's transaction output.
    void when_invoice_years_summarised() {
        invoice_years_by_year_.clear();
        const qif::File file = read_testdata_file("pughkilleen.QIF");
        for (const qif::Record& r : file.records) {
            if (r.section != qif::Section::InvoiceTxns) continue;
            if (!is_real_invoice_document(r)) continue;
            const auto d = dates::parse_qif(r.first('D'), types::DateOrder::MDY);
            if (d) ++invoice_years_by_year_[d->year()];
        }
    }

    void then_invoice_years_are(const std::vector<InvoiceYearSpanString>& values) {
        for (const auto& v : values) {
            const int from = std::stoi(v.fromyear);
            const int to = std::stoi(v.toyear);
            int actual = 0;
            for (const auto& kv : invoice_years_by_year_)
                if (kv.first >= from && kv.first <= to) actual += kv.second;
            EXPECT_EQ(std::stoi(v.invoices), actual) << v.span << " (" << from << "-" << to << ")";
        }
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
        const qif::BalanceCheck got = result_.check_the_one_balance();
        for (const auto& value : values) {
            EXPECT_EQ(Money(value.clearedbalance).cents(), got.cleared_balance.cents())
                << "cleared balance";
            EXPECT_EQ(Money(value.statedbal).cents(), got.stated_bal.cents()) << "stated bal";
            EXPECT_EQ(Money(value.difference).cents(), got.difference.cents()) << "difference";
            EXPECT_EQ(parse_bool_cell(value.agrees), got.agrees) << "agrees";
        }
    }

    void then_transfer_pairing_report_is(const std::vector<TransferPairingReportString>& values) {
        for (const auto& value : values) {
            EXPECT_EQ(std::stoi(value.bracketedfields), result_.transfer_pairing.bracketed_fields)
                << "bracketed fields";
            EXPECT_EQ(std::stoi(value.pairsformed), result_.transfer_pairing.pairs_formed)
                << "pairs formed";
            EXPECT_EQ(std::stoi(value.unpaired), result_.transfer_pairing.unpaired)
                << "unpaired";
            EXPECT_EQ(std::stoi(value.nearmisses), result_.transfer_pairing.near_misses)
                << "near misses";
        }
    }

    // An EnumerationValues table is the fixed set of values the type takes, so
    // what it asserts is that each one is known and reads back as itself.
    void examples_datatype_qifsection(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            ASSERT_NO_THROW(qif::section_from_string(v.value)) << v.value;
            EXPECT_EQ(v.value, qif::to_string(qif::section_from_string(v.value)));
        }
    }

    void examples_businessrule_what_an_account_block_means_depends_on_autoswitch(const std::vector<AccountBlockMeaningString>& values) {
        for (const auto& value : values) {
            const AccountBlockMeaningTyped t = AccountBlockMeaningTyped::from_string_struct(value);
            const bool on = t.autoswitch == "on";
            EXPECT_EQ(t.meaning, qif::account_block_meaning(on)) << t.notes;
        }
    }

    // Each row is run through the importer as a one-record file, so the table
    // says what the import does rather than what a helper believes.
    void examples_businessrule_how_a_category_name_becomes_an_account_path(
            const std::vector<CategoryMappingString>& values) {
        for (const auto& value : values) {
            const CategoryMappingTyped t = CategoryMappingTyped::from_string_struct(value);
            std::string text = "!Type:Bank\nD01/15/2024\nT" + value.amount.value + "\n";
            if (!t.categorytext.empty()) text += "L" + t.categorytext + "\n";
            text += "^\n";
            if (t.inlist != "Absent") {
                const bool income = t.inlist == "Income";
                text = "!Type:Cat\nN" + t.categorytext + "\n" +
                       (income ? "I" : "E") + "\n^\n" + text;
            }
            qif::Importer importer(types::DateOrder::MDY);
            importer.import_into("Assets:Checking");
            const qif::Imported out = importer.run(text);
            ASSERT_FALSE(out.refused) << out.refusal;
            ASSERT_EQ(1u, out.transactions.size());
            const ledger::Transaction& made = out.transactions.front();
            ASSERT_FALSE(chart::is_split(made.destination));
            EXPECT_EQ(t.accountpath.text, out.accounts.path_for_ref(made.destination)) << t.categorytext;
            const bool guessed = std::any_of(
                out.notes.begin(), out.notes.end(),
                [](const qif::Note& n) { return n.kind == "CategoryGuessed"; });
            EXPECT_EQ(t.fallback, guessed) << t.categorytext;
        }
    }

    void examples_businessrule_the_split_lines_decide_the_categories_not_the_l_field(
            const std::vector<SplitCategorySourceString>& values) {
        for (const auto& value : values) {
            const SplitCategorySourceTyped t = SplitCategorySourceTyped::from_string_struct(value);
            const std::string from = t.hassplitlines ? "SplitLines"
                                    : t.lfield == qif::kSplitMarker ? "None"
                                                                   : "LField";
            EXPECT_EQ(t.categoriesfrom, from) << t.notes;
        }
    }

    // Deferred: a narrative table of the four conditions a pairing already
    // satisfies, with nothing of its own to compute beyond what the transfer
    // scenarios already exercise. Left tagged $WIP.
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

    // Classifies every real !Type:Invoice record the same way is_document_record
    // in qif_import.h does, then counts each row's own Kind+LField combination
    // against the real file -- not a synthetic fixture, because the point of
    // this table is what the actual export contains.
    void examples_businessrule_how_the_invoice_section_is_shaped(const std::vector<InvoiceRecordKindString>& values) {
        const qif::File file = read_testdata_file("pughkilleen.QIF");
        int split_count = 0, income_count = 0, payment_count = 0;
        for (const qif::Record& r : file.records) {
            if (r.section != qif::Section::InvoiceTxns) continue;
            const bool has_split_lines = r.has('$') && r.section != qif::Section::InvestmentTxns;
            const std::string l = r.first('L');
            const bool bracket = !l.empty() && l.front() == '[';
            if (has_split_lines) ++split_count;
            else if (bracket) ++payment_count;
            else if (!l.empty()) ++income_count;
        }
        for (const auto& v : values) {
            int actual = 0;
            if (v.kind == "Invoice" && v.lfield == "--Split--") actual = split_count;
            else if (v.kind == "Invoice") actual = income_count;
            else if (v.kind == "Payment") actual = payment_count;
            EXPECT_EQ(std::stoi(v.count), actual) << v.kind << " / " << v.lfield;
        }
    }

    void examples_businessrule_the_export_does_not_say_which_payment_paid_which_invoice(const std::vector<InvoiceLinkEvidenceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_the_export_does_not_say_which_payment_paid_which_invoice";
    }

    void examples_businessrule_how_a_payment_is_matched_to_the_invoice_it_paid(const std::vector<PaymentMatchPassString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_how_a_payment_is_matched_to_the_invoice_it_paid";
    }

    void examples_businessrule_what_encoding_a_qif_file_is_read_as(
            const std::vector<EncodingChoiceString>& values) {
        for (const auto& value : values) {
            const EncodingChoiceTyped t = EncodingChoiceTyped::from_string_struct(value);
            if (t.observed.find("byte order mark") != std::string::npos) {
                const std::string got = qif::decode("\xEF\xBB\xBF!Type:Bank");
                EXPECT_EQ("!Type:Bank", got) << t.observed;
                EXPECT_EQ("UTF8", t.readas);
            } else if (t.observed.find("Fails to decode") != std::string::npos) {
                const std::string bytes = "Caf\x96 shop";
                const std::string got = qif::decode(bytes);
                EXPECT_NE(bytes, got) << t.observed;
                EXPECT_EQ(got, qif::decode(got)) << "now valid UTF-8: " << t.observed;
                EXPECT_EQ("Windows1252", t.readas);
            } else {
                const std::string bytes = "Caf\xC3\xA9 shop";
                const std::string got = qif::decode(bytes);
                EXPECT_EQ(bytes, got) << t.observed;
                EXPECT_EQ("UTF8", t.readas);
            }
        }
    }

    // Built from a minimal two-line fixture: a real field, then the row's own
    // Line, so the test exercises the reader's own continuation decision rather
    // than restating it.
    void examples_businessrule_a_field_whose_value_will_not_parse_is_a_continuation_of_the_one_before(
            const std::vector<ContinuationLineString>& values) {
        for (const auto& value : values) {
            const ContinuationLineTyped t = ContinuationLineTyped::from_string_struct(value);
            if (t.section == "!Type:Invoice") {
                if (t.reading.find("date of the record") != std::string::npos) {
                    const qif::File file = qif::read("!Type:Invoice\n" + t.line + "\n^\n");
                    ASSERT_EQ(1u, file.records.size()) << t.line;
                    EXPECT_TRUE(dates::parse_qif(file.records.front().first('D'),
                                                 types::DateOrder::MDY).has_value())
                        << t.line;
                } else {
                    const qif::File file =
                        qif::read("!Type:Invoice\nD4/ 2'24\n" + t.line + "\n^\n");
                    ASSERT_EQ(1u, file.records.size()) << t.line;
                    const qif::Record& r = file.records.front();
                    EXPECT_EQ(1u, r.fields.count('D')) << t.line;
                    EXPECT_NE(std::string::npos, r.first('D').find(t.line)) << t.line;
                }
            } else if (t.section == "!Type:Cat") {
                const qif::File file = qif::read("!Type:Cat\nNFoo\n" + t.line + "\n^\n");
                ASSERT_EQ(1u, file.categories.size()) << t.line;
                EXPECT_EQ("Foo", file.categories.front().name) << t.line;
            } else {
                const qif::File file = qif::read("!Account\nNFoo\n" + t.line + "\n^\n");
                ASSERT_EQ(1u, file.accounts.size()) << t.line;
                EXPECT_EQ("Foo", file.accounts.front().name) << t.line;
            }
        }
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

    // "Ours": the real full import's own all-dates category report, which is
    // supposed to equal Quicken's own all-dates report because the cleared
    // marker is followed rather than second-guessed (see was_paid() in
    // qif_import.h). "Uncollected" is read straight off the raw invoice
    // records the cleared marker leaves out, independent of the importer, so
    // this does not just restate what the importer already does; "OnAccrual"
    // is checked as the arithmetic identity the Description states rather
    // than computed by any accrual-mode import, because the program has no
    // such mode and none is wanted -- see the decision in README.md.
    void examples_businessrule_what_the_cash_basis_is_worth_measured_against_the_all_dates_report(
            const std::vector<CashBasisTargetString>& values) {
        qif::Importer importer(types::DateOrder::MDY);
        importer.import_into("Assets:Business Checking");
        const qif::Imported real = importer.run(read_testdata_raw("pughkilleen.QIF"));
        reports::Spec spec;
        spec.from = types::Date(2000, 1, 1);
        spec.to = types::Date(2030, 12, 31);
        const reports::CategoryReport ours_report =
            reports::category_report(real.accounts, real.book, spec);

        const qif::File file = read_testdata_file("pughkilleen.QIF");
        std::map<std::string, Money> uncollected;
        for (const qif::Record& r : file.records) {
            if (r.section != qif::Section::InvoiceTxns) continue;
            if (!qif::detail::trim(r.first('C')).empty()) continue;  // collected
            const bool has_split_lines = r.has('$') && r.section != qif::Section::InvestmentTxns;
            const std::string l = r.first('L');
            const bool bracket = !l.empty() && l.front() == '[';
            if (bracket) continue;  // a payment, not a document
            std::string category;
            if (has_split_lines) {
                for (const std::string& s : r.all('S')) {
                    const std::string text = qif::detail::trim(s);
                    if (!text.empty()) { category = text; break; }
                }
            } else {
                category = l;
            }
            const auto amt = qif::parse_amount(r.first('T'), true);
            if (amt) uncollected[category] += *amt;
        }

        for (const auto& v : values) {
            const Money want_uncollected(v.uncollected);
            const auto at = uncollected.find(v.category);
            const Money got_uncollected = at != uncollected.end() ? at->second : Money();
            EXPECT_EQ(want_uncollected.cents(), got_uncollected.cents()) << v.category << " uncollected";

            Money ours;
            for (const auto& row : ours_report.rows)
                if (row.account == "Income:" + v.category) { ours = row.amount; break; }
            EXPECT_EQ(Money(v.ours).cents(), ours.cents()) << v.category << " ours (all-dates report)";

            EXPECT_EQ(Money(v.onaccrual).cents(),
                      Money(v.quickenalldates).cents() + want_uncollected.cents())
                << v.category << " accrual is all-dates plus uncollected";
        }
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

public:

    // What the migration made, in the order it made it.
    void then_transactions_are(const std::vector<TransactionString>& values) {
        ASSERT_EQ(values.size(), result_.transactions.size())
            << "transactions the file became: " << first_note();
        for (std::size_t i = 0; i < values.size(); ++i) {
            const ledger::Transaction& got = result_.transactions[i];
            const TransactionString& want = values[i];
            const std::string where = "transaction " + std::to_string(i + 1);
            if (want.ref.value != DNCString)
                EXPECT_EQ(want.ref.value, got.ref.value()) << where << " ref";
            if (want.date != DNCString)
                EXPECT_EQ(want.date, got.date.iso()) << where << " date";
            if (want.payee.value != DNCString)
                EXPECT_EQ(blank(want.payee), got.payee.value()) << where << " payee";
            if (want.checkno.value != DNCString)
                EXPECT_EQ(blank(want.checkno), got.check_no.value()) << where << " check number";
            if (want.memo != DNCString)
                EXPECT_EQ(blank(want.memo), got.memo) << where << " memo";
            if (want.tag != DNCString)
                EXPECT_EQ(blank(want.tag), got.tag) << where << " tag";
            if (want.sourceaccount.value != DNCString)
                EXPECT_EQ(blank(want.sourceaccount),
                          result_.accounts.path_for_ref(got.source_account))
                    << where << " source account";
            if (want.destination.value != DNCString)
                EXPECT_EQ(blank(want.destination),
                          result_.accounts.path_for_ref(got.destination))
                    << where << " destination";
            if (want.amount.value != DNCString)
                EXPECT_EQ(Money(want.amount).cents(), got.amount.cents()) << where << " amount";
            if (want.cleared.value != DNCString && !want.cleared.value.empty())
                EXPECT_EQ(want.cleared.value, types::to_string(got.cleared)) << where << " cleared";
        }
    }

    // The lines of the one split the file became. A scenario with more than one
    // split in it would have no way to say which lines were whose, so there is
    // never more than one.
    void then_split_lines_are(const std::vector<TransactionSplitString>& values) {
        const ledger::Transaction* owner = nullptr;
        for (const ledger::Transaction& t : result_.transactions)
            if (chart::is_split(t.destination)) owner = &t;
        ASSERT_NE(nullptr, owner) << "no split was made: " << first_note();
        const std::vector<ledger::TransactionSplit> got =
            ledger::split_lines_of(result_.splits, owner->id);
        ASSERT_EQ(values.size(), got.size()) << "number of split lines";
        for (std::size_t k = 0; k < values.size(); ++k) {
            const std::string where = "split line " + std::to_string(k + 1);
            EXPECT_EQ(blank(values[k].destination),
                      result_.accounts.path_for_ref(got[k].destination))
                << where << " destination";
            EXPECT_EQ(Money(values[k].amount).cents(), got[k].amount.cents())
                << where << " amount";
            if (values[k].memo != DNCString)
                EXPECT_EQ(blank(values[k].memo), got[k].memo) << where << " memo";
        }
    }

    void then_transactions_created_are(const std::vector<TransactionString>& values) {
        then_transactions_are(values);
    }

};
