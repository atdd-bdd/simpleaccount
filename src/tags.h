#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace tags {

// A tag is a single word, and the field on a transaction holds words: every
// word in it is a tag. So splitting the field is reading rather than storage,
// which is why there is nothing here but functions over strings -- the field
// stays one Text column on the transaction. See the tag rules in
// UserInterface.spectable and the tag table rule in Books.spectable.
inline std::vector<std::string> words_of(const std::string& field) {
    std::vector<std::string> out;
    std::string word;
    for (char c : field) {
        if (c == ' ' || c == '\t') {
            if (!word.empty()) out.push_back(word);
            word.clear();
            continue;
        }
        word += c;
    }
    if (!word.empty()) out.push_back(word);
    return out;
}

inline std::string joined(const std::vector<std::string>& words) {
    std::string out;
    for (const std::string& word : words) {
        if (!out.empty()) out += " ";
        out += word;
    }
    return out;
}

// Case is ignored wherever two tags are compared, the way the category column
// ignores it: Rental typed as rental is the same word.
inline std::string fold(const std::string& s) {
    std::string out;
    for (char c : s) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

inline bool same_tag(const std::string& a, const std::string& b) {
    return fold(a) == fold(b);
}

// One line of the listing: the word, and how many transactions carry it.
struct Row {
    std::string name;
    long long used = 0;
};

// The vocabulary and the use of it. A word is in the listing because it is in
// the table or because something carries it; a tag nothing carries is still a
// row, because the list is the author's vocabulary and a year with no rental
// transactions in it is not a reason to forget the word.
inline std::vector<Row> listing(const std::vector<std::string>& vocabulary,
                                const std::vector<std::string>& fields) {
    std::vector<Row> out;
    const auto find_row = [&out](const std::string& name) -> Row* {
        for (Row& row : out)
            if (same_tag(row.name, name)) return &row;
        return nullptr;
    };
    for (const std::string& name : vocabulary)
        if (!name.empty() && find_row(name) == nullptr) out.push_back({name, 0});
    for (const std::string& field : fields)
        for (const std::string& word : words_of(field)) {
            Row* row = find_row(word);
            if (row == nullptr) {
                out.push_back({word, 1});
                continue;
            }
            ++row->used;
        }
    std::sort(out.begin(), out.end(), [](const Row& x, const Row& y) {
        if (fold(x.name) != fold(y.name)) return fold(x.name) < fold(y.name);
        return x.name < y.name;
    });
    return out;
}

// Renaming replaces the word and leaves the other words in the field alone: a
// rename of a tag is a rename of every use of it, which is the behaviour
// wanted and the reason a transaction keeps the name rather than an id.
inline std::string renamed_field(const std::string& field, const std::string& was,
                                 const std::string& now) {
    std::vector<std::string> words = words_of(field);
    for (std::string& word : words)
        if (same_tag(word, was)) word = now;
    return joined(words);
}

inline std::vector<std::string> renamed(const std::vector<std::string>& vocabulary,
                                        const std::string& was, const std::string& now) {
    std::vector<std::string> out;
    for (const std::string& name : vocabulary)
        out.push_back(same_tag(name, was) ? now : name);
    return out;
}

// The word being completed is the one after the last space, so a second tag
// can be completed once a first has been accepted.
inline std::string word_being_typed(const std::string& typed) {
    const std::vector<std::string> words = words_of(typed);
    return words.empty() ? std::string() : words.back();
}

// What the column offers against what has been typed. The first character is
// enough to bring the list up; nothing is offered for an empty column, and
// nothing offers to complete a tag to the empty string. A tag that is a prefix
// of another can still be typed in full, because the offer is only an offer
// and typing on past it replaces it.
inline std::string offer(const std::vector<std::string>& vocabulary,
                         const std::string& typed) {
    const std::string word = word_being_typed(typed);
    if (word.empty()) return std::string();
    std::vector<std::string> matches;
    for (const std::string& name : vocabulary)
        if (fold(name).rfind(fold(word), 0) == 0) matches.push_back(name);
    if (matches.empty()) return std::string();
    std::sort(matches.begin(), matches.end(),
              [](const std::string& x, const std::string& y) { return fold(x) < fold(y); });
    return matches.front();
}

// Accepting what was typed. A word that matches no tag is added: this is the
// one place a tag is created, which is why the listing needs no New button for
// the common case. Clearing the column adds nothing -- a tag with no name is
// not a tag.
inline std::vector<std::string> accepted(const std::vector<std::string>& vocabulary,
                                         const std::string& typed) {
    std::vector<std::string> out = vocabulary;
    for (const std::string& word : words_of(typed)) {
        bool known = false;
        for (const std::string& name : out)
            if (same_tag(name, word)) known = true;
        if (!known) out.push_back(word);
    }
    return out;
}

}  // namespace tags
