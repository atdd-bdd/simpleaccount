#pragma once
#include <gtest/gtest.h>
#include <algorithm>
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
#include "transaction.h"
#include "register_entry.h"
#include "register_lines.h"
#include "register_view.h"
#include "text_types.h"
#include "transaction_id.h"

// Glue for TransactionRegister.spectable.
//
// Every step drives src/register_lines.h, src/register_view.h or
// src/register_entry.h. None of those knows about a window, which is what lets
// the whole of a register's behaviour be checked here: a widget decides where
// the cursor is, and the production code decides what there is to look at and
// whether a transaction exists.
//
// Two conventions of the generated tables, stated once. An attribute whose
// Default is "none" arrives as the literal word, so a comparison reads it as
// nothing; and a column a CompareOnly table does not name arrives as "?DNC?",
// so a row is compared by starting from the table's own row and overwriting only
// what the step measures -- which leaves what the table did not say unasserted.
class TransactionRegisterGlue {
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
            const std::string balance = blank(value.downloadedbalance.value);
            if (!balance.empty() && Money::is_valid(balance))
                a.downloaded_balance = Money(balance);
            // never is no download rather than a date: there is nothing to
            // compare the register against, which is not a bank saying zero.
            const std::string at = blank(value.downloadedbalanceat);
            if (!at.empty() && at != "never") a.downloaded_balance_at = types::Date::from_iso(at);
            chart_.put(a);
        }
    }

    // There is no Background in this file, so these establish nothing and no
    // scenario reaches them. Kept because the converter writes a glue file once.
    void given_the_chart_of_accounts_is_as_previous() {}
    void given_postings_are_as_previous() {}
    void given_postings_in_entry_order_are_as_previous() {}
    void given_today_is_as_previous() {}

    // The order they were entered in, which is what the register falls back on
    // when two transactions share a date. EntrySeq is not stored: the order of
    // the rows is the order they were entered in, which is what the vector
    // already holds.
    void given_transactions_in_entry_order_are(
            const std::vector<SequencedTransactionString>& values) {
        transactions_.clear();
        splits_.clear();
        for (const auto& value : values) {
            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef(value.ref);
            const auto on = types::Date::from_iso(value.date);
            if (on) t.date = *on;
            t.payee = types::PayeeName(blank(value.payee));
            t.source_account = chart_.ref_for_path(blank(value.sourceaccount));
            t.destination = chart_.ref_for_path(blank(value.destination));
            t.amount = Money(value.amount);
            transactions_.push_back(t);
        }
    }

    void given_today_is(const std::vector<TodayString>& values) {
        ASSERT_FALSE(values.empty());
        const auto on = types::Date::from_iso(values.front().date);
        ASSERT_TRUE(on.has_value()) << values.front().date;
        today_ = *on;
    }

    // A register too large to write out: how far it runs and how many lines.
    // Stated rather than built, because forty thousand rows in a table would say
    // nothing the three numbers do not.
    void given_the_register_holds(const std::vector<RegisterExtentString>& values) {
        ASSERT_FALSE(values.empty());
        const auto first = types::Date::from_iso(values.front().firstdate);
        const auto last = types::Date::from_iso(values.front().lastdate);
        ASSERT_TRUE(first.has_value() && last.has_value());
        extent_first_ = *first;
        extent_last_ = *last;
        extent_lines_ = std::stoi(values.front().linecount);
        have_extent_ = true;
    }

    // ----------------------------------------------------------------- whens

    void when_register_opened_on(const std::vector<AccountSelectString>& values) {
        ASSERT_FALSE(values.empty());
        account_ = values.front().path;
        open_lines_ = reg::lines_for(chart_, transactions_, splits_, account_);
        // The book's hide-before date applies to every register the book opens,
        // so it is applied by opening one rather than by a filter the user asks
        // for.
        filtered_ = reg::in_range(open_lines_, reg::all_dates(open_lines_, today_),
                                  hide_before_);
        lines_ = filtered_.lines;
        balance_shown_ = true;
    }

    void when_register_sorted_by(const std::vector<SortByString>& values) {
        ASSERT_FALSE(values.empty());
        open_if_needed();
        const reg::Sorted sorted =
            reg::sorted_by(lines_, values.front().column, values.front().direction);
        lines_ = sorted.lines;
        balance_shown_ = sorted.balance_shown;
        balance_reason_ = sorted.reason;
    }

    void when_line_expanded(const std::vector<TransactionSelectString>& values) {
        ASSERT_FALSE(values.empty());
        open_if_needed();
        split_ = reg::split_lines_of(chart_, transactions_, splits_, values.front().ref);
    }

    void when_register_filtered_to_dates(const std::vector<DateRangeString>& values) {
        ASSERT_FALSE(values.empty());
        open_if_needed();
        const auto from = types::Date::from_iso(values.front().from);
        const auto to = types::Date::from_iso(values.front().to);
        ASSERT_TRUE(from.has_value() && to.has_value());
        // Over the register as it was built rather than over what a previous
        // filter left, so that choosing a range after a range is choosing a
        // range rather than narrowing one.
        filtered_ = reg::in_range(open_lines_, reg::DateRange{*from, *to}, hide_before_);
        lines_ = filtered_.lines;
    }

    void when_register_searched_for(const std::vector<SearchTermString>& values) {
        ASSERT_FALSE(values.empty());
        open_if_needed();
        lines_ = reg::matching(lines_, blank(values.front().term));
    }

    // Typing on the blank line. Nothing is committed by typing: the line holds
    // what has been entered and leaving it decides.
    void when_blank_line_filled(const std::vector<RegisterLineString>& values) {
        // A scenario about one column of the blank line says nothing about
        // which register it is typed into, because the chart says: there is one
        // account that takes postings. Without this the commit below has no
        // account and goes nowhere, and the register comes out empty.
        open_if_needed();
        ASSERT_FALSE(values.empty());
        const RegisterLineString& value = values.front();
        blank_ = reg::blank_line_on(today_);
        if (value.date != DNCString && !value.date.empty()) {
            const auto on = types::Date::from_iso(value.date);
            if (on.has_value()) blank_.date = *on;
        }
        if (value.payee.value != DNCString) blank_.payee = blank(value.payee.value);
        if (value.category != DNCString) blank_.category = blank(value.category);
        if (value.memo != DNCString) blank_.memo = blank(value.memo);
        if (value.tag != DNCString) blank_.tag = blank(value.tag);
        if (value.checkno.value != DNCString) blank_.check_no = blank(value.checkno.value);
        if (value.payment.value != DNCString && !value.payment.value.empty())
            blank_.payment = Money(value.payment);
        if (value.deposit.value != DNCString && !value.deposit.value.empty())
            blank_.deposit = Money(value.deposit);
        filled_ = true;
    }

    void when_blank_line_left() {
        open_if_needed();
        if (!filled_) blank_ = reg::blank_line_on(today_);
        const reg::Committed done =
            reg::commit(blank_, account_, ledger::transaction_count(transactions_), &chart_);
        if (done.committed) {
            transactions_.push_back(done.transaction);
            if (done.is_transfer) transactions_.push_back(done.other_side);
        }
        lines_ = reg::lines_for(chart_, transactions_, splits_, account_);
        filled_ = false;
    }

    // ----------------------------------------------------------------- thens

    void then_register_lines_are(const std::vector<RegisterLineString>& values) {
        ASSERT_EQ(values.size(), lines_.size()) << "lines in the register:\n" << listing();
        for (std::size_t i = 0; i < values.size(); ++i) {
            RegisterLineString got = values[i];
            got.ref = lines_[i].ref;
            got.date = lines_[i].date.iso();
            got.checkno = lines_[i].check_no.empty() ? "none" : lines_[i].check_no;
            got.payee = lines_[i].payee.empty() ? "none" : lines_[i].payee;
            got.category = lines_[i].category.empty() ? "none" : lines_[i].category;
            got.memo = lines_[i].memo.empty() ? "none" : lines_[i].memo;
            got.tag = lines_[i].tag.empty() ? "none" : lines_[i].tag;
            got.cleared = types::to_string(lines_[i].cleared);
            // Amounts through Money, so a figure written either way is the same
            // figure, and a column the table did not name stays unasserted.
            if (values[i].payment.value != DNCString)
                EXPECT_EQ(Money(values[i].payment), lines_[i].payment)
                    << "line " << i << " payment";
            if (values[i].deposit.value != DNCString)
                EXPECT_EQ(Money(values[i].deposit), lines_[i].deposit)
                    << "line " << i << " deposit";
            if (values[i].balance.value != DNCString)
                EXPECT_EQ(Money(values[i].balance), lines_[i].balance)
                    << "line " << i << " balance";
            got.payment = values[i].payment;
            got.deposit = values[i].deposit;
            got.balance = values[i].balance;
            EXPECT_EQ(values[i], got) << "line " << i << ": wanted "
                                      << values[i].to_string() << " got " << got.to_string();
        }
    }

    void then_blank_line_is(const std::vector<RegisterLineString>& values) {
        ASSERT_FALSE(values.empty());
        const reg::BlankLine line = reg::blank_line_on(today_);
        RegisterLineString got = values.front();
        got.date = line.date.iso();
        got.payee = line.payee.empty() ? "none" : line.payee;
        if (values.front().payment.value != DNCString)
            EXPECT_EQ(Money(values.front().payment), line.payment) << "blank payment";
        if (values.front().deposit.value != DNCString)
            EXPECT_EQ(Money(values.front().deposit), line.deposit) << "blank deposit";
        got.payment = values.front().payment;
        got.deposit = values.front().deposit;
        EXPECT_EQ(values.front(), got) << "wanted " << values.front().to_string()
                                       << " got " << got.to_string();
    }

    void then_running_balance_is_shown(const std::vector<BalanceColumnStateString>& values) {
        ASSERT_FALSE(values.empty());
        BalanceColumnStateString got = values.front();
        got.shown = balance_shown_ ? "true" : "false";
        if (values.front().reason != DNCString && !balance_reason_.empty())
            got.reason = balance_reason_;
        EXPECT_EQ(values.front(), got) << "wanted " << values.front().to_string()
                                       << " got " << got.to_string();
    }

    void then_split_lines_shown_are(const std::vector<SplitLineShownString>& values) {
        ASSERT_EQ(values.size(), split_.size()) << "lines under the split";
        for (std::size_t i = 0; i < values.size(); ++i) {
            SplitLineShownString got = values[i];
            got.category = split_[i].category;
            got.memo = split_[i].memo.empty() ? "none" : split_[i].memo;
            if (values[i].amount.value != DNCString)
                EXPECT_EQ(Money(values[i].amount), split_[i].amount)
                    << "split line " << i << " amount";
            got.amount = values[i].amount;
            EXPECT_EQ(values[i], got) << "split line " << i;
        }
    }

    void then_today_marker_is(const std::vector<TodayMarkerString>& values) {
        ASSERT_FALSE(values.empty());
        const reg::TodayMarker got_marker = reg::marker_for(lines_, today_);
        TodayMarkerString got = values.front();
        got.afterref = got_marker.after_ref.empty() ? "none" : got_marker.after_ref;
        got.futurecount = std::to_string(got_marker.future_count);
        if (values.front().balancetoday.value != DNCString)
            EXPECT_EQ(Money(values.front().balancetoday), got_marker.balance_today)
                << "the balance today";
        if (values.front().futuretotal.value != DNCString)
            EXPECT_EQ(Money(values.front().futuretotal), got_marker.future_total)
                << "the total below the line";
        got.balancetoday = values.front().balancetoday;
        got.futuretotal = values.front().futuretotal;
        EXPECT_EQ(values.front(), got) << "wanted " << values.front().to_string()
                                       << " got " << got.to_string();
    }

    void then_register_view_is(const std::vector<RegisterViewString>& values) {
        ASSERT_FALSE(values.empty());
        const int count = have_extent_ ? extent_lines_ : static_cast<int>(lines_.size());
        const types::Date last =
            have_extent_ ? extent_last_
                         : (lines_.empty() ? today_ : lines_.back().date);
        const reg::Opening opening = reg::opens_at(last, count);
        RegisterViewString got = values.front();
        got.scrolledto = opening.scrolled_to.iso();
        got.selectedline = opening.selected_is_last ? "last" : "1";
        got.linesloaded = std::to_string(opening.lines_loaded);
        EXPECT_EQ(values.front(), got) << "wanted " << values.front().to_string()
                                       << " got " << got.to_string();
    }

    void then_opening_balance_line_is(const std::vector<OpeningLineString>& values) {
        if (values.empty()) {
            // A table with no rows: there is nothing before the range, so there
            // is no opening line at all rather than one reading zero.
            EXPECT_FALSE(filtered_.has_opening)
                << "an opening line of " << filtered_.opening.in_register()
                << " as at " << filtered_.opening_as_at.iso();
            return;
        }
        OpeningLineString got = values.front();
        got.asat = filtered_.opening_as_at.iso();
        if (values.front().balance.value != DNCString)
            EXPECT_EQ(Money(values.front().balance), filtered_.opening)
                << "the opening balance of the range";
        got.balance = values.front().balance;
        EXPECT_EQ(values.front(), got) << "wanted " << values.front().to_string()
                                       << " got " << got.to_string();
    }

    // ------------------------------------------------------- example tables

    void examples_businessrule_what_the_two_amount_columns_are_called(
            const std::vector<ColumnHeadingsString>& values) {
        for (const auto& value : values) {
            const reg::ColumnHeadings got =
                reg::headings_for(types::account_type_from_string(value.accounttype));
            EXPECT_EQ(value.outcolumn, got.out_column)
                << value.accounttype << " money out -- " << value.notes;
            EXPECT_EQ(value.incolumn, got.in_column)
                << value.accounttype << " money in -- " << value.notes;
        }
    }

    void examples_businessrule_the_running_balance_uses_the_display_sign_of_the_account(
            const std::vector<RunningBalanceSignString>& values) {
        for (const auto& value : values) {
            const Money got = reg::balance_shown(
                types::account_type_from_string(value.accounttype),
                Money(value.postingamount));
            EXPECT_EQ(Money(value.balanceshown), got)
                << value.accounttype << " -- " << value.notes;
        }
    }

    // A fixture transaction, named and dated, against the checking account.
    // One call where the old shape needed a pair, because a transaction names
    // both of its sides itself.
    void add_fixture(const std::string& ref, const std::string& date,
                     const std::string& payee, const std::string& amount) {
        ledger::Transaction t;
        t.id = ledger::new_id();
        t.ref = types::TransactionRef(ref);
        const auto on = types::Date::from_iso(date);
        if (on) t.date = *on;
        t.payee = types::PayeeName(payee);
        t.source_account = chart_.ref_for_path("Assets:Checking");
        t.destination = chart_.ref_for_path("Expenses:Groceries");
        t.amount = Money(amount);
        transactions_.push_back(t);
    }

    // The order is date, then the order things were entered. Driven by building a
    // register whose dates tie and whose entry order is known, because the claim
    // is about what happens when the dates cannot decide.
    void examples_businessrule_what_order_the_register_is_in(
            const std::vector<RegisterOrderString>& values) {
        // The table is the register: a date and the order it was entered for each
        // transaction, and where it should end up. Built in the stated entry
        // order, because that is the thing being relied on when dates tie.
        chart_ = chart::Chart();
        chart::Account bank;
        bank.path = types::AccountPath("Assets:Checking");
        bank.type = types::AccountType::Checking;
        chart_.put(bank);
        chart::Account other;
        other.path = types::AccountPath("Expenses:Groceries");
        other.type = types::AccountType::Expense;
        chart_.put(other);
        transactions_.clear();

        std::vector<std::pair<int, std::string>> by_entry;   // sequence, ref
        for (const auto& value : values)
            by_entry.push_back({std::stoi(value.entryseq), "E" + value.entryseq});
        std::sort(by_entry.begin(), by_entry.end());

        for (const auto& one : by_entry) {
            const std::string want = std::to_string(one.first);
            for (const auto& value : values) {
                if (value.entryseq != want) continue;
                add_fixture(one.second, value.date, "Shop " + one.second, "-10.00");
                break;
            }
        }
        account_ = "Assets:Checking";
        lines_ = reg::lines_for(chart_, transactions_, splits_, account_);

        ASSERT_EQ(values.size(), lines_.size()) << "lines in the register";
        for (const auto& value : values) {
            const std::size_t at = static_cast<std::size_t>(std::stoi(value.position)) - 1;
            ASSERT_LT(at, lines_.size());
            EXPECT_EQ("E" + value.entryseq, lines_[at].ref)
                << "position " << value.position << " -- " << value.notes;
            EXPECT_EQ(value.date, lines_[at].date.iso())
                << "position " << value.position << " -- " << value.notes;
        }
    }

    void examples_datatype_sortcolumn(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            // Every listed column is one the register can be sorted by, and
            // sorting by anything but the date empties the balance.
            a_register_with_tied_dates();
            const reg::Sorted got = reg::sorted_by(lines_, value.value);
            EXPECT_EQ(lines_.size(), got.lines.size()) << value.value;
            EXPECT_EQ(value.value == "Date", got.balance_shown)
                << value.value << " -- " << value.notes;
        }
    }

    void examples_datatype_searchtext(const std::vector<ValidValuesString>& values) {
        for (const auto& value : values) {
            const bool valid = types::SearchText::is_valid(value.value);
            EXPECT_EQ(parse_bool_cell(value.isvalid), valid)
                << "\"" << value.value << "\" -- " << value.notes;
        }
    }

