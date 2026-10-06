#pragma once
#include <stdexcept>
#include <string>
#include <vector>

// The named text and number types of CoreTypes.spectable. Each one validates on
// construction and throws std::invalid_argument when the value is not one of its
// own, so that a wrong value cannot travel any further than the edge it entered
// at.
namespace types {

namespace detail {

inline std::string trim(const std::string& s) {
    const std::size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    const std::size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

inline std::string upper(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    return out;
}

// Digits, at most `places` of them after the point, never negative.
inline void check_decimal(const std::string& raw, int places, const char* what) {
    const std::string s = trim(raw);
    if (s.empty()) throw std::invalid_argument(std::string(what) + " is required");
    if (s.front() == '-') throw std::invalid_argument(std::string(what) + " is not negative");

    std::string whole, frac;
    bool seen_point = false;
    for (char c : s) {
        if (c == '.') {
            if (seen_point) throw std::invalid_argument("two decimal points");
            seen_point = true;
            continue;
        }
        if (c < '0' || c > '9') throw std::invalid_argument(std::string(what) + " is not a number");
        (seen_point ? frac : whole) += c;
    }
    if (whole.empty() && frac.empty())
        throw std::invalid_argument(std::string(what) + " is not a number");
    if (static_cast<int>(frac.size()) > places)
        throw std::invalid_argument(std::string(what) + " has too many decimal places");
}

}  // namespace detail

// A value that is simply text, with its own rule about what text is allowed.
#define SIMPLEACCOUNT_TEXT_TYPE(Name)                                         \
    class Name {                                                              \
    public:                                                                   \
        Name() = default;                                                     \
        explicit Name(const std::string& text) : value_(check(text)) {}        \
        static bool is_valid(const std::string& text) {                       \
            try { check(text); return true; }                                 \
            catch (const std::invalid_argument&) { return false; }            \
        }                                                                     \
        const std::string& value() const { return value_; }                   \
        bool operator==(const Name& o) const { return value_ == o.value_; }   \
        bool operator!=(const Name& o) const { return value_ != o.value_; }   \
        bool operator<(const Name& o) const { return value_ < o.value_; }     \
    private:                                                                  \
        std::string value_;                                                   \
        static std::string check(const std::string& text);                    \
    };

SIMPLEACCOUNT_TEXT_TYPE(AccountName)
SIMPLEACCOUNT_TEXT_TYPE(AccountPath)
SIMPLEACCOUNT_TEXT_TYPE(PayeeName)
SIMPLEACCOUNT_TEXT_TYPE(Symbol)
SIMPLEACCOUNT_TEXT_TYPE(SecurityName)
SIMPLEACCOUNT_TEXT_TYPE(CheckNumber)
SIMPLEACCOUNT_TEXT_TYPE(FitId)
SIMPLEACCOUNT_TEXT_TYPE(Heading)
SIMPLEACCOUNT_TEXT_TYPE(SearchText)
SIMPLEACCOUNT_TEXT_TYPE(ProfileName)
SIMPLEACCOUNT_TEXT_TYPE(TransactionRef)
SIMPLEACCOUNT_TEXT_TYPE(TransactionId)
SIMPLEACCOUNT_TEXT_TYPE(Shares)
SIMPLEACCOUNT_TEXT_TYPE(Price)

#undef SIMPLEACCOUNT_TEXT_TYPE

// One segment of a path. Blanks around it are trimmed, so they can never make a
// name invalid; a colon can, because it is what separates segments.
inline std::string AccountName::check(const std::string& text) {
    const std::string s = detail::trim(text);
    if (s.empty()) throw std::invalid_argument("an account name is required");
    if (s.find(':') != std::string::npos)
        throw std::invalid_argument("an account name may not contain a colon");
    return s;
}

// Root segment first, colon separated, and no segment may be empty.
inline std::string AccountPath::check(const std::string& text) {
    const std::string s = detail::trim(text);
    if (s.empty()) throw std::invalid_argument("an account path is required");
    std::size_t from = 0;
    while (true) {
        const std::size_t at = s.find(':', from);
        const std::string segment = s.substr(from, at == std::string::npos ? at : at - from);
        if (detail::trim(segment).empty())
            throw std::invalid_argument("an account path has an empty segment");
        if (at == std::string::npos) break;
        from = at + 1;
    }
    return s;
}

// Optional: a bank fee and an opening balance have no payee.
inline std::string PayeeName::check(const std::string& text) {
    return detail::trim(text);
}

// Optional, because a QIF export generally has no symbol at all. Folded to
// upper case, and never containing a space.
inline std::string Symbol::check(const std::string& text) {
    const std::string s = detail::trim(text);
    if (s.find(' ') != std::string::npos)
        throw std::invalid_argument("a symbol may not contain a space");
    return detail::upper(s);
}

// Required, and spaces are ordinary here: it is the identifier of a holding.
inline std::string SecurityName::check(const std::string& text) {
    const std::string s = detail::trim(text);
    if (s.empty()) throw std::invalid_argument("a security name is required");
    return s;
}

// Optional, and not always a number: some banks suffix a letter.
inline std::string CheckNumber::check(const std::string& text) {
    return detail::trim(text);
}

inline std::string FitId::check(const std::string& text) {
    const std::string s = detail::trim(text);
    if (s.empty()) throw std::invalid_argument("an identifier is required");
    return s;
}

inline std::string Heading::check(const std::string& text) {
    const std::string s = detail::trim(text);
    if (s.empty()) throw std::invalid_argument("a column heading is required");
    return s;
}

// Anything, including nothing: an empty search clears the search.
inline std::string SearchText::check(const std::string& text) { return text; }

inline std::string ProfileName::check(const std::string& text) {
    const std::string s = detail::trim(text);
    if (s.empty()) throw std::invalid_argument("a saved profile is named");
    return s;
}

// The creation moment, to the millisecond: eight digits of date, a T, then nine
// of time. Checked for shape rather than for being a real instant, because a
// name is not a date and nothing reads it back as one -- what matters is that
// every name has the same shape, so they sort into creation order as text.
inline std::string TransactionId::check(const std::string& text) {
    const std::string s = detail::trim(text);
    if (s.empty()) throw std::invalid_argument("a transaction always has an id");
    if (s.size() != 18 || s[8] != 'T')
        throw std::invalid_argument("an id is YYYYMMDDThhmmssSSS: " + s);
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i == 8) continue;
        if (s[i] < '0' || s[i] > '9')
            throw std::invalid_argument("an id is YYYYMMDDThhmmssSSS: " + s);
    }
    return s;
}

