#pragma once
#include <string>
#include <vector>
#include "account_type.h"
#include "chart.h"

// Where an import is going, worked out before the file is read.
//
// The file first, the name of the file second, the register in front last. Each
// one is only consulted because the one before it had nothing to say, and which
// one answered is carried along with the answer: "we read this out of the file"
// and "this is the tab you were on" are worth different amounts of trust, and
// the dialog says which it is. See the section on where an import is going in
// UserInterface.spectable.
//
// Nothing here reads a file or knows what a file looks like. What a QFX says
// about its account is ImportOfx's business and what a CSV says is nothing at
// all, so the account the file named arrives here as a path or as empty.
namespace importing {

// What kind of file was chosen, and so what the file itself can say about the
// account it is for: a QFX names its institution and account number, a QIF
// names an account only when it carries an !Account block, and a CSV says
// nothing at all. Nothing here branches on it -- which reader runs is decided
// by what is inside the file -- but the dialog says which kind it is reading.
enum class FileFormat { Qfx, Qif, Csv };

inline std::string to_string(FileFormat f) {
    switch (f) {
        case FileFormat::Qfx: return "Qfx";
        case FileFormat::Qif: return "Qif";
        case FileFormat::Csv: return "Csv";
    }
    return "Csv";
}

enum class TargetSource { File, FileName, ActiveTab, NoneOffered };

inline std::string to_string(TargetSource s) {
    switch (s) {
        case TargetSource::File:        return "File";
        case TargetSource::FileName:    return "FileName";
        case TargetSource::ActiveTab:   return "ActiveTab";
        case TargetSource::NoneOffered: return "NoneOffered";
    }
    return "NoneOffered";
}

struct Proposal {
    std::string account;
    TargetSource source = TargetSource::NoneOffered;
};

// The precedence, and the whole of it. An empty proposal is not a failure of
// this function: it is the one case with no answer at all -- a file that says
// nothing, read while a report is in front -- and the caller refuses rather
// than guesses.
inline Proposal propose(const std::string& account_in_file,
                        const std::string& from_file_name,
                        const std::string& active_tab) {
    if (!account_in_file.empty()) return {account_in_file, TargetSource::File};
    if (!from_file_name.empty()) return {from_file_name, TargetSource::FileName};
    if (!active_tab.empty()) return {active_tab, TargetSource::ActiveTab};
    return {std::string(), TargetSource::NoneOffered};
}

namespace detail {

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

inline bool is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline char lowered(char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; }

// Letters and digits only, lowercased: an account's name with the spaces taken
// out of it, so that Opening Balances is the same word as OpeningBalances.
inline std::string squashed(const std::string& text) {
    std::string out;
    for (const char c : text)
        if (is_letter(c) || is_digit(c)) out.push_back(lowered(c));
    return out;
}

inline std::string only_digits(const std::string& text) {
    std::string out;
    for (const char c : text)
        if (is_digit(c)) out.push_back(c);
    return out;
}

inline bool all_digits(const std::string& word) {
    if (word.empty()) return false;
    for (const char c : word)
        if (!is_digit(c)) return false;
    return true;
}

}  // namespace detail

// The words of a file name: the extension dropped, and what is left cut at
// every run of characters that is not a letter or a digit, and between a letter
// and a digit. So Visa4455 is two words and 2026-02 is two more.
inline std::vector<std::string> words_of(const std::string& file_name) {
    std::string stem = file_name;
    const std::size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos && dot > 0) stem = stem.substr(0, dot);

    std::vector<std::string> out;
    std::string word;
    bool digits = false;
    for (const char c : stem) {
        const bool digit = detail::is_digit(c);
        if (!digit && !detail::is_letter(c)) {
            if (!word.empty()) out.push_back(word);
            word.clear();
            continue;
        }
        if (!word.empty() && digit != digits) {
            out.push_back(word);
            word.clear();
        }
        word.push_back(detail::lowered(c));
        digits = digit;
    }
    if (!word.empty()) out.push_back(word);
    return out;
}

// Whether the name of a file is talking about this one account. Asked of every
// candidate; the answer is only used when exactly one of them says yes.
//
// A word, or a run of words next to each other, that reads the same as the
// account's name once case and the spaces in the name are ignored, fits. So
// does a word of four digits or more that the account's number ends with.
// Three digits or fewer never fit anything: they are months, days and sequence
// numbers, and matching an account number on them would put a statement in the
// wrong account for a living.
inline bool name_fits(const std::string& file_name, const std::string& account_name,
                      const std::string& account_number) {
    const std::vector<std::string> words = words_of(file_name);

    const std::string wanted = detail::squashed(account_name);
    if (!wanted.empty())
        for (std::size_t i = 0; i < words.size(); ++i) {
            std::string run;
            for (std::size_t j = i; j < words.size(); ++j) {
                run += words[j];
                if (run == wanted) return true;
                if (run.size() >= wanted.size()) break;
            }
        }

    const std::string digits = detail::only_digits(account_number);
    if (!digits.empty())
        for (const std::string& word : words) {
            if (word.size() < 4 || !detail::all_digits(word)) continue;
            if (word.size() > digits.size()) continue;
            if (digits.compare(digits.size() - word.size(), word.size(), word) == 0)
                return true;
        }
    return false;
}

// An account a register can be opened on, and so an account an import can go
// into: not a placeholder, not a category, and not a hidden account.
inline bool can_be_imported_into(const chart::Account& a) {
    if (a.placeholder || a.hidden) return false;
    return types::class_of(a.type) == types::AccountClass::Real;
}

inline std::vector<std::string> candidates(const chart::Chart& accounts) {
    std::vector<std::string> out;
    for (const chart::Account& a : accounts.all())
        if (can_be_imported_into(a)) out.push_back(a.path.value());
    return out;
}

// The one candidate the name of the file fits. Empty if none fits, and empty if
// two do: a name that fits two accounts has not named either of them.
inline std::string account_from_file_name(const chart::Chart& accounts,
                                          const std::string& file_name) {
    std::string found;
    for (const chart::Account& a : accounts.all()) {
        if (!can_be_imported_into(a)) continue;
        if (!name_fits(file_name, types::name_of(a.path), a.account_number)) continue;
        if (!found.empty()) return std::string();
        found = a.path.value();
    }
    return found;
}

}  // namespace importing
