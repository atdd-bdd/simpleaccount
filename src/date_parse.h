#pragma once
#include <optional>
#include <string>
#include <vector>
#include "date.h"

// Reading a date out of a file. The two formats need different readers: a QIF
// date carries Quicken's own century marker and pads with blanks, a CSV date
// does neither and may use dashes. Both take the order from a profile and never
// guess it.
namespace dates {

namespace detail {

inline std::string trim(const std::string& s) {
    const std::size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    const std::size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

// Digits with blanks allowed around them, which is how an older Quicken pads.
inline std::optional<int> number(const std::string& raw) {
    const std::string s = trim(raw);
    if (s.empty()) return std::nullopt;
    for (char c : s)
        if (c < '0' || c > '9') return std::nullopt;
    return std::stoi(s);
}

// Puts the three numbers the right way round for the order given.
inline std::optional<types::Date> assemble(int a, int b, int c, types::DateOrder order) {
    int y = 0, m = 0, d = 0;
    switch (order) {
        case types::DateOrder::MDY: m = a; d = b; y = c; break;
        case types::DateOrder::DMY: d = a; m = b; y = c; break;
        case types::DateOrder::YMD: y = a; m = b; d = c; break;
    }
    if (!types::Date::is_valid(y, m, d)) return std::nullopt;
    return types::Date(y, m, d);
}

}  // namespace detail

// A QIF date: M/D'YY with an apostrophe, or M/D/YY with a slash, and any part
// blank-padded rather than zero-padded -- the year included, which is the form
// easiest to get wrong. The apostrophe is Quicken's century marker and always
// means the two thousands; a slash year follows the window rule.
//
// Every date in both of the real exports in testdata uses the apostrophe form,
// with year tokens from " 5" to "26" and no gap, which is what settles the
// reading.
inline std::optional<types::Date> parse_qif(const std::string& text,
                                            types::DateOrder order) {
    const std::string s = detail::trim(text);
    if (s.empty()) return std::nullopt;

    // The second separator is either a slash or the century apostrophe.
    const std::size_t first = s.find('/');
    if (first == std::string::npos) return std::nullopt;
    std::size_t second = std::string::npos;
    bool apostrophe = false;
    for (std::size_t i = first + 1; i < s.size(); ++i) {
        if (s[i] == '/' || s[i] == '\'') {
            second = i;
            apostrophe = (s[i] == '\'');
            break;
        }
    }
    if (second == std::string::npos) return std::nullopt;

    const auto a = detail::number(s.substr(0, first));
    const auto b = detail::number(s.substr(first + 1, second - first - 1));
    const auto c = detail::number(s.substr(second + 1));
    if (!a || !b || !c) return std::nullopt;

    int year = *c;
    const std::size_t year_digits = detail::trim(s.substr(second + 1)).size();
    if (year_digits <= 2)
        year = apostrophe ? 2000 + year : types::year_from_two_digits(year);

    // The year is always last in a QIF date, whichever way round the day and
    // month are, so YMD is not a reading this format offers.
    if (order == types::DateOrder::YMD) return std::nullopt;
    return detail::assemble(*a, *b, year, order);
}

// A CSV date: three numbers separated by slashes or dashes, a two or four digit
// year, in the order the profile states.
inline std::optional<types::Date> parse_csv(const std::string& text,
                                            types::DateOrder order) {
    const std::string s = detail::trim(text);
    if (s.empty()) return std::nullopt;

    std::vector<std::string> parts;
    std::string current;
    for (char ch : s) {
        if (ch == '/' || ch == '-') {
            parts.push_back(current);
            current.clear();
        } else {
            current += ch;
        }
    }
    parts.push_back(current);
    if (parts.size() != 3) return std::nullopt;

    const auto a = detail::number(parts[0]);
    const auto b = detail::number(parts[1]);
    const auto c = detail::number(parts[2]);
    if (!a || !b || !c) return std::nullopt;

    // The year is last except in YMD, where it is first.
    const bool year_first = (order == types::DateOrder::YMD);
    const std::string& year_text = year_first ? parts[0] : parts[2];
    int year = year_first ? *a : *c;
    if (detail::trim(year_text).size() <= 2) year = types::year_from_two_digits(year);

    return year_first ? detail::assemble(year, *b, *c, order)
                      : detail::assemble(*a, *b, year, order);
}

// Which order a file's own dates prove, where they prove anything. A first part
// above twelve can only be a day; a second part above twelve can only be a day
// the other way round. A file that proves neither is ambiguous and is reported
// rather than guessed at.
struct OrderEvidence {
    int first_part_over_12 = 0;
    int second_part_over_12 = 0;

    bool ambiguous() const { return first_part_over_12 == 0 && second_part_over_12 == 0; }
    std::optional<types::DateOrder> proven() const {
        if (first_part_over_12 > 0 && second_part_over_12 == 0) return types::DateOrder::DMY;
        if (second_part_over_12 > 0 && first_part_over_12 == 0) return types::DateOrder::MDY;
        return std::nullopt;  // nothing, or a contradiction
    }
};

}  // namespace dates
