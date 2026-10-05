#pragma once
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>
#include "common/common.h"
#include "text_types.h"
#include "qif_lexer.h"
#include "posting.h"
#include "money.h"
#include "date_parse.h"
#include "date.h"

class TransactionsGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

    void given_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is";
    }

    void when_postings_committed(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_postings_committed";
    }

    void then_rejected_because(const std::vector<RejectionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_rejected_because";
    }

    void given_the_chart_of_accounts_is_as_previous() {
        ADD_FAILURE() << "Not implemented: given_the_chart_of_accounts_is_as_previous";
    }

    void when_register_entry_committed(const std::vector<RegisterEntryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_register_entry_committed";
    }

    void then_postings_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_postings_are";
    }

    void then_transaction_is(const std::vector<TransactionString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_transaction_is";
    }

    void then_transaction_is_a_transfer(const std::vector<TransferCheckString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_transaction_is_a_transfer";
    }

    void then_balances_are(const std::vector<AccountBalanceString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_balances_are";
    }

    void when_split_entry_committed(const std::vector<RegisterEntryString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_split_entry_committed";
    }

    void when_split_lines_are(const std::vector<SplitLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_split_lines_are";
    }

    void given_postings_are(const std::vector<PostingRowString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: given_postings_are";
    }

    void when_amount_changed(const std::vector<AmountChangeString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_amount_changed";
    }

    void when_category_changed(const std::vector<CategoryChangeString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_category_changed";
    }

    void when_transaction_deleted(const std::vector<TransactionSelectString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_transaction_deleted";
    }

    void then_warned_that(const std::vector<WarningString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_warned_that";
    }

    void when_posting_cleared(const std::vector<PostingSelectString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_posting_cleared";
    }

    void examples_datatype_transactionref(const std::vector<ValidValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_transactionref";
    }

    void examples_businessrule_every_transaction_balances(const std::vector<BalanceCheckString>& values) {
        for (const auto& value : values) {
            const BalanceCheckTyped t = BalanceCheckTyped::from_string_struct(value);
            // The table is in cents, and a blank column is a posting that is not
            // there. from_string_struct gives a blank as zero, so the count of
            // postings comes from the string struct.
            std::vector<Money> amounts;
            const std::string cells[4] = {value.a1, value.a2, value.a3, value.a4};
            const int ints[4] = {t.a1, t.a2, t.a3, t.a4};
            for (int i = 0; i < 4; ++i)
                if (!cells[i].empty()) amounts.push_back(Money::from_cents(ints[i]));
            EXPECT_EQ(static_cast<long long>(t.total),
                      ledger::imbalance_of(amounts).cents()) << t.notes;
            EXPECT_EQ(t.balanced, ledger::balances(amounts)) << t.notes;
        }
    }

    void examples_businessrule_a_register_entry_becomes_two_postings(const std::vector<RegisterEntryToPostingsString>& values) {
        for (const auto& value : values) {
            const RegisterEntryToPostingsTyped t =
                RegisterEntryToPostingsTyped::from_string_struct(value);
            const Money payment{t.payment};
            const Money deposit{t.deposit};
            EXPECT_EQ(Money(t.accountamount).cents(),
                      ledger::account_amount_of(payment, deposit).cents()) << t.notes;
            EXPECT_EQ(Money(t.categoryamount).cents(),
                      ledger::category_amount_of(payment, deposit).cents()) << t.notes;
        }
    }

    void examples_businessrule_which_uncategorized_account_the_other_side_takes(const std::vector<UncategorizedChoiceString>& values) {
        for (const auto& value : values) {
            const UncategorizedChoiceTyped t =
                UncategorizedChoiceTyped::from_string_struct(value);
            EXPECT_EQ(t.otheraccount,
                      ledger::uncategorized_for(Money(t.knownamount)).value())
                << t.knownamount << " -- " << t.notes;
        }
    }

public:

    void examples_datatype_importsource(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_importsource";
    }

};
