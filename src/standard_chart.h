#pragma once
#include "account_type.h"
#include "chart.h"
#include "text_types.h"

namespace chart {

namespace detail {
inline void put(Chart* chart, const char* path, types::AccountType type,
                bool placeholder) {
    Account account;
    account.path = types::AccountPath(path);
    account.type = type;
    account.placeholder = placeholder;
    account.hidden = false;
    chart->put(account);
}
}  // namespace detail

// What a book that starts from nothing starts with: three accounts somebody
// can type into on the first day, and a category list taken from Quicken's
// own, because a stranger opening this program expects the categories they
// have used for twenty years. Uncategorized is this program's own addition --
// an import puts rows in it from the first file onwards. Every parent is a
// placeholder: it has no register and its total is what is underneath it.
//
// The list is the one in the new-book section of UserInterface.spectable, and
// that scenario is what keeps the two the same.
inline Chart standard_chart() {
    using detail::put;
    Chart chart;
    put(&chart, "Checking", types::AccountType::Checking, false);
    put(&chart, "Savings", types::AccountType::Savings, false);
    put(&chart, "Credit Card", types::AccountType::CreditCard, false);
    put(&chart, "Expenses", types::AccountType::Expense, true);
    put(&chart, "Expenses:Auto", types::AccountType::Expense, true);
    put(&chart, "Expenses:Auto:Fuel", types::AccountType::Expense, false);
    put(&chart, "Expenses:Auto:Insurance", types::AccountType::Expense, false);
    put(&chart, "Expenses:Auto:Registration", types::AccountType::Expense, false);
    put(&chart, "Expenses:Auto:Service", types::AccountType::Expense, false);
    put(&chart, "Expenses:Bank Charge", types::AccountType::Expense, false);
    put(&chart, "Expenses:Charity", types::AccountType::Expense, false);
    put(&chart, "Expenses:Childcare", types::AccountType::Expense, false);
    put(&chart, "Expenses:Clothing", types::AccountType::Expense, false);
    put(&chart, "Expenses:Dining", types::AccountType::Expense, false);
    put(&chart, "Expenses:Education", types::AccountType::Expense, false);
    put(&chart, "Expenses:Entertainment", types::AccountType::Expense, false);
    put(&chart, "Expenses:Gifts Given", types::AccountType::Expense, false);
    put(&chart, "Expenses:Groceries", types::AccountType::Expense, false);
    put(&chart, "Expenses:Home Repair", types::AccountType::Expense, false);
    put(&chart, "Expenses:Household", types::AccountType::Expense, false);
    put(&chart, "Expenses:Insurance", types::AccountType::Expense, false);
    put(&chart, "Expenses:Interest Expense", types::AccountType::Expense, false);
    put(&chart, "Expenses:Medical", types::AccountType::Expense, true);
    put(&chart, "Expenses:Medical:Dentist", types::AccountType::Expense, false);
    put(&chart, "Expenses:Medical:Doctor", types::AccountType::Expense, false);
    put(&chart, "Expenses:Medical:Medicine", types::AccountType::Expense, false);
    put(&chart, "Expenses:Miscellaneous", types::AccountType::Expense, false);
    put(&chart, "Expenses:Mortgage Interest", types::AccountType::Expense, false);
    put(&chart, "Expenses:Personal Care", types::AccountType::Expense, false);
    put(&chart, "Expenses:Pet Care", types::AccountType::Expense, false);
    put(&chart, "Expenses:Recreation", types::AccountType::Expense, false);
    put(&chart, "Expenses:Subscriptions", types::AccountType::Expense, false);
    put(&chart, "Expenses:Taxes", types::AccountType::Expense, true);
    put(&chart, "Expenses:Taxes:Federal", types::AccountType::Expense, false);
    put(&chart, "Expenses:Taxes:Property", types::AccountType::Expense, false);
    put(&chart, "Expenses:Taxes:Social Security", types::AccountType::Expense, false);
    put(&chart, "Expenses:Taxes:State", types::AccountType::Expense, false);
    put(&chart, "Expenses:Telephone", types::AccountType::Expense, false);
    put(&chart, "Expenses:Travel", types::AccountType::Expense, false);
    put(&chart, "Expenses:Uncategorized", types::AccountType::Expense, false);
    put(&chart, "Expenses:Utilities", types::AccountType::Expense, true);
    put(&chart, "Expenses:Utilities:Cable", types::AccountType::Expense, false);
    put(&chart, "Expenses:Utilities:Electric", types::AccountType::Expense, false);
    put(&chart, "Expenses:Utilities:Gas", types::AccountType::Expense, false);
    put(&chart, "Expenses:Utilities:Water", types::AccountType::Expense, false);
    put(&chart, "Income", types::AccountType::Income, true);
    put(&chart, "Income:Bonus", types::AccountType::Income, false);
    put(&chart, "Income:Dividend", types::AccountType::Income, false);
    put(&chart, "Income:Gifts Received", types::AccountType::Income, false);
    put(&chart, "Income:Interest", types::AccountType::Income, false);
    put(&chart, "Income:Other Income", types::AccountType::Income, false);
    put(&chart, "Income:Salary", types::AccountType::Income, false);
    return chart;
}

}  // namespace chart