inline std::string TransactionRef::check(const std::string& text) {
    const std::string s = detail::trim(text);
    if (s.empty()) throw std::invalid_argument("a posting belongs to one transaction");
    return s;
}

// Six decimal places, which is what fund statements report, and never negative:
// this release does not model short positions.
inline std::string Shares::check(const std::string& text) {
    detail::check_decimal(text, 6, "a share count");
    return detail::trim(text);
}

// Four decimal places, never negative.
inline std::string Price::check(const std::string& text) {
    detail::check_decimal(text, 4, "a price");
    return detail::trim(text);
}

// The name of an account is the last segment of its path, and its parent is
// everything before that. A root account has no parent, so the parent is empty.
// Nothing stores either one: both are derived, so they cannot disagree with the
// path. See Accounts.spectable.
inline std::string name_of(const AccountPath& path) {
    const std::string& p = path.value();
    const std::size_t at = p.rfind(':');
    return at == std::string::npos ? p : p.substr(at + 1);
}

inline std::string parent_of(const AccountPath& path) {
    const std::string& p = path.value();
    const std::size_t at = p.rfind(':');
    return at == std::string::npos ? std::string() : p.substr(0, at);
}

// The segments of a path, root first.
inline std::vector<std::string> segments_of(const AccountPath& path) {
    std::vector<std::string> out;
    const std::string& p = path.value();
    std::size_t from = 0;
    while (true) {
        const std::size_t at = p.find(':', from);
        out.push_back(p.substr(from, at == std::string::npos ? at : at - from));
        if (at == std::string::npos) break;
        from = at + 1;
    }
    return out;
}

}  // namespace types
