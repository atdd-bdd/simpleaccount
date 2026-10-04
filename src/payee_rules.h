#pragma once
#include <regex>
#include <string>
#include "text_types.h"

// Turning what the bank calls a payee into what the user calls it. Nothing here
// guesses: a rule exists because the user made it, or because the user
// categorised a transaction and agreed to remember it. See Payees.spectable.
namespace payees {

enum class MatchType { Contains, StartsWith, Exact, Regex };

inline MatchType match_type_from_string(const std::string& s) {
    if (s == "StartsWith") return MatchType::StartsWith;
    if (s == "Exact")      return MatchType::Exact;
    if (s == "Regex")      return MatchType::Regex;
    return MatchType::Contains;
}

inline std::string to_string(MatchType t) {
    switch (t) {
        case MatchType::Contains:   return "Contains";
        case MatchType::StartsWith: return "StartsWith";
        case MatchType::Exact:      return "Exact";
        case MatchType::Regex:      return "Regex";
    }
    return "Contains";
}

namespace detail {
inline std::string upper(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    return out;
}
}  // namespace detail

// The comparison ignores case in every form. Contains is the default because it
// is what a user means by typing part of a name.
inline bool matches(MatchType type, const std::string& pattern, const std::string& raw_name) {
    const std::string p = detail::upper(pattern);
    const std::string n = detail::upper(raw_name);
    switch (type) {
        case MatchType::Contains:   return n.find(p) != std::string::npos;
        case MatchType::StartsWith: return n.rfind(p, 0) == 0;
        case MatchType::Exact:      return n == p;
        case MatchType::Regex: {
            try {
                return std::regex_search(n, std::regex(p, std::regex::icase));
            } catch (const std::regex_error&) {
                return false;
            }
        }
    }
    return false;
}

// The most specific rule wins, which is taken to be the one with the longest
// pattern. Length is a crude measure and is used because it needs no explaining:
// a user who adds a longer pattern expects it to win. Ties go by match type, in
// the order Exact, StartsWith, Regex, Contains, and then by the order the rules
// were added.
inline int specificity_of(MatchType t) {
    switch (t) {
        case MatchType::Exact:      return 3;
        case MatchType::StartsWith: return 2;
        case MatchType::Regex:      return 1;
        case MatchType::Contains:   return 0;
    }
    return 0;
}

// True when A wins over B.
inline bool wins(const std::string& pattern_a, MatchType type_a,
                 const std::string& pattern_b, MatchType type_b) {
    if (pattern_a.size() != pattern_b.size())
        return pattern_a.size() > pattern_b.size();
    if (specificity_of(type_a) != specificity_of(type_b))
        return specificity_of(type_a) > specificity_of(type_b);
    return false;  // added earlier wins, and A is taken to be the earlier one
}

}  // namespace payees
