#pragma once
#include <gtest/gtest.h>
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
#include "payee_rules.h"
#include "posting.h"
#include "text_types.h"
#include "transaction_id.h"
#include "payee_rules.h"

// Glue for Payees.spectable.
//
// The rules themselves live in src/payee_rules.h and are applied by the two
// importers, which is the point: a rule written once applies whether the row
// arrived in a QFX or a CSV, because both ask the same function.
//
// Still stubs, and saying so: remembered splits, learning a rule from a
// categorisation, applying one backwards over transactions already there, and
// merging two payees. Each needs production code that does not exist, and a stub
// that passed would be worse than one that fails by name.
class PayeesGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

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

    void given_payee_rules_are(const std::vector<PayeeRuleString>& values) {
        rules_.clear();
        for (const auto& value : values) {
            payees::Rule rule;
            rule.pattern = value.pattern;
            rule.match_type = payees::match_type_from_string(value.matchtype);
            rule.payee = blank(value.payee);
            rule.category = blank(value.category);
            if (value.enabled != DNCString && !value.enabled.empty())
                rule.enabled = parse_bool_cell(value.enabled);
            rules_.push_back(rule);
        }
    }

    void given_import_target_is(const std::vector<ImportTargetString>& values) {
        ASSERT_FALSE(values.empty());
        target_ = values.front().accountpath;
    }

    // Imported with the rules in force, which is where a rule does its work.
    void when_transactions_imported(const std::vector<OfxTransactionString>& values) {
        ofx::Statement statement;
        for (const auto& value : values) {
            ofx::Transaction t;
            t.trn_type = value.trntype;
            const auto on = types::Date::from_iso(value.dateposted);
            ASSERT_TRUE(on.has_value()) << value.dateposted;
            t.date_posted = *on;
            t.amount = Money(value.amount);
            t.fit_id = blank(value.fitid);
            t.name = blank(value.name);
            t.memo = blank(value.memo);
            t.check_num = blank(value.checknum);
            statement.transactions.push_back(t);
        }
        const ofx::Imported decided =
            ofx::decide(statement, target_, transactions_, chart_);
        const std::vector<ledger::Transaction> made = ofx::transactions_for(
            decided, target_, transactions_.size(), &chart_, rules_);
        transactions_.insert(transactions_.end(), made.begin(), made.end());
    }

    void then_postings_are(const std::vector<PostingRowString>& values) {
        const std::vector<PostingRowString> got = flatten();
        ASSERT_EQ(values.size(), got.size()) << "postings:\n" << listing(got);
        for (std::size_t i = 0; i < values.size(); ++i) {
            PostingRowString back = values[i];
            back.ref = got[i].ref;
            back.date = got[i].date;
            back.payee = got[i].payee;
            back.account = got[i].account;
            back.memo = got[i].memo;
            back.cleared = got[i].cleared;
            if (values[i].amount != DNCString)
                EXPECT_EQ(Money(values[i].amount), Money(got[i].amount))
                    << "posting " << i << " amount";
            back.amount = values[i].amount;
            EXPECT_EQ(values[i], back) << "posting " << i << ": wanted "
                                       << values[i].to_string() << " got "
                                       << back.to_string();
        }
    }

    // No Background in this file, so this establishes nothing and no scenario
    // reaches it.
    void given_the_chart_of_accounts_is_as_previous() {}

    // The tidied name and the one the bank sent, which is kept so that a rule
    // written next year can still match what arrived this year.
    void then_transaction_names_are(const std::vector<PayeeNamesString>& values) {
        ASSERT_EQ(values.size(), transactions_.size()) << "transactions";
        for (std::size_t i = 0; i < values.size(); ++i) {
            PayeeNamesString back = values[i];
            back.payee = transactions_[i].payee.value().empty()
                             ? "none" : transactions_[i].payee.value();
            back.rawname = transactions_[i].raw_name.empty()
                               ? "none" : transactions_[i].raw_name;
            EXPECT_EQ(values[i], back) << "transaction " << i << ": wanted "
                                       << values[i].to_string() << " got "
                                       << back.to_string();
        }
    }

    void given_payee_split_rules_are(const std::vector<PayeeSplitRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: remembered splits";
    }

    void given_remembered_split_lines_are(const std::vector<RememberedSplitLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: remembered splits";
    }

    void then_split_offered_is(const std::vector<OfferedSplitString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: remembered splits";
    }

    void then_split_shortfall_is(const std::vector<SplitShortfallString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: remembered splits";
    }

    void given_payee_split_rules_are_as_previous() {
        ADD_FAILURE() << "Not implemented: given_payee_split_rules_are_as_previous";
    }

    void given_remembered_split_lines_are_as_previous() {
        ADD_FAILURE() << "Not implemented: given_remembered_split_lines_are_as_previous";
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
            ledger::Posting posting;
            posting.account = types::AccountPath(value.account);
            posting.amount = Money(value.amount);
            posting.memo = blank(value.memo);
            if (value.cleared != DNCString && !value.cleared.empty())
                posting.cleared = types::cleared_status_from_string(value.cleared);
            into->postings.push_back(posting);
        }
    }

    void when_category_changed(const std::vector<CategoryChangeString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: learning a rule from a categorisation";
    }

    void then_rule_offered_is(const std::vector<PayeeRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: learning a rule from a categorisation";
    }

    void when_rule_applied_to_existing(const std::vector<PayeeRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: applying a rule to transactions already there";
    }

    void then_would_change(const std::vector<RuleApplicationString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: applying a rule to transactions already there";
    }

    void then_payee_list_is(const std::vector<PayeeListEntryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: the payee list";
    }

    void when_payee_typed(const std::vector<PayeeSelectString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: the payee list";
    }

    void then_category_suggested_is(const std::vector<CategorySuggestionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: the payee list";
    }

    void when_payees_merged(const std::vector<PayeeMergeString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "No code yet: merging two payees";
    }

    void examples_datatype_matchtype(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.value,
                      payees::to_string(payees::match_type_from_string(value.value)))
                << value.value << " -- " << value.notes;
    }

    void examples_businessrule_how_a_pattern_is_matched(
            const std::vector<MatchCaseString>& values) {
        for (const auto& value : values) {
            const bool got = payees::matches(
                payees::match_type_from_string(value.matchtype), value.pattern,
                blank(value.rawname));
            EXPECT_EQ(parse_bool_cell(value.matches), got)
                << value.matchtype << " \"" << value.pattern << "\" against \""
                << value.rawname << "\"";
        }
    }

    // Driven both ways round: A against B and B against A, because a rule about
    // which of two wins is wrong if it depends on the order they are asked in.
    void examples_businessrule_which_rule_wins_when_several_match(
            const std::vector<RulePrecedenceString>& values) {
        for (const auto& value : values) {
            payees::Rule a;
            a.pattern = value.patterna;
            a.match_type = payees::match_type_from_string(value.typea);
            a.payee = "A";
            payees::Rule b;
            b.pattern = value.patternb;
            b.match_type = payees::match_type_from_string(value.typeb);
            b.payee = "B";

            // A name both patterns match, so the choice is the rule's to make.
            const std::string name = value.winner == value.patterna ? value.patterna
                                                                    : value.patternb;
            for (const std::vector<payees::Rule>& order :
                 {std::vector<payees::Rule>{a, b}, std::vector<payees::Rule>{b, a}}) {
                const payees::Rule* won = payees::best_for(order, name);
                ASSERT_NE(nullptr, won) << value.patterna << " / " << value.patternb;
                EXPECT_EQ(value.winner, won->pattern)
                    << value.patterna << " against " << value.patternb << " -- "
                    << value.notes;
            }
        }
    }

    void examples_datatype_splitshape(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_splitshape";
    }

    void examples_businessrule_how_a_percentage_split_rounds(const std::vector<PercentageSplitString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_how_a_percentage_split_rounds";
    }


private:
    chart::Chart chart_;
    std::vector<ledger::Transaction> transactions_;
    std::vector<payees::Rule> rules_;
    std::string target_ = "Assets:Checking";

    // A Default of "none" arrives as the literal word.
    static std::string blank(const std::string& s) {
        return (s == "none" || s == DNCString) ? std::string() : s;
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
};
