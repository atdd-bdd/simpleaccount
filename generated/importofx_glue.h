#pragma once
#include <gtest/gtest.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include "common/common.h"
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "ofx_import.h"
#include "ofx_reader.h"
#include "posting.h"
#include "text_types.h"

// Glue for ImportOfx.spectable.
//
// Two conventions of the generated tables, stated once. An attribute whose
// Default is "none" arrives as the literal word, so a comparison has to read
// that as nothing; and a column a CompareOnly table does not name arrives as
// "?DNC?", which the generated string structs already treat as equal to
// anything -- so where a subset is the point, comparing string structs is the
// right way round rather than comparing fields by hand.
//
// What is not connected, and why. The transfer-candidate scenarios, the
// downloaded-account mapping and the ledger-balance check are specified but
// have no production code behind them, so their steps are still stubs that fail
// out loud, each naming what is missing. A stub that quietly passed would be
// worse than a failing one: the failure count is the only honest record of how
// far the specification runs ahead of the program.
class ImportOfxGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

    // ---------------------------------------------------------------- givens

    void given_the_file_contains(const std::string& value) {
        read_ = ofx::read(value);
    }

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

    // No scenario in this file reaches these. There is no Background here, so
    // "as previous" would establish nothing, and the tables are spelled out
    // instead. They are kept because the converter writes a glue file once and
    // never overwrites it, so a step dropped from the spec leaves its stub.
    void given_the_chart_of_accounts_is_as_previous() {}
    void given_account_mappings_are_as_previous() {}
    void given_transfer_match_rules_are_as_previous() {}

    // The postings already in the book, grouped into transactions by Ref. An
    // identifier in the FitId column is one an earlier Ofx import left.
    void given_postings_are(const std::vector<ImportedPostingRowString>& values) {
        transactions_.clear();
        for (const auto& value : values) {
            ledger::Transaction* into = nullptr;
            for (ledger::Transaction& t : transactions_)
                if (t.ref.value() == value.ref) into = &t;
            if (into == nullptr) {
                ledger::Transaction made;
                made.ref = types::TransactionRef(value.ref);
                const auto on = types::Date::from_iso(value.date);
                ASSERT_TRUE(on.has_value()) << value.date;
                made.date = *on;
                made.payee = types::PayeeName(blank(value.payee));
                made.check_no = types::CheckNumber(blank(value.checkno));
                transactions_.push_back(made);
                into = &transactions_.back();
            }
            ledger::Posting p;
            p.account = types::AccountPath(value.account);
            p.amount = Money(value.amount);
            p.cleared = types::cleared_status_from_string(value.cleared);
            ledger::stamp(&p, types::ImportSource::Ofx, blank(value.fitid));
            into->postings.push_back(p);
        }
    }

    void given_import_target_is(const std::vector<ImportTargetString>& values) {
        ASSERT_FALSE(values.empty());
        target_ = values.front().accountpath;
    }

    // ----------------------------------------------------------------- whens

    void when_transactions_imported(const std::vector<OfxTransactionString>& values) {
        ofx::Statement statement;
        for (const auto& value : values) statement.transactions.push_back(downloaded(value));
        import(statement);
    }

    // Which offered row is being answered, and what the answer is. A row not
    // named here is left alone, which is the whole point of offering it.
    void when_the_possible_duplicate_is_answered(
            const std::vector<PossibleResolutionString>& values) {
        for (const auto& value : values) {
            const ofx::Decided* offered = nullptr;
            for (const ofx::Decided& one : decided_.decided)
                if (one.disposition == ofx::Disposition::Possible &&
                    one.downloaded.fit_id == value.fitid)
                    offered = &one;
            ASSERT_NE(nullptr, offered) << "nothing possible was offered for " << value.fitid;
            if (value.answer == "TheSame") {
                EXPECT_TRUE(ofx::answer_the_same(*offered, target_, &transactions_))
                    << "could not clear " << offered->claimed_ref;
            } else if (value.answer == "Different") {
                const ledger::Transaction made = ofx::answer_different(
                    *offered, target_, transactions_.size(), &chart_);
                ASSERT_FALSE(made.postings.empty()) << "nothing was created for " << value.fitid;
                transactions_.push_back(made);
            } else {
                FAIL() << "there is no answer called " << value.answer;
            }
        }
    }

    // ----------------------------------------------------------------- thens

    void then_statement_is(const std::vector<OfxStatementString>& values) {
        ASSERT_FALSE(read_.refused) << read_.refusal;
        ASSERT_EQ(values.size(), read_.statements.size()) << "statements in the file";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const ofx::Statement& got = read_.statements[i];
            const OfxStatementString& want = values[i];
            EXPECT_EQ(blank(want.bankid), got.bank_id) << "statement " << i << " BANKID";
            EXPECT_EQ(blank(want.ofxaccountid), got.account_id)
                << "statement " << i << " ACCTID";
            EXPECT_EQ(want.ofxaccttype, ofx::to_string(got.acct_type))
                << "statement " << i << " ACCTTYPE";
            EXPECT_EQ(want.currency, got.currency) << "statement " << i << " CURDEF";
            EXPECT_EQ(want.startdate, got.start_date.iso()) << "statement " << i << " DTSTART";
            EXPECT_EQ(want.enddate, got.end_date.iso()) << "statement " << i << " DTEND";
            EXPECT_EQ(Money(want.ledgerbal).cents(), got.ledger_balance.cents())
                << "statement " << i << " BALAMT";
            EXPECT_EQ(want.ledgerbalat, got.ledger_balance_at.iso())
                << "statement " << i << " DTASOF";
        }
    }

    void then_transactions_read_are(const std::vector<OfxTransactionString>& values) {
        ASSERT_FALSE(read_.refused) << read_.refusal;
        ASSERT_FALSE(read_.statements.empty()) << "no statement was read";
        const std::vector<ofx::Transaction>& got = read_.statements.front().transactions;
        ASSERT_EQ(values.size(), got.size()) << "transactions in the statement";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].trntype, got[i].trn_type) << "transaction " << i << " TRNTYPE";
            EXPECT_EQ(values[i].dateposted, got[i].date_posted.iso())
                << "transaction " << i << " DTPOSTED";
            EXPECT_EQ(Money(values[i].amount).cents(), got[i].amount.cents())
                << "transaction " << i << " TRNAMT";
            EXPECT_EQ(blank(values[i].fitid), got[i].fit_id) << "transaction " << i << " FITID";
            EXPECT_EQ(blank(values[i].name), got[i].name) << "transaction " << i << " NAME";
            EXPECT_EQ(blank(values[i].memo), got[i].memo) << "transaction " << i << " MEMO";
            EXPECT_EQ(blank(values[i].checknum), got[i].check_num)
                << "transaction " << i << " CHECKNUM";
        }
    }

    // A file that is not OFX is reported rather than guessed at, so the refusal
    // is the observable thing -- and a refused file must have yielded nothing.
    void then_rejected_because(const std::vector<RejectionString>& values) {
        ASSERT_FALSE(values.empty());
        EXPECT_TRUE(read_.refused) << "the file was accepted";
        EXPECT_TRUE(read_.statements.empty()) << "a refused file still yielded a statement";
        EXPECT_EQ(values.front().reason, read_.refusal);
    }

    void then_import_summary_is(const std::vector<ImportSummaryString>& values) {
        ASSERT_FALSE(values.empty());
        ImportSummaryString got;
        got.new_ = std::to_string(summary_.New);
        got.duplicate = std::to_string(summary_.Duplicate);
        got.matched = std::to_string(summary_.Matched);
        got.possible = std::to_string(summary_.Possible);
        EXPECT_EQ(values.front(), got) << "wanted " << values.front().to_string()
                                       << " but the import decided " << got.to_string();
    }

    void then_import_target_is(const std::vector<ImportTargetString>& values) {
        ASSERT_FALSE(values.empty());
        EXPECT_EQ(values.front().accountpath, target_);
    }

    void then_postings_are(const std::vector<ImportedPostingRowString>& values) {
        const std::vector<ImportedPostingRowString> got = flatten();
        ASSERT_EQ(values.size(), got.size()) << "postings in the book:\n" << listing(got);
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].ref, got[i].ref) << "posting " << i << " ref";
            EXPECT_EQ(values[i].date, got[i].date) << "posting " << i << " date";
            EXPECT_EQ(blank(values[i].payee), blank(got[i].payee))
                << "posting " << i << " payee";
            EXPECT_EQ(values[i].account, got[i].account) << "posting " << i << " account";
            EXPECT_EQ(Money(values[i].amount).cents(), Money(got[i].amount).cents())
                << "posting " << i << " amount";
            EXPECT_EQ(values[i].cleared, got[i].cleared) << "posting " << i << " cleared";
            EXPECT_EQ(blank(values[i].checkno), blank(got[i].checkno))
                << "posting " << i << " check number";
            EXPECT_EQ(blank(values[i].fitid), blank(got[i].fitid))
                << "posting " << i << " FITID";
        }
    }

    void then_balances_are(const std::vector<AccountBalanceString>& values) {
        const ledger::Ledger ledger = as_ledger();
        for (const auto& value : values) {
            const chart::Account* a = chart_.find(value.account);
            ASSERT_NE(nullptr, a) << "no account " << value.account;
            EXPECT_EQ(Money(value.rawbalance).cents(),
                      ledger.raw_balance(value.account).cents())
                << value.account << " raw -- " << value.notes;
            EXPECT_EQ(Money(value.displaybalance).cents(),
                      ledger.display_balance(value.account, a->type).cents())
                << value.account << " display -- " << value.notes;
        }
    }

    // ------------------------------------------------------- example tables

    void examples_datatype_ofxaccttype(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.value, ofx::to_string(ofx::acct_type_from(value.value)))
                << value.value << " does not survive being read -- " << value.notes;
    }

    // There is no enumeration behind a transaction type: the program carries
    // whatever the bank wrote and decides nothing from it. So what there is to
    // check is that every listed type survives being read unchanged -- which is
    // also what makes a type nobody listed harmless.
    void examples_datatype_ofxtrntype(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const ofx::Read got = ofx::read(one_transaction_file(value.value, "-1.00"));
            ASSERT_FALSE(got.refused) << got.refusal;
            ASSERT_EQ(1u, got.statements.size()) << value.value;
            ASSERT_EQ(1u, got.statements.front().transactions.size()) << value.value;
            EXPECT_EQ(value.value, got.statements.front().transactions.front().trn_type)
                << value.value << " -- " << value.notes;
        }
    }

    void examples_datatype_importdisposition(
            const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.value, ofx::to_string(disposition_named(value.value)))
                << value.value << " -- " << value.notes;
    }

    void examples_businessrule_an_ofx_date_is_read_to_the_day(
            const std::vector<OfxDateParseString>& values) {
        for (const auto& value : values) {
            const auto got = ofx::detail::date_of(value.text);
            ASSERT_TRUE(got.has_value()) << value.text << " was not read at all";
            EXPECT_EQ(value.date, got->iso()) << value.text << " -- " << value.notes;
        }
    }

    // The bank's own amount becomes the posting amount, whatever kind of account
    // the statement is for. The account type is in the table to show that it
    // changes nothing, so each row is driven through an account of that kind.
    void examples_businessrule_the_amount_in_an_ofx_file_is_used_as_the_posting_amount(
            const std::vector<OfxAmountToPostingString>& values) {
        for (const auto& value : values) {
            const bool card = value.ofxaccttype == "CREDITCARD";
            const std::string account = card ? "Liabilities:Card" : "Assets:Account";
            start_fresh(account, card ? types::AccountType::CreditCard
                                      : types::AccountType::Bank);
            ofx::Statement statement;
            statement.acct_type = ofx::acct_type_from(value.ofxaccttype);
            ofx::Transaction t;
            t.date_posted = types::Date(2024, 1, 15);
            t.amount = Money(value.trnamt);
            t.fit_id = "AMT1";
            t.name = "WHOEVER";
            statement.transactions.push_back(t);
            import(statement);

            const ledger::Posting* here = posting_in(account);
            ASSERT_NE(nullptr, here) << value.ofxaccttype << " " << value.trnamt
                                     << " produced no posting in " << account;
            EXPECT_EQ(Money(value.postingamount).cents(), here->amount.cents())
                << value.ofxaccttype << " " << value.trnamt << " -- " << value.notes;
        }
    }

    // The tests are tried in order and the first that applies decides, so every
    // row is checked by building the book that reaches exactly that row and
    // stops there -- the behaviour the row describes, not a reading of the code.
    void examples_businessrule_how_a_downloaded_transaction_is_identified(
            const std::vector<DispositionRuleString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.disposition, ofx::to_string(disposition_reaching_test(value.test)))
                << "test " << value.test << ": " << value.condition;
    }

    // An identifier is only an identifier to the kind of file that gave it.
    // Every row is driven through an Ofx import of one row carrying F100
    // against one posting already there, stamped as the row says. The posting is
    // cleared, so the match tests cannot claim it and the only question left is
    // the one about identifiers.
    //
    // The Then column is a disposition on the rows where test one applies and a
    // phrase on the rows where it does not, so what is asserted is the
    // distinction the rule is about: whether the identifier settled it.
    void examples_businessrule_an_identifier_is_only_an_identifier_to_the_file_that_gave_it(
            const std::vector<IdentifierScopeString>& values) {
        for (const auto& value : values) {
            const ledger::Posting here = already_there(value.postingcarries);

            // The one row stated about a Csv import, which has no import of its
            // own yet. What it turns on is that a stamp is found under the
            // source that wrote it and under no other, which is the whole of
            // the rule and is checkable now.
            if (value.importis == "Csv") {
                EXPECT_NE(nullptr, ledger::id_from(here, types::ImportSource::Csv))
                    << "a Csv id should be visible to a Csv import";
                EXPECT_EQ(nullptr, ledger::id_from(here, types::ImportSource::Ofx))
                    << "a Csv id should be invisible to an Ofx import";
                continue;
            }

            import(identified_statement());
            ASSERT_EQ(1u, decided_.decided.size());
            const std::string got = ofx::to_string(decided_.decided.front().disposition);
            const bool settled_by_the_identifier = value.testoneapplies.rfind("Yes", 0) == 0;
            if (settled_by_the_identifier) {
                EXPECT_EQ("Duplicate", got)
                    << value.postingcarries << ", imported as " << value.importis
                    << " -- then: " << value.then;
            } else {
                EXPECT_NE("Duplicate", got)
                    << value.postingcarries << ", imported as " << value.importis
                    << " -- then: " << value.then;
            }
        }
    }

    // All three answers, driven through the same offered row.
    void examples_businessrule_a_possible_duplicate_is_the_only_disposition_that_waits(
            const std::vector<PossibleAnswerString>& values) {
        for (const auto& value : values) {
            offer_a_possible();
            ASSERT_EQ(1, summary_.Possible) << "nothing was offered to answer";
            const ofx::Decided offered = decided_.decided.front();
            const std::size_t before = transactions_.size();

            if (value.answer == "TheSame") {
                ASSERT_TRUE(ofx::answer_the_same(offered, target_, &transactions_));
                EXPECT_EQ(before, transactions_.size()) << value.thedownloadedrow;
                const ledger::Posting* here = posting_in(target_);
                ASSERT_NE(nullptr, here);
                EXPECT_EQ(types::ClearedStatus::Cleared, here->cleared)
                    << value.theonealreadythere;
                const std::string* id = ledger::id_from(*here, types::ImportSource::Ofx);
                ASSERT_NE(nullptr, id) << value.theonealreadythere;
                EXPECT_EQ("F003", *id) << value.theonealreadythere;
            } else if (value.answer == "Different") {
                const ledger::Transaction made = ofx::answer_different(
                    offered, target_, transactions_.size(), &chart_);
                ASSERT_FALSE(made.postings.empty()) << value.thedownloadedrow;
                transactions_.push_back(made);
                EXPECT_EQ(before + 1, transactions_.size()) << value.thedownloadedrow;
                // And nothing happened to the one already there.
                const ledger::Posting* first = posting_in(target_);
                ASSERT_NE(nullptr, first);
                EXPECT_EQ(nullptr, ledger::id_from(*first, types::ImportSource::Ofx))
                    << value.theonealreadythere;
            } else if (value.answer == "Unanswered") {
                // Nothing is called at all, which is the row being specified.
                EXPECT_EQ(before, transactions_.size()) << value.thedownloadedrow;
                const ledger::Posting* here = posting_in(target_);
                ASSERT_NE(nullptr, here);
                EXPECT_EQ(nullptr, ledger::id_from(*here, types::ImportSource::Ofx))
                    << value.theonealreadythere;
                // And the same file reaches the same question again.
                const ofx::Imported again =
                    ofx::decide(offered_statement(), target_, transactions_);
                EXPECT_EQ(1, again.summary.Possible) << "the question was not asked again";
            } else {
                FAIL() << "there is no answer called " << value.answer;
            }
        }
    }

    // ------------------------------------------- specified, not yet written

    void given_account_mappings_are(const std::vector<AccountMappingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: mapping a downloaded account to one of ours";
    }

    void when_statement_imported(const std::vector<OfxStatementString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: mapping a downloaded account to one of ours";
    }

    void then_asked_to_choose_an_account_for(const std::vector<UnmappedAccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: mapping a downloaded account to one of ours";
    }

    void when_statements_imported(const std::vector<OfxImportFileString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: importing more than one statement at once";
    }

    void then_transfer_candidates_are(const std::vector<TransferCandidateString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: pairing two downloaded rows as one transfer";
    }

    void when_candidate_approved(const std::vector<TransferCandidateString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: pairing two downloaded rows as one transfer";
    }

    void when_candidate_rejected(const std::vector<TransferCandidateString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: pairing two downloaded rows as one transfer";
    }

    void then_rule_offered_is(const std::vector<TransferMatchRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: remembering a transfer pairing";
    }

    void given_transfer_match_rules_are(const std::vector<TransferMatchRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: remembering a transfer pairing";
    }

    void then_confirmation_dialog_shows(const std::vector<ConfirmationRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: the import confirmation dialog";
    }

    void then_import_preview_is(const std::vector<ImportPreviewString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: the import preview";
    }

    void when_ledger_balance_checked(const std::vector<LedgerBalanceCheckString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: checking the import against the balance the bank states";
    }

    void then_balance_check_is(const std::vector<LedgerBalanceResultString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: checking the import against the balance the bank states";
    }

    void when_report_run(const std::vector<ReportSpecString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: running a report over an import in this file";
    }

    void then_report_rows_are(const std::vector<ReportRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: running a report over an import in this file";
    }

    void examples_datatype_transfercandidatestate(
            const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: pairing two downloaded rows as one transfer";
    }

    void examples_businessrule_when_two_new_transactions_are_proposed_as_one_transfer(
            const std::vector<TransferCandidateRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: pairing two downloaded rows as one transfer";
    }

    void examples_businessrule_the_two_date_windows_and_why_they_differ(
            const std::vector<DateWindowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: pairing two downloaded rows as one transfer";
    }

    void examples_businessrule_which_half_pairs_with_which_when_several_match(
            const std::vector<CandidatePriorityString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: pairing two downloaded rows as one transfer";
    }

