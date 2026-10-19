#pragma once
#include <gtest/gtest.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "common/common.h"
#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "posting.h"
#include "balance_sheet.h"
#include "report.h"
#include "report_ranges.h"
#include "text_types.h"
#include "transaction_id.h"

// Glue for Reports.spectable.
class ReportsGlue {
public:
    // ---------------------------------------------------------------- givens

    void given_the_chart_of_accounts_is(const std::vector<AccountString>& values) {
        chart_ = chart::Chart();
        for (const auto& value : values) {
            chart::Account a;
            a.path = types::AccountPath(value.path);
            a.type = types::account_type_from_string(value.type);
            a.placeholder = parse_bool_cell(value.placeholder);
            a.hidden = parse_bool_cell(value.hidden);
            chart_.put(a);
        }
    }

    void given_the_chart_of_accounts_is_as_previous() {}
    void given_transactions_are_as_previous() {}

    void group_into(std::vector<ledger::Transaction>* into, const PostingRowString& value) {
        ledger::Transaction* first = nullptr;
        for (ledger::Transaction& t : *into) {
            if (t.ref.value() != value.ref) continue;
            first = &t;
            break;
        }
        const auto on = types::Date::from_iso(value.date);
        ASSERT_TRUE(on.has_value()) << value.date;
        const chart::Ref account_ref = chart_.ref_for_path(value.account);
        const Money amount{value.amount};
        const std::string memo = value.memo == "none" ? std::string() : value.memo;

        if (first == nullptr) {
            ledger::Transaction made;
            made.id = ledger::new_id();
            made.ref = types::TransactionRef(value.ref);
            made.date = *on;
            made.payee = types::PayeeName(value.payee);
            made.memo = memo;
            made.source_account = account_ref;
            made.amount = amount;
            into->push_back(made);
            return;
        }
        // Which row this is for its transaction is read from the
        // transaction's own state, not from how many rows have been seen:
        // a third or later row collapses into the same one object as the
        // second once its Destination is set, so counting objects sharing
        // a Ref stops distinguishing a third row from a second.
        if (first->destination.is_none()) {
            first->destination = account_ref;
            if (first->memo.empty() && !memo.empty()) first->memo = memo;
            second_row_amount_[first->id.value()] = {amount, memo};
            return;
        }
        ledger::Transaction& t = *first;
        if (!chart::is_split(t.destination)) {
            if (!t.destination.is_none()) {
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
            }
            t.destination = chart::split_ref();
        }
        ledger::TransactionSplit s;
        s.transaction_id = t.id;
        s.destination = account_ref;
        s.amount = amount;
        s.memo = memo;
        splits_.push_back(s);
    }

    void rebuild_book() {
        book_ = ledger::Ledger();
        ledger::add_to_book(&book_, chart_, transactions_, splits_);
    }

    // ----------------------------------------------------------------- whens

    void when_report_run(const std::vector<ReportSpecString>& values) {
        for (const auto& value : values) run(value);
    }

    void when_report_run_with_average(const std::vector<ReportSpecString>& values) {
        when_report_run(values);
    }

    void when_net_worth_report_run(const std::vector<ReportSpecString>& values) {
        when_report_run(values);
    }

    void when_report_exported_as_csv(const std::vector<ReportSpecString>& values) {
        when_report_run(values);
    }

    void when_report_run_for_category(const std::vector<CategoryFilterString>& values) {
        for (const auto& value : values) {
            spec_ = range_of(value.from, value.to);
            category_filter_ = value.category;
            report_ = reports::category_report(chart_, book_, spec_);
        }
    }

    void when_report_figure_opened(const std::vector<ReportCellString>& values) {
        for (const auto& value : values) {
            spec_ = range_of(value.from, value.to);
            opened_ = value.account;
        }
    }

    // ----------------------------------------------------------------- thens

