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

    // ----------------------------------------------------------------- whens

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

    // The splits go with the transaction, so they are handed over to be
    // erased alongside it. Deleting the transaction alone would leave its
    // lines carrying money in the categories with nothing in any register to
    // say where it came from.
    void when_transaction_deleted(const std::vector<TransactionSelectString>& values) {
        for (const auto& value : values)
            edit::delete_transaction(&transactions_, value.ref, &splits_);
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

    void then_transaction_is(const std::vector<TransactionString>& values) {
        for (const auto& value : values) {
            // A table that names no Ref means the one the When just made:
            // a register entry is committed without the scenario naming it,
            // because Ref is a device of these tables and the register has
            // no such thing to type in.
            const ledger::Transaction* t =
                value.ref == DNCString ? (transactions_.empty() ? nullptr
                                                                : &transactions_.back())
                                       : find_by_ref(value.ref);
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            TransactionString got = as_string(*t);
            // Compared by value, not by text: in_register() adds a
            // thousands separator a table cell may not have written.
            if (value.amount != DNCString &&
                Money(value.amount).cents() == Money(got.amount).cents())
                got.amount = value.amount;
            EXPECT_EQ(value, got) << "wanted " << value.to_string()
                                  << " got " << got.to_string();
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
            const std::vector<BalanceByConstructionString>& values) {
        for (const auto& value : values) {
            // A transaction holds one amount against one source account, so the
            // check is that the destination's effect is its negation. There is
            // no list of amounts to sum any more: nothing can fail to add up
            // except a split, which the split rule below covers.
            const Money amount{value.amount};
            EXPECT_EQ((-amount).cents(), Money(value.destinationeffect).cents())
                << value.notes;
            EXPECT_TRUE(ledger::balances({amount, -amount})) << value.notes;
        }
    }

    void examples_businessrule_a_register_entry_becomes_one_transaction(
            const std::vector<RegisterEntryToTransactionString>& values) {
        for (const auto& value : values) {
            const RegisterEntryToTransactionTyped t =
                RegisterEntryToTransactionTyped::from_string_struct(value);
            const Money payment{t.payment};
            const Money deposit{t.deposit};
            // One amount now, not two: the deposit less the payment, carried
            // against the register's own account. The destination takes the
            // opposite of it by construction, so there is nothing else to check.
            EXPECT_EQ(Money(t.amount).cents(),
                      ledger::account_amount_of(payment, deposit).cents()) << t.notes;
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
            // A split is checked once its lines are in, below: at this point
            // it has none, and every split would look short of its own total.
            if (!chart::is_split(t.destination))
                rejection_ = ledger::validate(chart_, t, splits_);
        }
        rebuild_book();
    }

    void then_no_split_lines_exist_for_it() {
        EXPECT_TRUE(ledger::split_lines_of(splits_, last_entered_id_).empty())
            << "expected no split lines";
    }

    // The lines of the split just entered, after which it can be checked:
    // its lines are the thing that has to total its amount.
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

    // Which transaction a table of split lines belongs to. The lines sit under
    // the transaction they are the parts of, so it is the one that says Split;
    // the last one entered is the fallback for a table that names no split,
    // which is how the register's own split steps already behave.
    types::TransactionId split_owner_id() const {
        for (auto it = transactions_.rbegin(); it != transactions_.rend(); ++it)
            if (chart::is_split(it->destination)) return it->id;
        return last_entered_id_;
    }

    void given_split_lines_are(const std::vector<TransactionSplitString>& values) {
        const types::TransactionId owner = split_owner_id();
        ASSERT_FALSE(owner.value().empty()) << "no transaction for these split lines";
        last_entered_id_ = owner;
        for (const auto& value : values) {
            ledger::TransactionSplit s;
            s.transaction_id = owner;
            s.destination = chart_.ref_for_path(blank(value.destination));
            s.amount = Money(value.amount);
            s.memo = blank(value.memo);
            splits_.push_back(s);
        }
        rebuild_book();
    }

    void then_split_lines_are(const std::vector<TransactionSplitString>& values) {
        const std::vector<ledger::TransactionSplit> got =
            ledger::split_lines_of(splits_, split_owner_id());
        ASSERT_EQ(values.size(), got.size()) << "number of split lines";
        for (std::size_t k = 0; k < values.size(); ++k) {
            EXPECT_EQ(blank(values[k].destination),
                      chart_.path_for_ref(got[k].destination)) << "line " << k + 1;
            EXPECT_EQ(Money(values[k].amount).cents(), got[k].amount.cents())
                << "line " << k + 1;
            if (values[k].memo != DNCString)
                EXPECT_EQ(blank(values[k].memo), got[k].memo) << "line " << k + 1;
        }
    }

    void when_transaction_cleared(const std::vector<TransactionSelectString>& values) {
        for (const auto& value : values) {
            ledger::Transaction* t = find_by_ref(value.ref);
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            // One account's own side: clearing this one says nothing about the
            // transaction the other account downloaded for the same movement.
            t->cleared = types::ClearedStatus::Cleared;
        }
        rebuild_book();
    }

    void then_transactions_are(const std::vector<TransactionString>& values) {
        ASSERT_EQ(values.size(), transactions_.size()) << "number of transactions";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const TransactionString& value = values[i];
            // A table that names no Ref is read in order, which is the only
            // thing it can mean: a Ref is this specification's way of saying
            // two rows belong together, and a register entry is committed
            // without the scenario naming one.
            const ledger::Transaction* t =
                value.ref == DNCString ? &transactions_[i] : find_by_ref(value.ref);
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            TransactionString got = as_string(*t);
            if (value.amount != DNCString &&
                Money(value.amount).cents() == Money(got.amount).cents())
                got.amount = value.amount;
            EXPECT_EQ(value, got) << "wanted " << value.to_string()
                                  << " got " << got.to_string();
        }
    }

public:

    void when_split_opened_on(const std::vector<TransactionSelectString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_split_opened_on";
    }

    void when_split_saved() {
        ADD_FAILURE() << "Not implemented: when_split_saved";
    }

    void then_not_matching_is_said(const std::vector<SplitMismatchString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_not_matching_is_said";
    }

    void when_not_matching_answered(const std::vector<SplitMismatchAnswerString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: when_not_matching_answered";
    }

    void examples_businessrule_a_split_is_saved_only_when_its_lines_match_the_total_it_was_opened_on(const std::vector<SplitSaveCheckString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_businessrule_a_split_is_saved_only_when_its_lines_match_the_total_it_was_opened_on";
    }

    void examples_datatype_splitmismatchchoice(const std::vector<EnumerationValuesString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: examples_datatype_splitmismatchchoice";
    }

public:

    void then_the_split_window_is_closed() {
        ADD_FAILURE() << "Not implemented: then_the_split_window_is_closed";
    }

    void then_the_split_window_is_open_on(const std::vector<SplitLineString>& values) {
        for (const auto& v : values) { std::cout << v.to_string() << "\n"; }
        ADD_FAILURE() << "Not implemented: then_the_split_window_is_open_on";
    }

};
