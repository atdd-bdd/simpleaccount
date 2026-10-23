#pragma once
#include <gtest/gtest.h>
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
#include "payee_rules.h"
#include "transaction.h"
#include "text_types.h"
#include "transaction_edit.h"
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
            decided, target_, ledger::transaction_count(transactions_), &chart_, rules_,
            conversions_, categories_);
        transactions_.insert(transactions_.end(), made.begin(), made.end());
        // A row whose payee has a split remembered against it opens the split
        // window rather than taking a category, so the offer is what the
        // import has to say about it.
        offer_ = payees::SplitOffer();
        for (const ledger::Transaction& t : made) {
            const std::string raw = t.raw_name.empty() ? t.payee.value() : t.raw_name;
            const payees::SplitRule* rule = payees::split_rule_for(split_rules_, raw);
            if (rule == nullptr) continue;
            offer_ = payees::offer_split(*rule, t.amount);
            break;
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

    // The rule comes first and the lines are added to it, which is the order
    // the scenarios state them in: a split rule with no lines offers nothing.
    void given_payee_split_rules_are(const std::vector<PayeeSplitRuleString>& values) {
        for (const auto& value : values) {
            payees::SplitRule rule;
            rule.pattern = blank(value.pattern);
            rule.match_type = payees::match_type_from_string(value.matchtype);
            rule.payee = blank(value.payee);
            rule.shape = payees::split_shape_from_string(value.shape);
            split_rules_.push_back(rule);
        }
    }

    void given_remembered_split_lines_are(const std::vector<RememberedSplitLineString>& values) {
        ASSERT_FALSE(split_rules_.empty()) << "no split rule to remember lines against";
        for (const auto& value : values) {
            payees::RememberedSplitLine line;
            line.category = blank(value.category);
            line.amount = Money(value.amount);
            line.percent = std::stod(value.percent);
            line.memo = blank(value.memo);
            split_rules_.back().lines.push_back(line);
        }
    }

    void then_split_offered_is(const std::vector<OfferedSplitString>& values) {
        ASSERT_TRUE(offer_.offered) << "no split was offered";
        ASSERT_EQ(values.size(), offer_.lines.size()) << "lines offered";
        for (std::size_t i = 0; i < values.size(); ++i) {
            OfferedSplitString back = values[i];
            back.category = offer_.lines[i].category;
            back.amount = offer_.lines[i].amount.in_register();
            back.memo = offer_.lines[i].memo.empty() ? "none" : offer_.lines[i].memo;
            EXPECT_EQ(values[i], back) << "line " << i + 1 << ": wanted "
                                       << values[i].to_string() << " got "
                                       << back.to_string();
        }
    }

    void then_split_shortfall_is(const std::vector<SplitShortfallString>& values) {
        ASSERT_FALSE(values.empty());
        const auto& want = values.front();
        EXPECT_EQ(Money(want.total).cents(), offer_.total.cents()) << "the total";
        EXPECT_EQ(Money(want.allocated).cents(), offer_.allocated.cents())
            << "what the lines come to";
        EXPECT_EQ(Money(want.shortfall).cents(), offer_.shortfall.cents())
            << "the shortfall";
        EXPECT_EQ(parse_bool_cell(want.balanced), offer_.balanced) << "balanced";
    }

    void given_payee_split_rules_are_as_previous() {
        ADD_FAILURE() << "Not implemented: given_payee_split_rules_are_as_previous";
    }

    void given_remembered_split_lines_are_as_previous() {
        ADD_FAILURE() << "Not implemented: given_remembered_split_lines_are_as_previous";
    }

    void when_category_changed(const std::vector<CategoryChangeString>& values) {
        ASSERT_FALSE(values.empty());
        const auto& value = values.front();
        ledger::Transaction* t = find_by_ref(value.ref);
        ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
        const std::string raw = t->raw_name.empty() ? t->payee.value() : t->raw_name;
        offered_rule_ = payees::suggest(raw, value.toaccount);
        edit::change_category(chart_, t, &splits_, value.fromaccount, value.toaccount);
    }

    void then_rule_offered_is(const std::vector<PayeeRuleString>& values) {
        ASSERT_FALSE(values.empty());
        const PayeeRuleString& want = values.front();
        EXPECT_EQ(want.pattern, offered_rule_.pattern) << "pattern";
        EXPECT_EQ(want.matchtype.value, payees::to_string(offered_rule_.match_type)) << "match type";
        EXPECT_EQ(want.payee.value, offered_rule_.payee) << "payee";
        EXPECT_EQ(blank(want.category), offered_rule_.category) << "category";
        EXPECT_EQ(parse_bool_cell(want.enabled), offered_rule_.enabled) << "enabled";
    }

    void when_rule_applied_to_existing(const std::vector<PayeeRuleString>& values) {
        ASSERT_FALSE(values.empty());
        const auto& value = values.front();
        payees::Rule rule;
        rule.pattern = value.pattern;
        rule.match_type = payees::match_type_from_string(value.matchtype);
        rule.payee = blank(value.payee);
        rule.category = blank(value.category);
        if (value.enabled != DNCString && !value.enabled.empty())
            rule.enabled = parse_bool_cell(value.enabled);
        rule_application_ = payees::apply_to_book({rule}, chart_, &transactions_);
    }

    void then_would_change(const std::vector<RuleApplicationString>& values) {
        for (const auto& value : values) {
            EXPECT_EQ(std::stoi(value.matched), rule_application_.matched) << "matched";
            EXPECT_EQ(std::stoi(value.alreadycorrect), rule_application_.already_correct)
                << "already correct";
            EXPECT_EQ(std::stoi(value.skipped), rule_application_.skipped) << "skipped";
        }
    }

    void then_payee_list_is(const std::vector<PayeeListEntryString>& values) {
        const std::vector<payees::PayeeListEntry> got = payees::payee_list(transactions_);
        ASSERT_EQ(values.size(), got.size()) << "number of payees";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].payee.value, got[i].payee) << "payee " << i;
            EXPECT_EQ(values[i].lastused, got[i].last_used.iso()) << got[i].payee << " last used";
            EXPECT_EQ(std::stoi(values[i].count), got[i].count) << got[i].payee << " count";
        }
    }

    void when_payee_typed(const std::vector<PayeeSelectString>& values) {
        ASSERT_FALSE(values.empty());
        suggested_ = payees::category_last_used_for(transactions_, chart_, splits_, values.front().payee);
    }

    void then_category_suggested_is(const std::vector<CategorySuggestionString>& values) {
        ASSERT_FALSE(values.empty());
        const auto& want = values.front();
        ASSERT_TRUE(suggested_.found) << "nothing was suggested";
        EXPECT_EQ(blank(want.category), suggested_.category) << "category";
        EXPECT_EQ(want.basedon, suggested_.based_on.iso()) << "based on";
    }

    void when_payees_merged(const std::vector<PayeeMergeString>& values) {
        ASSERT_FALSE(values.empty());
        payees::merge_payees(&transactions_, values.front().newname);
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
        for (const auto& value : values)
            EXPECT_EQ(value.value,
                      payees::to_string(payees::split_shape_from_string(value.value)))
                << "the shape " << value.value;
    }

    void examples_businessrule_how_a_percentage_split_rounds(const std::vector<PercentageSplitString>& values) {
        for (const auto& value : values) {
            const std::vector<Money> amounts = payees::percentage_amounts(
                Money(value.total),
                {std::stod(value.percenta), std::stod(value.percentb)});
            ASSERT_EQ(2u, amounts.size());
            EXPECT_EQ(Money(value.amounta).in_register(), amounts[0].in_register())
                << "the first line -- " << value.notes;
            EXPECT_EQ(Money(value.amountb).in_register(), amounts[1].in_register())
                << "the second line -- " << value.notes;
        }
    }