private:
    ofx::Read read_;
    chart::Chart chart_;
    std::vector<ledger::Transaction> transactions_;
    std::string target_;
    ofx::Imported decided_;
    ofx::Summary summary_;

    // A Default of "none" arrives as the literal word.
    static std::string blank(const std::string& s) {
        return (s == "none" || s == DNCString) ? std::string() : s;
    }

    static chart::Account account_of(const std::string& path, types::AccountType type) {
        chart::Account a;
        a.path = types::AccountPath(path);
        a.type = type;
        return a;
    }

    // The book an example row starts from: one real account, somewhere for the
    // other side to go, and nothing in it.
    void start_fresh(const std::string& account, types::AccountType type) {
        chart_ = chart::Chart();
        chart_.put(account_of(account, type));
        chart_.put(account_of("Expenses:Uncategorized", types::AccountType::Expense));
        transactions_.clear();
        target_ = account;
    }

    static ofx::Transaction downloaded(const OfxTransactionString& value) {
        ofx::Transaction t;
        t.trn_type = value.trntype;
        const auto on = types::Date::from_iso(value.dateposted);
        EXPECT_TRUE(on.has_value()) << value.dateposted;
        if (on.has_value()) t.date_posted = *on;
        t.amount = Money(value.amount);
        t.fit_id = blank(value.fitid);
        t.name = blank(value.name);
        t.memo = blank(value.memo);
        t.check_num = blank(value.checknum);
        return t;
    }

    // One import, in the order the program does it: decide, create what is new,
    // then clear what was matched. Nothing is written for a duplicate or for a
    // possible duplicate, which is what the summary then shows.
    void import(const ofx::Statement& statement) {
        decided_ = ofx::decide(statement, target_, transactions_);
        summary_ = decided_.summary;
        const std::vector<ledger::Transaction> made =
            ofx::transactions_for(decided_, target_, transactions_.size(), &chart_);
        ofx::apply_matches(decided_, target_, &transactions_);
        transactions_.insert(transactions_.end(), made.begin(), made.end());
    }

    const ledger::Posting* posting_in(const std::string& account) const {
        for (const ledger::Transaction& t : transactions_)
            for (const ledger::Posting& p : t.postings)
                if (p.account.value() == account) return &p;
        return nullptr;
    }

    ledger::Ledger as_ledger() const {
        ledger::Ledger out;
        for (const ledger::Transaction& t : transactions_)
            for (const ledger::Posting& p : t.postings)
                out.add({t.date, p.account, p.amount, t.ref.value()});
        return out;
    }

    std::vector<ImportedPostingRowString> flatten() const {
        std::vector<ImportedPostingRowString> out;
        for (const ledger::Transaction& t : transactions_) {
            for (const ledger::Posting& p : t.postings) {
                ImportedPostingRowString row;
                row.ref = t.ref.value();
                row.date = t.date.iso();
                row.payee = t.payee.value().empty() ? "none" : t.payee.value();
                row.account = p.account.value();
                row.amount = p.amount.in_register();
                row.cleared = types::to_string(p.cleared);
                row.checkno = t.check_no.value().empty() ? "none" : t.check_no.value();
                const std::string* id = ledger::id_from(p, types::ImportSource::Ofx);
                row.fitid = id == nullptr || id->empty() ? "none" : *id;
                out.push_back(row);
            }
        }
        return out;
    }

    static std::string listing(const std::vector<ImportedPostingRowString>& rows) {
        std::string out;
        for (const auto& row : rows) out += "  " + row.to_string() + "\n";
        return out;
    }

    static ofx::Disposition disposition_named(const std::string& name) {
        if (name == "Duplicate") return ofx::Disposition::Duplicate;
        if (name == "Matched")   return ofx::Disposition::Matched;
        if (name == "Possible")  return ofx::Disposition::Possible;
        return ofx::Disposition::New;
    }

    // The smallest file that holds one transaction, for the rows that are about
    // what survives being read rather than about a whole statement.
    static std::string one_transaction_file(const std::string& trn_type,
                                            const std::string& amount) {
        return "OFXHEADER:100\n<OFX><BANKMSGSRSV1><STMTTRNRS><STMTRS>\n"
               "<BANKACCTFROM><BANKID>1<ACCTID>2<ACCTTYPE>CHECKING</BANKACCTFROM>\n"
               "<BANKTRANLIST><DTSTART>20240101<DTEND>20240131\n"
               "<STMTTRN><TRNTYPE>" + trn_type + "<DTPOSTED>20240115<TRNAMT>" + amount +
               "<FITID>X1<NAME>WHOEVER</STMTTRN>\n"
               "</BANKTRANLIST></STMTRS></STMTTRNRS></BANKMSGSRSV1></OFX>";
    }

    // --- the identifier-scope rows ----------------------------------------

    // One downloaded row carrying F100, which is the identifier every row of
    // that table is about.
    static ofx::Statement identified_statement() {
        ofx::Statement statement;
        ofx::Transaction row;
        row.date_posted = types::Date(2024, 1, 15);
        row.amount = Money("-45.00");
        row.fit_id = "F100";
        row.name = "HARRIS TEETER";
        statement.transactions.push_back(row);
        return statement;
    }

    // The posting already in the book, stamped as the row says. Returned by
    // value so a row can be asked what it carries without reaching back into
    // the book, which is what the Csv row needs.
    ledger::Posting already_there(const std::string& carries) {
        start_fresh("Assets:Checking", types::AccountType::Bank);
        ledger::Transaction t;
        t.ref = types::TransactionRef("T1");
        t.date = types::Date(2024, 1, 15);
        t.payee = types::PayeeName("HARRIS TEETER");

        ledger::Posting here;
        here.account = types::AccountPath("Assets:Checking");
        here.amount = Money("-45.00");
        // Cleared, so neither match test can claim it and the only question
        // left is the one about identifiers.
        here.cleared = types::ClearedStatus::Cleared;
        if (carries == "An Ofx id, the same") {
            ledger::stamp(&here, types::ImportSource::Ofx, "F100");
        } else if (carries == "An Ofx id, a different one") {
            ledger::stamp(&here, types::ImportSource::Ofx, "F999");
        } else if (carries == "A Csv id only") {
            ledger::stamp(&here, types::ImportSource::Csv, "F100");
        } else if (carries == "A Csv id and an Ofx id") {
            ledger::stamp(&here, types::ImportSource::Csv, "F100");
            ledger::stamp(&here, types::ImportSource::Ofx, "F100");
        } else {
            EXPECT_EQ("Nothing", carries) << "unlisted case";
        }

        ledger::Posting there;
        there.account = types::AccountPath("Expenses:Uncategorized");
        there.amount = Money("45.00");
        t.postings = {here, there};
        transactions_ = {t};
        return here;
    }

    // --- the possible-duplicate rows --------------------------------------

    // The downloaded row every answer is given about.
    static ofx::Statement offered_statement() {
        ofx::Statement statement;
        ofx::Transaction row;
        row.date_posted = types::Date(2026, 2, 5);
        row.amount = Money("-88.20");
        row.fit_id = "F003";
        row.name = "HARRIS TEETER";
        statement.transactions.push_back(row);
        return statement;
    }

    // A book holding one cleared transaction of the same date, amount and payee
    // and no identifier, which is the one case the program cannot settle.
    void offer_a_possible() {
        start_fresh("Assets:Checking", types::AccountType::Bank);
        chart_.put(account_of("Expenses:Groceries", types::AccountType::Expense));
        ledger::Transaction t;
        t.ref = types::TransactionRef("T1");
        t.date = types::Date(2026, 2, 5);
        t.payee = types::PayeeName("Harris Teeter");
        ledger::Posting here;
        here.account = types::AccountPath("Assets:Checking");
        here.amount = Money("-88.20");
        here.cleared = types::ClearedStatus::Cleared;
        ledger::Posting there;
        there.account = types::AccountPath("Expenses:Groceries");
        there.amount = Money("88.20");
        t.postings = {here, there};
        transactions_ = {t};
        import(offered_statement());
    }

    // --- the disposition-rule rows ----------------------------------------

    // The book that makes the import reach exactly the numbered test and stop
    // there, so each row of the rule table is checked by the behaviour it
    // describes.
    ofx::Disposition disposition_reaching_test(const std::string& test) {
        start_fresh("Assets:Checking", types::AccountType::Bank);
        ofx::Transaction row;
        row.date_posted = types::Date(2024, 1, 20);
        row.amount = Money("-45.00");
        row.fit_id = "F001";
        row.name = "HARRIS TEETER";

        ledger::Transaction t;
        t.ref = types::TransactionRef("T1");
        t.payee = types::PayeeName("HARRIS TEETER");
        ledger::Posting here;
        here.account = types::AccountPath("Assets:Checking");
        here.amount = Money("-45.00");
        ledger::Posting there;
        there.account = types::AccountPath("Expenses:Uncategorized");
        there.amount = Money("45.00");

        bool anything_already_there = true;
        if (test == "1") {
            // Already carries this FITID from an Ofx import.
            t.date = types::Date(2024, 1, 20);
            here.cleared = types::ClearedStatus::Cleared;
            ledger::stamp(&here, types::ImportSource::Ofx, "F001");
        } else if (test == "2") {
            // Uncleared, same amount and check number, inside the window.
            //
            // At the default window of one day this cannot be told apart from
            // test three, whose window on amount alone is five days and wholly
            // contains it. That is what the book's check-number window is for,
            // and the scenario that widens it to thirty days is where the
            // number does work the amount cannot. Here the claim is only the
            // one the rule table makes: this condition yields Matched.
            t.date = types::Date(2024, 1, 20);
            t.check_no = types::CheckNumber("2041");
            row.check_num = "2041";
        } else if (test == "3") {
            // Uncleared, same amount, five days earlier, no check number.
            t.date = types::Date(2024, 1, 15);
        } else if (test == "4") {
            // Cleared, same amount and date, no identifier: nothing above can
            // apply, and it is too like the downloaded row to create blindly.
            t.date = types::Date(2024, 1, 20);
            here.cleared = types::ClearedStatus::Cleared;
        } else {
            // Test 5: an empty book, so nothing above applies.
            anything_already_there = false;
        }

        if (anything_already_there) {
            t.postings = {here, there};
            transactions_ = {t};
        }
        ofx::Statement statement;
        statement.transactions.push_back(row);
        import(statement);
        EXPECT_EQ(1u, decided_.decided.size()) << "test " << test;
        return decided_.decided.empty() ? ofx::Disposition::New
                                        : decided_.decided.front().disposition;
    }