private:
    chart::Chart chart_;
    std::vector<ledger::Transaction> transactions_;
    std::vector<ledger::TransactionSplit> splits_;
    // See the matching member in transactions_glue.h: a second row's own
    // amount and memo, kept per transaction id in case a third row later
    // promotes its destination to a split line.
    struct PendingSecondRow { Money amount; std::string memo; };
    std::map<std::string, PendingSecondRow> second_row_amount_;
    std::vector<reg::Line> lines_;
    // The register as it was built, before any filter the user asked for. A
    // named range is chosen over the register rather than over what the last
    // range left, which is what lets All dates put it back.
    std::vector<reg::Line> open_lines_;
    // The book's own date, as the book stores it: empty for a book that hides
    // nothing.
    std::string hide_before_;
    std::vector<reg::SplitLine> split_;
    reg::Filtered filtered_;
    reg::BlankLine blank_;
    bool filled_ = false;
    std::string account_;
    types::Date today_{2024, 1, 1};
    bool balance_shown_ = true;
    std::string balance_reason_;
    types::Date extent_first_{2024, 1, 1};
    types::Date extent_last_{2024, 12, 31};
    int extent_lines_ = 0;
    bool have_extent_ = false;

    // Some scenarios filter or search a register without a step that opened one,
    // because the register they mean is obvious from the chart: there is one
    // account that takes postings and the categories are the other side. Opening
    // it here lets those scenarios say what they are about and nothing else.
    void open_if_needed() {
        if (!account_.empty()) return;
        for (const chart::Account& a : chart_.all()) {
            if (a.placeholder) continue;
            if (types::class_of(a.type) != types::AccountClass::Real) continue;
            account_ = a.path.value();
            break;
        }
        open_lines_ = reg::lines_for(chart_, transactions_, splits_, account_);
        lines_ = open_lines_;
    }

    static std::string blank(const std::string& s) {
        return (s == "none" || s == DNCString) ? std::string() : s;
    }

    std::string listing() const {
        std::string out;
        for (const reg::Line& line : lines_)
            out += "  " + line.ref + " " + line.date.iso() + " " + line.payee + " " +
                   line.payment.in_register() + " " + line.deposit.in_register() + " " +
                   line.balance.in_register() + "\n";
        return out;
    }

    // Three transactions, two of them on one date, entered in a known order. The
    // register should put the earlier date first and then the two tied ones in
    // the order they were entered.
    void a_register_with_tied_dates() {
        chart_ = chart::Chart();
        chart::Account bank;
        bank.path = types::AccountPath("Assets:Checking");
        bank.type = types::AccountType::Checking;
        chart_.put(bank);
        chart::Account other;
        other.path = types::AccountPath("Expenses:Groceries");
        other.type = types::AccountType::Expense;
        chart_.put(other);

        transactions_.clear();
        const char* refs[] = {"T1", "T2", "T3"};
        const char* dates[] = {"2024-01-20", "2024-01-15", "2024-01-20"};
        const char* amounts[] = {"-10.00", "-20.00", "-30.00"};
        for (int i = 0; i < 3; ++i) {
            add_fixture(refs[i], dates[i], std::string("Shop ") + refs[i], amounts[i]);
        }
        account_ = "Assets:Checking";
        lines_ = reg::lines_for(chart_, transactions_, splits_, account_);
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
            t.check_no = types::CheckNumber(blank(value.checkno));
            t.memo = blank(value.memo);
            t.tag = blank(value.tag);
            t.source_account = chart_.ref_for_path(blank(value.sourceaccount));
            t.destination = chart_.ref_for_path(blank(value.destination));
            t.amount = Money(value.amount);
            if (value.cleared.value != DNCString && !value.cleared.value.empty())
                t.cleared = types::cleared_status_from_string(value.cleared);
            transactions_.push_back(t);
        }
    }