    void then_report_rows_are(const std::vector<ReportRowString>& values) {
        ASSERT_EQ(values.size(), report_.rows.size()) << "number of report rows";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const reports::Row& got = report_.rows[i];
            EXPECT_EQ(values[i].account.value, got.account) << "row " << i;
            EXPECT_EQ(std::stoi(values[i].level), got.level) << got.account << " level";
            EXPECT_EQ(Money(values[i].amount).cents(), got.amount.cents())
                << got.account << " amount";
            EXPECT_EQ(parse_bool_cell(values[i].issubtotal), got.is_subtotal)
                << got.account << " subtotal";
        }
    }

    void then_report_total_is(const std::vector<ReportTotalString>& values) {
        for (const auto& value : values) {
            EXPECT_EQ(Money(value.totalincome).cents(), report_.totals.income.cents())
                << "total income";
            EXPECT_EQ(Money(value.totalexpenses).cents(), report_.totals.expenses.cents())
                << "total expenses";
            EXPECT_EQ(Money(value.net).cents(), report_.totals.net.cents()) << "net";
        }
    }

    void then_payee_report_rows_are(const std::vector<PayeeReportRowString>& values) {
        const std::optional<std::string> only =
            category_filter_.empty() ? std::nullopt
                                     : std::optional<std::string>(category_filter_);
        const std::vector<reports::PayeeRow> got =
            reports::payee_report(chart_, transactions_, splits_, spec_, only);
        ASSERT_EQ(values.size(), got.size()) << "number of payee rows";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].payee.value, got[i].payee) << "row " << i;
            EXPECT_EQ(Money(values[i].amount).cents(), got[i].amount.cents())
                << got[i].payee << " amount";
            EXPECT_EQ(std::stoi(values[i].count), got[i].count) << got[i].payee << " count";
        }
    }

    void then_transactions_behind_it_are(const std::vector<DrillDownRowString>& values) {
        // Any figure can be taken apart, a subtotal included: opening one reaches
        // through the placeholder to the leaves beneath it.
        std::vector<DrillDownRowString> got;
        for (const ledger::Transaction& t : transactions_) {
            if (t.date < spec_.from || spec_.to < t.date) continue;
            for (const ledger::Side& s : ledger::sides_of(t, splits_)) {
                const std::string p_path = chart_.path_for_ref(s.account);
                if (!chart::Chart::is_descendant_or_self(p_path, opened_)) continue;
                DrillDownRowString row;
                row.ref = t.ref.value();
                row.date = t.date.iso();
                row.payee = t.payee.value();
                row.account = p_path;
                row.amount = s.amount.in_register();
                got.push_back(row);
            }
        }
        ASSERT_EQ(values.size(), got.size()) << "number of transactions behind the figure";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].ref.value, got[i].ref.value) << "row " << i;
            EXPECT_EQ(values[i].date, got[i].date) << "row " << i;
            EXPECT_EQ(values[i].account.value, got[i].account.value) << "row " << i;
            EXPECT_EQ(Money(values[i].amount).cents(), Money(got[i].amount).cents())
                << "row " << i;
        }
    }

    // ------------------------------------------------------------ the rules

    void examples_businessrule_what_the_named_date_ranges_mean(
            const std::vector<NamedRangeString>& values) {
        // Taken relative to today, which the rule states as 2024-05-15, and to
        // the first date in the book, which its All row states as 2005-01-01.
        const types::Date today(2024, 5, 15);
        const types::Date book_start(2005, 1, 1);
        for (const auto& value : values) {
            const NamedRangeTyped t = NamedRangeTyped::from_string_struct(value);
            const auto got = reports::named_range(t.name, today, book_start);
            ASSERT_TRUE(got.has_value()) << "unknown range: " << t.name;
            EXPECT_EQ(t.from, got->from.iso()) << t.name << " from -- " << t.notes;
            EXPECT_EQ(t.to, got->to.iso()) << t.name << " to -- " << t.notes;
        }
    }

    void examples_businessrule_how_a_fiscal_year_is_named_and_bounded(
            const std::vector<FiscalYearString>& values) {
        for (const auto& value : values) {
            const FiscalYearTyped t = FiscalYearTyped::from_string_struct(value);
            // The label is written FY2024; the number is what bounds the year.
            const std::string digits = t.fiscalyear.substr(t.fiscalyear.size() - 4);
            const reports::Range got =
                reports::fiscal_year(t.startmonth, std::stoi(digits));
            EXPECT_EQ(t.from, got.from.iso()) << t.fiscalyear << " from -- " << t.notes;
            EXPECT_EQ(t.to, got.to.iso()) << t.fiscalyear << " to -- " << t.notes;
        }
    }

