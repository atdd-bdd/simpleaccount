#pragma once
#include <gtest/gtest.h>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#include "common/common.h"
#include "text_types.h"
#include "qif_lexer.h"
#include "posting.h"
#include "money.h"
#include "date_parse.h"
#include "date.h"
#include "chart.h"
#include "ledger.h"
#include "register_entry.h"
#include "transaction_edit.h"

// Glue for Transactions.spectable.
//
// Three state-building paths, same as the production code they mirror: raw
// PostingRow tables go through ledger::validate() the way nothing in the
// window does yet (there is no free-form posting entry); a register entry or
// a split goes through reg::commit()/commit_split(), exactly as the real
// register does; and the rest -- amount, category, delete, clear, assign,
// match -- are the edit:: functions MainWindow itself calls.
class TransactionsGlue {
public:
    static constexpr const char* DNC_STRING = "?DNC?";

    // ---------------------------------------------------------------- givens

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

    // Dead: no step in this file reads "as previous" for the chart, so this is
    // never called. Left harmless rather than removed -- see the converter's
    // append-only rule on hand-written glue.
    void given_the_chart_of_accounts_is_as_previous() {}

    void given_postings_are(const std::vector<PostingRowString>& values) {
        transactions_.clear();
        for (const auto& value : values) group_into(&transactions_, value);
        rebuild_book();
    }

    // ----------------------------------------------------------------- whens

    void when_postings_committed(const std::vector<PostingRowString>& values) {
        std::vector<ledger::Transaction> batch;
        for (const auto& value : values) group_into(&batch, value);
        rejection_ = {};
        for (const ledger::Transaction& t : batch) {
            rejection_ = ledger::validate(chart_, t);
            if (rejection_.refused) return;   // all or nothing: nothing committed
        }
        for (const ledger::Transaction& t : batch) transactions_.push_back(t);
        rebuild_book();
    }

    void when_register_entry_committed(const std::vector<RegisterEntryString>& values) {
        for (const auto& value : values) {
            const reg::BlankLine line = to_blank_line(value);
            const reg::Committed done =
                reg::commit(line, value.account, transactions_.size(), &chart_);
            if (done.committed) {
                transactions_.push_back(done.transaction);
                rejection_ = {};
            } else if (done.reason != "nothing was entered") {
                rejection_ = {true, done.reason};
            }
        }
        rebuild_book();
    }

    void when_split_entry_committed(const std::vector<RegisterEntryString>& values) {
        ASSERT_FALSE(values.empty());
        pending_line_ = to_blank_line(values.front());
        pending_account_ = values.front().account;
    }

    void when_split_lines_are(const std::vector<SplitLineString>& values) {
        std::vector<reg::SplitLine> lines;
        for (const auto& value : values) {
            reg::SplitLine l;
            l.category = value.category;
            l.amount = Money(value.amount);
            l.memo = blank(value.memo);
            lines.push_back(l);
        }
        const reg::Committed done = reg::commit_split(
            pending_line_, pending_account_, lines, transactions_.size(), &chart_);
        if (done.committed) {
            transactions_.push_back(done.transaction);
            rejection_ = {};
        } else {
            rejection_ = {true, done.reason};
        }
        rebuild_book();
    }

    void when_amount_changed(const std::vector<AmountChangeString>& values) {
        for (const auto& value : values) {
            const Money payment{value.payment};
            const Money deposit{value.deposit};
            const Money signed_amount = payment.cents() != 0 ? -payment : deposit;
            const edit::AmountOutcome done =
                edit::change_amount(&transactions_, value.ref, value.account, signed_amount);
            warning_ = done.warning;
            rejection_ = done.changed ? chart::Rejection{} : chart::Rejection{true, done.reason};
        }
        rebuild_book();
    }

    void when_category_changed(const std::vector<CategoryChangeString>& values) {
        for (const auto& value : values) {
            ledger::Transaction* t = find_by_ref(value.ref);
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            const edit::Outcome done =
                edit::change_category(t, value.fromaccount, value.toaccount);
            rejection_ = done.changed ? chart::Rejection{} : chart::Rejection{true, done.reason};
        }
        rebuild_book();
    }

    void when_transaction_deleted(const std::vector<TransactionSelectString>& values) {
        for (const auto& value : values) edit::delete_transaction(&transactions_, value.ref);
        rebuild_book();
    }