public:

    // Which transaction a table of split lines belongs to: the one that says
    // Split, or failing that the last one the scenario established.
    types::TransactionId split_owner_id() const {
        for (auto it = transactions_.rbegin(); it != transactions_.rend(); ++it)
            if (chart::is_split(it->destination)) return it->id;
        return transactions_.empty() ? types::TransactionId()
                                     : transactions_.back().id;
    }

    void given_split_lines_are(const std::vector<TransactionSplitString>& values) {
        const types::TransactionId owner = split_owner_id();
        ASSERT_FALSE(owner.value().empty()) << "no transaction for these split lines";
        for (const auto& value : values) {
            ledger::TransactionSplit s;
            s.transaction_id = owner;
            s.destination = chart_.ref_for_path(blank(value.destination));
            s.amount = Money(value.amount);
            s.memo = blank(value.memo);
            splits_.push_back(s);
        }
    }

public:

    // Of the settings a book carries, the register reads one: the date before
    // which it shows nothing. The fiscal year start is a reporting decision and
    // the register does not read it, which is what the calendar-year scenario
    // is about; the other two belong to importing.
    void given_the_book_setting_is(const std::vector<BookSettingString>& values) {
        ASSERT_FALSE(values.empty());
        hide_before_ = blank(values.front().hidebefore);
    }

    void when_register_filtered_to_named_range(
            const std::vector<NamedRangeSelectString>& values) {
        ASSERT_FALSE(values.empty());
        open_if_needed();
        const reg::DateRange range =
            reg::named_range(values.front().name, today_, open_lines_,
                             blank(values.front().since));
        filtered_ = reg::in_range(open_lines_, range, hide_before_);
        lines_ = filtered_.lines;
    }

    void then_register_note_is(const std::vector<RegisterNoteString>& values) {
        ASSERT_FALSE(values.empty());
        RegisterNoteString got = values.front();
        got.note = filtered_.note.empty() ? "none" : filtered_.note;
        EXPECT_EQ(values.front(), got) << "wanted " << values.front().to_string()
                                       << " got " << got.to_string();
    }

    // A register with a transaction in the years a book might hide, one just
    // after them and one in the current year: the three lines the rows about
    // ranges and hiding are stated over.
    static std::vector<reg::Line> dated_sample() {
        chart::Chart accounts;
        accounts.add(types::AccountPath("Assets:Checking"), types::AccountType::Checking);
        accounts.add(types::AccountPath("Expenses:Groceries"), types::AccountType::Expense);
        const char* dates[] = {"2012-03-01", "2016-02-20", "2024-02-20"};
        const char* amounts[] = {"1000.00", "-52.00", "-10.00"};
        std::vector<ledger::Transaction> transactions;
        for (int i = 0; i < 3; ++i) {
            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef("T" + std::to_string(i + 1));
            const auto on = types::Date::from_iso(dates[i]);
            if (on) t.date = *on;
            t.payee = types::PayeeName("Shop");
            t.source_account = accounts.ref_for_path("Assets:Checking");
            t.destination = accounts.ref_for_path("Expenses:Groceries");
            t.amount = Money(amounts[i]);
            transactions.push_back(t);
        }
        return reg::lines_for(accounts, transactions, {}, "Assets:Checking");
    }

    static std::string refs_of(const std::vector<reg::Line>& lines) {
        std::string out;
        for (const reg::Line& line : lines) {
            if (!out.empty()) out += ",";
            out += line.ref;
        }
        return out;
    }

    // Each row is checked against a handful of lines built for it, chosen so
    // that sorting on what the column means and sorting on the text it shows
    // come out in different orders. The refs say which order was got, so a row
    // whose SortsOn is prose is still measured rather than read.
    void examples_businessrule_what_each_column_sorts_on(
            const std::vector<SortKeyString>& values) {
        for (const SortKeyString& value : values) {
            std::vector<reg::Line> lines;
            std::vector<std::string> wanted;
            if (value.column == "Date") {
                lines.push_back(sort_sample("T1", "2024-01-20", "Shop", "-10.00"));
                lines.push_back(sort_sample("T2", "2024-01-15", "Shop", "-20.00"));
                wanted = {"T2", "T1"};
            } else if (value.column == "Payee") {
                lines.push_back(sort_sample("T1", "2024-01-10", "amazon", "-10.00"));
                lines.push_back(sort_sample("T2", "2024-01-11", "Bravo", "-20.00"));
                lines.push_back(sort_sample("T3", "2024-01-12", "AMAZON", "-30.00"));
                wanted = {"T1", "T3", "T2"};
            } else if (value.column == "Category") {
                lines.push_back(sort_sample("T1", "2024-01-10", "Shop", "-10.00"));
                lines.back().category = "Expenses:Groceries";
                lines.push_back(sort_sample("T2", "2024-01-11", "Shop", "-20.00"));
                lines.back().category = "--Split--";
                wanted = {"T2", "T1"};
            } else if (value.column == "Amount") {
                lines.push_back(sort_sample("T1", "2024-01-10", "Shop", "-9.99"));
                lines.push_back(sort_sample("T2", "2024-01-11", "Shop", "-100.00"));
                lines.push_back(sort_sample("T3", "2024-01-12", "Shop", "20.00"));
                wanted = {"T2", "T1", "T3"};
            } else if (value.column == "CheckNo") {
                lines.push_back(sort_sample("T1", "2024-01-10", "Shop", "-10.00"));
                lines.back().check_no = "100";
                lines.push_back(sort_sample("T2", "2024-01-11", "Shop", "-20.00"));
                lines.push_back(sort_sample("T3", "2024-01-12", "Shop", "-30.00"));
                lines.back().check_no = "7";
                wanted = {"T2", "T3", "T1"};
            } else if (value.column == "Cleared") {
                lines.push_back(sort_sample("T1", "2024-01-10", "Shop", "-10.00"));
                lines.back().cleared = types::ClearedStatus::Cleared;
                lines.push_back(sort_sample("T2", "2024-01-11", "Shop", "-20.00"));
                wanted = {"T2", "T1"};
            } else if (value.column == "Memo") {
                // Blank ones first, then the text as it reads -- so a memo
                // sorted on would not be in this order by any other key.
                lines.push_back(sort_sample("T1", "2024-01-10", "Shop", "-10.00"));
                lines.back().memo = "zebra";
                lines.push_back(sort_sample("T2", "2024-01-11", "Shop", "-20.00"));
                lines.push_back(sort_sample("T3", "2024-01-12", "Shop", "-30.00"));
                lines.back().memo = "apple";
                wanted = {"T2", "T3", "T1"};
            } else if (value.column == "Tag") {
                // The field, not the words in it: T2's field begins with Zulu
                // and holds Apple, so a sort on the words would bring it to the
                // front and a sort on the field puts it last. T1 and T3 differ
                // only in case, so they stay in the order they were in.
                lines.push_back(sort_sample("T1", "2024-01-10", "Shop", "-10.00"));
                lines.back().tag = "rental";
                lines.push_back(sort_sample("T2", "2024-01-11", "Shop", "-20.00"));
                lines.back().tag = "Zulu Apple";
                lines.push_back(sort_sample("T3", "2024-01-12", "Shop", "-30.00"));
                lines.back().tag = "RENTAL";
                wanted = {"T1", "T3", "T2"};
            } else {
                ADD_FAILURE() << "no sample for column " << value.column;
                continue;
            }
            const reg::Sorted sorted = reg::sorted_by(lines, value.column, "Ascending");
            ASSERT_EQ(wanted.size(), sorted.lines.size()) << value.column;
            for (std::size_t i = 0; i < wanted.size(); ++i)
                EXPECT_EQ(wanted[i], sorted.lines[i].ref)
                    << value.column << " sorts on " << value.sortson << " -- line " << i + 1;
            // Date is the only order the balance column means anything in.
            EXPECT_EQ(value.column == "Date", sorted.balance_shown) << value.column;
        }
    }

    // A line with nothing on it but the fields a sort reads, in the order the
    // register would have built them.
    static reg::Line sort_sample(const std::string& ref, const std::string& date,
                                 const std::string& payee, const std::string& amount) {
        reg::Line line;
        line.ref = ref;
        const auto on = types::Date::from_iso(date);
        if (on) line.date = *on;
        line.payee = payee;
        const Money money{amount};
        if (money.cents() < 0) line.payment = -money;
        else line.deposit = money;
        return line;
    }

    void examples_businessrule_which_way_round_a_sort_starts(
            const std::vector<SortStartString>& values) {
        for (const SortStartString& value : values)
            EXPECT_EQ(value.firstclick, reg::first_direction_for(value.column))
                << value.column << ": " << value.whatthatshows;
    }

    // Each row's two dates are prose, so each is worked out here for one
    // stated today and compared: the middle of May 2024, over a register whose
    // first line is well before it.
    void examples_businessrule_the_date_ranges_the_register_offers_by_name(
            const std::vector<NamedDateRangeString>& values) {
        const types::Date today(2024, 5, 15);
        const std::vector<reg::Line> lines = dated_sample();
        for (const NamedDateRangeString& value : values) {
            const reg::DateRange got =
                reg::named_range(value.name, today, lines, "2021-07-04");
            std::string from = "2012-03-01";  // the first transaction
            std::string to = "2024-05-15";    // today
            if (value.name == "This year" || value.name == "Year to date") {
                from = "2024-01-01";
            } else if (value.name == "Last year") {
                from = "2023-01-01";
                to = "2023-12-31";
            } else if (value.name == "Past two years") {
                from = "2022-05-15";
            } else if (value.name == "Since a date") {
                from = "2021-07-04";
            }
            EXPECT_EQ(from, got.from.iso()) << value.name << " from " << value.from;
            EXPECT_EQ(to, got.to.iso()) << value.name << " to " << value.to;
        }
    }

    // The four rows over the same three lines: what is shown, what is brought
    // forward onto the opening line, and whether the register says why it is
    // empty.
    void examples_businessrule_a_book_may_hide_everything_before_a_date(
            const std::vector<HideBeforeUseString>& values) {
        const types::Date today(2024, 5, 15);
        for (const HideBeforeUseString& value : values) {
            const std::vector<reg::Line> lines = dated_sample();
            const std::string hides =
                value.thebooksays == "Nothing" ? std::string() : std::string("2015-01-01");
            const bool asks_for_the_hidden_years = value.theuserasksfor == "2012 to 2013";
            const reg::DateRange range =
                asks_for_the_hidden_years
                    ? reg::DateRange{types::Date(2012, 1, 1), types::Date(2013, 12, 31)}
                    : reg::named_range(value.theuserasksfor, today, lines, "");
            const reg::Filtered got = reg::in_range(lines, range, hides);
            const std::string shown = refs_of(got.lines);

            if (asks_for_the_hidden_years) {
                EXPECT_EQ("", shown) << value.whattheregistershows;
                EXPECT_EQ("This book hides transactions before 2015-01-01", got.note)
                    << value.whattheregistershows;
                continue;
            }
            EXPECT_EQ("", got.note) << value.whattheregistershows;
            if (hides.empty()) {
                // Everything, as now, and nothing before the first line.
                EXPECT_EQ("T1,T2,T3", shown) << value.whattheregistershows;
                EXPECT_FALSE(got.has_opening) << value.whattheregistershows;
            } else if (value.theuserasksfor == "All dates") {
                // 2015 onwards, with the hidden year's balance brought forward.
                EXPECT_EQ("T2,T3", shown) << value.whattheregistershows;
                EXPECT_TRUE(got.has_opening) << value.whattheregistershows;
                EXPECT_EQ(Money("1000.00"), got.opening) << value.whattheregistershows;
                EXPECT_EQ("2014-12-31", got.opening_as_at.iso())
                    << value.whattheregistershows;
            } else {
                // This year, as it would have been anyway.
                EXPECT_EQ("T3", shown) << value.whattheregistershows;
                EXPECT_TRUE(got.has_opening) << value.whattheregistershows;
                EXPECT_EQ(Money("948.00"), got.opening) << value.whattheregistershows;
            }
        }
    }

