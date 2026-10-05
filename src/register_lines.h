#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "posting.h"
#include "register_view.h"
#include "text_types.h"

// Building the lines of a register from the transactions of a book. One line per
// posting to the account the register is open on, with the other side of the
// transaction summarised in the category column. See TransactionRegister.spectable.
namespace reg {

struct Line {
    std::string ref;
    types::Date date;
    std::string check_no;
    std::string payee;
    std::string category;   // the other account, or --Split-- where there is no one other
    // The whole of the other side, for a reader who wants it: the full path
    // where there is one other posting, and every category with its amount
    // where the transaction is a split. --Split-- in a column says nothing on
    // its own, and this is what it stands for.
    std::string category_detail;
    std::string memo;
    Money payment;          // money out of this account
    Money deposit;          // money into it
    types::ClearedStatus cleared = types::ClearedStatus::Uncleared;
    Money balance;          // running, with the display sign of the account
};

// Selecting a placeholder shows everything beneath it: a placeholder takes no
// postings of its own, so a register of only its own would be empty.
inline std::vector<Line> lines_for(
        const chart::Chart& accounts,
        const std::vector<ledger::Transaction>& transactions,
        const std::string& account) {
    const chart::Account* a = accounts.find(account);
    const int sign = a != nullptr ? types::display_sign(a->type) : 1;

    // Date, then the order the transactions were entered. Entry order is stable:
    // importing more never reshuffles lines already there, so the running balance
    // against a line does not change under the reader.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < transactions.size(); ++i) order.push_back(i);
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t x, std::size_t y) {
                         return transactions[x].date < transactions[y].date;
                     });

    std::vector<Line> out;
    Money running;
    for (std::size_t index : order) {
        const ledger::Transaction& t = transactions[index];
        // Every posting of this transaction that lands in the register.
        for (const ledger::Posting& p : t.postings) {
            if (!chart::Chart::is_descendant_or_self(p.account.value(), account)) continue;
            Line line;
            line.ref = t.ref.value();
            line.date = t.date;
            line.check_no = t.check_no.value();
            line.payee = t.payee.value();
            line.memo = p.memo.empty() ? t.memo : p.memo;
            line.cleared = p.cleared;
            if (p.amount.cents() < 0) line.payment = -p.amount;
            else line.deposit = p.amount;

            // The other side: the one other account where there is exactly one,
            // and otherwise that there are several.
            std::vector<std::string> others;
            for (const ledger::Posting& q : t.postings)
                if (&q != &p) others.push_back(q.account.value());
            line.category = others.size() == 1 ? others.front() : std::string("--Split--");
            if (others.empty()) line.category.clear();

            if (others.size() == 1) {
                line.category_detail = others.front();
            } else {
                for (const ledger::Posting& q : t.postings) {
                    if (&q == &p) continue;
                    if (!line.category_detail.empty()) line.category_detail += '\n';
                    line.category_detail += q.account.value() + "   " +
                                            q.amount.in_register();
                    if (!q.memo.empty()) line.category_detail += "   " + q.memo;
                }
            }

            running += p.amount;
            line.balance = sign < 0 ? -running : running;
            out.push_back(line);
        }
    }
    return out;
}

}  // namespace reg