private:
    struct PendingSecondRow { Money amount; std::string memo; };
    std::map<std::string, PendingSecondRow> second_row_amount_;
    chart::Chart chart_;
    ledger::Ledger book_;
    std::vector<ledger::Transaction> transactions_;
    std::vector<ledger::TransactionSplit> splits_;
    reports::Spec spec_;
    reports::CategoryReport report_;
    std::vector<reports::DetailLine> lines_;
    std::string category_filter_;
    std::string opened_;

    static reports::Spec range_of(const std::string& from, const std::string& to) {
        reports::Spec s;
        const auto a = types::Date::from_iso(from);
        const auto b = types::Date::from_iso(to);
        if (a) s.from = *a;
        if (b) s.to = *b;
        return s;
    }

    void run(const ReportSpecString& value) {
        spec_ = range_of(value.from, value.to);
        spec_.depth = value.depth.empty() ? 0 : std::stoi(value.depth);
        spec_.detail = reports::detail_from_string(value.detail);
        spec_.zero_rows = parse_bool_cell(value.zerorows);
        spec_.include_investment_activity =
            parse_bool_cell(value.includeinvestmentactivity);
        spec_.quicken_signs = parse_bool_cell(value.quickensigns);
        category_filter_.clear();
        report_ = reports::category_report(chart_, book_, spec_);
        // Both shapes, every time. The detailed one walks the report rather
        // than totalling anything again, so having it here costs nothing and
        // the two can never be out of step with each other.
        lines_ = reports::detail_report(chart_, book_, transactions_, splits_, spec_);
    }