public:

    void then_register_balances_are(const std::vector<RegisterBalancesString>& values) {
        ASSERT_FALSE(values.empty());
        const RegisterBalancesString& want = values.front();
        const reg::Balances got =
            reg::balances_for(chart_, transactions_, splits_, want.account.value);
        EXPECT_EQ(Money(want.balance.value).cents(), got.balance.cents()) << "balance";
        EXPECT_EQ(Money(want.cleared.value).cents(), got.cleared.cents()) << "cleared balance";
        EXPECT_EQ(Money(want.downloaded.value).cents(), got.downloaded.cents())
            << "downloaded balance";
        const std::string at =
            got.downloaded_at.has_value() ? got.downloaded_at->iso() : std::string("never");
        EXPECT_EQ(want.downloadedat, at) << "the date the downloaded balance was for";
        EXPECT_EQ(Money(want.difference.value).cents(), got.difference.cents()) << "difference";
    }

public:

    // The fixture with this Ref. Ref is a device of the tables, so this only
    // ever looks for something a table just above established.
    ledger::Transaction* by_ref(const std::string& ref) {
        for (auto& t : transactions_)
            if (t.ref.value() == ref) return &t;
        return nullptr;
    }

    // Matching two rows already in the book: each ends up naming the other's
    // account and recording the other's id. The register reads that record and
    // nothing else -- see a transfer is a line in each of the two registers it
    // names.
    void when_the_other_entry_is_matched(const std::vector<EntryMatchString>& values) {
        for (const auto& value : values) {
            ledger::Transaction* t = by_ref(value.ref);
            ASSERT_NE(nullptr, t) << "no transaction " << value.ref;
            ledger::Transaction* other = by_ref(value.otherref);
            ASSERT_NE(nullptr, other) << "no transaction " << value.otherref;
            EXPECT_TRUE(ledger::match_destinations(&transactions_, t->id, other->id))
                << value.ref << " could not be joined with " << value.otherref;
        }
    }

    // The table names a kind of transaction in words; this is the fixture that
    // is one of that kind.
    static std::string ref_for_kind(const std::string& kind) {
        if (kind == "Lives in this account") return "OWN";
        if (kind == "Names this account") return "IN";
        if (kind == "A split line names this account") return "SPLIT";
        if (kind == "Names neither") return "AWAY";
        if (kind == "Matched by hand to a row here") return "PAIR";
        return std::string();
    }

    // One book holding every kind of transaction the rule distinguishes, read
    // as the register of Assets:Checking. Built here rather than as five
    // scenarios because the point of the table is the kinds side by side; the
    // two that have been got wrong have scenarios of their own above.
    void examples_businessrule_a_transfer_is_a_line_in_each_of_the_two_registers_it_names(
            const std::vector<RegisterMembershipString>& values) {
        chart_ = chart::Chart();
        const char* paths[] = {"Assets:Checking", "Assets:Joint", "Liabilities:Card",
                               "Expenses:Groceries", "Unassigned"};
        const types::AccountType kinds[] = {
            types::AccountType::Checking, types::AccountType::Checking,
            types::AccountType::CreditCard, types::AccountType::Expense,
            types::AccountType::OtherAsset};
        for (int i = 0; i < 5; ++i) {
            chart::Account one;
            one.path = types::AccountPath(paths[i]);
            one.type = kinds[i];
            chart_.put(one);
        }
        transactions_.clear();
        splits_.clear();

        // Ref, where it lives, what it names, amount. PAIR is the card's side
        // of the payment MINE pays, and the two are matched below.
        const char* rows[][4] = {
            {"OWN",   "Assets:Checking",  "Expenses:Groceries", "-10.00"},
            {"IN",    "Assets:Joint",     "Assets:Checking",    "-20.00"},
            {"SPLIT", "Assets:Joint",     "Split",              "-30.00"},
            {"AWAY",  "Assets:Joint",     "Expenses:Groceries", "-40.00"},
            {"MINE",  "Assets:Checking",  "Unassigned",         "-50.00"},
            {"PAIR",  "Liabilities:Card", "Unassigned",         "50.00"},
        };
        for (const auto& row : rows) {
            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef(row[0]);
            const auto on = types::Date::from_iso("2026-05-01");
            if (on) t.date = *on;
            t.payee = types::PayeeName(row[0]);
            t.source_account = chart_.ref_for_path(row[1]);
            t.destination = chart_.ref_for_path(row[2]);
            t.amount = Money(row[3]);
            transactions_.push_back(t);
        }
        ledger::Transaction* split_owner = by_ref("SPLIT");
        ASSERT_NE(nullptr, split_owner);
        ledger::TransactionSplit part;
        part.transaction_id = split_owner->id;
        part.destination = chart_.ref_for_path("Assets:Checking");
        part.amount = Money("-30.00");
        splits_.push_back(part);

        ledger::Transaction* mine = by_ref("MINE");
        ledger::Transaction* pair = by_ref("PAIR");
        ASSERT_NE(nullptr, mine);
        ASSERT_NE(nullptr, pair);
        ASSERT_TRUE(ledger::match_destinations(&transactions_, mine->id, pair->id));

        account_ = "Assets:Checking";
        lines_ = reg::lines_for(chart_, transactions_, splits_, account_);

        for (const auto& value : values) {
            const std::string ref = ref_for_kind(value.thetransaction);
            ASSERT_FALSE(ref.empty()) << "no fixture for " << value.thetransaction;
            bool shown = false;
            for (const reg::Line& line : lines_)
                if (line.ref == ref) shown = true;
            EXPECT_EQ(parse_bool_cell(value.isaline), shown)
                << value.thetransaction << " -- " << value.why;
        }
    }

};
