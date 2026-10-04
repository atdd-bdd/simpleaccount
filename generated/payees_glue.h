#pragma once
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>
#include "common/common.h"
#include "payee_rules.h"

class PayeesGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

    void given_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is";
    }

    void given_payee_rules_are(const std::vector<PayeeRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_payee_rules_are";
    }

    void given_import_target_is(const std::vector<ImportTargetString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_import_target_is";
    }

    void when_transactions_imported(const std::vector<OfxTransactionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_transactions_imported";
    }

    void then_postings_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_postings_are";
    }

    void given_the_chart_of_accounts_is_as_previous() {
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is_as_previous";
    }

    void then_transaction_names_are(const std::vector<PayeeNamesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_transaction_names_are";
    }

    void given_payee_split_rules_are(const std::vector<PayeeSplitRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_payee_split_rules_are";
    }

    void given_remembered_split_lines_are(const std::vector<RememberedSplitLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_remembered_split_lines_are";
    }

    void then_split_offered_is(const std::vector<OfferedSplitString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_split_offered_is";
    }

    void then_split_shortfall_is(const std::vector<SplitShortfallString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_split_shortfall_is";
    }

    void given_payee_split_rules_are_as_previous() {
        ADD_FAILURE() << "Not implemented: given_payee_split_rules_are_as_previous";
    }

    void given_remembered_split_lines_are_as_previous() {
        ADD_FAILURE() << "Not implemented: given_remembered_split_lines_are_as_previous";
    }

    void given_postings_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_postings_are";
    }

    void when_category_changed(const std::vector<CategoryChangeString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_category_changed";
    }

    void then_rule_offered_is(const std::vector<PayeeRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_rule_offered_is";
    }

    void when_rule_applied_to_existing(const std::vector<PayeeRuleString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_rule_applied_to_existing";
    }

    void then_would_change(const std::vector<RuleApplicationString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_would_change";
    }

    void then_payee_list_is(const std::vector<PayeeListEntryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_payee_list_is";
    }

    void when_payee_typed(const std::vector<PayeeSelectString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_payee_typed";
    }

    void then_category_suggested_is(const std::vector<CategorySuggestionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_category_suggested_is";
    }

    void when_payees_merged(const std::vector<PayeeMergeString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_payees_merged";
    }

    void examples_datatype_matchtype(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_matchtype";
    }

    void examples_businessrule_how_a_pattern_is_matched(const std::vector<MatchCaseString>& values) {
        for (const auto& value : values) {
            const MatchCaseTyped t = MatchCaseTyped::from_string_struct(value);
            EXPECT_EQ(t.matches,
                      payees::matches(payees::match_type_from_string(t.matchtype),
                                      t.pattern, t.rawname))
                << t.matchtype << " [" << t.pattern << "] against [" << t.rawname << "]";
        }
    }

    void examples_businessrule_which_rule_wins_when_several_match(const std::vector<RulePrecedenceString>& values) {
        for (const auto& value : values) {
            const RulePrecedenceTyped t = RulePrecedenceTyped::from_string_struct(value);
            const auto ta = payees::match_type_from_string(t.typea);
            const auto tb = payees::match_type_from_string(t.typeb);
            const bool a_wins = payees::wins(t.patterna, ta, t.patternb, tb);
            const std::string winner = a_wins ? t.patterna : t.patternb;
            EXPECT_EQ(t.winner, winner)
                << "[" << t.patterna << "] against [" << t.patternb << "] -- " << t.notes;
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

};
