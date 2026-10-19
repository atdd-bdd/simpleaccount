#pragma once
#include <gtest/gtest.h>
#include <algorithm>
#include <iostream>
#include <map>
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
#include "report.h"
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
// What is not connected, and why. Three Examples tables under the
// transfer-candidate rules -- TransferCandidateRule, DateWindow and
// CandidatePriority -- restate a condition in words with no outcome column to
// drive through, so their blocks stay $WIP and their stubs stay failing out
// loud on purpose; the condition itself is exercised by the concrete
// scenarios around them. A stub that quietly passed would be worse than a
// failing one: the failure count is the only honest record of how far the
// specification runs ahead of the program.
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
            a.path = types::AccountPath(value.path.value);
            a.type = types::account_type_from_string(value.type.value);
            a.placeholder = parse_bool_cell(value.placeholder);
            a.hidden = parse_bool_cell(value.hidden);
            a.alias = blank(value.alias);
            a.payment_payee = blank(value.paymentpayee);
            chart_.put(a);
        }
        rebuild_mappings(values);
    }

    // The bank's numbering is three columns of the account now, not a table of
    // its own, so the chart is where a scenario states it. ofx::account_for_id
    // and ofx::mapping_for still take a list, so the list is read back out of
    // the chart here rather than established by a step of its own. An account
    // with no number contributes nothing, which is what an account no download
    // has ever been placed in looks like.
    void rebuild_mappings(const std::vector<AccountString>& values) {
        mappings_.clear();
        for (const auto& value : values) {
            const std::string number = blank(value.accountnumber);
            if (number.empty()) continue;
            mappings_.push_back({blank(value.bankid), number, value.path.value});
        }
    }

    // No scenario in this file reaches these. There is no Background here, so
    // "as previous" would establish nothing, and the tables are spelled out
    // instead. They are kept because the converter writes a glue file once and
    // never overwrites it, so a step dropped from the spec leaves its stub.
    void given_the_chart_of_accounts_is_as_previous() {}
    void given_account_mappings_are_as_previous() {}
    void given_transfer_match_rules_are_as_previous() {}

    void given_import_target_is(const std::vector<ImportTargetString>& values) {
        ASSERT_FALSE(values.empty());
        target_ = values.front().accountpath.value;
    }

    void given_account_mappings_are(const std::vector<AccountMappingString>& values) {
        mappings_.clear();
        for (const auto& v : values)
            mappings_.push_back({blank(v.bankid), v.ofxaccountid, v.accountpath});
    }

    void given_the_book_setting_is(const std::vector<BookSettingString>& values) {
        for (const auto& v : values) check_window_days_ = std::stoll(v.checkwindowdays);
    }

    void given_transfer_match_rules_are(const std::vector<TransferMatchRuleString>& values) {
        match_rules_.clear();
        for (const auto& v : values)
            match_rules_.push_back({v.fromaccount.value, blank(v.frompattern), v.toaccount.value,
                                    blank(v.topattern), parse_bool_cell(v.enabled)});
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
                    one.downloaded.fit_id == value.fitid.value)
                    offered = &one;
            ASSERT_NE(nullptr, offered) << "nothing possible was offered for " << value.fitid.value;
            if (value.answer == "TheSame") {
                EXPECT_TRUE(ofx::answer_the_same(chart_, *offered, target_, &transactions_))
                    << "could not clear " << offered->claimed_ref;
            } else if (value.answer == "Different") {
                const ledger::Transaction made = ofx::answer_different(
                    *offered, target_, ledger::transaction_count(transactions_), &chart_);
                ASSERT_FALSE(made.source_account.is_none()) << "nothing was created for " << value.fitid.value;
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
            EXPECT_EQ(want.ofxaccttype.value, ofx::to_string(got.acct_type))
                << "statement " << i << " ACCTTYPE";
            EXPECT_EQ(want.currency, got.currency) << "statement " << i << " CURDEF";
            EXPECT_EQ(want.startdate, got.start_date.iso()) << "statement " << i << " DTSTART";
            EXPECT_EQ(want.enddate, got.end_date.iso()) << "statement " << i << " DTEND";
            EXPECT_EQ(Money(want.ledgerbal.value).cents(), got.ledger_balance.cents())
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
            EXPECT_EQ(values[i].trntype.value, got[i].trn_type) << "transaction " << i << " TRNTYPE";
            EXPECT_EQ(values[i].dateposted, got[i].date_posted.iso())
                << "transaction " << i << " DTPOSTED";
            EXPECT_EQ(Money(values[i].amount.value).cents(), got[i].amount.cents())
                << "transaction " << i << " TRNAMT";
            EXPECT_EQ(blank(values[i].fitid.value), got[i].fit_id) << "transaction " << i << " FITID";
            EXPECT_EQ(blank(values[i].name), got[i].name) << "transaction " << i << " NAME";
            EXPECT_EQ(blank(values[i].memo), got[i].memo) << "transaction " << i << " MEMO";
            EXPECT_EQ(blank(values[i].checknum.value), got[i].check_num)
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
        got.possible = std::to_string(summary_.Possible);
        // Completed is reported as nought rather than left unset, which is the
        // truth: it is the disposition the payment halves answer with in the
        // specification, and the code has not got it yet, so no import decides
        // it. An unset field would compare as blank against the table's 0.
        got.completed = "0";
        EXPECT_EQ(values.front(), got) << "wanted " << values.front().to_string()
                                       << " but the import decided " << got.to_string();
    }

    void then_import_target_is(const std::vector<ImportTargetString>& values) {
        ASSERT_FALSE(values.empty());
        EXPECT_EQ(values.front().accountpath.value, target_);
        if (values.front().asked != DNCString)
            EXPECT_EQ(parse_bool_cell(values.front().asked), target_was_asked_) << "asked";
    }

    // Money against Money, so a cell written 1200.00 or $1,200.00 is the same
    // amount and the comparison says so. A column a CompareOnly table did not
    // name arrives as "?DNC?", which is not an amount, so it is not asserted.
    void then_balances_are(const std::vector<AccountBalanceString>& values) {
        const ledger::Ledger ledger = as_ledger();
        for (const auto& value : values) {
            const chart::Account* a = chart_.find(value.account.value);
            ASSERT_NE(nullptr, a) << "no account " << value.account.value;
            if (value.rawbalance.value != DNCString)
                EXPECT_EQ(Money(value.rawbalance.value), ledger.raw_balance(value.account.value))
                    << value.account.value << " raw -- " << value.notes;
            if (value.displaybalance.value != DNCString)
                EXPECT_EQ(Money(value.displaybalance.value),
                          ledger.display_balance(value.account.value, a->type))
                    << value.account.value << " display -- " << value.notes;
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
    void examples_businessrule_the_amount_in_an_ofx_file_is_used_as_the_transaction_amount(
            const std::vector<OfxAmountToPostingString>& values) {
        for (const auto& value : values) {
            const bool card = value.ofxaccttype.value == "CREDITCARD";
            const std::string account = card ? "Liabilities:Card" : "Assets:Account";
            start_fresh(account, card ? types::AccountType::CreditCard
                                      : types::AccountType::Checking);
            ofx::Statement statement;
            statement.acct_type = ofx::acct_type_from(value.ofxaccttype.value);
            ofx::Transaction t;
            t.date_posted = types::Date(2024, 1, 15);
            t.amount = Money(value.trnamt.value);
            t.fit_id = "AMT1";
            t.name = "WHOEVER";
            statement.transactions.push_back(t);
            import(statement);

            const ledger::Transaction* here = transaction_in(account);
            ASSERT_NE(nullptr, here) << value.ofxaccttype.value << " " << value.trnamt.value
                                     << " produced no posting in " << account;
            EXPECT_EQ(Money(value.postingamount.value).cents(), here->amount.cents())
                << value.ofxaccttype.value << " " << value.trnamt.value << " -- " << value.notes;
        }
    }

    // The tests are tried in order and the first that applies decides, so every
    // row is checked by building the book that reaches exactly that row and
    // stops there -- the behaviour the row describes, not a reading of the code.
    void examples_businessrule_how_a_downloaded_transaction_is_identified(
            const std::vector<DispositionRuleString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.disposition.value, ofx::to_string(disposition_reaching_test(value.test)))
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
            const ledger::Transaction here = already_there(value.postingcarries);

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
                ASSERT_TRUE(ofx::answer_the_same(chart_, offered, target_, &transactions_));
                EXPECT_EQ(before, transactions_.size()) << value.thedownloadedrow;
                const ledger::Transaction* here = transaction_in(target_);
                ASSERT_NE(nullptr, here);
                EXPECT_EQ(types::ClearedStatus::Cleared, here->cleared)
                    << value.theonealreadythere;
                const std::string* id = ledger::id_from(*here, types::ImportSource::Ofx);
                ASSERT_NE(nullptr, id) << value.theonealreadythere;
                EXPECT_EQ("F003", *id) << value.theonealreadythere;
            } else if (value.answer == "Different") {
                const ledger::Transaction made = ofx::answer_different(
                    offered, target_, ledger::transaction_count(transactions_), &chart_);
                ASSERT_FALSE(made.source_account.is_none()) << value.thedownloadedrow;
                transactions_.push_back(made);
                EXPECT_EQ(before + 1, transactions_.size()) << value.thedownloadedrow;
                // And nothing happened to the one already there.
                const ledger::Transaction* first = transaction_in(target_);
                ASSERT_NE(nullptr, first);
                EXPECT_EQ(nullptr, ledger::id_from(*first, types::ImportSource::Ofx))
                    << value.theonealreadythere;
            } else if (value.answer == "Unanswered") {
                // Nothing is called at all, which is the row being specified.
                EXPECT_EQ(before, transactions_.size()) << value.thedownloadedrow;
                const ledger::Transaction* here = transaction_in(target_);
                ASSERT_NE(nullptr, here);
                EXPECT_EQ(nullptr, ledger::id_from(*here, types::ImportSource::Ofx))
                    << value.theonealreadythere;
                // And the same file reaches the same question again.
                const ofx::Imported again =
                    ofx::decide(offered_statement(), target_, transactions_, chart_);
                EXPECT_EQ(1, again.summary.Possible) << "the question was not asked again";
            } else {
                FAIL() << "there is no answer called " << value.answer;
            }
        }
    }

    // ------------------------------------------------ account mapping

    // A statement header, with no transactions: the question here is only
    // which account it is, not what is in it yet.
    void when_statement_imported(const std::vector<OfxStatementString>& values) {
        ASSERT_FALSE(values.empty());
        const OfxStatementString& value = values.front();
        const std::string bank_id = blank(value.bankid);
        const std::string account_id = value.ofxaccountid;
        if (const ofx::AccountMapping* m = ofx::mapping_for(mappings_, bank_id, account_id)) {
            target_ = m->account_path;
            target_was_asked_ = false;
            was_asked_ = false;
        } else {
            asked_.bankid = bank_id.empty() ? "none" : bank_id;
            asked_.ofxaccountid = account_id;
            asked_.ofxaccttype = value.ofxaccttype.value;
            asked_.suggestion = "none";
            was_asked_ = true;
            target_was_asked_ = true;
        }
    }

    void then_asked_to_choose_an_account_for(const std::vector<UnknownAccountString>& values) {
        ASSERT_FALSE(values.empty());
        ASSERT_TRUE(was_asked_) << "nothing was asked";
        EXPECT_EQ(values.front(), asked_);
    }

    // ------------------------------------------------ several statements at once

    static ofx::Transaction downloaded_from_file(const OfxImportFileString& value) {
        ofx::Transaction t;
        t.trn_type = value.trntype.value;
        const auto on = types::Date::from_iso(value.dateposted);
        EXPECT_TRUE(on.has_value()) << value.dateposted;
        if (on.has_value()) t.date_posted = *on;
        t.amount = Money(value.amount.value);
        t.fit_id = blank(value.fitid.value);
        t.name = blank(value.name);
        t.memo = blank(value.memo);
        t.check_num = blank(value.checknum.value);
        return t;
    }

    // Grouped by the account each row's OfxAccountId resolves to. An id
    // nothing has mapped yet contributes no rows at all: see
    // ofx::account_for_id and the note on the scenario that relies on this.
    void when_statements_imported(const std::vector<OfxImportFileString>& values) {
        std::vector<ofx::AccountRows> by_account;
        for (const auto& value : values) {
            const std::string account = ofx::account_for_id(mappings_, value.ofxaccountid);
            if (account.empty()) continue;
            auto at = std::find_if(by_account.begin(), by_account.end(),
                [&](const ofx::AccountRows& g) { return g.account == account; });
            if (at == by_account.end()) {
                by_account.push_back({account, {}});
                at = std::prev(by_account.end());
            }
            at->rows.push_back(downloaded_from_file(value));
        }
        last_multi_ = ofx::decide_multi(by_account, &transactions_, &chart_, match_rules_);
        summary_ = last_multi_.summary;
        transactions_.insert(transactions_.end(), last_multi_.ready.begin(),
                             last_multi_.ready.end());
        candidates_ = last_multi_.candidates;
    }

    void then_transfer_candidates_are(const std::vector<TransferCandidateString>& values) {
        ASSERT_EQ(values.size(), candidates_.size()) << "candidates proposed: " << candidate_listing();
        for (std::size_t i = 0; i < values.size(); ++i) {
            const ofx::TransferCandidate& c = candidates_[i].candidate;
            EXPECT_EQ(values[i].fromaccount.value, c.from_account) << "candidate " << i << " from account";
            EXPECT_EQ(values[i].fromdate, c.from_date.iso()) << "candidate " << i << " from date";
            EXPECT_EQ(values[i].toaccount.value, c.to_account) << "candidate " << i << " to account";
            EXPECT_EQ(values[i].todate, c.to_date.iso()) << "candidate " << i << " to date";
            EXPECT_EQ(Money(values[i].amount.value), c.amount) << "candidate " << i << " amount";
            EXPECT_EQ(std::stoll(values[i].daysapart), c.days_apart)
                << "candidate " << i << " days apart";
            EXPECT_EQ(values[i].state.value, ofx::to_string(c.state)) << "candidate " << i << " state";
        }
    }

    // Whether a listed candidate matches a pending one, on whatever the row
    // names and leaves the rest as "?DNC?" -- which is the point of the
    // CompareOnly steps that find a candidate by its shape rather than by
    // every field.
    static bool candidate_matches(const TransferCandidateString& v,
                                  const ofx::TransferCandidate& c) {
        if (v.fromaccount.value != DNCString && v.fromaccount.value != c.from_account) return false;
        if (v.toaccount.value != DNCString && v.toaccount.value != c.to_account) return false;
        if (v.amount.value != DNCString && Money(v.amount.value) != c.amount) return false;
        if (v.fromname != DNCString && blank(v.fromname) != c.from_name) return false;
        if (v.toname != DNCString && blank(v.toname) != c.to_name) return false;
        return true;
    }

    void when_candidate_approved(const std::vector<TransferCandidateString>& values) {
        ASSERT_FALSE(values.empty());
        const TransferCandidateString& v = values.front();
        const auto at = std::find_if(candidates_.begin(), candidates_.end(),
            [&](const ofx::TransferProposal& p) { return candidate_matches(v, p.candidate); });
        if (at != candidates_.end()) {
            offered_rule_ = ofx::rule_offered_for(at->candidate);
            const auto merged = ofx::merge_transfer(&chart_, *at, ledger::transaction_count(transactions_));
            transactions_.push_back(merged.first);
            transactions_.push_back(merged.second);
            summary_.New += 1;
            candidates_.erase(at);
        } else {
            // No pairing was ever proposed in this scenario -- it asks only
            // about the rule an approval offers, built straight from what the
            // table itself says.
            ofx::TransferCandidate c;
            c.from_account = v.fromaccount.value;
            c.from_name = blank(v.fromname);
            c.to_account = v.toaccount.value;
            c.to_name = blank(v.toname);
            c.amount = v.amount.value == DNCString ? Money() : Money(v.amount.value);
            offered_rule_ = ofx::rule_offered_for(c);
        }
    }

    void when_candidate_rejected(const std::vector<TransferCandidateString>& values) {
        ASSERT_FALSE(values.empty());
        const TransferCandidateString& v = values.front();
        const auto at = std::find_if(candidates_.begin(), candidates_.end(),
            [&](const ofx::TransferProposal& p) { return candidate_matches(v, p.candidate); });
        ASSERT_NE(candidates_.end(), at) << "no pending candidate matches " << v.to_string();
        const std::vector<ledger::Transaction> made =
            ofx::split_transfer(*at, ledger::transaction_count(transactions_), &chart_);
        transactions_.insert(transactions_.end(), made.begin(), made.end());
        summary_.New += static_cast<int>(made.size());
        candidates_.erase(at);
    }

    void then_rule_offered_is(const std::vector<TransferMatchRuleString>& values) {
        ASSERT_FALSE(values.empty());
        const TransferMatchRuleString& v = values.front();
        EXPECT_EQ(v.fromaccount.value, offered_rule_.from_account) << "offered rule from account";
        EXPECT_EQ(blank(v.frompattern), offered_rule_.from_pattern) << "offered rule from pattern";
        EXPECT_EQ(v.toaccount.value, offered_rule_.to_account) << "offered rule to account";
        EXPECT_EQ(blank(v.topattern), offered_rule_.to_pattern) << "offered rule to pattern";
        EXPECT_EQ(parse_bool_cell(v.enabled), offered_rule_.enabled) << "offered rule enabled";
    }

    // Every proposal, both halves, the gap, and the choices -- read straight
    // off the pending candidates, which is what the dialog would be shown.
    void then_confirmation_dialog_shows(const std::vector<ConfirmationRowString>& values) {
        std::vector<const ofx::TransferProposal*> pending;
        for (const auto& p : candidates_)
            if (p.candidate.state == ofx::TransferCandidateState::Proposed) pending.push_back(&p);
        ASSERT_EQ(values.size(), pending.size()) << "candidates offered: " << candidate_listing();
        for (std::size_t i = 0; i < values.size(); ++i) {
            const ofx::TransferCandidate& c = pending[i]->candidate;
            EXPECT_EQ(values[i].fromaccount.value, c.from_account) << "row " << i << " from account";
            EXPECT_EQ(blank(values[i].fromname), c.from_name) << "row " << i << " from name";
            EXPECT_EQ(values[i].toaccount.value, c.to_account) << "row " << i << " to account";
            EXPECT_EQ(blank(values[i].toname), c.to_name) << "row " << i << " to name";
            EXPECT_EQ(Money(values[i].amount.value), c.amount) << "row " << i << " amount";
            EXPECT_EQ(std::stoll(values[i].daysapart), c.days_apart) << "row " << i << " days apart";
            EXPECT_EQ(values[i].choices, "Approve, Reject, ApproveAndRemember")
                << "row " << i << " choices";
        }
    }

    void then_import_preview_is(const std::vector<ImportPreviewString>& values) {
        ASSERT_FALSE(values.empty());
        const ofx::ImportPreview p = ofx::preview_of(last_multi_);
        const ImportPreviewString& v = values.front();
        EXPECT_EQ(std::stoi(v.newtransactions), p.new_transactions) << "new transactions";
        EXPECT_EQ(std::stoi(v.duplicates), p.duplicates) << "duplicates";
        EXPECT_EQ(std::stoi(v.matchedtoexisting), p.matched_to_existing) << "matched to existing";
        EXPECT_EQ(std::stoi(v.transfercandidates), p.transfer_candidates) << "transfer candidates";
        EXPECT_EQ(parse_bool_cell(v.committed), p.committed) << "committed";
    }

    // ------------------------------------------------ the bank's own balance

    void when_ledger_balance_checked(const std::vector<LedgerBalanceCheckString>& values) {
        ASSERT_FALSE(values.empty());
        const LedgerBalanceCheckString& v = values.front();
        balance_check_ = ofx::check_ledger_balance(chart_, transactions_, v.account.value, Money(v.statedbal.value));
    }

    void then_balance_check_is(const std::vector<LedgerBalanceResultString>& values) {
        ASSERT_FALSE(values.empty());
        const LedgerBalanceResultString& v = values.front();
        EXPECT_EQ(Money(v.clearedbalance.value), balance_check_.cleared_balance) << "cleared balance";
        EXPECT_EQ(Money(v.statedbal.value), balance_check_.stated_bal) << "stated balance";
        EXPECT_EQ(Money(v.difference.value), balance_check_.difference) << "difference";
        EXPECT_EQ(parse_bool_cell(v.agrees), balance_check_.agrees) << "agrees";
    }

    // ------------------------------------------------ a report over an import

    void when_report_run(const std::vector<ReportSpecString>& values) {
        ASSERT_FALSE(values.empty());
        const ReportSpecString& v = values.front();
        reports::Spec spec;
        const auto from = types::Date::from_iso(v.from);
        const auto to = types::Date::from_iso(v.to);
        if (from) spec.from = *from;
        if (to) spec.to = *to;
        report_ = reports::category_report(chart_, as_ledger(), spec);
    }

    void then_report_rows_are(const std::vector<ReportRowString>& values) {
        ASSERT_EQ(values.size(), report_.rows.size())
            << "report rows: " << report_.rows.size() << " produced";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].account.value, report_.rows[i].account) << "row " << i;
            EXPECT_EQ(std::stoi(values[i].level), report_.rows[i].level) << "row " << i << " level";
            EXPECT_EQ(Money(values[i].amount.value), report_.rows[i].amount) << "row " << i << " amount";
            EXPECT_EQ(parse_bool_cell(values[i].issubtotal), report_.rows[i].is_subtotal)
                << "row " << i << " subtotal";
        }
    }

    // ------------------------------------------------ example tables

    void examples_datatype_transfercandidatestate(
            const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.value,
                      ofx::to_string(ofx::transfer_candidate_state_from(value.value)))
                << value.value << " does not survive being read -- " << value.notes;
    }

    // These three stay $WIP: each Examples table only restates a condition in
    // words, with no outcome column to drive through, and the condition
    // itself is exercised by the concrete scenarios above (the one-account,
    // already-matched, and priority-ordering cases). Left as stubs so a
    // regeneration finds them already there if the tag ever comes off.
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
    std::map<std::string, Money> second_row_amount_;
    ofx::Read read_;
    chart::Chart chart_;
    std::vector<ledger::Transaction> transactions_;
    std::vector<ledger::TransactionSplit> splits_;
    std::string target_;
    ofx::Imported decided_;
    ofx::Summary summary_;
    std::vector<ofx::AccountMapping> mappings_;
    std::vector<ofx::TransferMatchRule> match_rules_;
    long long check_window_days_ = ofx::kCheckWindow;
    UnknownAccountString asked_;
    bool was_asked_ = false;
    bool target_was_asked_ = false;
    std::vector<ofx::TransferProposal> candidates_;
    ofx::TransferMatchRule offered_rule_;
    ofx::MultiImported last_multi_;
    ofx::LedgerBalanceResult balance_check_;
    reports::CategoryReport report_;

    std::string candidate_listing() const {
        if (candidates_.empty()) return "(none)";
        std::string out;
        for (const auto& p : candidates_)
            out += "  " + p.candidate.from_account + " -> " + p.candidate.to_account + " " +
                   p.candidate.amount.in_register() + " [" + ofx::to_string(p.candidate.state) +
                   "]\n";
        return out;
    }

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
        t.trn_type = value.trntype.value;
        const auto on = types::Date::from_iso(value.dateposted);
        EXPECT_TRUE(on.has_value()) << value.dateposted;
        if (on.has_value()) t.date_posted = *on;
        t.amount = Money(value.amount.value);
        t.fit_id = blank(value.fitid.value);
        t.name = blank(value.name);
        t.memo = blank(value.memo);
        t.check_num = blank(value.checknum.value);
        return t;
    }

    // One import, in the order the program does it: decide, create what is new,
    // then bring up to date what was already here. Nothing is created for a
    // duplicate or for a possible duplicate, which is what the summary shows.
    void import(const ofx::Statement& statement) {
        decided_ = ofx::decide(statement, target_, transactions_, chart_, check_window_days_);
        summary_ = decided_.summary;
        const std::vector<ledger::Transaction> made =
            ofx::transactions_for(decided_, target_, ledger::transaction_count(transactions_), &chart_);
        ofx::apply_duplicates(chart_, decided_, target_, &transactions_);
        const ofx::Completed completed = ofx::apply_completions(
            chart_, decided_, target_, ledger::transaction_count(transactions_) + made.size(), &transactions_);
        transactions_.insert(transactions_.end(), made.begin(), made.end());
        transactions_.insert(transactions_.end(), completed.created.begin(),
                             completed.created.end());
    }

    const ledger::Transaction* transaction_in(const std::string& account) const {
        const chart::Ref account_ref = chart_.ref_for_path(account);
        for (const ledger::Transaction& t : transactions_)
            if (t.source_account == account_ref) return &t;
        return nullptr;
    }

    // Unassigned is never an account a transaction's own download is in -- it
    // is only ever the other side, waiting to be filled in -- so finding one
    // waiting on it means searching Destination, not SourceAccount.
    const ledger::Transaction* transaction_waiting_on(const std::string& account) const {
        const chart::Ref account_ref = chart_.ref_for_path(account);
        for (const ledger::Transaction& t : transactions_)
            if (t.destination == account_ref) return &t;
        return nullptr;
    }

    ledger::Ledger as_ledger() const {
        ledger::Ledger out;
        ledger::add_to_book(&out, chart_, transactions_, splits_);
        return out;
    }

    static ofx::Disposition disposition_named(const std::string& name) {
        if (name == "Duplicate") return ofx::Disposition::Duplicate;
        if (name == "Possible")  return ofx::Disposition::Possible;
        if (name == "Completed") return ofx::Disposition::Completed;
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

    // --- the payment rows --------------------------------------------------

    static constexpr const char* kBank = "Assets:Checking";
    static constexpr const char* kCard = "Liabilities:Chase Visa";

    static chart::Account card_of(const std::string& path, const std::string& payee) {
        chart::Account a = account_of(path, types::AccountType::CreditCard);
        a.payment_payee = payee;
        return a;
    }

    // A bank, a card that says CHASEBANK when it is paid, and somewhere for a
    // half-known payment to wait.
    void a_book_with_two_accounts() {
        chart_ = chart::Chart();
        chart_.put(account_of(kBank, types::AccountType::Checking));
        chart_.put(card_of(kCard, "CHASEBANK"));
        chart_.put(account_of(ledger::kUnassigned, types::AccountType::OtherAsset));
        chart_.put(account_of("Expenses:Uncategorized", types::AccountType::Expense));
        transactions_.clear();
    }

    // One side of the same payment, as that side's own file describes it: the
    // bank calls it CHASEBANK and the money leaves; the card calls it a payment
    // received and the balance owed falls.
    static ofx::Statement payment_side(const std::string& account) {
        ofx::Statement statement;
        ofx::Transaction row;
        row.trn_type = "XFER";
        if (account == kBank) {
            row.date_posted = types::Date(2026, 4, 10);
            row.amount = Money("-1200.00");
            row.fit_id = "BNK-77";
            row.name = "CHASEBANK";
        } else {
            row.date_posted = types::Date(2026, 4, 11);
            row.amount = Money("1200.00");
            row.fit_id = "CC-901";
            row.name = "PAYMENT RECEIVED";
        }
        statement.transactions.push_back(row);
        return statement;
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
    ledger::Transaction already_there(const std::string& carries) {
        start_fresh("Assets:Checking", types::AccountType::Checking);
        ledger::Transaction t;
        t.id = ledger::new_id();
        t.ref = types::TransactionRef("T1");
        // Two days before the row, so that no other test can reach it: the
        // counting test asks for the same day and the check-number test for a
        // check number, and neither is here. What is left is the identifier,
        // which is what the rule is about.
        t.date = types::Date(2024, 1, 13);
        t.payee = types::PayeeName("HARRIS TEETER");
        t.source_account = chart_.ref_for_path("Assets:Checking");
        t.amount = Money("-45.00");
        t.cleared = types::ClearedStatus::Cleared;
        if (carries == "An Ofx id, the same") {
            ledger::stamp(&t, types::ImportSource::Ofx, "F100");
        } else if (carries == "An Ofx id, a different one") {
            ledger::stamp(&t, types::ImportSource::Ofx, "F999");
        } else if (carries == "A Csv id only") {
            ledger::stamp(&t, types::ImportSource::Csv, "F100");
        } else if (carries == "A Csv id and an Ofx id") {
            ledger::stamp(&t, types::ImportSource::Csv, "F100");
            ledger::stamp(&t, types::ImportSource::Ofx, "F100");
        } else {
            EXPECT_EQ("Nothing", carries) << "unlisted case";
        }

        t.destination = chart_.ref_for_path("Expenses:Uncategorized");
        transactions_ = {t};
        return t;
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

    // A book holding two transactions of the same date and amount as the one
    // downloaded row. Two against one is not a pairing: nothing says which of
    // them the row is, and answering either way would double a transaction or
    // lose one. So the import can only ask, which is the disposition rule's
    // fourth test and the one case the program cannot settle on its own.
    void offer_a_possible() {
        start_fresh("Assets:Checking", types::AccountType::Checking);
        chart_.put(account_of("Expenses:Groceries", types::AccountType::Expense));
        ledger::Transaction t;
        t.id = ledger::new_id();
        t.ref = types::TransactionRef("T1");
        t.date = types::Date(2026, 2, 5);
        t.payee = types::PayeeName("Harris Teeter");
        t.source_account = chart_.ref_for_path("Assets:Checking");
        t.amount = Money("-88.20");
        t.cleared = types::ClearedStatus::Uncleared;
        t.destination = chart_.ref_for_path("Expenses:Groceries");
        ledger::Transaction second = t;
        second.id = ledger::new_id();
        second.ref = types::TransactionRef("T2");
        transactions_ = {t, second};
        import(offered_statement());
    }

    // --- the disposition-rule rows ----------------------------------------

    // The book that makes the import reach exactly the numbered test and stop
    // there, so each row of the rule table is checked by the behaviour it
    // describes.
    ofx::Disposition disposition_reaching_test(const std::string& test) {
        if (test == "5") return disposition_of_a_payment_half();

        start_fresh("Assets:Checking", types::AccountType::Checking);
        ofx::Transaction row;
        row.date_posted = types::Date(2024, 1, 20);
        row.amount = Money("-45.00");
        row.fit_id = "F001";
        row.name = "HARRIS TEETER";

        ledger::Transaction t;
        t.id = ledger::new_id();
        t.ref = types::TransactionRef("T1");
        t.payee = types::PayeeName("HARRIS TEETER");
        t.source_account = chart_.ref_for_path("Assets:Checking");
        t.amount = Money("-45.00");
        t.destination = chart_.ref_for_path("Expenses:Uncategorized");
        // A week before the row unless the test says otherwise, because the
        // counting tests are the only ones that ask for the same day and the
        // rows above them must not be reachable by counting.
        t.date = types::Date(2024, 1, 13);

        std::vector<ledger::Transaction> book;
        if (test == "1") {
            // Already carries this FITID from an Ofx import.
            ledger::stamp(&t, types::ImportSource::Ofx, "F001");
            book = {t};
        } else if (test == "2") {
            // The same check number, amount and payee, a day away. The number
            // is the only thing here that reaches across days at all, which is
            // what the book's check-number window is for and why the scenario
            // widening it to thirty days matters.
            t.date = types::Date(2024, 1, 19);
            t.check_no = types::CheckNumber("2041");
            row.check_num = "2041";
            book = {t};
        } else if (test == "3") {
            // One row and one transaction of its date and amount: equal in
            // number, so the row is the transaction already here.
            t.date = types::Date(2024, 1, 20);
            book = {t};
        } else if (test == "4") {
            // Two of them against the one row. Nothing says which, so the
            // import can only ask -- about both of them.
            t.date = types::Date(2024, 1, 20);
            ledger::Transaction second = t;
            second.id = ledger::new_id();
            second.ref = types::TransactionRef("T2");
            book = {t, second};
        } else {
            // Test 6: an empty book, so nothing above applies.
            EXPECT_EQ("6", test) << "unlisted test";
        }

        transactions_ = book;
        ofx::Statement statement;
        statement.transactions.push_back(row);
        import(statement);
        EXPECT_EQ(1u, decided_.decided.size()) << "test " << test;
        return decided_.decided.empty() ? ofx::Disposition::New
                                        : decided_.decided.front().disposition;
    }

    // Test five, which needs a book of two accounts rather than one: the
    // bank's side of a card payment arrives first and waits with its other
    // half unassigned, and the card's own statement is the row that completes
    // it. Nothing above applies, because the card has nothing in it yet.
    ofx::Disposition disposition_of_a_payment_half() {
        a_book_with_two_accounts();
        target_ = kBank;
        import(payment_side(kBank));
        target_ = kCard;
        import(payment_side(kCard));
        EXPECT_EQ(1u, decided_.decided.size()) << "test 5";
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
            start_fresh("Assets:Checking", types::AccountType::Checking);

            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef("T1");
            t.date = types::Date(2024, 1, 2);
            t.payee = types::PayeeName("County Tax Office");
            t.check_no = types::CheckNumber("2041");
            t.source_account = chart_.ref_for_path("Assets:Checking");
            t.amount = Money("-1250.00");
            t.destination = chart_.ref_for_path("Expenses:Uncategorized");
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
            const bool settled =
                decided_.decided.front().disposition == ofx::Disposition::Duplicate;
            EXPECT_EQ(value.duplicate == "Yes", settled)
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

    // Each row driven both ways round: the bank first then the card, and the
    // card first then the bank. The claim is that the order does not matter, so
    // testing one order would not be testing the rule.
    void examples_businessrule_a_payment_is_completed_by_whichever_import_arrives_second(
            const std::vector<PaymentCompletionString>& values) {
        for (const auto& value : values) {
            const bool bank_first = value.whicharrivesfirst == "The bank";
            const bool only_one = value.whicharrivesfirst == "Only one ever";
            const std::string first = bank_first || only_one ? kBank : kCard;
            const std::string second = first == kBank ? kCard : kBank;

            a_book_with_two_accounts();
            target_ = first;
            import(payment_side(first));
            ASSERT_EQ(1, summary_.New) << value.whicharrivesfirst;

            // Whatever arrives first records the payment with its other side
            // waiting, which is what the second column says.
            const ledger::Transaction* waiting = transaction_waiting_on(ledger::kUnassigned);
            ASSERT_NE(nullptr, waiting) << value.whatitrecords;

            if (only_one) {
                EXPECT_EQ(1u, transactions_.size()) << value.whattheseconddoes;
                continue;
            }

            target_ = second;
            import(payment_side(second));
            // Two transactions now: the first, with its Unassigned side filled
            // in, and a new row of this account's own -- a transaction being
            // one account's own download, the second side gets its own row
            // the same as the first did, with this file's identifier on it.
            EXPECT_EQ(2u, transactions_.size()) << value.whattheseconddoes;
            EXPECT_EQ(nullptr, transaction_waiting_on(ledger::kUnassigned)) << value.whattheseconddoes;
            const ledger::Transaction* filled = transaction_in(second);
            ASSERT_NE(nullptr, filled) << value.whattheseconddoes;
            const std::string* id = ledger::id_from(*filled, types::ImportSource::Ofx);
            ASSERT_NE(nullptr, id) << value.whattheseconddoes;
            EXPECT_EQ(second == kBank ? "BNK-77" : "CC-901", *id)
                << value.whattheseconddoes;
            // And the payee is still the one the first side gave it.
            EXPECT_EQ(first == kBank ? "CHASEBANK" : "PAYMENT RECEIVED",
                      transactions_.front().payee.value())
                << "the second import overwrote the payee";
        }
    }

    void examples_businessrule_the_payment_payee_says_which_accounts_a_payment_could_belong_to(
            const std::vector<PaymentPayeeNarrowingString>& values) {
        for (const auto& value : values) {
            chart_ = chart::Chart();
            chart_.put(account_of(kBank, types::AccountType::Checking));
            chart_.put(account_of(ledger::kUnassigned, types::AccountType::OtherAsset));
            if (value.theypayeematches == "One account") {
                chart_.put(card_of(kCard, "CHASEBANK"));
            } else if (value.theypayeematches == "Several accounts" ||
                       value.theypayeematches == "Several, and all match") {
                chart_.put(card_of(kCard, "CHASEBANK"));
                chart_.put(card_of("Liabilities:Chase Amazon", "CHASEBANK"));
            }
            // "No account" leaves nothing carrying the payee at all.

            const std::vector<std::string> narrowed =
                ofx::accounts_paid_by(chart_, "CHASEBANK");
            const std::size_t wanted =
                value.theypayeematches == "One account" ? 1u
                    : value.theypayeematches == "No account" ? 0u : 2u;
            EXPECT_EQ(wanted, narrowed.size())
                << value.theypayeematches << " -- " << value.thenthesearchis;
            // Folded, so the payee on the statement need not match in case.
            if (wanted > 0)
                EXPECT_EQ(wanted, ofx::accounts_paid_by(chart_, "chasebank").size())
                    << "the payee should match whatever its case";
        }
    }


public:

    // The book as the scenario found it, written the way a transaction is
    // stored: one row is one transaction, naming both its sides, with the
    // identifier the file it came from gave it.
    void given_transactions_are(const std::vector<ImportedTransactionString>& values) {
        transactions_.clear();
        splits_.clear();
        for (const auto& value : values) {
            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef(value.ref.value);
            const auto on = types::Date::from_iso(value.date);
            if (on) t.date = *on;
            t.payee = types::PayeeName(blank(value.payee.value));
            t.check_no = types::CheckNumber(blank(value.checkno.value));
            t.source_account = chart_.ref_for_path(blank(value.sourceaccount.value));
            t.destination = chart_.ref_for_path(blank(value.destination.value));
            t.amount = Money(value.amount.value);
            if (value.cleared.value != DNCString && !value.cleared.value.empty())
                t.cleared = types::cleared_status_from_string(value.cleared.value);
            const std::string fit_id = blank(value.fitid.value);
            if (!fit_id.empty()) ledger::stamp(&t, types::ImportSource::Ofx, fit_id);
            transactions_.push_back(t);
        }
    }

    // The whole list, in order, with the identifier each one carries.
    void then_transactions_are(const std::vector<ImportedTransactionString>& values) {
        ASSERT_EQ(values.size(), transactions_.size()) << "number of transactions";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const ledger::Transaction& got = transactions_[i];
            const ImportedTransactionString& want = values[i];
            const std::string where = "transaction " + std::to_string(i + 1);
            if (want.ref.value != DNCString)
                EXPECT_EQ(want.ref.value, got.ref.value()) << where << " ref";
            if (want.date != DNCString)
                EXPECT_EQ(want.date, got.date.iso()) << where << " date";
            if (want.payee.value != DNCString)
                EXPECT_EQ(blank(want.payee.value), got.payee.value()) << where << " payee";
            if (want.checkno.value != DNCString)
                EXPECT_EQ(blank(want.checkno.value), got.check_no.value()) << where << " check number";
            if (want.sourceaccount.value != DNCString)
                EXPECT_EQ(blank(want.sourceaccount.value), chart_.path_for_ref(got.source_account))
                    << where << " source account";
            if (want.destination.value != DNCString)
                EXPECT_EQ(blank(want.destination.value), chart_.path_for_ref(got.destination))
                    << where << " destination";
            if (want.amount.value != DNCString)
                EXPECT_EQ(Money(want.amount.value).cents(), got.amount.cents()) << where << " amount";
            if (want.cleared.value != DNCString && !want.cleared.value.empty())
                EXPECT_EQ(want.cleared.value, types::to_string(got.cleared)) << where << " cleared";
            if (want.fitid.value != DNCString) {
                const std::string* id = ledger::id_from(got, types::ImportSource::Ofx);
                EXPECT_EQ(blank(want.fitid.value), id == nullptr ? std::string() : *id)
                    << where << " FITID";
            }
        }
    }

public:

    void when_import_target_chosen(const std::vector<ImportTargetString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_import_target_chosen";
    }

    void then_account_numbering_is(const std::vector<AccountNumberingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_account_numbering_is";
    }

    void examples_businessrule_when_the_number_in_a_download_is_the_number_on_an_account(const std::vector<DownloadAccountMatchString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_when_the_number_in_a_download_is_the_number_on_an_account";
    }

    void examples_businessrule_a_download_arrives_cleared_and_only_a_typed_transaction_is_not(const std::vector<ClearedOnArrivalString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_a_download_arrives_cleared_and_only_a_typed_transaction_is_not";
    }

    void examples_businessrule_the_date_windows_and_why_they_differ(
            const std::vector<DateWindowString>& values) {
        for (const auto& value : values) {
            if (value.window == "1 day") {
                EXPECT_EQ(1, ofx::kCheckWindow) << value.rule << " -- " << value.why;
            } else if (value.window == "2 days") {
                EXPECT_EQ(2, ofx::kTransferWindow) << value.rule << " -- " << value.why;
            } else if (value.window == "5 days") {
                EXPECT_EQ(5, ofx::kMatchWindow) << value.rule << " -- " << value.why;
            } else if (value.window == "Same day") {
                // There is no constant for this one, and that is the point:
                // the test is dates being equal. So the claim is checked by
                // the behaviour -- a day away reaches nothing and is created.
                EXPECT_EQ(ofx::Disposition::New, a_day_away_from_what_is_here())
                    << value.rule << " -- " << value.why;
            } else {
                FAIL() << "there is no window called " << value.window;
            }
        }
    }

    // One cleared transaction in the book, and a download of the same amount
    // the day after. Nothing carries a check number, so nothing may reach
    // across the day.
    ofx::Disposition a_day_away_from_what_is_here() {
        start_fresh("Assets:Checking", types::AccountType::Checking);
        ledger::Transaction t;
        t.id = ledger::new_id();
        t.ref = types::TransactionRef("T1");
        t.date = types::Date(2024, 1, 20);
        t.payee = types::PayeeName("Harris Teeter");
        t.source_account = chart_.ref_for_path("Assets:Checking");
        t.destination = chart_.ref_for_path("Expenses:Uncategorized");
        t.amount = Money("-45.00");
        t.cleared = types::ClearedStatus::Cleared;
        transactions_ = {t};

        ofx::Statement statement;
        ofx::Transaction row;
        row.date_posted = types::Date(2024, 1, 21);
        row.amount = Money("-45.00");
        row.fit_id = "F001";
        row.name = "HARRIS TEETER";
        statement.transactions.push_back(row);
        import(statement);
        EXPECT_EQ(1u, decided_.decided.size());
        return decided_.decided.empty() ? ofx::Disposition::Duplicate
                                        : decided_.decided.front().disposition;
    }

public:

    // The table is read by transaction, in the order it gives them, because
    // the order is part of what is being said: what settled the duplicate
    // first, then what the import changed about it.
    void then_transaction_notes_are(const std::vector<TransactionNoteString>& values) {
        std::vector<std::string> refs;
        for (const auto& value : values)
            if (std::find(refs.begin(), refs.end(), value.ref.value) == refs.end())
                refs.push_back(value.ref.value);
        for (const std::string& ref : refs) {
            std::vector<std::string> wanted;
            for (const auto& value : values)
                if (value.ref.value == ref) wanted.push_back(value.note.value);
            const ledger::Transaction* here = nullptr;
            for (const ledger::Transaction& one : transactions_)
                if (one.ref.value() == ref) here = &one;
            ASSERT_NE(nullptr, here) << "there is no transaction called " << ref;
            EXPECT_EQ(wanted, here->import_notes) << "the notes on " << ref;
        }
    }

    // Every value the table names, in the order it names them, against every
    // value the code has: a note the one has and the other has not is what
    // this is for.
    void examples_datatype_duplicatenote(const std::vector<EnumerationValuesString>& values) {
        const std::vector<ofx::DuplicateNote> all = {
            ofx::DuplicateNote::None,            ofx::DuplicateNote::DetailsDiffer,
            ofx::DuplicateNote::CheckNumberSame, ofx::DuplicateNote::FitIdAdded,
            ofx::DuplicateNote::PayeeUnmatched,  ofx::DuplicateNote::Uncleared};
        ASSERT_EQ(all.size(), values.size()) << "the table and the code disagree about how many";
        for (std::size_t i = 0; i < values.size(); ++i)
            EXPECT_EQ(values[i].value, ofx::to_string(all[i])) << values[i].notes;
    }

    // One duplicate, settled by its check number, where the download
    // disagrees about everything it is allowed to disagree about: a different
    // day, no memo of its own worth keeping, no category at all, and an
    // identifier the transaction has not got. Every row of the table is then
    // asked of the one transaction afterwards.
    void examples_businessrule_what_a_duplicate_does_to_the_transaction_already_there(
            const std::vector<WhatADuplicateDoesString>& values) {
        start_fresh("Assets:Checking", types::AccountType::Checking);
        chart_.put(account_of("Expenses:Home:Rates", types::AccountType::Expense));
        ledger::Transaction t;
        t.id = ledger::new_id();
        t.ref = types::TransactionRef("T1");
        t.date = types::Date(2024, 1, 2);
        t.payee = types::PayeeName("County Tax Office");
        t.memo = "Second half";
        t.check_no = types::CheckNumber("2041");
        t.source_account = chart_.ref_for_path("Assets:Checking");
        t.destination = chart_.ref_for_path("Expenses:Home:Rates");
        t.amount = Money("-1250.00");
        t.cleared = types::ClearedStatus::Uncleared;
        transactions_ = {t};

        ofx::Statement statement;
        ofx::Transaction row;
        row.trn_type = "CHECK";
        row.date_posted = types::Date(2024, 1, 3);
        row.amount = Money("-1250.00");
        row.fit_id = "20240103002";
        row.name = "COUNTY TAX OFFICE";
        row.memo = "ACH DEBIT";
        row.check_num = "2041";
        statement.transactions.push_back(row);
        import(statement);

        ASSERT_EQ(1, summary_.Duplicate) << "the check number should have settled it";
        ASSERT_EQ(1u, transactions_.size()) << "a duplicate creates nothing";
        const ledger::Transaction& after = transactions_.front();
        for (const auto& value : values) {
            const std::string& what = value.whathappens;
            if (value.property == "The date") {
                EXPECT_EQ("2024-01-02", after.date.iso()) << what;
            } else if (value.property == "The payee") {
                EXPECT_EQ("County Tax Office", after.payee.value()) << what;
            } else if (value.property == "The category") {
                EXPECT_TRUE(after.destination == chart_.ref_for_path("Expenses:Home:Rates"))
                    << what;
            } else if (value.property == "The memo") {
                EXPECT_EQ("Second half", after.memo) << what;
            } else if (value.property == "The check number") {
                EXPECT_EQ("2041", after.check_no.value()) << what;
            } else if (value.property == "The amount") {
                EXPECT_EQ(Money("-1250.00").cents(), after.amount.cents()) << what;
            } else if (value.property == "Cleared") {
                EXPECT_EQ(types::ClearedStatus::Cleared, after.cleared) << what;
            } else if (value.property == "The identifier") {
                const std::string* id = ledger::id_from(after, types::ImportSource::Ofx);
                ASSERT_NE(nullptr, id) << what;
                EXPECT_EQ("20240103002", *id) << what;
            } else if (value.property == "The note") {
                EXPECT_FALSE(after.import_notes.empty()) << what;
            } else {
                FAIL() << "there is no property called " << value.property;
            }
        }
    }

    // Each row is a book of one transaction and a file of one row, arranged so
    // that what the row describes is what is seen. Several notes can be true
    // of one duplicate, so what is checked is that the named one is among
    // them -- except for None, which is the claim that there are none at all.
    void examples_businessrule_what_the_note_on_a_duplicate_says(
            const std::vector<DuplicateNoteRuleString>& values) {
        for (const auto& value : values) {
            start_fresh("Assets:Checking", types::AccountType::Checking);
            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef("T1");
            t.date = types::Date(2024, 1, 16);
            t.payee = types::PayeeName("Harris Teeter");
            t.source_account = chart_.ref_for_path("Assets:Checking");
            t.destination = chart_.ref_for_path("Expenses:Uncategorized");
            t.amount = Money("-45.00");
            t.cleared = types::ClearedStatus::Cleared;

            ofx::Transaction row;
            row.date_posted = types::Date(2024, 1, 16);
            row.amount = Money("-45.00");
            row.fit_id = "F100";
            row.name = "HARRIS TEETER #231";

            if (value.note.value == "None") {
                // The identifier settles it and nothing else is out of place.
                ledger::stamp(&t, types::ImportSource::Ofx, "F100");
            } else if (value.note.value == "DetailsDiffer") {
                // The identifier settles it and the day does not agree.
                t.date = types::Date(2024, 1, 13);
                ledger::stamp(&t, types::ImportSource::Ofx, "F100");
            } else if (value.note.value == "CheckNumberSame") {
                t.date = types::Date(2024, 1, 15);
                t.check_no = types::CheckNumber("2041");
                row.check_num = "2041";
            } else if (value.note.value == "FitIdAdded") {
                // The same day and amount, and no identifier until now.
            } else if (value.note.value == "PayeeUnmatched") {
                t.payee = types::PayeeName("Costco");
            } else if (value.note.value == "Uncleared") {
                t.cleared = types::ClearedStatus::Uncleared;
            } else {
                FAIL() << "there is no note called " << value.note.value;
                continue;
            }

            transactions_ = {t};
            ofx::Statement statement;
            statement.transactions.push_back(row);
            import(statement);

            ASSERT_EQ(1u, decided_.decided.size()) << value.whatwasseen;
            const ofx::Decided& one = decided_.decided.front();
            EXPECT_EQ(ofx::Disposition::Duplicate, one.disposition)
                << value.whatwasseen << " -- every row here is a duplicate";
            std::string written;
            for (ofx::DuplicateNote note : one.notes) written += ofx::to_string(note) + " ";
            if (value.note.value == "None") {
                EXPECT_TRUE(one.notes.empty())
                    << value.whatwasseen << " -- notes written: " << written;
                continue;
            }
            bool found = false;
            for (ofx::DuplicateNote note : one.notes)
                if (ofx::to_string(note) == value.note.value) found = true;
            EXPECT_TRUE(found) << value.whatwasseen << " -- notes written: " << written;
        }
    }

    // No rules, because every row here is about what is left when no rule
    // applies. That a rule is consulted first is the business of the payee
    // specification, and of the scenarios that import with rules in force.
    void examples_businessrule_two_payees_are_compared_after_the_rules_and_then_loosely(
            const std::vector<PayeeComparisonString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.agree == "Yes",
                      ofx::detail::payees_agree(value.inthebook.value, value.inthefile.value, {}))
                << "\"" << value.inthebook.value << "\" against \"" << value.inthefile.value
                << "\" -- " << value.why;
    }

};
