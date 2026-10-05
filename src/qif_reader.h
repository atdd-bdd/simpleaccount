#pragma once
#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "money.h"
#include "qif_lexer.h"

// Reading a QIF file into records, with the account each one belongs to.
//
// The reader does two things nothing downstream should have to think about: it
// tracks which section it is in, because the same field letter means a different
// field in a different one, and it tracks the current account, because a
// whole-file export names it with an !Account block between sections.
//
// See ImportQif.spectable.
namespace qif {

// One record: the fields it carried, in the order they appeared, plus where it
// came from. Fields repeat -- S, E and the dollar sign repeat once per split
// line -- so each letter holds a list.
struct Record {
    Section section = Section::Unknown;
    std::string account;          // the account the record belongs to
    std::map<char, std::vector<std::string>> fields;
    int line = 0;                 // where it started, for a message

    bool has(char code) const { return fields.count(code) != 0; }

    std::string first(char code) const {
        const auto at = fields.find(code);
        return at == fields.end() || at->second.empty() ? std::string() : at->second.front();
    }

    const std::vector<std::string>& all(char code) const {
        static const std::vector<std::string> none;
        const auto at = fields.find(code);
        return at == fields.end() ? none : at->second;
    }
};

// An account as the account list declares it.
struct AccountRecord {
    std::string name;
    std::string qif_type;
    std::string description;
    std::optional<Money> credit_limit;
};

// A category as the category list declares it, with the flag that says which
// side of a report it belongs on. Read before any transaction, because it is the
// only thing in the file that says whether Bonus is income or an expense.
struct CategoryRecord {
    std::string name;
    bool is_income = false;
    bool is_expense = false;
};

struct File {
    std::vector<Record> records;             // transaction records, in file order
    std::vector<AccountRecord> accounts;     // from the account list
    std::vector<CategoryRecord> categories;  // from the category list
    std::vector<std::string> tags;
    std::vector<std::string> skipped_sections;
};

// A QIF file is one byte sequence and the two real exports are encoded
// differently -- one is valid UTF-8, the other has a lone 0x96 which is an en
// dash in Windows-1252 and illegal in UTF-8. So the encoding is decided by
// trying, not by asking.
inline std::string decode(const std::string& bytes) {
    // Valid UTF-8 is left alone; anything else is read as Windows-1252, which
    // for the bytes that matter here means mapping each one to its code point.
    std::size_t i = 0;
    bool utf8 = true;
    while (i < bytes.size() && utf8) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        int extra = 0;
        if (c < 0x80) extra = 0;
        else if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else { utf8 = false; break; }
        if (i + extra >= bytes.size()) { utf8 = false; break; }
        for (int k = 1; k <= extra; ++k)
            if ((static_cast<unsigned char>(bytes[i + k]) & 0xC0) != 0x80) { utf8 = false; break; }
        i += extra + 1;
    }
    if (utf8) {
        // A byte order mark is not part of the first field's name.
        if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
            static_cast<unsigned char>(bytes[1]) == 0xBB &&
            static_cast<unsigned char>(bytes[2]) == 0xBF)
            return bytes.substr(3);
        return bytes;
    }
    std::string out;
    out.reserve(bytes.size());
    for (char ch : bytes) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x80) { out += static_cast<char>(c); continue; }
        // Two UTF-8 bytes for anything in the upper half, which is enough for
        // Windows-1252's letters and punctuation.
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

// Whether an !Account block declares an account or switches to one. A whole-file
// export turns AutoSwitch on, lists every account, turns it off, and then
// alternates !Account with a transaction section once per account. Reading the
// second half as declarations would lose the account context and put every
// transaction in the first account.
inline File read(const std::string& text, const std::string& default_account = {}) {
    File file;
    Section section = Section::Unknown;
    bool auto_switch = false;
    std::string current_account = default_account;

    std::map<char, std::vector<std::string>> fields;
    int record_line = 0;
    int line_no = 0;
    char last_code = 0;

    const auto flush = [&]() {
        if (fields.empty()) return;
        switch (section) {
            case Section::AccountList: {
                AccountRecord a;
                const auto name = fields.find('N');
                const auto type = fields.find('T');
                const auto desc = fields.find('D');
                const auto limit = fields.find('L');
                if (name != fields.end() && !name->second.empty()) a.name = name->second.front();
                if (type != fields.end() && !type->second.empty()) a.qif_type = type->second.front();
                if (desc != fields.end() && !desc->second.empty()) a.description = desc->second.front();
                if (limit != fields.end() && !limit->second.empty()) {
                    const auto m = parse_amount(limit->second.front(), false);
                    if (m) a.credit_limit = *m;
                }
                if (auto_switch) file.accounts.push_back(a);
                // With AutoSwitch off the block is a change of account, and the
                // account may not have been declared, so it is recorded either way.
                if (!a.name.empty()) {
                    current_account = a.name;
                    const bool known = std::any_of(
                        file.accounts.begin(), file.accounts.end(),
                        [&](const AccountRecord& x) { return x.name == a.name; });
                    if (!known) file.accounts.push_back(a);
                }
                break;
            }
            case Section::CategoryList: {
                CategoryRecord c;
                const auto name = fields.find('N');
                if (name != fields.end() && !name->second.empty()) c.name = name->second.front();
                c.is_income = fields.count('I') != 0;
                c.is_expense = fields.count('E') != 0;
                if (!c.name.empty()) file.categories.push_back(c);
                break;
            }
            case Section::TagList: {
                const auto name = fields.find('N');
                if (name != fields.end() && !name->second.empty())
                    file.tags.push_back(name->second.front());
                break;
            }
            case Section::BankTxns:
            case Section::CashTxns:
            case Section::CreditCardTxns:
            case Section::AssetTxns:
            case Section::LiabilityTxns:
            case Section::InvestmentTxns:
            case Section::InvoiceTxns:
            case Section::BillTxns:
            case Section::TaxTxns: {
                Record r;
                r.section = section;
                r.account = current_account;
                r.fields = fields;
                r.line = record_line;
                file.records.push_back(r);
                break;
            }
            default:
                break;  // memorized, templates, invoice items: not imported here
        }
        fields.clear();
        last_code = 0;
    };

    std::string line;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t nl = text.find('\n', at);
        line = text.substr(at, nl == std::string::npos ? nl : nl - at);
        at = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        ++line_no;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (line[0] == '!') {
            flush();
            const Section s = section_of(line);
            if (s == Section::AutoSwitchOn) { auto_switch = true; continue; }
            if (s == Section::AutoSwitchOff) { auto_switch = false; continue; }
            if (s == Section::Unknown) file.skipped_sections.push_back(line);
            section = s;
            continue;
        }
        if (line[0] == '^') { flush(); continue; }

        const char code = line[0];
        const std::string value = line.substr(1);
        if (fields.empty()) record_line = line_no;

        // A value containing a newline becomes a line beginning with whatever
        // follows it, and QIF has no way to quote one. A D that will not parse as
        // a date is such a continuation, not a date. Seen only in the invoice
        // sections of the real files.
        const bool continuation =
            (section == Section::InvoiceTxns || section == Section::BillTxns) &&
            code == 'D' && last_code != 0 && !looks_like_a_date(value);
        if (continuation) {
            fields[last_code].back() += " " + line;
            continue;
        }

        fields[code].push_back(value);
        last_code = code;
    }
    flush();
    return file;
}

}  // namespace qif