public:

    // The row set and levels are read straight off report_, which when_report_run
    // already built over the whole range -- a category active in any one column
    // is active over the whole of it, so the two trees are the same tree.
    void then_year_column_rows_are(const std::vector<YearColumnRowString>& values) {
        const std::vector<reports::NamedColumn> cols = reports::year_columns(spec_.from, spec_.to);
        ASSERT_EQ(3u, cols.size()) << "this step only reads three year columns";
        ASSERT_EQ(values.size(), report_.rows.size()) << "number of rows";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const reports::Row& row = report_.rows[i];
            EXPECT_EQ(values[i].account.value, row.account) << "row " << i;
            EXPECT_EQ(std::stoi(values[i].level), row.level) << row.account << " level";

            Money years[3];
            Money total;
            for (std::size_t y = 0; y < 3; ++y) {
                reports::Spec slice = spec_;
                slice.from = cols[y].range.from;
                slice.to = cols[y].range.to;
                slice.zero_rows = true;
                const reports::CategoryReport sliced = reports::category_report(chart_, book_, slice);
                for (const reports::Row& one : sliced.rows)
                    if (one.account == row.account) { years[y] = one.amount; break; }
                total += years[y];
            }
            const std::string cells[3] = {values[i].y2023, values[i].y2024, values[i].y2025};
            for (std::size_t y = 0; y < 3; ++y)
                EXPECT_EQ(Money(cells[y]).cents(), years[y].cents())
                    << row.account << " year " << y;
            EXPECT_EQ(Money(values[i].total).cents(), total.cents()) << row.account << " total";
        }
    }

    void then_report_columns_are(const std::vector<ReportColumnString>& values) {
        const std::vector<reports::NamedColumn> cols = reports::year_columns(spec_.from, spec_.to);
        ASSERT_EQ(values.size(), cols.size()) << "number of columns";
        for (std::size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(values[i].heading, cols[i].label) << "column " << i;
            EXPECT_EQ(values[i].from, cols[i].range.from.iso()) << "column " << i << " from";
            EXPECT_EQ(values[i].to, cols[i].range.to.iso()) << "column " << i << " to";
            bool has = false;
            for (const ledger::DatedPosting& p : book_.postings())
                if (!(p.date < cols[i].range.from) && !(cols[i].range.to < p.date)) {
                    has = true;
                    break;
                }
            EXPECT_EQ(parse_bool_cell(values[i].haspostings), has)
                << "column " << i << " has postings";
        }
    }

    // The two accounts named here illustrate the rule -- divide by the number
    // of columns, not by how many of them had activity -- rather than being the
    // whole of the report, so this checks only the rows the table names.
    void then_average_column_is(const std::vector<AverageRowString>& values) {
        const int columns =
            static_cast<int>(reports::year_columns(spec_.from, spec_.to).size());
        for (const auto& value : values) {
            const reports::Row* row = nullptr;
            for (const reports::Row& one : report_.rows)
                if (one.account == value.account.value) { row = &one; break; }
            ASSERT_NE(nullptr, row) << "no row for " << value.account;
            EXPECT_EQ(Money(value.total).cents(), row->amount.cents())
                << value.account << " total";
            EXPECT_EQ(std::stoi(value.columns), columns) << value.account << " columns";
            const Money average = Money::from_cents(row->amount.cents() / columns);
            EXPECT_EQ(Money(value.average).cents(), average.cents())
                << value.account << " average";
        }
    }

    // Net worth as at the end of each year the range covers, which is why this
    // reads the balance sheet rather than the category report: a balance sheet
    // is already a figure as at a date, and a point in a range of them is what
    // this report's columns are.
    void then_net_worth_columns_are(const std::vector<NetWorthColumnString>& values) {
        const std::vector<reports::NamedColumn> cols = reports::year_columns(spec_.from, spec_.to);
        ASSERT_EQ(values.size(), cols.size()) << "number of columns";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const reports::BalanceSheet bs =
                reports::balance_sheet(chart_, book_, cols[i].range.to, false);
            EXPECT_EQ(values[i].asat, cols[i].range.to.iso()) << "column " << i << " as at";
            EXPECT_EQ(Money(values[i].assets).cents(), bs.totals.assets.cents())
                << "column " << i << " assets";
            EXPECT_EQ(Money(values[i].liabilities).cents(), bs.totals.liabilities.cents())
                << "column " << i << " liabilities";
            EXPECT_EQ(Money(values[i].networth).cents(), bs.totals.net.cents())
                << "column " << i << " net worth";
        }
    }

    void then_csv_written_is(const std::string& value) {
        std::vector<std::string> lines;
        lines.push_back("Account,Level,Amount");
        for (const reports::Row& row : report_.rows)
            lines.push_back(row.account + "," + std::to_string(row.level) + "," +
                            row.amount.in_csv());
        std::string joined;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (i != 0) joined += "\n";
            joined += lines[i];
        }
        EXPECT_EQ(value, joined);
    }

    // Neither of these is its own C++ enum: GroupBy picks which When step runs
    // (category, payee, or balance sheet), and Interval beyond Whole and Year
    // is read nowhere yet. What is checked is that the declared set of values
    // is the one the rest of this file's scenarios actually use.
    void examples_datatype_reportgrouping(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            EXPECT_TRUE(v.value == "Category" || v.value == "Payee" || v.value == "Account")
                << "unknown report grouping: " << v.value;
        }
    }

    void examples_datatype_reportinterval(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            EXPECT_TRUE(v.value == "Whole" || v.value == "Month" || v.value == "Quarter" ||
                       v.value == "Year" || v.value == "FiscalYear")
                << "unknown report interval: " << v.value;
        }
    }

