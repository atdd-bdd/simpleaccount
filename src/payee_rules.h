#pragma once
#include <cstddef>
#include <algorithm>
#include <set>
#include <vector>
#include <regex>
#include <string>
#include "account_type.h"
#include "chart.h"
#include "ledger.h"
#include "posting.h"
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
// A pattern typed with nothing but blanks in it is a pattern of nothing.
inline std::string trimmed(const std::string& s) {
    std::size_t from = 0;
    while (from < s.size() && (s[from] == ' ' || s[from] == 9)) ++from;
    std::size_t to = s.size();
    while (to > from && (s[to - 1] == ' ' || s[to - 1] == 9)) --to;
    return s.substr(from, to - from);
}

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

// Words a card network adds to a name that say nothing about who was paid.
inline const std::set<std::string>& noise_words() {
    static const std::set<std::string> table = {
        "PMTS", "PMT", "PAYMENT", "PURCHASE", "POS", "DEBIT", "CREDIT", "CARD",
        "XXXXX", "ACH", "EFT", "WEB", "RECUR", "RECURRING", "ONLINE", "BILLPAY",
    };
    return table;
}

// The two-letter codes a till adds for the state it stands in. Only dropped at
// the end of a name, where they are a location rather than a word.
inline bool looks_like_a_state(const std::string& word) {
    if (word.size() != 2) return false;
    for (const char c : word)
        if (c < 'A' || c > 'Z') return false;
    return true;
}

inline bool carries_a_digit(const std::string& word) {
    for (const char c : word)
        if (c >= '0' && c <= '9') return true;
    return false;
}

// The part of a name that identifies the payee, which is what a rule is made
// from. See the significant-name rule in Payees.spectable.
//
// A bank writes the shop, then whatever that shop's till, branch or town adds.
// The shop repeats; the rest changes every visit, so a rule made from the whole
// name would match once and never again -- SHELL OIL 3344 today, SHELL OIL 2343
// next week, and the second one uncategorised.
//
// A guess, and offered rather than applied: a payee whose name genuinely carries
// a digit would be cut too short, and only the user can see that. The first word
// is always kept, because a pattern of nothing would match everything.
inline std::string pattern_for(const std::string& raw_name) {
    std::vector<std::string> words;
    std::string current;
    for (const char c : raw_name + " ") {
        if (c == ' ' || c == '\t') {
            if (!current.empty()) words.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (words.empty()) return raw_name;

    // Everything from the first word carrying a digit is a number, a date or a
    // reference, and none of those repeat.
    std::vector<std::string> kept;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i > 0 && (carries_a_digit(words[i]) || words[i].front() == '#')) break;
        kept.push_back(words[i]);
    }
    // Then whatever a card network or a till added at the end.
    while (kept.size() > 1 &&
           (looks_like_a_state(kept.back()) || noise_words().count(kept.back()) == 1))
        kept.pop_back();

    std::string out;
    for (std::size_t i = 0; i < kept.size(); ++i) {
        if (i > 0) out += ' ';
        out += kept[i];
    }
    return out;
}

// One rule: what to look for, and what to do when it is found. A rule with no
// category tidies the name and leaves the category alone, which is the common
// case for a shop whose charges go to different places.
struct Rule {
    std::string pattern;
    MatchType match_type = MatchType::Contains;
    std::string payee;
    std::string category;
    bool enabled = true;
};

// The rule that applies to this name, or nothing. Several may match and the most
// specific wins -- taken to be the longest pattern, because a user who adds a
// longer pattern expects it to win and that needs no explaining.
//
// A disabled rule is passed over rather than deleted: a rule turned off to see
// what happens without it should be easy to turn back on.
inline const Rule* best_for(const std::vector<Rule>& rules, const std::string& raw_name) {
    const Rule* best = nullptr;
    for (const Rule& rule : rules) {
        if (!rule.enabled) continue;
        if (!matches(rule.match_type, rule.pattern, raw_name)) continue;
        if (best == nullptr ||
            wins(rule.pattern, rule.match_type, best->pattern, best->match_type))
            best = &rule;
    }
    return best;
}

