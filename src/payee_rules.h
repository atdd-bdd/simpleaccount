#pragma once
#include <cstddef>
#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include <regex>
#include <string>
#include "account_type.h"
#include "chart.h"
#include "ledger.h"
#include "transaction.h"
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

inline bool is_a_category_account(const chart::Chart& accounts, const chart::Ref& ref) {
    const chart::Account* a = accounts.find_account_by_ref(ref);
    return a != nullptr && (a->type == types::AccountType::Income ||
                            a->type == types::AccountType::Expense);
}

inline bool is_uncategorized(const chart::Chart& accounts, const chart::Ref& ref) {
    if (ref.is_none()) return false;
    return ref == accounts.ref_for_path("Expenses:Uncategorized") ||
          ref == accounts.ref_for_path("Income:Uncategorized");
}

inline bool has_category(const Rule& rule) {
    return !rule.category.empty() && rule.category != "none";
}

// One transaction, counted into out. The whole of what applying the rules
// means, so that doing it to a book and doing it to a selection cannot drift
// apart.
inline void apply_to_one(const std::vector<Rule>& rules, const chart::Chart& accounts,
                         ledger::Transaction* t, BookApplication* out) {
    if (t->cleared == types::ClearedStatus::Reconciled) { ++out->skipped; return; }
    if (t->destination.is_none() || chart::is_split(t->destination) ||
        !is_a_category_account(accounts, t->destination)) {
        // A split, a transfer, or nothing known yet -- none of them a
        // category a rule has anything to move.
        ++out->skipped;
        return;
    }

    const std::string raw = t->raw_name.empty() ? t->payee.value() : t->raw_name;
    const Rule* rule = best_for(rules, raw);
    if (rule == nullptr) { ++out->skipped; return; }

    bool changed = false;
    if (!rule->payee.empty() && rule->payee != raw && t->payee.value() != rule->payee) {
        t->payee = types::PayeeName(rule->payee);
        t->raw_name = raw;
        changed = true;
    }

    if (!has_category(*rule)) {
        if (changed) ++out->matched; else ++out->already_correct;
        return;
    }
    const chart::Ref rule_ref = accounts.ref_for_path(rule->category);
    if (t->destination == rule_ref) {
        ++out->already_correct;
    } else if (is_uncategorized(accounts, t->destination)) {
        t->destination = rule_ref;
        ++out->matched;
    } else {
        ++out->skipped;
    }
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
    for (ledger::Transaction& t : *transactions) detail::apply_to_one(rules, accounts, &t, &out);
    return out;
}

