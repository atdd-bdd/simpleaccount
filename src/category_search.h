#pragma once
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>
#include "chart.h"
#include "date.h"
#include "ledger.h"

// What a person is offered while typing a category into a transaction. See
// the finding-a-category section of Accounts.spectable.
namespace chart {

// A category untouched for this long is left out while typing, though it
// keeps its history and still appears in any report that covers the years it
// was used. Two years is long enough that something posted to once a year --
// an insurance renewal -- stays offered, and short enough that a category
// nobody has used since a job or a habit ended drops out of the way.
inline constexpr long long kCategoryIsUnusedAfterDays = 730;

namespace detail {

inline std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Any segment of the path matches, not just the whole of it: "fuel" finds
// Expenses:Auto:Fuel without anyone having typed Expenses or Auto.
inline bool any_segment_matches(const types::AccountPath& path, const std::string& typed) {
    for (const std::string& segment : types::segments_of(path))
        if (lowered(segment).find(lowered(typed)) != std::string::npos) return true;
    return false;
}

}  // namespace detail

// The most recent date anything posted to this account, or nothing if it
// never has -- which is not the same thing as "used, but long ago", and a
// brand new category is offered either way.
inline std::optional<types::Date> last_used(const ledger::Ledger& book,
                                            const std::string& path) {
    std::optional<types::Date> latest;
    for (const ledger::DatedPosting& p : book.postings())
        if (p.account.value() == path && (!latest || p.date > *latest)) latest = p.date;
    return latest;
}

// What is offered while typing, in path order. Typing a parent brings every
// category under it, because that is how a person says "somewhere in here" --
// except for an alias (a token starting with '@'), which stands for exactly
// the one category it names and brings no children, because typing one is
// saying exactly which category is meant. A hidden account is never offered,
// and neither is a placeholder-free category nobody has posted to in
// kCategoryIsUnusedAfterDays, unless unused ones were asked for.
inline std::vector<Account> offered_for_typed(const Chart& accounts, const ledger::Ledger& book,
                                              const std::string& typed, bool include_unused,
                                              const types::Date& today) {
    std::vector<Account> all = accounts.all();

    const bool is_alias = !typed.empty() && typed.front() == '@';
    std::vector<Account> matched;
    for (const Account& a : all) {
        if (a.hidden) continue;
        if (is_alias) {
            if (a.alias == typed) matched.push_back(a);
        } else if (detail::any_segment_matches(a.path, typed)) {
            matched.push_back(a);
        }
    }

    std::vector<Account> out;
    const auto already_have = [&](const std::string& path) {
        return std::any_of(out.begin(), out.end(),
                           [&](const Account& a) { return a.path.value() == path; });
    };
    for (const Account& m : matched) {
        for (const Account& a : all) {
            if (a.hidden || already_have(a.path.value())) continue;
            const bool belongs = is_alias ? a.path.value() == m.path.value()
                                          : Chart::is_descendant_or_self(a.path.value(),
                                                                         m.path.value());
            if (!belongs) continue;
            if (!include_unused && !a.placeholder) {
                const std::optional<types::Date> used = last_used(book, a.path.value());
                if (used && types::Date::days_between(*used, today) > kCategoryIsUnusedAfterDays)
                    continue;
            }
            out.push_back(a);
        }
    }
    std::sort(out.begin(), out.end(), [](const Account& a, const Account& b) {
        return a.path.value() < b.path.value();
    });
    return out;
}

}  // namespace chart