public:

    // Each case is built as a real one-transaction book and put through the
    // report, rather than asking the rule in the abstract: the table is then a
    // statement about what the report does and not about what a helper thinks.
    void examples_businessrule_income_and_expense_arising_inside_an_investment_account_is_left_out(const std::vector<InvestmentActivityCaseString>& values) {
        for (const auto& value : values) {
            const std::string where = value.transactiontouches;
            const bool brokerage = where.find("brokerage") != std::string::npos;
            const bool retirement = where.find("retirement") != std::string::npos;
            const bool bank = where.find("bank") != std::string::npos;

            chart_ = chart::Chart();
            const auto put = [&](const std::string& path, const char* type) {
                chart::Account a;
                a.path = types::AccountPath(path);
                a.type = types::account_type_from_string(type);
                chart_.put(a);
            };
            put("Assets:Checking", "Bank");
            put("Assets:Brokerage", "Investment");
            put("Assets:Ken IRA", "Investment");
            put(value.category.value, value.category.value.rfind("Income", 0) == 0 ? "Income" : "Expense");

            // The category takes a credit for income and a debit for an expense,
            // and the real accounts take the other side.
            const bool is_income = value.category.value.rfind("Income", 0) == 0;
            const Money hundred{"100.00"};
            const types::Date on{2024, 5, 1};
            book_ = ledger::Ledger();
            transactions_.clear();
            ledger::Transaction t;
            t.ref = types::TransactionRef("T1");
            t.date = on;
            t.payee = types::PayeeName("Vanguard");

            std::vector<std::pair<std::string, Money>> legs;
            legs.push_back({value.category, is_income ? -hundred : hundred});
            // Where the money was. Both a brokerage and a bank means the one
            // transaction touched each, which is the case that decides whether
            // the investment side taints the whole of it.
            if (brokerage && bank) {
                legs.push_back({"Assets:Brokerage", is_income ? Money{"40.00"} : Money{"-40.00"}});
                legs.push_back({"Assets:Checking", is_income ? Money{"60.00"} : Money{"-60.00"}});
            } else if (brokerage) {
                legs.push_back({"Assets:Brokerage", is_income ? hundred : -hundred});
            } else if (retirement) {
                legs.push_back({"Assets:Ken IRA", is_income ? hundred : -hundred});
            } else {
                legs.push_back({"Assets:Checking", is_income ? hundred : -hundred});
            }

            t.id = ledger::new_id();
            for (const auto& leg : legs)
                book_.add({on, types::AccountPath(leg.first), leg.second, t.ref.value()});
            t.source_account = chart_.ref_for_path(legs[0].first);
            t.amount = legs[0].second;
            if (legs.size() == 2) {
                t.destination = chart_.ref_for_path(legs[1].first);
            } else {
                t.destination = chart::split_ref();
                for (std::size_t i = 1; i < legs.size(); ++i) {
                    ledger::TransactionSplit s;
                    s.transaction_id = t.id;
                    s.destination = chart_.ref_for_path(legs[i].first);
                    s.amount = legs[i].second;
                    splits_.push_back(s);
                }
            }
            transactions_.push_back(t);

            reports::Spec spec;
            spec.from = types::Date{2024, 1, 1};
            spec.to = types::Date{2024, 12, 31};
            const reports::CategoryReport r =
                reports::category_report(chart_, book_, spec);
            const bool counted = r.totals.income.cents() != 0 ||
                                 r.totals.expenses.cents() != 0;
            EXPECT_EQ(parse_bool_cell(value.counts), counted)
                << where << " / " << value.category << " -- " << value.notes;
        }
    }

public:

    void given_dated_entries_are(const std::vector<DatedAccountEntryString>& values) {
        book_ = ledger::Ledger();
        transactions_.clear();
        for (const auto& value : values) {
            const auto date = types::Date::from_iso(value.date);
            ASSERT_TRUE(date.has_value()) << value.date;
            book_.add({*date, types::AccountPath(value.account), Money(value.amount),
                      std::string()});
        }
    }

    void examples_businessrule_a_category_with_its_own_transactions_shows_them_on_an_other_row(
            const std::vector<OtherRowNamingString>& values) {
        for (const auto& value : values)
            EXPECT_EQ(value.therowiscalled, reports::other_row_name(value.containingcategory))
                << value.containingcategory;
    }

