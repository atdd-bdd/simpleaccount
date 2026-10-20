#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include "date.h"

namespace ui {

// One file the import dialog lists, and when it was last written. The dialog
// shows more than this; these are the two things the order and the filtering
// are about. See the import-dialog section of UserInterface.spectable.
struct FileOffered {
    std::string name;
    types::Date modified;
};

namespace detail {
inline std::string lowered(const std::string& text) {
    std::string out;
    for (char c : text) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
}  // namespace detail

// Whether a file is one this program reads. A book, a spreadsheet and a PDF
// statement live in the same folder as the downloads, and none of them is
// something an import can do anything with.
inline bool is_import_file(const std::string& name) {
    const std::string lower = detail::lowered(name);
    const char* kinds[] = {".csv", ".qfx", ".ofx", ".qif"};
    for (const char* kind : kinds) {
        const std::string ending(kind);
        if (lower.size() > ending.size() &&
            lower.compare(lower.size() - ending.size(), ending.size(), ending) == 0)
            return true;
    }
    return false;
}

// What the dialog offers, newest first: the file wanted is almost always the
// one just downloaded, and alphabetical order puts it wherever its bank's name
// falls. Files of any other kind are left out.
inline std::vector<FileOffered> files_offered(const std::vector<FileOffered>& found) {
    std::vector<FileOffered> out;
    for (const FileOffered& one : found)
        if (is_import_file(one.name)) out.push_back(one);
    std::stable_sort(out.begin(), out.end(),
                     [](const FileOffered& a, const FileOffered& b) {
                         return b.modified < a.modified;
                     });
    return out;
}

}  // namespace ui
