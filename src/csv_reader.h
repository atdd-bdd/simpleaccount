#pragma once
#include <string>
#include <vector>

#include "csv_headers.h"

// Reading a delimited file into rows of cells. See the file-structure section of
// ImportCsv.spectable.
//
// Nothing here knows what a column means. This takes a file and gives back the
// headings and the rows; what a heading is, whether a row is a transaction, and
// what the cells amount to are all decided afterwards. Keeping that split is
// what lets a file with an unreadable amount still have its other rows read.
namespace csv {

struct Row {
    std::vector<std::string> cells;
    // Which line of the file this came from, counting from one, so a complaint
    // about a row can say where to look.
    int line = 0;
};

struct Read {
    std::vector<std::string> headings;   // empty where the file has no header row
    std::vector<Row> rows;
    bool refused = false;
    std::string refusal;
};

namespace detail {

inline char delimiter_char(const std::string& name) {
    if (name == "Tab") return '\t';
    if (name == "Semicolon") return ';';
    if (name == "Pipe") return '|';
    return ',';
}

// A byte order mark is three bytes a spreadsheet puts at the front of a UTF-8
// file. Left in place it becomes part of the first heading, which then matches
// nothing -- and the user sees a file whose first column is unrecognised for no
// reason they can see.
inline std::string without_bom(const std::string& text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        return text.substr(3);
    return text;
}

inline std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// Every cell of the file, in order, with the row breaks marked. One pass,
// because a quoted field may hold the delimiter and may hold a line break, so
// neither can be found by splitting.
struct Cell {
    std::string text;
    bool ends_row = false;
};

inline constexpr char NEWLINE = '\n';
inline constexpr char RETURN = '\r';

inline std::vector<Cell> cells_of(const std::string& text, char delimiter) {
    std::vector<Cell> out;
    std::string current;
    bool quoted = false;
    bool any = false;

    const auto end_cell = [&](bool ends_row) {
        out.push_back(Cell{current, ends_row});
        current.clear();
        any = false;
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            // Two quotes inside a quoted field are one quote. Anything else,
            // including the delimiter and a newline, is content.
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    current += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else if (c == NEWLINE || c == RETURN) {
                // The field spans two lines of the file and is still one field.
                // It becomes one line here, because a memo is one line in a
                // register and nothing downstream wants to wonder.
                if (!current.empty() && current.back() != ' ') current += ' ';
            } else {
                current += c;
            }
            any = true;
            continue;
        }
        if (c == '"') {
            quoted = true;
            any = true;
            continue;
        }
        if (c == delimiter) {
            end_cell(false);
            continue;
        }
        if (c == '\n') {
            end_cell(true);
            continue;
        }
        if (c == '\r') continue;        // a line ending written the other way
        current += c;
        any = true;
    }
    if (any || !current.empty() || (!out.empty() && !out.back().ends_row))
        end_cell(true);
    return out;
}

inline std::vector<Row> rows_of(const std::vector<Cell>& cells) {
    std::vector<Row> out;
    Row current;
    int line = 1;
    current.line = line;
    for (const Cell& cell : cells) {
        current.cells.push_back(cell.text);
        if (!cell.ends_row) continue;
        out.push_back(current);
        current = Row();
        current.line = ++line;
    }
    return out;
}

// A row with nothing in it, or nothing but blanks. Exports end with one more
// often than not, and a trailing blank row is not a transaction with no fields.
inline bool is_blank(const Row& row) {
    for (const std::string& cell : row.cells)
        if (!trim(cell).empty()) return false;
    return true;
}

}  // namespace detail

// Reads the file. Nothing about it is guessed: the profile says whether there is
// a header row, how many lines come before it, and what separates the cells,
// because a file whose first row happens to look like headings is still a file
// the user told us about.
//
// Line numbers count every line of the file, blank ones included, so a complaint
// about line 4 means the fourth line of the file as an editor shows it.
inline Read read(const std::string& text, bool has_header = true,
                 const std::string& delimiter = "Comma", int skip_rows = 0) {
    Read out;
    const std::string body = detail::without_bom(text);
    if (detail::trim(body).empty()) {
        out.refused = true;
        out.refusal = "the file is empty";
        return out;
    }

    std::vector<Row> rows =
        detail::rows_of(detail::cells_of(body, detail::delimiter_char(delimiter)));

    std::size_t at = 0;
    // Whatever the profile says comes before the header: an account number, a
    // period, a page of branding.
    while (at < rows.size() && static_cast<int>(at) < skip_rows) ++at;

    if (has_header) {
        while (at < rows.size() && detail::is_blank(rows[at])) ++at;
        if (at == rows.size()) {
            out.refused = true;
            out.refusal = "the file has no header row";
            return out;
        }
        for (const std::string& cell : rows[at].cells)
            out.headings.push_back(detail::trim(cell));
        ++at;
    }

    for (; at < rows.size(); ++at) {
        // A blank line is passed over without comment. Everything else is a row
        // the file is offering, and whether it can be read is decided later --
        // where there is somewhere to report it.
        if (detail::is_blank(rows[at])) continue;
        out.rows.push_back(rows[at]);
    }
    return out;
}

}  // namespace csv