// What a rule makes of a downloaded name: the payee to record, the category to
// post the other side to, and the raw name to keep. Returned together because a
// caller that applies one without the others would lose something.
struct Applied {
    std::string payee;
    std::string category;      // empty leaves the category to be chosen by sign
    std::string raw_name;      // empty where nothing was renamed
    bool renamed = false;
};

inline Applied apply(const std::vector<Rule>& rules, const std::string& raw_name) {
    Applied out;
    out.payee = raw_name;
    const Rule* rule = best_for(rules, raw_name);
    if (rule == nullptr) return out;

    if (!rule->payee.empty() && rule->payee != raw_name) {
        out.payee = rule->payee;
        out.raw_name = raw_name;
        out.renamed = true;
    }
    if (rule->category != "none") out.category = rule->category;
    return out;
}

// What applying the rules to the whole book came to, counted, because a book
// of twenty years is too many transactions to check by eye. See the
// retroactive-application scenarios of Payees.spectable.
struct BookApplication {
    int matched = 0;          // changed by a rule
    int already_correct = 0;  // a rule matched and there was nothing to change
    int skipped = 0;          // locked, a split, no rule matched, or categorised by hand
};

namespace detail {

inline bool is_a_category_account(const chart::Chart& accounts, const std::string& path) {
    const chart::Account* a = accounts.find(path);
    return a != nullptr && (a->type == types::AccountType::Income ||
                            a->type == types::AccountType::Expense);
}

inline bool is_uncategorized(const std::string& path) {
    return path == "Expenses:Uncategorized" || path == "Income:Uncategorized";
}

inline bool has_category(const Rule& rule) {
    return !rule.category.empty() && rule.category != "none";
}

}  // namespace detail

// Runs every enabled rule over every transaction already in the book: the
// menu item that does in bulk what accepting one rule offers to do for the
// history it would have caught. Two protections, both because a bulk change
// has to earn more trust than a single one does.
//
// A category chosen by hand is a decision and is left alone -- only a posting
// still in Uncategorized is moved. A reconciled transaction is left alone
// entirely, payee included: a bulk change that altered a reconciled balance,
// or even just the name beside it, would disturb an agreement with a
// statement that nobody asked it to touch. A split is left alone too, because
// there is no one category posting to move without guessing which.
inline BookApplication apply_to_book(const std::vector<Rule>& rules,
                                     const chart::Chart& accounts,
                                     std::vector<ledger::Transaction>* transactions) {
    BookApplication out;
    if (transactions == nullptr) return out;

    for (ledger::Transaction& t : *transactions) {
        bool locked = false;
        for (const ledger::Posting& p : t.postings)
            if (p.cleared == types::ClearedStatus::Reconciled) locked = true;
        if (locked) { ++out.skipped; continue; }

        std::vector<std::size_t> categories;
        for (std::size_t i = 0; i < t.postings.size(); ++i)
            if (detail::is_a_category_account(accounts, t.postings[i].account.value()))
                categories.push_back(i);
        if (categories.size() != 1) { ++out.skipped; continue; }
        ledger::Posting& on = t.postings[categories.front()];

        const std::string raw = t.raw_name.empty() ? t.payee.value() : t.raw_name;
        const Rule* rule = best_for(rules, raw);
        if (rule == nullptr) { ++out.skipped; continue; }

        bool changed = false;
        if (!rule->payee.empty() && rule->payee != raw && t.payee.value() != rule->payee) {
            t.payee = types::PayeeName(rule->payee);
            t.raw_name = raw;
            changed = true;
        }

        if (!detail::has_category(*rule)) {
            // A pattern-only rule: the name is the whole of what it does.
            if (changed) ++out.matched; else ++out.already_correct;
            continue;
        }
        if (on.account.value() == rule->category) {
            ++out.already_correct;
        } else if (detail::is_uncategorized(on.account.value())) {
            on.account = types::AccountPath(rule->category);
            ++out.matched;
        } else {
            // Categorised by hand to something else, which is a decision.
            ++out.skipped;
        }
    }
    return out;
}

