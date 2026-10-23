#pragma once
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

// The same answer for every account at once, in one pass over the postings.
// Asked one account at a time it is a scan of the whole book each time, so
// offering from a chart of a few hundred categories read a book of thirty
// thousand transactions a few hundred times -- which is what made the picker
// take seconds to open and every keystroke take as long again.
//
// An account that is in no posting is absent from the map rather than present
// with some stand-in date: never used and used long ago are different answers
// and the first one is offered.
using LastUsedDates = std::unordered_map<std::string, types::Date>;

inline LastUsedDates last_used_dates(const ledger::Ledger& book) {
    LastUsedDates latest;
    latest.reserve(book.postings().size() / 4 + 1);
    for (const ledger::DatedPosting& p : book.postings()) {
        const auto at = latest.find(p.account.value());
        if (at == latest.end())
            latest.emplace(p.account.value(), p.date);
        else if (p.date > at->second)
            at->second = p.date;
    }
    return latest;
}

// What is offered while typing, in path order. Typing a parent brings every
// category under it, because that is how a person says "somewhere in here" --
// except for an alias (a token starting with '@'), which stands for exactly
// the one category it names and brings no children, because typing one is
// saying exactly which category is meant. A hidden account is never offered,
// and neither is a placeholder-free category nobody has posted to in
// kCategoryIsUnusedAfterDays, unless unused ones were asked for.
inline std::vector<Account> offered_for_typed(const Chart& accounts,
                                              const LastUsedDates& latest,
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
    std::unordered_set<std::string> already_have;
    for (const Account& m : matched) {
        for (const Account& a : all) {
            if (a.hidden || already_have.count(a.path.value())) continue;
            const bool belongs = is_alias ? a.path.value() == m.path.value()
                                          : Chart::is_descendant_or_self(a.path.value(),
                                                                         m.path.value());
            if (!belongs) continue;
            if (!include_unused && !a.placeholder) {
                const auto used = latest.find(a.path.value());
                if (used != latest.end() &&
                    types::Date::days_between(used->second, today) > kCategoryIsUnusedAfterDays)
                    continue;
            }
            already_have.insert(a.path.value());
            out.push_back(a);
        }
    }
    std::sort(out.begin(), out.end(), [](const Account& a, const Account& b) {
        return a.path.value() < b.path.value();
    });
    return out;
}

// The old shape, kept for callers with a book and no map in hand. It reads the
// dates first, which is one pass rather than one per candidate.
inline std::vector<Account> offered_for_typed(const Chart& accounts, const ledger::Ledger& book,
                                              const std::string& typed, bool include_unused,
                                              const types::Date& today) {
    return offered_for_typed(accounts, last_used_dates(book), typed, include_unused, today);
}

}  // namespace chart