    void when_posting_cleared(const std::vector<PostingSelectString>& values) {
        for (const auto& value : values) {
            ledger::Transaction* t = find_by_ref(value.ref);
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            bool found = false;
            for (ledger::Posting& p : t->postings)
                if (p.account.value() == value.account) {
                    p.cleared = types::ClearedStatus::Cleared;
                    found = true;
                }
            EXPECT_TRUE(found) << "no posting on " << value.account << " in " << value.ref;
        }
        rebuild_book();
    }

    // These two production functions take the transaction's id, not its Ref --
    // the id is what MainWindow actually has in hand, from a selection in the
    // register, and Ref is only ever a label of these tables. So the Ref is
    // resolved to an id here, the one place that gap between spec and
    // production addressing has to be bridged.
    void when_the_other_account_is_assigned(const std::vector<AccountAssignmentString>& values) {
        for (const auto& value : values) {
            const ledger::Transaction* t = find_by_ref(value.ref);
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            const std::string id = t->id.value();
            EXPECT_TRUE(reg::assign_other_account(&transactions_, id, value.account))
                << "nothing unassigned in " << value.ref;
        }
        rebuild_book();
    }

    void when_the_other_entry_is_matched(const std::vector<EntryMatchString>& values) {
        for (const auto& value : values) {
            const ledger::Transaction* t = find_by_ref(value.ref);
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            const ledger::Transaction* other = find_by_ref(value.otherref);
            ASSERT_NE(nullptr, other) << "no transaction " << value.otherref;
            const std::string id = t->id.value();
            const std::string other_id = other->id.value();
            EXPECT_TRUE(reg::join_as_transfer(&transactions_, id, other_id))
                << value.ref << " could not be joined with " << value.otherref;
        }
        rebuild_book();
    }

    void when_entries_offered_for(const std::vector<AccountAssignmentString>& values) {
        ASSERT_FALSE(values.empty());
        offered_ = reg::entries_offered_for(transactions_, values.front().ref,
                                            values.front().account);
    }

    void when_transactions_are_created_in_a_row(
            const std::vector<TransactionsCreatedString>& values) {
        ASSERT_FALSE(values.empty());
        const int n = std::stoi(values.front().howmany);
        created_names_.clear();
        for (int i = 0; i < n; ++i) created_names_.push_back(ledger::new_id().value());
    }

    // ----------------------------------------------------------------- thens

    void then_rejected_because(const std::vector<RejectionString>& values) {
        for (const auto& value : values) {
            EXPECT_TRUE(rejection_.refused) << "expected a refusal: " << value.reason;
            EXPECT_EQ(value.reason, rejection_.reason);
        }
    }