// The rule a categorisation suggests: made from the significant part of the name,
// recording that name as the payee and the category that was just chosen.
//
// Offered, not taken. The user is the only one who knows whether this payee
// always takes this category, and whether the shortening went too far.
inline Rule suggest(const std::string& raw_name, const std::string& category) {
    Rule rule;
    rule.pattern = pattern_for(raw_name);
    rule.match_type = MatchType::Contains;
    rule.payee = rule.pattern;
    rule.category = category;
    rule.enabled = true;
    return rule;
}

// ---------------------------------------------------------------------------
// Keeping the rules
// ---------------------------------------------------------------------------
//
// The rules are worked on in a window of their own. See the keeping-the-rules
// section of Payees.spectable.

// Why a rule was not taken. Refused where it is typed rather than allowed to
// reach the rules, because both of these would do damage quietly.
struct Refusal {
    bool refused = false;
    std::string reason;
};

// The rules in the order they are tried, which is the order they are listed in:
// a rule above another in the list is a rule that beats it. Disabled rules keep
// their place rather than being moved out of the way.
//
// A stable sort, so that two rules the precedence rule cannot separate stay in
// the order they were added -- which is what that rule says happens.
inline std::vector<Rule> in_order(const std::vector<Rule>& rules) {
    std::vector<Rule> out = rules;
    std::stable_sort(out.begin(), out.end(), [](const Rule& a, const Rule& b) {
        return wins(a.pattern, a.match_type, b.pattern, b.match_type);
    });
    return out;
}

// What is wrong with a rule, if anything. A pattern of nothing is contained in
// every name, so it would rename every payee in the book; a rule with no payee
// would blank the name the bank sent and leave nothing in its place.
inline Refusal check(const Rule& one) {
    if (detail::trimmed(one.pattern).empty())
        return {true, "a rule needs something to look for"};
    if (detail::trimmed(one.payee).empty())
        return {true, "a rule needs a payee name"};
    return {};
}

inline Refusal add(std::vector<Rule>* rules, const Rule& one) {
    const Refusal no = check(one);
    if (no.refused) return no;
    rules->push_back(one);
    return {};
}

// Which rule is meant, by what it looks for. Two rules may share a pattern, so
// the match type is part of naming one. The first that matches is the one
// changed, which is the one the list showed.
inline Rule* find(std::vector<Rule>* rules, const std::string& pattern,
                  MatchType type) {
    for (Rule& one : *rules)
        if (one.pattern == pattern && one.match_type == type) return &one;
    return nullptr;
}

inline Refusal change(std::vector<Rule>* rules, const std::string& was_pattern,
                      MatchType was_type, const Rule& to) {
    const Refusal no = check(to);
    if (no.refused) return no;
    Rule* at = find(rules, was_pattern, was_type);
    if (at == nullptr) return {true, "there is no rule looking for " + was_pattern};
    *at = to;
    return {};
}

// Deleting a rule stops it applying from then on. It does not undo the
// categorising it already did: a rule applies when a transaction is imported or
// categorised, not continuously, and a delete that rewrote a year of history
// would be a surprise nobody asked for.
inline bool remove(std::vector<Rule>* rules, const std::string& pattern,
                   MatchType type) {
    for (std::size_t i = 0; i < rules->size(); ++i) {
        if ((*rules)[i].pattern != pattern) continue;
        if ((*rules)[i].match_type != type) continue;
        rules->erase(rules->begin() + static_cast<std::ptrdiff_t>(i));
        return true;
    }
    return false;
}

// Disabling is the usual move: a rule that was wrong once is usually wanted
// again in a changed form.
inline bool set_enabled(std::vector<Rule>* rules, const std::string& pattern,
                        MatchType type, bool enabled) {
    Rule* at = find(rules, pattern, type);
    if (at == nullptr) return false;
    at->enabled = enabled;
    return true;
}

}  // namespace payees