// The same pass over the same rules in the same order, given a selection
// rather than a book. Applying the rules to everything is the right thing
// after a long import and the wrong thing while working through one statement:
// a rule written a minute ago is meant for the twenty lines in front of
// somebody, not for twenty years, where it will also rewrite transactions that
// were already right and that nobody asked about.
//
// Only the set of transactions is different. The two protections, the order,
// and the three counts are all apply_to_book's; see the register's context
// menu in UserInterface.spectable.
inline BookApplication apply_to_selection(const std::vector<Rule>& rules,
                                          const chart::Chart& accounts,
                                          std::vector<ledger::Transaction>* transactions,
                                          const std::vector<types::TransactionId>& selected) {
    BookApplication out;
    if (transactions == nullptr) return out;
    std::set<std::string> wanted;
    for (const types::TransactionId& id : selected) wanted.insert(id.value());
    for (ledger::Transaction& t : *transactions)
        if (wanted.count(t.id.value()) != 0) detail::apply_to_one(rules, accounts, &t, &out);
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

// Two rules are the same rule when they look for the same thing in the same
// way. The pattern is compared without case and without surrounding blanks,
// because matching ignores case as well, so the second of two rules differing
// only in that would never be reached. See what makes two rules the same rule
// in Payees.spectable.
inline bool same_rule(const std::string& pattern_a, MatchType type_a,
                      const std::string& pattern_b, MatchType type_b) {
    return type_a == type_b && detail::upper(detail::trimmed(pattern_a)) ==
                                   detail::upper(detail::trimmed(pattern_b));
}

inline Refusal add(std::vector<Rule>* rules, const Rule& one) {
    const Refusal no = check(one);
    if (no.refused) return no;
    // The book keeps the rules under the pattern and the match type together,
    // so a duplicate cannot be stored. Accepted here it broke the next write
    // of the whole book instead -- and every write after it, including one
    // that was no more than a column being widened.
    for (const Rule& already : *rules)
        if (same_rule(already.pattern, already.match_type, one.pattern,
                      one.match_type))
            return {true, "a rule already looks for " + already.pattern};
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
    // The same collision by the other road, with the rule being changed left
    // out of the comparison: it is the one it would otherwise collide with.
    for (Rule& already : *rules) {
        if (&already == at) continue;
        if (same_rule(already.pattern, already.match_type, to.pattern,
                      to.match_type))
            return {true, "a rule already looks for " + already.pattern};
    }
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

// There is no table of payees to keep in step with the register: the list
// offered for autocompletion is just the distinct payees the transactions
// already use, most recently used first, which is why it can never disagree
// with them. See the payee-list section of Payees.spectable.
struct PayeeListEntry {
    std::string payee;
    types::Date last_used;
    int count = 0;
};

inline std::vector<PayeeListEntry> payee_list(
        const std::vector<ledger::Transaction>& transactions) {
    std::map<std::string, PayeeListEntry> by_payee;
    for (const ledger::Transaction& t : transactions) {
        if (t.payee.value().empty()) continue;
        PayeeListEntry& e = by_payee[t.payee.value()];
        if (e.count == 0 || t.date > e.last_used) e.last_used = t.date;
        e.payee = t.payee.value();
        ++e.count;
    }
    std::vector<PayeeListEntry> out;
    for (auto& entry : by_payee) out.push_back(entry.second);
    std::sort(out.begin(), out.end(), [](const PayeeListEntry& a, const PayeeListEntry& b) {
        if (a.last_used != b.last_used) return b.last_used < a.last_used;
        return a.payee < b.payee;
    });
    return out;
}

// The best guess for a payee's category is the one it took last time -- the
// register already knows it, so offering it costs nothing. See the
// category-suggestion scenario of Payees.spectable.
struct PayeeSuggestion {
    bool found = false;
    std::string category;
    types::Date based_on;
};

inline PayeeSuggestion category_last_used_for(
        const std::vector<ledger::Transaction>& transactions, const chart::Chart& accounts,
        const std::vector<ledger::TransactionSplit>& splits, const std::string& payee) {
    PayeeSuggestion out;
    for (const ledger::Transaction& t : transactions) {
        if (t.payee.value() != payee) continue;
        for (const ledger::Side& s : ledger::sides_of(t, splits)) {
            if (!detail::is_a_category_account(accounts, s.account)) continue;
            if (!out.found || t.date > out.based_on) {
                out.found = true;
                out.category = accounts.path_for_ref(s.account);
                out.based_on = t.date;
            }
        }
    }
    return out;
}

// Merging is a rename applied to every transaction whose payee is the same
// shop under another spelling -- recognised the same way a rule's own
// pattern is, by the significant part of the name, case-insensitively, so
// "HARRIS TEETER #231" and "harris teeter 418" both become whatever name was
// chosen. See the merge scenario of Payees.spectable.
inline int merge_payees(std::vector<ledger::Transaction>* transactions,
                        const std::string& new_name) {
    const std::string target = detail::upper(pattern_for(new_name));
    int changed = 0;
    for (ledger::Transaction& t : *transactions) {
        if (t.payee.value().empty()) continue;
        if (detail::upper(pattern_for(t.payee.value())) != target) continue;
        if (t.payee.value() != new_name) {
            t.payee = types::PayeeName(new_name);
            ++changed;
        }
    }
    return changed;
}


// ---------------------------------------------------------------------------
// Two names, and the two tables between them
// ---------------------------------------------------------------------------
//
// A transaction carries two payee fields: the name the file wrote, kept exactly
// as it arrived, and the converted name, which is what the register shows and
// what a report groups by. Neither can do the other's job -- a duplicate test
// against the converted name fails the moment a conversion is added, and a
// report grouped by the name the bank wrote has one line per branch and per
// visit. See the two-names section of Payees.spectable.
//
// Two tables act on the two fields, and they are separate because they answer
// different questions and are edited at different times: the converter table
// says what a name reads as, the categorisation table says where the money
// goes.

// One row of the converter table.
struct Conversion {
    std::string match;      // with * standing for any number of characters
    std::string converted;  // the name the second field is given
};

// One row of the categorisation table.
struct Categorisation {
    std::string payee;      // matched the way a conversion pattern is
    std::string category;
};

// Simple wildcards and nothing else: the pattern is matched against the whole
// name, with * standing for any number of characters including none, so a
// pattern with no * in it is an exact comparison. Case is ignored, because a
// bank writes in capitals and a person does not.
//
// No other syntax -- no anchors, no classes, no counts -- because this table is
// edited by the person who keeps the book, and a pattern that is wrong in a way
// they cannot see would rename payees quietly. A blank is a character like any
// other, which is why HARRIS TEETER * does not match the bare name and
// HARRIS TEETER* does. See the conversion-matching rule in Payees.spectable.
inline bool conversion_matches(const std::string& pattern, const std::string& name) {
    const std::string p = detail::upper(pattern);
    const std::string n = detail::upper(name);
    std::size_t pi = 0;
    std::size_t ni = 0;
    std::size_t star = std::string::npos;  // the last * in the pattern
    std::size_t after_star = 0;            // and what it stood for so far
    while (ni < n.size()) {
        if (pi < p.size() && p[pi] == '*') {
            star = pi++;
            after_star = ni;
            continue;
        }
        if (pi < p.size() && p[pi] == n[ni]) {
            ++pi;
            ++ni;
            continue;
        }
        // Nothing matched here, so the last * has to stand for one more
        // character. With no * behind us there is nothing left to try.
        if (star == std::string::npos) return false;
        pi = star + 1;
        ni = ++after_star;
    }
    while (pi < p.size() && p[pi] == '*') ++pi;
    return pi == p.size();
}

// The first row of the table that matches, in the order the table lists them.
//
// First wins because the table is in the order the person put it in and they
// are looking at it. A hidden ranking -- longest pattern, fewest wildcards --
// would mean the row that acted was not the row they read, and the fix for a
// conversion that is too broad is then to move the specific row above it, which
// is a thing they can see themselves doing. Deliberately not how the rules
// above rank: those are a set with no order a user ever sees, and this is a
// list.
inline const Conversion* conversion_for(const std::vector<Conversion>& table,
                                        const std::string& raw_name) {
    for (const Conversion& one : table)
        if (conversion_matches(one.match, raw_name)) return &one;
    return nullptr;
}

// Which of the two names a categorisation was written against, so that a
// specification can say so and a reader of a window can be told.
struct Categorised {
    bool found = false;
    std::string category;
    bool against_converted = false;
};

// Both fields are offered to the table, the converted name first. The name the
// file wrote is tried only because the converted name matched nothing: a
// categorisation written before any conversion existed names what the bank
// sent, one written after names what the person reads, and both are worth
// keeping. Where both match, the row written against the chosen name is the
// later decision and the one meant.
inline Categorised category_for(const std::vector<Categorisation>& table,
                                const std::string& converted,
                                const std::string& raw_name) {
    Categorised out;
    if (!converted.empty()) {
        for (const Categorisation& one : table) {
            if (!conversion_matches(one.payee, converted)) continue;
            return {true, one.category, true};
        }
    }
    if (!raw_name.empty()) {
        for (const Categorisation& one : table) {
            if (!conversion_matches(one.payee, raw_name)) continue;
            return {true, one.category, false};
        }
    }
    return out;
}

// What an import records for one name a file wrote: both fields and, where
// either table or a rule says so, the category to post the other side to.
//
// The name the file wrote is kept whether or not anything converted it, because
// that is the field the next reading of the same statement is tested against.
struct Naming {
    std::string payee;
    std::string raw_name;
    std::string category;  // empty where nothing chose one
};

inline Naming name_for(const std::vector<Rule>& rules,
                       const std::vector<Conversion>& conversions,
                       const std::vector<Categorisation>& categories,
                       const std::string& from_file) {
    Naming out;
    const Applied by_rule = apply(rules, from_file);
    out.payee = by_rule.payee;
    out.category = by_rule.category;
    out.raw_name = from_file;
    // The converter table is the later and more explicit of the two, so where
    // it has something to say about this name it is what the register shows.
    if (const Conversion* c = conversion_for(conversions, from_file))
        out.payee = c->converted;
    if (out.category.empty()) {
        const Categorised where = category_for(categories, out.payee, from_file);
        if (where.found) out.category = where.category;
    }
    return out;
}

// The second use of the converter table: transactions already in the book,
// converted in one pass. The name found is kept as the first field where the
// conversion changes it, and a row nothing matched is left exactly as it was --
// there is no previous name to keep.
//
// A reconciled transaction is left alone, name included: a bulk change beside a
// balance that was agreed with a statement would disturb something nobody asked
// it to touch.
inline int convert_payees(const std::vector<Conversion>& table,
                          std::vector<ledger::Transaction>* transactions) {
    if (transactions == nullptr) return 0;
    int changed = 0;
    for (ledger::Transaction& t : *transactions) {
        if (t.cleared == types::ClearedStatus::Reconciled) continue;
        const std::string raw = t.raw_name.empty() ? t.payee.value() : t.raw_name;
        const Conversion* c = conversion_for(table, raw);
        if (c == nullptr || c->converted == t.payee.value()) continue;
        t.raw_name = raw;
        t.payee = types::PayeeName(c->converted);
        ++changed;
    }
    return changed;
}

// The same for the categorisation table, with the two protections a bulk change
// has to earn: only a posting still in Uncategorized is moved, because a
// category chosen by hand is a decision, and a reconciled transaction is left
// alone entirely. A split or a transfer has no one category posting to move
// without guessing which, so it is skipped too.
inline BookApplication assign_categories(const std::vector<Categorisation>& table,
                                         const chart::Chart& accounts,
                                         std::vector<ledger::Transaction>* transactions) {
    BookApplication out;
    if (transactions == nullptr) return out;
    for (ledger::Transaction& t : *transactions) {
        if (t.cleared == types::ClearedStatus::Reconciled) { ++out.skipped; continue; }
        if (t.destination.is_none() || chart::is_split(t.destination) ||
            !detail::is_a_category_account(accounts, t.destination)) {
            ++out.skipped;
            continue;
        }
        const Categorised where = category_for(table, t.payee.value(), t.raw_name);
        if (!where.found) { ++out.skipped; continue; }
        const chart::Ref wanted = accounts.ref_for_path(where.category);
        if (t.destination == wanted) {
            ++out.already_correct;
        } else if (detail::is_uncategorized(accounts, t.destination)) {
            t.destination = wanted;
            ++out.matched;
        } else {
            ++out.skipped;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// A split this payee always has
// ---------------------------------------------------------------------------
//
// A mortgage payment is three lines every month and the lines are the same
// three every month, so they are remembered against the payee and offered when
// a download of it arrives. Offered rather than written: the amounts move, and
// a split that no longer adds up is a thing to be shown rather than quietly
// absorbed into whichever line is biggest. See the remembered-split scenarios
// in Payees.spectable.

// Whether the remembered lines are amounts or proportions.
enum class SplitShape { Amounts, Percentages };

inline std::string to_string(SplitShape shape) {
    return shape == SplitShape::Percentages ? "Percentages" : "Amounts";
}

inline SplitShape split_shape_from_string(const std::string& text) {
    return text == "Percentages" ? SplitShape::Percentages : SplitShape::Amounts;
}

// One remembered line. Amount is read when the shape is Amounts and Percent
// when it is Percentages; the other is nothing and is not consulted, rather
// than the two being kept in step with each other.
struct RememberedSplitLine {
    std::string category;
    Money amount;
    double percent = 0.0;
    std::string memo;
};

// The rule that says a payee has one. Matched the way a payee rule is matched,
// against the name the bank sent, so that the pattern a person writes here
// reads like the patterns they write everywhere else in this program.
struct SplitRule {
    std::string pattern;
    MatchType match_type = MatchType::Contains;
    std::string payee;
    SplitShape shape = SplitShape::Amounts;
    std::vector<RememberedSplitLine> lines;
};

inline const SplitRule* split_rule_for(const std::vector<SplitRule>& rules,
                                       const std::string& raw_name) {
    for (const SplitRule& rule : rules)
        if (matches(rule.match_type, rule.pattern, raw_name)) return &rule;
    return nullptr;
}

// One line of what is offered.
struct OfferedSplitLine {
    std::string category;
    Money amount;
    std::string memo;
};

// What the split window is opened with, and what it has to say about it. The
// shortfall is the total less the lines: short by twenty is 20.00, over by
// twenty is -20.00, and balanced is the ordinary case that needs no words.
struct SplitOffer {
    bool offered = false;
    std::string payee;
    std::vector<OfferedSplitLine> lines;
    Money total;
    Money allocated;
    Money shortfall;
    bool balanced = false;
};

namespace detail {

inline long long to_the_cent(double value) {
    return static_cast<long long>(value < 0 ? value - 0.5 : value + 0.5);
}

}  // namespace detail

// Dividing a total in remembered proportions. Each line is rounded to the cent
// and whatever the rounding left over is added to the largest line, so that the
// lines come to what the percentages do and the adjustment lands where it is
// least visible.
//
// The percentages need not come to a hundred. Two lines of 33 each allocate
// two thirds of the payment and the rest is a shortfall, which is the honest
// answer: the person wrote 33 and 33, and inventing the missing 34 for them
// would be this program deciding where a third of the money went. See
// questions.md on the fourth row of the rounding rule.
inline std::vector<Money> percentage_amounts(const Money& total,
                                             const std::vector<double>& percents) {
    std::vector<Money> out;
    double whole = 0.0;
    long long allocated = 0;
    for (double percent : percents) {
        const long long cents =
            detail::to_the_cent(static_cast<double>(total.cents()) * percent / 100.0);
        out.push_back(Money::from_cents(cents));
        allocated += cents;
        whole += percent;
    }
    if (out.empty()) return out;
    const long long wanted =
        detail::to_the_cent(static_cast<double>(total.cents()) * whole / 100.0);
    const long long over = wanted - allocated;
    if (over == 0) return out;
    std::size_t biggest = 0;
    for (std::size_t i = 1; i < out.size(); ++i)
        if (out[i].cents() > out[biggest].cents()) biggest = i;
    out[biggest] = Money::from_cents(out[biggest].cents() + over);
    return out;
}

// The offer itself. The total is a magnitude: a payment of 1500.00 arrives as
// -1500.00 and its split lines are positive.
inline SplitOffer offer_split(const SplitRule& rule, const Money& amount) {
    SplitOffer out;
    out.offered = !rule.lines.empty();
    out.payee = rule.payee;
    out.total = amount.cents() < 0 ? -amount : amount;
    if (rule.shape == SplitShape::Percentages) {
        std::vector<double> percents;
        for (const RememberedSplitLine& line : rule.lines)
            percents.push_back(line.percent);
        const std::vector<Money> amounts = percentage_amounts(out.total, percents);
        for (std::size_t i = 0; i < rule.lines.size(); ++i)
            out.lines.push_back({rule.lines[i].category, amounts[i], rule.lines[i].memo});
    } else {
        for (const RememberedSplitLine& line : rule.lines)
            out.lines.push_back({line.category, line.amount, line.memo});
    }
    for (const OfferedSplitLine& line : out.lines)
        out.allocated = out.allocated + line.amount;
    out.shortfall = out.total - out.allocated;
    out.balanced = out.shortfall.cents() == 0;
    return out;
}

}  // namespace payees
