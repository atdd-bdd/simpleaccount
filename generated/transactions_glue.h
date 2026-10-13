#pragma once
#include <gtest/gtest.h>
#include <iostream>
#include <map>
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
        splits_.clear();
        second_row_amount_.clear();
        std::map<std::string, int> counts;
        for (const auto& value : values) ++counts[value.ref];
        for (const auto& value : values) group_into(&transactions_, value, counts[value.ref]);
        rebuild_book();
    }

    // ----------------------------------------------------------------- whens

    void when_postings_committed(const std::vector<PostingRowString>& values) {
        rejection_ = {};
        // An exactly-two-row group that does not sum to zero has nowhere to
        // keep the disagreement under this shape -- a plain Transaction's
        // Destination balances by construction, so a mismatch has to be
        // caught here, against the rows as given, before it is lost.
        std::map<std::string, Money> sums;
        std::map<std::string, int> counts;
        for (const auto& value : values) {
            sums[value.ref] += Money(value.amount);
            ++counts[value.ref];
        }
        for (const auto& entry : counts) {
            if (entry.second != 2 || sums[entry.first].cents() == 0) continue;
            const Money off = sums[entry.first];
            const Money magnitude = off.cents() < 0 ? -off : off;
            rejection_ = {true, entry.first + " is out of balance by " +
                                magnitude.in_register()};
            return;
        }

        std::vector<ledger::Transaction> batch;
        for (const auto& value : values) group_into(&batch, value, counts[value.ref]);
        for (const ledger::Transaction& t : batch) {
            rejection_ = ledger::validate(chart_, t, splits_);
            if (rejection_.refused) return;   // all or nothing: nothing committed
        }
        for (const ledger::Transaction& t : batch) transactions_.push_back(t);
        rebuild_book();
    }

    void when_register_entry_committed(const std::vector<RegisterEntryString>& values) {
        for (const auto& value : values) {
            const reg::BlankLine line = to_blank_line(value);
            const reg::Committed done =
                reg::commit(line, value.account, ledger::transaction_count(transactions_), &chart_);
            if (done.committed) {
                transactions_.push_back(done.transaction);
                if (done.is_transfer) transactions_.push_back(done.other_side);
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
            pending_line_, pending_account_, lines, ledger::transaction_count(transactions_), &chart_);
        if (done.committed) {
            transactions_.push_back(done.transaction);
            splits_.insert(splits_.end(), done.splits.begin(), done.splits.end());
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
                edit::change_amount(chart_, &transactions_, &splits_, value.ref, value.account,
                                    signed_amount);
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
                edit::change_category(chart_, t, &splits_, value.fromaccount, value.toaccount);
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
            bool found = false;
            const chart::Ref account_ref = chart_.ref_for_path(value.account);
            for (ledger::Transaction& t : transactions_) {
                if (t.ref.value() != value.ref) continue;
                // Only the transaction that is this account's own entry: a
                // transfer is two rows precisely so each clears on its own
                // account's statement, never the other's.
                if (t.source_account != account_ref) continue;
                t.cleared = types::ClearedStatus::Cleared;
                found = true;
            }
            EXPECT_TRUE(found) << "no posting on " << value.account << " in " << value.ref;
        }
        rebuild_book();
    }

    // The production function takes the transaction's id, not its Ref -- the
    // id is what MainWindow actually has in hand, from a selection in the
    // register, and Ref is only ever a label of these tables. So the Ref is
    // resolved to an id here, the one place that gap between spec and
    // production addressing has to be bridged.
    void when_the_other_account_is_assigned(const std::vector<AccountAssignmentString>& values) {
        for (const auto& value : values) {
            const ledger::Transaction* t = find_by_ref(value.ref);
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            EXPECT_TRUE(ledger::assign_destination(chart_, &transactions_, t->id,
                                                   chart_.ref_for_path(value.account)))
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
            EXPECT_TRUE(ledger::match_destinations(&transactions_, t->id, other->id))
                << value.ref << " could not be joined with " << value.otherref;
        }
        rebuild_book();
    }

    void when_entries_offered_for(const std::vector<AccountAssignmentString>& values) {
        ASSERT_FALSE(values.empty());
        offered_ = reg::entries_offered_for(chart_, transactions_, values.front().ref,
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
            TransactionString got = as_string(*t);
            // Compared by value, not by text: in_register() adds a
            // thousands separator a table cell may not have written.
            if (value.amount != DNCString &&
                Money(value.amount).cents() == Money(got.amount).cents())
                got.amount = value.amount;
            EXPECT_EQ(value, got) << value.ref;
        }
    }

    void then_transaction_is_a_transfer(const std::vector<TransferCheckString>& values) {
        ASSERT_FALSE(transactions_.empty());
        const ledger::Transaction& t = transactions_.back();
        const bool is_transfer = ledger::is_transfer(chart_, t);
        std::string from, to;
        if (is_transfer) {
            if (t.amount.cents() < 0) from = chart_.path_for_ref(t.source_account);
            else if (t.amount.cents() > 0) to = chart_.path_for_ref(t.source_account);
            if (-t.amount.cents() < 0) from = chart_.path_for_ref(t.destination);
            else if (-t.amount.cents() > 0) to = chart_.path_for_ref(t.destination);
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
                chart::Chart fixture;
                fixture.add(ledger::unassigned(), types::AccountType::OtherAsset);
                EXPECT_TRUE(ledger::is_unassigned(fixture, fixture.ref_for_path(ledger::kUnassigned)));
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

    // A PostingRow table groups rows into a transaction with a Ref column,
    // the way these tables have always written one down -- it says nothing
    // about how a transaction is stored. The first row of a group is the
    // transaction's own SourceAccount; the second is its Destination, unless
    // both are real accounts and neither is Unassigned, in which case it is
    // a transfer and becomes a second transaction of its own (sharing the
    // Ref, its own Id), mirroring reg::commit's pairing -- a transaction
    // represents one account's own entry, so a transfer is two of them. A
    // third row or later makes the first a split, with every row from the
    // second on a line of it.
    void group_into(std::vector<ledger::Transaction>* into,
                    const PostingRowString& value, int total_for_ref) {
        ledger::Transaction* first = nullptr;
        for (ledger::Transaction& t : *into) {
            if (t.ref.value() != value.ref) continue;
            first = &t;
            break;
        }
        const auto on = types::Date::from_iso(value.date);
        const chart::Ref account_ref = chart_.ref_for_path(value.account);
        const Money amount{value.amount};
        const std::string memo = blank(value.memo);
        types::ClearedStatus cleared = types::ClearedStatus::Uncleared;
        if (value.cleared != DNCString && !value.cleared.empty())
            cleared = types::cleared_status_from_string(value.cleared);

        if (first == nullptr) {
            ledger::Transaction made;
            made.id = ledger::new_id();
            made.ref = types::TransactionRef(value.ref);
            if (on) made.date = *on;
            made.payee = types::PayeeName(blank(value.payee));
            made.memo = memo;
            made.source_account = account_ref;
            made.amount = amount;
            made.cleared = cleared;
            into->push_back(made);
            return;
        }

        // Which row this is for its transaction is read from the
        // transaction's own state, not from how many rows have been seen:
        // a third or later row collapses into the same one object as the
        // second once its Destination is set, so counting objects sharing
        // a Ref stops distinguishing a third row from a second.
        ledger::Transaction& t = *first;
        if (t.destination.is_none()) {
            const bool transfer = total_for_ref == 2 && account_ref != t.source_account &&
                                  chart_.is_real(t.source_account) &&
                                  chart_.is_real(account_ref) &&
                                  !ledger::is_unassigned(chart_, t.source_account) &&
                                  !ledger::is_unassigned(chart_, account_ref);
            if (transfer) {
                ledger::Transaction made;
                made.id = ledger::new_id();
                made.ref = t.ref;
                if (on) made.date = *on;
                made.payee = types::PayeeName(blank(value.payee));
                made.memo = memo;
                made.source_account = account_ref;
                made.destination = t.source_account;
                made.amount = amount;
                made.cleared = cleared;
                // Set before push_back: the vector may reallocate, and t is a
                // reference into it -- writing through it afterward would be
                // a dangling write.
                t.destination = account_ref;
                into->push_back(made);
            } else {
                t.destination = account_ref;
                second_row_amount_[t.id.value()] = {amount, memo};
            }
            return;
        }

        if (!chart::is_split(t.destination)) {
            ledger::TransactionSplit promoted;
            promoted.transaction_id = t.id;
            promoted.destination = t.destination;
            const auto at = second_row_amount_.find(t.id.value());
            if (at != second_row_amount_.end()) {
                promoted.amount = at->second.amount;
                promoted.memo = at->second.memo;
            } else {
                promoted.amount = -t.amount;
            }
            splits_.push_back(promoted);
            t.destination = chart::split_ref();
        }
        ledger::TransactionSplit s;
        s.transaction_id = t.id;
        s.destination = account_ref;
        s.amount = amount;
        s.memo = memo;
        splits_.push_back(s);
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

    ledger::Transaction* find_by_id(const types::TransactionId& id) {
        for (ledger::Transaction& t : transactions_)
            if (t.id.value() == id.value()) return &t;
        return nullptr;
    }

    ledger::Transaction make_transaction(const TransactionString& value) {
        ledger::Transaction t;
        t.id = ledger::new_id();
        t.ref = types::TransactionRef(value.ref);
        const auto on = types::Date::from_iso(value.date);
        if (on) t.date = *on;
        t.payee = types::PayeeName(blank(value.payee));
        t.check_no = types::CheckNumber(blank(value.checkno));
        t.memo = blank(value.memo);
        t.tag = blank(value.tag);
        t.source_account = chart_.ref_for_path(blank(value.sourceaccount));
        t.destination = chart_.ref_for_path(blank(value.destination));
        t.amount = Money(value.amount);
        if (value.cleared != DNCString && !value.cleared.empty())
            t.cleared = types::cleared_status_from_string(value.cleared);
        return t;
    }

    // Deliberately matches the convention in then_account_is: Id is never
    // stated by a table, so it is always read back as "none" rather than the
    // real, generated name.
    TransactionString as_string(const ledger::Transaction& t) const {
        TransactionString s;
        s.id = "none";
        s.ref = t.ref.value();
        s.date = t.date.iso();
        s.payee = t.payee.value().empty() ? "none" : t.payee.value();
        s.checkno = t.check_no.value().empty() ? "none" : t.check_no.value();
        s.memo = t.memo.empty() ? "none" : t.memo;
        s.tag = t.tag.empty() ? "none" : t.tag;
        const std::string source_path = chart_.path_for_ref(t.source_account);
        const std::string dest_path =
            chart::is_split(t.destination) ? "Split" : chart_.path_for_ref(t.destination);
        s.sourceaccount = source_path.empty() ? "none" : source_path;
        s.destination = dest_path.empty() ? "none" : dest_path;
        s.amount = t.amount.in_register();
        s.cleared = types::to_string(t.cleared);
        return s;
    }

    void rebuild_book() {
        book_ = ledger::Ledger();
        ledger::add_to_book(&book_, chart_, transactions_, splits_);
    }

    std::vector<PostingRowString> flatten() const {
        std::vector<PostingRowString> out;
        for (const ledger::Transaction& t : transactions_) {
            PostingRowString source_row;
            source_row.ref = t.ref.value();
            source_row.date = t.date.iso();
            source_row.payee = t.payee.value().empty() ? "none" : t.payee.value();
            source_row.account = chart_.path_for_ref(t.source_account);
            source_row.amount = t.amount.in_register();
            source_row.memo = t.memo.empty() ? "none" : t.memo;
            source_row.cleared = types::to_string(t.cleared);
            out.push_back(source_row);

            if (chart::is_split(t.destination)) {
                for (const ledger::TransactionSplit& s : ledger::split_lines_of(splits_, t.id)) {
                    PostingRowString row;
                    row.ref = t.ref.value();
                    row.date = t.date.iso();
                    row.payee = source_row.payee;
                    row.account = chart_.path_for_ref(s.destination);
                    row.amount = s.amount.in_register();
                    row.memo = s.memo.empty() ? "none" : s.memo;
                    row.cleared = types::to_string(types::ClearedStatus::Uncleared);
                    out.push_back(row);
                }
                continue;
            }
            if (t.destination.is_none()) continue;
            // A real destination that is another transaction's own source is
            // that transaction's row; showing it again here would duplicate
            // one movement of money as two lines. See add_to_book's rule.
            bool paired = false;
            if (chart_.is_real(t.destination))
                for (const ledger::Transaction& other : transactions_)
                    if (other.id.value() != t.id.value() && other.source_account == t.destination) {
                        paired = true;
                        break;
                    }
            if (paired) continue;
            PostingRowString dest_row;
            dest_row.ref = t.ref.value();
            dest_row.date = t.date.iso();
            dest_row.payee = source_row.payee;
            dest_row.account = chart_.path_for_ref(t.destination);
            dest_row.amount = (-t.amount).in_register();
            dest_row.memo = t.memo.empty() ? "none" : t.memo;
            dest_row.cleared = types::to_string(types::ClearedStatus::Uncleared);
            out.push_back(dest_row);
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
    std::vector<ledger::TransactionSplit> splits_;
    types::TransactionId last_entered_id_;
    // The amount and memo a second row actually stated, by transaction id,
    // kept in case a third row later promotes that destination to a split
    // line -- -t.amount is only a safe stand-in for the amount while
    // exactly two rows have been seen, and has no stand-in at all for the
    // memo, so a group_into that assumed one for the promoted line would
    // silently answer a different question than the one PostingRow asked.
    // See the matching member in transactionregister_glue.h.
    struct PendingSecondRow { Money amount; std::string memo; };
    std::map<std::string, PendingSecondRow> second_row_amount_;
public:

    void when_transaction_entered(const std::vector<TransactionString>& values) {
        for (const auto& value : values) {
            const ledger::Transaction t = make_transaction(value);
            last_entered_id_ = t.id;
            transactions_.push_back(t);
        }
        rebuild_book();
    }

    void then_no_split_lines_exist_for_it() {
        EXPECT_TRUE(ledger::split_lines_of(splits_, last_entered_id_).empty())
            << "expected no split lines";
    }

    void when_split_lines_entered(const std::vector<TransactionSplitString>& values) {
        for (const auto& value : values) {
            ledger::TransactionSplit s;
            s.transaction_id = last_entered_id_;
            s.destination = chart_.ref_for_path(blank(value.destination));
            s.amount = Money(value.amount);
            s.memo = blank(value.memo);
            splits_.push_back(s);
        }
    }

    void then_the_split_lines_total_the_transaction_s_amount() {
        const ledger::Transaction* t = find_by_id(last_entered_id_);
        ASSERT_NE(nullptr, t) << "no transaction entered";
        const Money total = ledger::split_total(ledger::split_lines_of(splits_, last_entered_id_));
        // The parts are the other side: they sum to the negation of the
        // transaction's own amount, the same way a posting's amount and its
        // category's amount are opposite under the old shape.
        EXPECT_EQ(t->amount.cents(), (-total).cents());
    }

    void when_another_entry_is_downloaded(const std::vector<TransactionString>& values) {
        for (const auto& value : values) {
            const ledger::Transaction t = make_transaction(value);
            last_entered_id_ = t.id;
            transactions_.push_back(t);
        }
        rebuild_book();
    }

    void then_entries_in_that_account_are(const std::vector<TransactionCountString>& values) {
        for (const auto& value : values) {
            int n = 0;
            for (const ledger::Transaction& t : transactions_)
                if (chart_.path_for_ref(t.source_account) == value.account) ++n;
            EXPECT_EQ(std::stoi(value.count), n) << value.account;
        }
    }

    void examples_businessrule_a_split_s_lines_are_what_is_totalled_against_its_transaction_s_amount(const std::vector<SplitTotalCheckString>& values) {
        for (const auto& value : values) {
            const Money got = reg::abs_of(Money(value.transactionamount) - Money(value.linetotal));
            EXPECT_EQ(Money(value.short_).cents(), got.cents()) << value.notes;
        }
    }

public:

    void given_transactions_are(const std::vector<TransactionString>& values) {
        transactions_.clear();
        for (const auto& value : values) transactions_.push_back(make_transaction(value));
        rebuild_book();
    }

    void then_transactions_are(const std::vector<TransactionString>& values) {
        ASSERT_EQ(values.size(), transactions_.size()) << "number of transactions";
        for (const auto& value : values) {
            const ledger::Transaction* t = find_by_ref(value.ref);
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            EXPECT_EQ(value, as_string(*t)) << value.ref;
        }
    }

};