    void then_postings_are(const std::vector<PostingRowString>& values) {
        const std::vector<PostingRowString> got = flatten();
        ASSERT_EQ(values.size(), got.size()) << "postings in the book:\n" << listing(got);
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].ref, got[i].ref) << "posting " << i << " ref";
            EXPECT_EQ(values[i].date, got[i].date) << "posting " << i << " date";
            EXPECT_EQ(blank(values[i].payee), blank(got[i].payee)) << "posting " << i << " payee";
            EXPECT_EQ(values[i].account, got[i].account) << "posting " << i << " account";
            EXPECT_EQ(Money(values[i].amount).cents(), Money(got[i].amount).cents())
                << "posting " << i << " amount";
            EXPECT_EQ(blank(values[i].memo), blank(got[i].memo)) << "posting " << i << " memo";
            if (values[i].cleared != DNCString)
                EXPECT_EQ(values[i].cleared, got[i].cleared) << "posting " << i << " cleared";
        }
    }

    void then_transaction_is(const std::vector<TransactionString>& values) {
        for (const auto& value : values) {
            const ledger::Transaction* t = find_by_ref(value.ref);
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            // Id is deliberately never named by a table -- see its Notes in the
            // Entity -- so it is the one field this never compares.
            EXPECT_EQ(value.date, t->date.iso()) << value.ref << " date";
            EXPECT_EQ(blank(value.payee), t->payee.value()) << value.ref << " payee";
            EXPECT_EQ(blank(value.checkno), t->check_no.value()) << value.ref << " check no";
            EXPECT_EQ(blank(value.memo), t->memo) << value.ref << " memo";
            EXPECT_EQ(blank(value.tag), t->tag) << value.ref << " tag";
        }
    }

    void then_transaction_is_a_transfer(const std::vector<TransferCheckString>& values) {
        ASSERT_FALSE(transactions_.empty());
        const ledger::Transaction& t = transactions_.back();
        std::vector<types::AccountType> kinds;
        for (const ledger::Posting& p : t.postings) {
            const chart::Account* a = chart_.find(p.account.value());
            kinds.push_back(a == nullptr ? types::AccountType::Checking : a->type);
        }
        const bool is_transfer = ledger::is_transfer(kinds);
        std::string from, to;
        if (is_transfer) {
            for (const ledger::Posting& p : t.postings) {
                if (p.amount.cents() < 0) from = p.account.value();
                else if (p.amount.cents() > 0) to = p.account.value();
            }
        }
        for (const auto& value : values) {
            EXPECT_EQ(parse_bool_cell(value.istransfer), is_transfer) << "is a transfer";
            EXPECT_EQ(blank(value.fromaccount), from) << "from account";
            EXPECT_EQ(blank(value.toaccount), to) << "to account";
        }
    }

    void then_balances_are(const std::vector<AccountBalanceString>& values) {
        for (const auto& value : values) {
            const chart::Account* a = chart_.find(value.account);
            ASSERT_NE(nullptr, a) << "no account " << value.account;
            if (value.rawbalance != DNCString)
                EXPECT_EQ(Money(value.rawbalance).cents(), book_.raw_balance(value.account).cents())
                    << value.account << " raw -- " << value.notes;
            if (value.displaybalance != DNCString)
                EXPECT_EQ(Money(value.displaybalance).cents(),
                          book_.display_balance(value.account, a->type).cents())
                    << value.account << " display -- " << value.notes;
        }
    }

    void then_warned_that(const std::vector<WarningString>& values) {
        for (const auto& value : values) EXPECT_EQ(value.message, warning_);
    }

    void then_the_names_are_all_different() {
        const std::set<std::string> uniq(created_names_.begin(), created_names_.end());
        EXPECT_EQ(created_names_.size(), uniq.size()) << "a name was repeated";
    }

    void then_the_names_increase() {
        for (std::size_t i = 1; i < created_names_.size(); ++i)
            EXPECT_LT(created_names_[i - 1], created_names_[i]) << "name " << i;
    }

    void then_entries_offered_are(const std::vector<OfferedEntryString>& values) {
        ASSERT_EQ(values.size(), offered_.size()) << "entries offered";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].ref, offered_[i].ref) << "entry " << i;
            EXPECT_EQ(values[i].date, offered_[i].date.iso()) << "entry " << i << " date";
            EXPECT_EQ(blank(values[i].payee), offered_[i].payee) << "entry " << i << " payee";
            EXPECT_EQ(Money(values[i].amount).cents(), offered_[i].amount.cents())
                << "entry " << i << " amount";
        }
    }

    // ------------------------------------------------------------ data types

    template <typename T>
    static void check_valid_values(const std::vector<ValidValuesString>& values,
                                   const char* what) {
        for (const auto& value : values) {
            const ValidValuesTyped v = ValidValuesTyped::from_string_struct(value);
            const std::string cell = v.value;
            bool accepted = true;
            try {
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

    void examples_datatype_transactionref(const std::vector<ValidValuesString>& values) {
        check_valid_values<types::TransactionRef>(values, "TransactionRef");
    }

    void examples_datatype_transactionid(const std::vector<ValidValuesString>& values) {
        check_valid_values<types::TransactionId>(values, "TransactionId");
    }

    void examples_datatype_importsource(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            bool known = false;
            for (types::ImportSource s :
                 {types::ImportSource::Ofx, types::ImportSource::Csv, types::ImportSource::Qif})
                if (types::to_string(s) == v.value) known = true;
            EXPECT_TRUE(known) << "unknown import source: " << v.value;
        }
    }

    // ------------------------------------------------------------ the rules

    void examples_businessrule_every_transaction_balances(
            const std::vector<BalanceCheckString>& values) {
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

    void examples_businessrule_a_register_entry_becomes_two_postings(
            const std::vector<RegisterEntryToPostingsString>& values) {
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

    void examples_businessrule_which_uncategorized_account_the_other_side_takes(
            const std::vector<UncategorizedChoiceString>& values) {
        for (const auto& value : values) {
            const UncategorizedChoiceTyped t =
                UncategorizedChoiceTyped::from_string_struct(value);
            EXPECT_EQ(t.otheraccount,
                      ledger::uncategorized_for(Money(t.knownamount)).value())
                << t.knownamount << " -- " << t.notes;
        }
    }

    // Unassigned and Uncategorized are two different unknowns by construction
    // -- a real account and a category -- rather than by anything this table
    // states and a fixture could get wrong, so what is checked is that the
    // production code actually keeps them apart.
    void examples_businessrule_an_unknown_category_and_an_unknown_account_are_different_unknowns(
            const std::vector<TwoUnknownsString>& values) {
        for (const auto& value : values) {
            if (value.theunknown == "Unassigned") {
                EXPECT_EQ(std::string("A real account"), value.whereitlives);
                EXPECT_TRUE(ledger::is_unassigned(ledger::unassigned()));
            } else if (value.theunknown == "Uncategorized") {
                EXPECT_EQ(std::string("A category"), value.whereitlives);
                EXPECT_NE(ledger::unassigned().value(),
                         ledger::uncategorized_for(Money("10.00")).value());
                EXPECT_NE(ledger::unassigned().value(),
                         ledger::uncategorized_for(Money("-10.00")).value());
            } else {
                FAIL() << "unknown row: " << value.theunknown;
            }
        }
    }

    // Left unimplemented on purpose: both tables are pure narrative (which row
    // is which kind of unknown's own description, not anything a fixture
    // computes), and the scenarios that give them teeth -- the deposit that
    // stays a category, the entry that gets matched or doesn't -- are
    // implemented and live above. See the Details on each rule in the spec.
    void examples_businessrule_whether_the_other_side_is_an_account_or_a_category(
            const std::vector<WhereTheOtherSideLivesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_whether_the_other_side_is_an_account_or_a_category";
    }

    void examples_businessrule_assigning_an_account_asks_whether_the_other_entry_already_exists(
            const std::vector<AssignmentOutcomeString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_assigning_an_account_asks_whether_the_other_entry_already_exists";
    }

    void examples_businessrule_a_transaction_is_named_when_it_is_created_and_keeps_that_name(
            const std::vector<TransactionNamingString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_a_transaction_is_named_when_it_is_created_and_keeps_that_name";
    }

private:
    static std::string blank(const std::string& s) {
        return (s == "none" || s == DNCString) ? std::string() : s;
    }

    static void group_into(std::vector<ledger::Transaction>* into,
                           const PostingRowString& value) {
        ledger::Transaction* found = nullptr;
        for (ledger::Transaction& t : *into)
            if (t.ref.value() == value.ref) { found = &t; break; }
        if (found == nullptr) {
            ledger::Transaction made;
            made.id = ledger::new_id();
            made.ref = types::TransactionRef(value.ref);
            const auto on = types::Date::from_iso(value.date);
            if (on) made.date = *on;
            made.payee = types::PayeeName(blank(value.payee));
            into->push_back(made);
            found = &into->back();
        }
        ledger::Posting p;
        p.account = types::AccountPath(value.account);
        p.amount = Money(value.amount);
        p.memo = blank(value.memo);
        if (value.cleared != DNCString && !value.cleared.empty())
            p.cleared = types::cleared_status_from_string(value.cleared);
        found->postings.push_back(p);
    }

    static reg::BlankLine to_blank_line(const RegisterEntryString& value) {
        reg::BlankLine line;
        const auto on = types::Date::from_iso(value.date);
        if (on) line.date = *on;
        line.payee = blank(value.payee);
        line.check_no = blank(value.checkno);
        line.category = blank(value.category);
        line.payment = Money(value.payment);
        line.deposit = Money(value.deposit);
        line.memo = blank(value.memo);
        return line;
    }

    ledger::Transaction* find_by_ref(const std::string& ref) {
        for (ledger::Transaction& t : transactions_)
            if (t.ref.value() == ref) return &t;
        return nullptr;
    }

    void rebuild_book() {
        book_ = ledger::Ledger();
        for (const ledger::Transaction& t : transactions_)
            for (const ledger::Posting& p : t.postings)
                book_.add({t.date, p.account, p.amount, t.ref.value()});
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

    chart::Chart chart_;
    std::vector<ledger::Transaction> transactions_;
    ledger::Ledger book_;
    chart::Rejection rejection_;
    std::string warning_;
    reg::BlankLine pending_line_;
    std::string pending_account_;
    std::vector<reg::OfferedEntry> offered_;
    std::vector<std::string> created_names_;
};
