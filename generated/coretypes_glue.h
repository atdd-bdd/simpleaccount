#pragma once
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>
#include "common/common.h"
#include "account_type.h"
#include "money.h"
#include "text_types.h"

// Glue for CoreTypes.spectable. Generated once as stubs and then written by
// hand; the converter does not overwrite a glue file that already exists.
class CoreTypesGlue {
public:
    // Each ValidValues table says which values a type accepts. The type throws
    // std::invalid_argument on the ones it does not, so the check is the same
    // shape for every one of them.
    template <typename T>
    static void check_valid_values(const std::vector<ValidValuesString>& values,
                                   const char* what) {
        for (const auto& value : values) {
            const ValidValuesTyped v = ValidValuesTyped::from_string_struct(value);
            const std::string cell = v.value;
            bool accepted = true;
            try {
                // Braces, not parentheses: T(cell) is read as a declaration of a
                // variable named cell, so the constructor never runs and nothing
                // ever throws.
                const T parsed{cell};
                (void)parsed;
            } catch (const std::invalid_argument&) {
                accepted = false;
            }
            EXPECT_EQ(v.isvalid, accepted)
                << what << " given [" << cell << "]"
                << (v.notes.empty() ? "" : " -- " + v.notes);
        }
    }

    void examples_datatype_money(const std::vector<ValidValuesString>& values) {
        check_valid_values<Money>(values, "Money");
    }

    void examples_businessrule_money_is_held_as_whole_cents(
            const std::vector<MoneyParseString>& values) {
        for (const auto& value : values) {
            const MoneyParseTyped t = MoneyParseTyped::from_string_struct(value);
            const std::string cell = t.text;
            EXPECT_EQ(static_cast<long long>(t.cents), Money(cell).cents())
                << "parsing [" << cell << "]";
        }
    }

    void examples_businessrule_money_formats_for_display(
            const std::vector<MoneyFormatString>& values) {
        for (const auto& value : values) {
            const MoneyFormatTyped t = MoneyFormatTyped::from_string_struct(value);
            const Money m = Money::from_cents(t.cents);
            EXPECT_EQ(t.inregister, m.in_register()) << t.cents << " in the register";
            EXPECT_EQ(t.inreport, m.in_report()) << t.cents << " in a report";
        }
    }

    void examples_datatype_accountname(const std::vector<ValidValuesString>& values) {
        check_valid_values<types::AccountName>(values, "AccountName");
    }

    void examples_datatype_accountpath(const std::vector<ValidValuesString>& values) {
        check_valid_values<types::AccountPath>(values, "AccountPath");
    }

    void examples_datatype_payeename(const std::vector<ValidValuesString>& values) {
        check_valid_values<types::PayeeName>(values, "PayeeName");
    }

    void examples_datatype_symbol(const std::vector<ValidValuesString>& values) {
        check_valid_values<types::Symbol>(values, "Symbol");
        // The rule also says a symbol is folded to upper case on input.
        EXPECT_EQ("VTSAX", types::Symbol("vtsax").value());
    }

    void examples_datatype_securityname(const std::vector<ValidValuesString>& values) {
        check_valid_values<types::SecurityName>(values, "SecurityName");
    }

    void examples_datatype_shares(const std::vector<ValidValuesString>& values) {
        check_valid_values<types::Shares>(values, "Shares");
    }

    void examples_datatype_price(const std::vector<ValidValuesString>& values) {
        check_valid_values<types::Price>(values, "Price");
    }

    void examples_datatype_checknumber(const std::vector<ValidValuesString>& values) {
        check_valid_values<types::CheckNumber>(values, "CheckNumber");
    }

    void examples_datatype_fitid(const std::vector<ValidValuesString>& values) {
        check_valid_values<types::FitId>(values, "FitId");
    }

    // An EnumerationValues table is the fixed set of values the type takes, so
    // what it asserts is that each one is known and reads back as itself.
    void examples_datatype_accounttype(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            ASSERT_NO_THROW(types::account_type_from_string(v.value)) << v.value;
            EXPECT_EQ(v.value, types::to_string(types::account_type_from_string(v.value)));
        }
    }

    void examples_datatype_accountclass(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            ASSERT_NO_THROW(types::account_class_from_string(v.value)) << v.value;
            EXPECT_EQ(v.value, types::to_string(types::account_class_from_string(v.value)));
        }
    }

    void examples_datatype_clearedstatus(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            ASSERT_NO_THROW(types::cleared_status_from_string(v.value)) << v.value;
            EXPECT_EQ(v.value, types::to_string(types::cleared_status_from_string(v.value)));
        }
    }

    void examples_businessrule_account_class_follows_from_account_type(
            const std::vector<AccountClassOfString>& values) {
        for (const auto& value : values) {
            const AccountClassOfTyped t = AccountClassOfTyped::from_string_struct(value);
            const types::AccountType type = types::account_type_from_string(t.accounttype);
            EXPECT_EQ(t.accountclass, types::to_string(types::class_of(type)))
                << t.accounttype;
        }
    }
};