private:
    struct PendingSecondRow { Money amount; std::string memo; };
    std::map<std::string, PendingSecondRow> second_row_amount_;
    chart::Chart chart_;
    std::vector<ledger::Transaction> transactions_;
    std::vector<ledger::TransactionSplit> splits_;
    payees::BookApplication applied_;
    std::vector<payees::Rule> rules_;
    // The splits remembered against a payee, and the offer the last import
    // made from them. Nothing is written to the book: a remembered split is
    // offered and the window is where it is accepted.
    std::vector<payees::SplitRule> split_rules_;
    payees::SplitOffer offer_;
    payees::Refusal refusal_;
    std::string target_ = "Assets:Checking";
    payees::Rule offered_rule_;
    payees::BookApplication rule_application_;
    payees::PayeeSuggestion suggested_;
    std::vector<payees::Conversion> conversions_;
    std::vector<payees::Categorisation> categories_;

    ledger::Transaction* find_by_ref(const std::string& ref) {
        for (ledger::Transaction& t : transactions_)
            if (t.ref.value() == ref) return &t;
        return nullptr;
    }

    static payees::Rule rule_from(const RuleEditString& edit) {
        payees::Rule rule;
        rule.pattern = blank(edit.pattern);
        rule.match_type = payees::match_type_from_string(edit.matchtype);
        rule.payee = blank(edit.payee);
        rule.category = blank(edit.category);
        rule.enabled = parse_bool_cell(edit.enabled);
        return rule;
    }

    static std::string listing_rules(const std::vector<payees::Rule>& rules) {
        std::string out;
        for (const payees::Rule& one : rules)
            out += "  " + one.pattern + " (" + payees::to_string(one.match_type) +
                   ") -> " + one.payee + " " + one.category + "\n";
        return out;
    }

    // A Default of "none" arrives as the literal word.
    static std::string blank(const std::string& s) {
        return (s == "none" || s == DNCString) ? std::string() : s;
    }