public:

    void then_import_notes_are(const std::vector<ImportNoteString>& values) {
        ASSERT_EQ(values.size(), decided_.notes.size()) << "notes written: " << all_notes();
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].kind, decided_.notes[i].kind) << "note " << i << " kind";
            EXPECT_EQ(values[i].detail, decided_.notes[i].detail) << "note " << i << " detail";
        }
    }

    void then_import_finishes_with(const std::vector<ImportCompletionString>& values) {
        ASSERT_FALSE(values.empty());
        EXPECT_EQ(values.front().notecount, std::to_string(decided_.notes.size()))
            << "notes written: " << all_notes();
        EXPECT_EQ(blank(values.front().summary), ofx::completion_of(decided_));
    }

    // Each row is driven through one check already in the book, dated the
    // second, and one downloaded row that many days later.
    void examples_businessrule_a_check_number_is_trusted_for_a_day_and_no_longer(
            const std::vector<CheckNumberWindowString>& values) {
        for (const auto& value : values) {
            const int days = std::stoi(value.daysapart);
            start_fresh("Assets:Checking", types::AccountType::Bank);

            ledger::Transaction t;
            t.ref = types::TransactionRef("T1");
            t.date = types::Date(2024, 1, 2);
            t.payee = types::PayeeName("County Tax Office");
            t.check_no = types::CheckNumber("2041");
            ledger::Posting here;
            here.account = types::AccountPath("Assets:Checking");
            here.amount = Money("-1250.00");
            ledger::Posting there;
            there.account = types::AccountPath("Expenses:Uncategorized");
            there.amount = Money("1250.00");
            t.postings = {here, there};
            transactions_ = {t};

            ofx::Statement statement;
            ofx::Transaction row;
            row.trn_type = "CHECK";
            row.date_posted = types::Date::from_day_number(
                types::Date::day_number(types::Date(2024, 1, 2)) + days);
            row.amount = Money("-1250.00");
            row.fit_id = "FW1";
            row.name = "COUNTY TAX OFFICE";
            // A number that does not match is a different check entirely.
            row.check_num = value.numbermatches == "Yes" ? "2041" : "9999";
            statement.transactions.push_back(row);
            import(statement);

            ASSERT_EQ(1u, decided_.decided.size());
            const bool matched =
                decided_.decided.front().disposition == ofx::Disposition::Matched;
            EXPECT_EQ(value.matched == "Yes", matched)
                << days << " days apart, number matches " << value.numbermatches
                << " -- " << value.notes << "; the import decided "
                << ofx::to_string(decided_.decided.front().disposition);
            EXPECT_EQ(value.notewritten == "Yes", !decided_.notes.empty())
                << days << " days apart -- " << value.notes
                << "; notes written: " << all_notes();
        }
    }

    std::string all_notes() const {
        if (decided_.notes.empty()) return "(none)";
        std::string out;
        for (const ofx::Note& n : decided_.notes) out += "[" + n.kind + "] " + n.detail + "; ";
        return out;
    }
public:

    void given_the_book_setting_is(const std::vector<BookSettingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_book_setting_is";
    }

};