public:

    // The two amount columns are compared as amounts rather than as text, so
    // that 42000.00 and the grouped 42,000.00 are the same figure -- but an
    // empty column is compared as empty, because a line with nothing in that
    // column is not a line with zero in it.
    void then_report_lines_are(const std::vector<ReportDetailLineString>& values) {
        ASSERT_EQ(values.size(), lines_.size())
            << "number of report lines. Got:" << "\n" << listing(lines_);
        for (std::size_t i = 0; i < values.size(); ++i) {
            const ReportDetailLineString& want = values[i];
            const reports::DetailLine& got = lines_[i];
            const std::string where = "line " + std::to_string(i + 1) + " (" +
                                      got.account + ")";
            EXPECT_EQ(want.kind.value, reports::to_string(got.kind)) << where << " kind";
            EXPECT_EQ(want.account.value, got.account) << where;
            EXPECT_EQ(std::stoi(want.level), got.level) << where << " level";
            // A date and a payee belong to a transaction line and to nothing
            // else: a category carries neither.
            const bool entry = got.kind == reports::LineKind::Transaction;
            EXPECT_EQ(want.date, entry ? got.date.iso() : std::string())
                << where << " date";
            EXPECT_EQ(want.payee.value, entry ? got.payee : std::string())
                << where << " payee";
            expect_column(want.each, got.has_each, got.each, where + " each");
            expect_column(want.amount, got.has_amount, got.amount, where + " amount");
        }
    }

    void examples_datatype_reportdetail(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            bool known = false;
            for (reports::Detail d : {reports::Detail::HighestOnly,
                                      reports::Detail::AllCategories,
                                      reports::Detail::Transactions})
                if (reports::to_string(d) == v.value) known = true;
            EXPECT_TRUE(known) << "unknown detail: " << v.value;
            // And it reads back as itself. Anything unrecognised is read as
            // AllCategories, so without this a spelling that differs between
            // the specification and the code would simply show the whole tree.
            EXPECT_EQ(v.value, reports::to_string(reports::detail_from_string(v.value)))
                << "detail does not round trip: " << v.value;
        }
    }

    void examples_datatype_reportlinekind(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            bool known = false;
            for (reports::LineKind k : {reports::LineKind::Category,
                                        reports::LineKind::Other,
                                        reports::LineKind::Transaction})
                if (reports::to_string(k) == v.value) known = true;
            EXPECT_TRUE(known) << "unknown line kind: " << v.value;
        }
    }

private:
    static void expect_column(const std::string& want, bool has, const Money& got,
                              const std::string& where) {
        if (want.empty()) {
            EXPECT_FALSE(has) << where << " should be empty, is " << got.in_register();
            return;
        }
        EXPECT_TRUE(has) << where << " should be " << want << ", is empty";
        if (has) EXPECT_EQ(Money(want).cents(), got.cents()) << where;
    }

    static std::string listing(const std::vector<reports::DetailLine>& lines) {
        std::string out;
        for (const reports::DetailLine& line : lines) {
            out += "  " + reports::to_string(line.kind) + " " + line.account +
                   " level " + std::to_string(line.level);
            if (line.kind == reports::LineKind::Transaction)
                out += " " + line.date.iso() + " " + line.payee;
            if (line.has_each) out += " each " + line.each.in_register();
            if (line.has_amount) out += " amount " + line.amount.in_register();
            out += "\n";
        }
        return out;
    }

public:

public:

    void when_balance_sheet_run(const std::vector<ReportSpecString>& values) {
        ASSERT_FALSE(values.empty());
        const auto to = types::Date::from_iso(values.front().to);
        ASSERT_TRUE(to.has_value()) << values.front().to;
        balance_sheet_as_at_ = *to;
        balance_sheet_ = reports::balance_sheet(chart_, book_, *to,
                                                parse_bool_cell(values.front().zerorows));
    }

    void then_balance_sheet_rows_are(const std::vector<BalanceSheetRowString>& values) {
        ASSERT_EQ(values.size(), balance_sheet_.rows.size())
            << "number of balance sheet rows";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const reports::BalanceSheetRow& got = balance_sheet_.rows[i];
            const std::string where = "row " + std::to_string(i);
            EXPECT_EQ(values[i].section.value, reports::to_string(got.section)) << where << " section";
            EXPECT_EQ(blank(values[i].group), got.group) << where << " group";
            EXPECT_EQ(blank(values[i].account), got.account) << where << " account";
            EXPECT_EQ(std::stoi(values[i].level), got.level) << where << " level";
            EXPECT_EQ(Money(values[i].balance).cents(), got.balance.cents()) << where << " balance";
            EXPECT_EQ(parse_bool_cell(values[i].isheading), got.is_heading) << where << " heading";
            EXPECT_EQ(parse_bool_cell(values[i].issubtotal), got.is_subtotal)
                << where << " subtotal";
        }
    }

    void then_balance_sheet_total_is(const std::vector<NetWorthString>& values) {
        for (const auto& value : values) {
            EXPECT_EQ(Money(value.assets).cents(), balance_sheet_.totals.assets.cents())
                << "assets";
            EXPECT_EQ(Money(value.liabilities).cents(),
                      balance_sheet_.totals.liabilities.cents()) << "liabilities";
            EXPECT_EQ(Money(value.networth).cents(), balance_sheet_.totals.net.cents())
                << "net worth";
        }
    }

    void then_net_worth_comes_from(const std::vector<NetWorthTieString>& values) {
        const reports::NetWorthTie got =
            reports::net_worth_tie(chart_, book_, balance_sheet_as_at_);
        for (const auto& value : values) {
            EXPECT_EQ(Money(value.openingequity).cents(), got.opening_equity.cents())
                << "opening equity";
            EXPECT_EQ(Money(value.earned).cents(), got.earned.cents()) << "earned";
            EXPECT_EQ(Money(value.spent).cents(), got.spent.cents()) << "spent";
            EXPECT_EQ(Money(value.networth).cents(), got.net_worth.cents()) << "net worth";
        }
    }

    void examples_datatype_balancesheetsection(const std::vector<EnumerationValuesString>& values) {
        for (const auto& value : values) {
            const EnumerationValuesTyped v = EnumerationValuesTyped::from_string_struct(value);
            bool known = false;
            for (reports::BalanceSheetSection s : {reports::BalanceSheetSection::Assets,
                                                   reports::BalanceSheetSection::Liabilities})
                if (reports::to_string(s) == v.value) known = true;
            EXPECT_TRUE(known) << "unknown balance sheet section: " << v.value;
        }
    }

private:
    static std::string blank(const std::string& s) {
        return (s == "none" || s == DNCString) ? std::string() : s;
    }

    types::Date balance_sheet_as_at_{2024, 1, 1};
    reports::BalanceSheet balance_sheet_;
public:

    // A transaction recorded through SourceAccount/Destination/Amount reports
    // the same as one recorded through postings. A Destination of Split is
    // not handled here yet -- see TransactionSplit in Transactions.
    void given_transactions_are(const std::vector<TransactionString>& values) {
        transactions_.clear();
        splits_.clear();
        for (const auto& value : values) {
            const auto date = types::Date::from_iso(value.date);
            ASSERT_TRUE(date.has_value()) << value.date;
            const std::string dest = blank(value.destination);

            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef(value.ref);
            t.date = *date;
            t.payee = types::PayeeName(blank(value.payee));
            t.source_account = chart_.ref_for_path(blank(value.sourceaccount));
            t.amount = Money(value.amount);
            // Split is not an account: it is the transaction saying its other
            // side is in the lines table, so it takes the split ref rather
            // than a path looked up in the chart.
            if (dest == "Split") t.destination = chart::split_ref();
            else if (!dest.empty()) t.destination = chart_.ref_for_path(dest);
            transactions_.push_back(t);
        }
        rebuild_book();
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
        rebuild_book();
    }

};