public:

    // The one function the whole rule turns on: what part of a bank's name a
    // rule should be made from, so that the next visit to the same shop matches
    // too. Notes is prose for a reader and is not asserted.
    void examples_businessrule_the_significant_part_of_a_name_is_what_a_rule_is_made_from(
            const std::vector<SignificantNameString>& values) {
        for (const auto& value : values) {
            EXPECT_EQ(value.pattern, payees::pattern_for(value.rawname))
                << "the significant part of \"" << value.rawname << "\"";
        }
    }

public:

    // --------------------------------------------------------- keeping them

    // Nothing to do: the list is asked for in the Then, which is the only place
    // the order can be seen. Kept so that reading the scenario shows a window
    // being opened rather than a table appearing from nowhere.
    void when_rule_list_shown() {}

    void then_rule_list_rows_are(const std::vector<RuleListRowString>& values) {
        const std::vector<payees::Rule> got = payees::in_order(rules_);
        ASSERT_EQ(values.size(), got.size()) << "rules listed:\n" << listing_rules(got);
        for (std::size_t i = 0; i < values.size(); ++i) {
            RuleListRowString back = values[i];
            back.position = std::to_string(i + 1);
            back.pattern = got[i].pattern;
            back.matchtype = payees::to_string(got[i].match_type);
            back.payee = got[i].payee;
            back.category = got[i].category.empty() ? "none" : got[i].category;
            back.enabled = got[i].enabled ? "true" : "false";
            EXPECT_EQ(values[i], back) << "rule " << i << ": wanted "
                                       << values[i].to_string() << " got "
                                       << back.to_string();
        }
    }

    void when_rule_added(const std::vector<RuleEditString>& values) {
        ASSERT_FALSE(values.empty());
        refusal_ = payees::add(&rules_, rule_from(values.front()));
    }

    void when_rule_changed(const std::vector<RuleEditString>& values) {
        ASSERT_FALSE(values.empty());
        const RuleEditString& edit = values.front();
        refusal_ = payees::change(
            &rules_, blank(edit.waspattern),
            payees::match_type_from_string(edit.wasmatchtype), rule_from(edit));
    }

    void when_rule_deleted(const std::vector<RuleSelectString>& values) {
        ASSERT_FALSE(values.empty());
        EXPECT_TRUE(payees::remove(&rules_, values.front().pattern,
                                   payees::match_type_from_string(
                                       values.front().matchtype)))
            << "no rule looking for " << values.front().pattern;
    }

    void when_rule_disabled(const std::vector<RuleSelectString>& values) {
        ASSERT_FALSE(values.empty());
        EXPECT_TRUE(payees::set_enabled(&rules_, values.front().pattern,
                                        payees::match_type_from_string(
                                            values.front().matchtype),
                                        false))
            << "no rule looking for " << values.front().pattern;
    }

    void then_rule_refused_because(const std::vector<RuleRefusalString>& values) {
        ASSERT_FALSE(values.empty());
        RuleRefusalString back = values.front();
        back.refused = refusal_.refused ? "true" : "false";
        back.because = refusal_.reason.empty() ? "none" : refusal_.reason;
        EXPECT_EQ(values.front(), back) << "wanted " << values.front().to_string()
                                        << " got " << back.to_string();
    }

    // Driven with the two rules added in the opposite order to the one the list
    // is expected to put them in, because a rule about which comes first is
    // worth nothing if it only holds when they were typed that way.
    void examples_businessrule_the_rules_are_listed_in_the_order_they_are_tried(
            const std::vector<RuleOrderingString>& values) {
        for (const auto& value : values) {
            std::vector<payees::Rule> rules;
            payees::Rule second;
            second.pattern = value.secondpattern;
            second.match_type = payees::match_type_from_string(value.secondtype);
            second.payee = "Second";
            payees::Rule first;
            first.pattern = value.firstpattern;
            first.match_type = payees::match_type_from_string(value.firsttype);
            first.payee = "First";
            rules.push_back(second);
            rules.push_back(first);

            const std::vector<payees::Rule> listed = payees::in_order(rules);
            ASSERT_EQ(2u, listed.size());
            EXPECT_EQ("First", listed.front().payee)
                << value.firstpattern << " (" << value.firsttype << ") should be "
                << "listed before " << value.secondpattern << " ("
                << value.secondtype << "): " << value.notes;
        }
    }

