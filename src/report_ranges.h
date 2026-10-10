#pragma once
#include <optional>
#include <string>
#include <vector>
#include "date.h"

// The date ranges a report can be asked for. See Reports.spectable.
namespace reports {

struct Range {
    types::Date from;
    types::Date to;
};

// Taken relative to today. The ranges that end today end today rather than at the
// end of the period, because a month-to-date total compared with a whole previous
// month is a comparison nobody wants by accident.
//
// Confirmed against Quicken: its Year to Date report reads "1/1/2026 through
// 10/3/2026", which is today, and its All Dates report ends at today too.
inline std::optional<Range> named_range(const std::string& name,
                                        const types::Date& today,
                                        const types::Date& book_start) {
    const int y = today.year();
    const int m = today.month();

    if (name == "ThisMonth")
        return Range{today.start_of_month(), today.end_of_month()};
    if (name == "MonthToDate")
        return Range{today.start_of_month(), today};
    if (name == "LastMonth") {
        const types::Date last = today.start_of_month().plus_days(-1);
        return Range{last.start_of_month(), last.end_of_month()};
    }
    if (name == "ThisQuarter") {
        const int first = ((m - 1) / 3) * 3 + 1;
        const types::Date begin(y, first, 1);
        const types::Date end = types::Date(y, first + 2, 1).end_of_month();
        return Range{begin, end};
    }
    if (name == "ThisYear")
        return Range{types::Date(y, 1, 1), types::Date(y, 12, 31)};
    if (name == "YearToDate")
        return Range{types::Date(y, 1, 1), today};
    if (name == "LastYear")
        return Range{types::Date(y - 1, 1, 1), types::Date(y - 1, 12, 31)};
    if (name == "Last12Months") {
        // Twelve months back from today, so the window is a year long inclusive.
        const types::Date a_year_ago(y - 1, m, today.day());
        return Range{a_year_ago.plus_days(1), today};
    }
    if (name == "All")
        return Range{book_start, today};
    return std::nullopt;
}

// A fiscal year is named for the calendar year it ends in. With a start month of
// July, the year running July 2023 to June 2024 is FY2024. A start month of
// January makes it the calendar year, which is the default and what most want.
inline Range fiscal_year(int start_month, int fiscal_year_label) {
    if (start_month <= 1)
        return Range{types::Date(fiscal_year_label, 1, 1),
                     types::Date(fiscal_year_label, 12, 31)};
    const types::Date from(fiscal_year_label - 1, start_month, 1);
    const types::Date to = types::Date(fiscal_year_label, start_month - 1, 1).end_of_month();
    return Range{from, to};
}

// One column of a multi-year report.
struct NamedColumn {
    std::string label;   // "2023", or "2024 to 31 May" when it is cut short
    Range range;
};

inline std::string month_name(int m) {
    static const char* const names[] = {"January", "February", "March",     "April",
                                        "May",      "June",     "July",      "August",
                                        "September","October", "November",  "December"};
    return (m >= 1 && m <= 12) ? names[m - 1] : "";
}

// Splits a range into one column per calendar year it touches, the last one
// short if the range ends before that year is out. See the year-column
// section of Reports.spectable.
inline std::vector<NamedColumn> year_columns(const types::Date& from, const types::Date& to) {
    std::vector<NamedColumn> out;
    int year = from.year();
    types::Date start = from;
    while (!(to < start)) {
        const types::Date full_year_end(year, 12, 31);
        const types::Date end = to < full_year_end ? to : full_year_end;
        NamedColumn c;
        c.range = Range{start, end};
        c.label = end == full_year_end
                      ? std::to_string(year)
                      : std::to_string(year) + " to " + std::to_string(end.day()) + " " +
                            month_name(end.month());
        out.push_back(c);
        ++year;
        start = types::Date(year, 1, 1);
    }
    return out;
}

}  // namespace reports