public:

    void when_rules_applied_to_the_book() {
        applied_ = payees::apply_to_book(rules_, chart_, &transactions_);
    }

    void then_applying_the_rules_reported_is(const std::vector<RuleApplicationString>& values) {
        ASSERT_FALSE(values.empty());
        const RuleApplicationString& want = values.front();
        EXPECT_EQ(std::stoi(want.matched), applied_.matched) << "matched";
        EXPECT_EQ(std::stoi(want.alreadycorrect), applied_.already_correct)
            << "already correct";
        EXPECT_EQ(std::stoi(want.skipped), applied_.skipped) << "skipped";
    }

public:

    void given_transactions_are(const std::vector<TransactionString>& values) {
        transactions_.clear();
        for (const auto& value : values) {
            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef(value.ref);
            const auto on = types::Date::from_iso(value.date);
            if (on) t.date = *on;
            t.payee = types::PayeeName(blank(value.payee));
            t.memo = blank(value.memo);
            t.source_account = chart_.ref_for_path(blank(value.sourceaccount));
            t.destination = chart_.ref_for_path(blank(value.destination));
            t.amount = Money(value.amount);
            if (value.cleared.value != DNCString && !value.cleared.value.empty())
                t.cleared = types::cleared_status_from_string(value.cleared);
            transactions_.push_back(t);
        }
    }

    // The whole list, in the order the import made it. Every column the table
    // names is compared; the ones it leaves out take their defaults, which the
    // string struct has already filled in, so they are compared too.
    void then_transactions_are(const std::vector<TransactionString>& values) {
        ASSERT_EQ(values.size(), transactions_.size()) << "number of transactions";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const ledger::Transaction& got = transactions_[i];
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
            if (want.sourceaccount.value != DNCString)
                EXPECT_EQ(blank(want.sourceaccount), chart_.path_for_ref(got.source_account))
                    << where << " source account";
            if (want.destination.value != DNCString)
                EXPECT_EQ(blank(want.destination), chart_.path_for_ref(got.destination))
                    << where << " destination";
            if (want.amount.value != DNCString)
                EXPECT_EQ(Money(want.amount).cents(), got.amount.cents()) << where << " amount";
            if (want.cleared.value != DNCString && !want.cleared.value.empty())
                EXPECT_EQ(want.cleared.value, types::to_string(got.cleared)) << where << " cleared";
        }
    }

    // ----------------------------------------------- the two payee fields

    void given_payee_conversions_are(const std::vector<PayeeConversionString>& values) {
        conversions_.clear();
        for (const auto& value : values)
            conversions_.push_back(payees::Conversion{value.match, blank(value.converted)});
    }

    void given_payee_categories_are(const std::vector<PayeeCategoryString>& values) {
        categories_.clear();
        for (const auto& value : values)
            categories_.push_back(
                payees::Categorisation{value.payee, blank(value.category)});
    }

    void when_payees_converted() {
        payees::convert_payees(conversions_, &transactions_);
    }

    void when_categories_assigned() {
        applied_ = payees::assign_categories(categories_, chart_, &transactions_);
    }

    // Both fields of every transaction, by the Ref of the table that made it.
    // A raw name nothing wrote is reported as the literal word, which is what
    // the table says where a row was left as it was.
    void then_payee_names_are(const std::vector<PayeeNameRowString>& values) {
        ASSERT_EQ(values.size(), transactions_.size()) << "number of transactions";
        for (const auto& want : values) {
            const ledger::Transaction* t = find_by_ref(want.ref.value);
            ASSERT_NE(nullptr, t) << "no transaction " << want.ref.value;
            EXPECT_EQ(blank(want.rawname), t->raw_name)
                << want.ref.value << " raw name";
            EXPECT_EQ(blank(want.payee), t->payee.value())
                << want.ref.value << " payee";
        }
    }

    void examples_businessrule_how_a_conversion_pattern_is_matched(
            const std::vector<ConversionCaseString>& values) {
        for (const auto& value : values) {
            EXPECT_EQ(parse_bool_cell(value.matches),
                      payees::conversion_matches(value.match, value.rawname))
                << "\"" << value.match << "\" against \"" << value.rawname
                << "\" -- " << value.notes;
        }
    }

    // Driven with the two rows in the order the table lists them, which is the
    // whole of the rule: the first that matches is the one that answers.
    void examples_businessrule_which_conversion_is_used_when_several_match(
            const std::vector<ConversionOrderString>& values) {
        for (const auto& value : values) {
            const std::vector<payees::Conversion> table = {
                payees::Conversion{value.firstmatch, value.firstconverted.value},
                payees::Conversion{value.secondmatch, value.secondconverted.value},
            };
            const payees::Conversion* got = payees::conversion_for(table, value.rawname);
            ASSERT_NE(nullptr, got) << value.rawname << " -- " << value.notes;
            EXPECT_EQ(value.converted.value, got->converted)
                << value.rawname << " -- " << value.notes;
        }
    }

    // One row in the table, offered both names. What is asserted is whether it
    // applied and which of the two it was written against.
    void examples_businessrule_the_converted_name_is_tried_first_and_the_name_the_bank_sent_second(
            const std::vector<WhichNameCategorisedString>& values) {
        for (const auto& value : values) {
            const std::vector<payees::Categorisation> table = {
                payees::Categorisation{value.pattern, "Expenses:Groceries"},
            };
            const payees::Categorised got = payees::category_for(
                table, blank(value.converted), blank(value.rawname));
            EXPECT_EQ(parse_bool_cell(value.applies), got.found)
                << value.pattern << " -- " << value.notes;
            const std::string against =
                !got.found ? "none" : (got.against_converted ? "Converted" : "RawName");
            EXPECT_EQ(value.against, against) << value.pattern << " -- " << value.notes;
        }
    }

    void then_transaction_is(const std::vector<TransactionString>& values) {
        for (const auto& value : values) {
            const ledger::Transaction* t = find_by_ref(value.ref);
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            if (value.payee.value != DNCString)
                EXPECT_EQ(blank(value.payee), t->payee.value()) << value.ref << " payee";
            if (value.destination.value != DNCString)
                EXPECT_EQ(blank(value.destination), chart_.path_for_ref(t->destination))
                    << value.ref << " destination";
            if (value.sourceaccount.value != DNCString)
                EXPECT_EQ(blank(value.sourceaccount), chart_.path_for_ref(t->source_account))
                    << value.ref << " source account";
            if (value.amount.value != DNCString)
                EXPECT_EQ(Money(value.amount).cents(), t->amount.cents())
                    << value.ref << " amount";
        }
    }
public:

    void examples_businessrule_what_makes_two_rules_the_same_rule(const std::vector<SameRuleCaseString>& values) {
        for (const auto& value : values) {
            const bool got = payees::same_rule(
                value.pattern, payees::match_type_from_string(value.matchtype),
                value.againstpattern,
                payees::match_type_from_string(value.againstmatchtype));
            EXPECT_EQ(parse_bool_cell(value.samerule), got)
                << value.pattern << " " << value.matchtype << " against "
                << value.againstpattern << " " << value.againstmatchtype
                << " -- " << value.notes;
        }
    }

};
