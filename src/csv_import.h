#pragma once
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "chart.h"
#include "ofx_import.h"
#include "csv_headers.h"
#include "csv_reader.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "posting.h"
#include "transaction_id.h"

// Turning a delimited file into transactions. See ImportCsv.spectable.
//
// Three stages, deliberately separate. The headers are matched to fields, which
// can be done knowing nothing about the rows. The rows are then read into dates,
// amounts and payees, which can be done knowing nothing about the book. Only then
// is anything compared against what the book already holds.
//
// The separation is what lets the program be useful when a file is not perfect:
// a header it cannot place is reported while the rest still match, a row whose
// amount will not parse is reported while the rest still import.
namespace csv {

// How a column came to be matched to a field.
enum class MatchedBy { Alias, Profile, User, Position, None };

inline std::string to_string(MatchedBy by) {
    switch (by) {
        case MatchedBy::Alias:    return "Alias";
        case MatchedBy::Profile:  return "Profile";
        case MatchedBy::User:     return "User";
        case MatchedBy::Position: return "Position";
        case MatchedBy::None:     return "None";
    }
    return "None";
}

inline MatchedBy matched_by_from_string(const std::string& name) {
    if (name == "Alias")    return MatchedBy::Alias;
    if (name == "Profile")  return MatchedBy::Profile;
    if (name == "User")     return MatchedBy::User;
    if (name == "Position") return MatchedBy::Position;
    return MatchedBy::None;
}

struct HeaderMatch {
    int position = 0;                 // counting from one, as a person counts
    std::string source_header;
    Field field = Field::Ignore;
    MatchedBy matched_by = MatchedBy::None;
};

// A field two columns both claim. Not resolved: a file carrying both Amount and
// Transaction Amount is a file worth looking at before importing, and choosing
// the first would be a guess wearing the clothes of a decision.
struct Ambiguity {
    Field field = Field::Date;
    std::vector<int> positions;
    std::vector<std::string> headers;
};

struct Matching {
    std::vector<HeaderMatch> matches;
    std::vector<Ambiguity> ambiguous;
    // Columns nothing matched, with the field each would most likely be. The
    // import can proceed without answering these; it cannot proceed without a
    // required field.
    std::vector<HeaderMatch> unmatched;
    std::vector<Field> missing;
    bool complete = false;
    bool refused = false;
    std::string refusal;

    const HeaderMatch* for_field(Field wanted) const {
        for (const HeaderMatch& m : matches)
            if (m.field == wanted) return &m;
        return nullptr;
    }
};

namespace detail {

// What a column would be if it were anything, for a heading the alias table does
// not know. A suggestion only, offered beside the column so the user can accept
// it with one key rather than reading a list of nine fields.
inline Field suggestion_for(const std::string& heading) {
    const std::string n = normalise_heading(heading);
    // Judged by what the word is like, which is all there is to go on.
    const auto has = [&](const char* part) { return n.find(part) != std::string::npos; };
    if (has("narrat") || has("descr") || has("detail") || has("merchant") ||
        has("name") || has("payee") || has("particular"))
        return Field::Payee;
    if (has("refer") || has("chq") || has("cheque") || has("check") || has("serial"))
        return Field::CheckNo;
    if (has("date") || has("posted")) return Field::Date;
    if (has("amount") || has("value")) return Field::Amount;
    if (has("balance")) return Field::Balance;
    if (has("categ") || has("class")) return Field::Category;
    if (has("memo") || has("note") || has("comment")) return Field::Memo;
    return Field::Ignore;
}

inline std::string list_of(const std::vector<int>& positions) {
    std::string out;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        if (i > 0) out += i + 1 == positions.size() ? " and " : ", ";
        out += std::to_string(positions[i]);
    }
    return out;
}

inline std::string joined(const std::vector<std::string>& parts) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += ", ";
        out += parts[i];
    }
    return out;
}

}  // namespace detail

// Which fields a file must supply. Date and Payee always; an amount in one shape
// or the other. Everything else is optional, because a register can show a
// transaction without a memo and cannot show one without a date.
inline bool is_required(Field field, bool amount_matched, bool debit_and_credit) {
    switch (field) {
        case Field::Date:
        case Field::Payee:
            return true;
        case Field::Amount:
            return !debit_and_credit;
        case Field::Debit:
        case Field::Credit:
            return !amount_matched;
        default:
            return false;
    }
}

// Matching the headings of a file to the fields the importer needs.
//
// A profile's column names win over the alias table: the user has already said
// what this bank's columns mean, and a later release adding an alias must not
// change what an existing profile does.
// A column named by its number rather than its heading, which is how a file with
// no header row is described. "3" means the third column.
inline std::optional<int> position_named(const std::string& column) {
    if (column.empty()) return std::nullopt;
    for (const char c : column)
        if (c < '0' || c > '9') return std::nullopt;
    return std::stoi(column);
}

inline Matching match_headers(const std::vector<std::string>& headings,
                              const std::map<std::string, Field>& from_profile = {}) {
    Matching out;
    std::map<Field, std::vector<int>> claimed;

    for (std::size_t i = 0; i < headings.size(); ++i) {
        HeaderMatch match;
        match.position = static_cast<int>(i) + 1;
        match.source_header = headings[i];

        const std::string normalised = normalise_heading(headings[i]);
        const auto in_profile = from_profile.find(normalised);
        if (in_profile != from_profile.end()) {
            match.field = in_profile->second;
            match.matched_by = MatchedBy::Profile;
        } else if (const auto by_alias = field_for_heading(headings[i])) {
            match.field = *by_alias;
            match.matched_by = MatchedBy::Alias;
        } else {
            match.field = Field::Ignore;
            match.matched_by = MatchedBy::None;
        }
        out.matches.push_back(match);

        if (match.matched_by == MatchedBy::None) {
            // Nothing matched it, so it is a question: offered with the field it
            // would most likely be, which the user can take or leave.
            HeaderMatch asked = match;
            asked.field = detail::suggestion_for(headings[i]);
            out.unmatched.push_back(asked);
        } else if (match.field != Field::Ignore) {
            claimed[match.field].push_back(match.position);
        }
    }

    for (const auto& entry : claimed) {
        if (entry.second.size() < 2) continue;
        Ambiguity one;
        one.field = entry.first;
        one.positions = entry.second;
        for (const int position : entry.second)
            one.headers.push_back(headings[static_cast<std::size_t>(position) - 1]);
        out.ambiguous.push_back(one);
    }

    const bool amount = claimed.count(Field::Amount) == 1;
    const bool debit_and_credit =
        claimed.count(Field::Debit) == 1 && claimed.count(Field::Credit) == 1;
    static const Field every[] = {Field::Date, Field::Payee, Field::Amount,
                                  Field::Debit, Field::Credit};
    for (const Field field : every) {
        if (!is_required(field, amount, debit_and_credit)) continue;
        if (claimed.count(field) == 1) continue;
        out.missing.push_back(field);
    }

    // A missing Date or Payee cannot be worked around: refused outright, saying
    // which field and that it is required, because the user's next move is to
    // look at the file rather than at the program.
    for (const Field field : out.missing) {
        if (field != Field::Date && field != Field::Payee) continue;
        if (!out.unmatched.empty()) break;   // there is a column to map instead
        out.refused = true;
        out.refusal = "No column matched " + to_string(field) + ", which is required";
        break;
    }

    out.complete = out.missing.empty() && out.ambiguous.empty() && out.unmatched.empty();
    return out;
}

// --- dates ------------------------------------------------------------------

// Years 70 to 99 are nineteen hundreds and 00 to 69 are two thousands. A file old
// enough to write a two-digit year will not contain 2070.
inline int year_from_two_digits(int written) {
    return written >= 70 ? 1900 + written : 2000 + written;
}

// The profile says which order the parts are in. The separator and the width of
// the year are taken as they come, because they vary within one bank's own
// exports and nothing turns on them.
inline std::optional<types::Date> date_of(const std::string& text,
                                          types::DateOrder order) {
    std::vector<int> parts;
    std::string current;
    for (const char c : text + "/") {
        if (c >= '0' && c <= '9') {
            current += c;
            continue;
        }
        if (!current.empty()) {
            parts.push_back(std::stoi(current));
            if (current.size() == 2 && parts.size() == 3 &&
                order != types::DateOrder::YMD)
                parts.back() = year_from_two_digits(parts.back());
            if (current.size() == 2 && parts.size() == 1 &&
                order == types::DateOrder::YMD)
                parts.back() = year_from_two_digits(parts.back());
            current.clear();
        }
    }
    if (parts.size() != 3) return std::nullopt;

    int year = 0;
    int month = 0;
    int day = 0;
    switch (order) {
        case types::DateOrder::MDY: month = parts[0]; day = parts[1]; year = parts[2]; break;
        case types::DateOrder::DMY: day = parts[0]; month = parts[1]; year = parts[2]; break;
        case types::DateOrder::YMD: year = parts[0]; month = parts[1]; day = parts[2]; break;
    }
    if (!types::Date::is_valid(year, month, day)) return std::nullopt;
    return types::Date(year, month, day);
}

// Whether the dates in a file could be read either way round. True only when
// every date works both ways and at least one means something different -- a
// single date of 13/04 settles the question for the whole file, which is why one
// unambiguous date is enough to stop asking.
struct DateAmbiguity {
    bool ambiguous = false;
    types::DateOrder reading = types::DateOrder::MDY;
    types::DateOrder other = types::DateOrder::DMY;
    int rows = 0;
};

inline DateAmbiguity date_ambiguity(const std::vector<std::string>& dates,
                                    types::DateOrder stated) {
    DateAmbiguity out;
    out.reading = stated;
    out.other = stated == types::DateOrder::DMY ? types::DateOrder::MDY
                                                : types::DateOrder::DMY;
    if (stated == types::DateOrder::YMD) return out;   // a year first is not in doubt

    bool any_differ = false;
    for (const std::string& text : dates) {
        const auto one = date_of(text, out.reading);
        const auto two = date_of(text, out.other);
        if (!one.has_value()) return out;          // the stated order is wrong, not ambiguous
        if (!two.has_value()) return out;          // only one reading works: settled
        if (one != two) any_differ = true;
        ++out.rows;
    }
    out.ambiguous = any_differ && !dates.empty();
    return out;
}

// --- profiles ---------------------------------------------------------------

struct Profile {
    std::string name;
    bool has_header = true;
    // Lines before the header row. Stated by the profile rather than guessed:
    // several banks put an account number and a period above the headings, and
    // how many lines that takes is a fact about the bank.
    int skip_rows = 0;
    std::string delimiter = "Comma";
    std::string date_column;
    types::DateOrder date_order = types::DateOrder::MDY;
    std::string payee_column;
    std::string memo_column;
    std::string check_column;
    std::string category_column;
    // Not used to make a transaction: the balance is a check on the import. Named
    // here so that a profile recognises the whole of its file.
    std::string balance_column;
    AmountStyle amount_style = AmountStyle::Signed;
    std::string amount_column;
    std::string debit_column;
    std::string credit_column;
    OutwardSign outward_sign = OutwardSign::Negative;

    // The headings this profile expects, normalised, as a set. A file is
    // recognised by carrying the same set -- the same headings in a different
    // order are the same bank, since a column order is a spreadsheet's business
    // and not an institution's.
    std::set<std::string> signature() const {
        std::set<std::string> out;
        for (const std::string* column : {&date_column, &payee_column, &memo_column,
                                          &check_column, &category_column,
                                          &balance_column, &amount_column,
                                          &debit_column, &credit_column})
            if (!column->empty() && *column != "none")
                out.insert(normalise_heading(*column));
        return out;
    }
};

// Which saved profile this file is, if any. By the headings it carries, so a
// bank already set up costs the user nothing on every later file.
inline const Profile* profile_for(const std::vector<Profile>& saved,
                                  const std::vector<std::string>& headings) {
    std::set<std::string> theirs;
    for (const std::string& heading : headings)
        theirs.insert(normalise_heading(heading));
    for (const Profile& one : saved) {
        const std::set<std::string> wanted = one.signature();
        if (wanted.empty()) continue;
        if (std::includes(theirs.begin(), theirs.end(), wanted.begin(), wanted.end()))
            return &one;
    }
    return nullptr;
}

inline Matching match_positions(std::size_t columns, const Profile& profile) {
    Matching out;
    const std::pair<const std::string*, Field> named[] = {
        {&profile.date_column, Field::Date},
        {&profile.payee_column, Field::Payee},
        {&profile.memo_column, Field::Memo},
        {&profile.check_column, Field::CheckNo},
        {&profile.category_column, Field::Category},
        {&profile.amount_column, Field::Amount},
        {&profile.debit_column, Field::Debit},
        {&profile.credit_column, Field::Credit},
    };
    for (std::size_t i = 0; i < columns; ++i) {
        HeaderMatch match;
        match.position = static_cast<int>(i) + 1;
        for (const auto& one : named) {
            const auto where = position_named(*one.first);
            if (where.has_value() && *where == match.position) {
                match.field = one.second;
                match.matched_by = MatchedBy::Position;
                break;
            }
        }
        out.matches.push_back(match);
    }
    out.complete = out.for_field(Field::Date) != nullptr &&
                   out.for_field(Field::Payee) != nullptr;
    return out;
}

// The columns a profile names, as the matcher wants them.
inline std::map<std::string, Field> columns_of(const Profile& profile) {
    std::map<std::string, Field> out;
    const auto put = [&](const std::string& column, Field field) {
        if (column.empty() || column == "none") return;
        out[normalise_heading(column)] = field;
    };
    put(profile.date_column, Field::Date);
    put(profile.payee_column, Field::Payee);
    put(profile.memo_column, Field::Memo);
    put(profile.check_column, Field::CheckNo);
    put(profile.category_column, Field::Category);
    put(profile.balance_column, Field::Balance);
    put(profile.amount_column, Field::Amount);
    put(profile.debit_column, Field::Debit);
    put(profile.credit_column, Field::Credit);
    return out;
}

// --- rows -------------------------------------------------------------------

// One row of the file, read into the things a transaction is made of. The
// identifier is the row's position in the file, which is the only name a CSV
// gives a row -- and why a CSV identifier is not an OFX one.
struct RowRead {
    types::Date date{2024, 1, 1};
    std::string payee;
    Money amount;
    std::string memo;
    std::string check_no;
    std::string category;
    Money balance;
    bool has_balance = false;
    int line = 0;
};

struct RowError {
    int line = 0;
    std::string reason;
};

struct Rows {
    std::vector<RowRead> rows;
    // A row that could not be read. Reported and skipped rather than refusing
    // the file: nineteen good rows should not wait for one bad one.
    std::vector<RowError> rejected;
};

inline Rows rows_of(const Read& file, const Matching& matching,
                    types::DateOrder order, AmountStyle style, OutwardSign outward) {
    Rows out;
    const auto cell = [&](const csv::Row& row, Field field) -> std::string {
        const HeaderMatch* match = matching.for_field(field);
        if (match == nullptr) return {};
        const auto at = static_cast<std::size_t>(match->position) - 1;
        return at < row.cells.size() ? csv::detail::trim(row.cells[at]) : std::string();
    };

    for (const csv::Row& row : file.rows) {
        RowRead read;
        read.line = row.line;

        const std::string date_cell = cell(row, Field::Date);
        const auto on = date_of(date_cell, order);
        if (!on.has_value()) {
            // Named for the column rather than the cell, because the user's
            // next move is to look at that column of that line.
            out.rejected.push_back({row.line, date_cell.empty()
                                                  ? "No date in the Date column"
                                                  : date_cell + " is not a date in " +
                                                        types::to_string(order) +
                                                        " order; the profile may be wrong"});
            continue;
        }
        read.date = *on;
        read.payee = cell(row, Field::Payee);
        read.memo = cell(row, Field::Memo);
        read.check_no = cell(row, Field::CheckNo);
        // A category in square brackets names an account rather than a
        // category, which is how Quicken and the files it exports write a
        // transfer. The brackets are notation and not part of the name.
        read.category = cell(row, Field::Category);
        if (read.category.size() >= 2 && read.category.front() == '[' &&
            read.category.back() == ']')
            read.category = read.category.substr(1, read.category.size() - 2);

        const std::string debit = cell(row, Field::Debit);
        const std::string credit = cell(row, Field::Credit);
        if (style == AmountStyle::DebitCredit && !debit.empty() && !credit.empty()) {
            // Both filled in is not a row anybody meant: it says money left and
            // arrived at once, and guessing which was intended would put a wrong
            // figure in a register that is supposed to be checkable.
            const HeaderMatch* debit_column = matching.for_field(Field::Debit);
            const HeaderMatch* credit_column = matching.for_field(Field::Credit);
            out.rejected.push_back(
                {row.line,
                 "Both " + (debit_column == nullptr ? std::string("the debit column")
                                                    : debit_column->source_header) +
                     " and " +
                     (credit_column == nullptr ? std::string("the credit column")
                                               : credit_column->source_header) +
                     " are filled in"});
            continue;
        }
        try {
            read.amount = amount_of_row(style, outward, cell(row, Field::Amount),
                                        debit, credit);
        } catch (const std::exception&) {
            out.rejected.push_back({row.line, "the amount cannot be read"});
            continue;
        }

        const std::string balance = cell(row, Field::Balance);
        if (!balance.empty()) {
            try {
                read.balance = Money(balance);
                read.has_balance = true;
            } catch (const std::exception&) {
                // A balance that will not parse is not worth refusing a row for:
                // it is a check on the import, not part of the transaction.
            }
        }
        out.rows.push_back(read);
    }
    return out;
}

// --- what the book already holds --------------------------------------------

// The tests, in order, and the first that applies decides. Weaker than the OFX
// tests and deliberately so: a CSV row carries no identifier its bank promises
// to repeat, so nothing here can say "this is certainly the row I saw before".
// A row that looks like something already present is offered, not skipped.
//
// That is why re-reading the same file offers every row again rather than
// reporting duplicates. It reads like a failing but it is the honest answer:
// two visits to one shop on one day for one amount are not rare, and a program
// that silently discarded the second would be wrong in a way nobody would
// notice until the balance did not match.
struct Decided {
    RowRead row;
    ofx::Disposition disposition = ofx::Disposition::New;
    std::string claimed_id;
    std::string claimed_ref;
};

struct Imported {
    std::vector<Decided> decided;
    ofx::Summary summary;
};

inline Imported decide(const std::vector<RowRead>& rows, const std::string& account,
                       const std::vector<ledger::Transaction>& transactions) {
    Imported out;
    // Claim pairing, shared with the OFX import rather than written twice: a
    // transaction already in the book may be claimed once, so two identical rows
    // pair off against two identical transactions and not against one of them
    // twice. See the claim rule in ImportCsv.spectable.
    std::vector<ofx::detail::Existing> already = ofx::existing_in(transactions, account);
    const auto unclaimed = [](const ofx::detail::Existing& e) { return !e.claimed; };

    for (const RowRead& row : rows) {
        Decided decided;
        decided.row = row;

        // 1. Something with the same fingerprint. First, because it is the
        // strongest thing a file without identifiers offers -- and still only
        // enough to ask.
        const std::string print = ofx::detail::fingerprint(row.date, row.amount, row.payee);
        auto found = std::find_if(already.begin(), already.end(),
            [&](const ofx::detail::Existing& e) {
                return unclaimed(e) &&
                       ofx::detail::fingerprint(e.date, e.amount, e.payee) == print;
            });
        if (found != already.end()) {
            found->claimed = true;
            decided.disposition = ofx::Disposition::Possible;
            decided.claimed_id = found->id;
            decided.claimed_ref = found->ref;
            ++out.summary.Possible;
            out.decided.push_back(decided);
            continue;
        }

        // 2. An uncleared posting with the same amount and check number: the
        // cheque written by hand, now reported by the bank.
        found = already.end();
        if (!row.check_no.empty()) {
            found = std::find_if(already.begin(), already.end(),
                [&](const ofx::detail::Existing& e) {
                    return unclaimed(e) && e.cleared == types::ClearedStatus::Uncleared &&
                           e.amount == row.amount && !e.check_no.empty() &&
                           e.check_no == row.check_no;
                });
        }
        // 3. An uncleared posting with the same amount within five days.
        if (found == already.end()) {
            found = std::find_if(already.begin(), already.end(),
                [&](const ofx::detail::Existing& e) {
                    return unclaimed(e) && e.cleared == types::ClearedStatus::Uncleared &&
                           e.amount == row.amount &&
                           std::abs(types::Date::days_between(e.date, row.date))
                               <= ofx::kMatchWindow;
                });
        }
        if (found != already.end()) {
            found->claimed = true;
            decided.disposition = ofx::Disposition::Matched;
            decided.claimed_id = found->id;
            decided.claimed_ref = found->ref;
            ++out.summary.Matched;
            out.decided.push_back(decided);
            continue;
        }

        // 4. Nothing above applies.
        decided.disposition = ofx::Disposition::New;
        ++out.summary.New;
        out.decided.push_back(decided);
    }
    return out;
}

// The transactions to add for what was decided New.
//
// A category the file carried is used as it stands: a file that says Auto:Fuel
// has said more than the importer could work out, and a file naming a real
// account has described a transfer. Where it says nothing, the other side is
// Uncategorized chosen by the sign.
inline std::vector<ledger::Transaction> transactions_for(
        const Imported& decided, const std::string& account,
        std::size_t already_in_book, chart::Chart* accounts) {
    std::vector<ledger::Transaction> out;
    for (const Decided& one : decided.decided) {
        if (one.disposition != ofx::Disposition::New) continue;
        const Money amount = one.row.amount;

        std::string other = one.row.category;
        if (other.empty() || other == "none") {
            other = ledger::uncategorized_for(amount).value();
            accounts->add(types::AccountPath(other),
                          amount.cents() < 0 ? types::AccountType::Expense
                                             : types::AccountType::Income);
        } else if (accounts->find(other) == nullptr) {
            // A category named by the file and not in the chart is created as a
            // category. A real account it names is already there, which is what
            // makes the row a transfer rather than a category.
            accounts->add(types::AccountPath(other),
                          amount.cents() < 0 ? types::AccountType::Expense
                                             : types::AccountType::Income);
        }

        ledger::Transaction t;
        t.id = ledger::new_id();
        t.ref = types::TransactionRef(
            "T" + std::to_string(already_in_book + out.size() + 1));
        t.date = one.row.date;
        t.payee = types::PayeeName(one.row.payee);
        t.check_no = types::CheckNumber(one.row.check_no);
        t.memo = one.row.memo;

        ledger::Posting here;
        here.account = types::AccountPath(account);
        here.amount = amount;
        // Left uncleared. A CSV file carries no statement and says nothing about
        // what the bank has settled, so nothing here can honestly say it has --
        // unlike an OFX file, whose rows are a statement and are cleared.
        // Named for the kind of file that gave it and by the line it came from,
        // which is the only name a CSV row has. It is not an OFX identifier and
        // is never compared with one: see the identifier-scope rule in ImportOfx.
        ledger::stamp(&here, types::ImportSource::Csv, std::to_string(one.row.line));

        ledger::Posting there;
        there.account = types::AccountPath(other);
        there.amount = -amount;
        t.postings = {here, there};
        out.push_back(t);
    }
    return out;
}

// What the balance column says the account should be after each row, against
// what the import makes it. A check on the import rather than part of it: the
// column is not stored, because a stored balance can disagree with the postings
// it was supposed to describe, and then there are two answers and no way to tell
// which is wrong.
//
// Checked at every row that carries one, not only the last. A single figure at
// the end says the file and the book disagree; a figure per row says where they
// started to, which is the difference between a search and a glance.
struct BalanceAtRow {
    int line = 0;
    Money row_balance;
    Money computed;
    bool agrees = false;
};

inline std::vector<BalanceAtRow> balance_checks(const std::vector<RowRead>& rows,
                                               const ledger::Ledger& book,
                                               const std::string& account) {
    std::vector<BalanceAtRow> out;
    // Where the account stood before the file: everything already in the book up
    // to the day before the first row. The file's own rows are then added in the
    // order the file gives them, because that is the order its balances follow.
    Money running;
    bool started = false;
    for (const RowRead& row : rows) {
        if (!started) {
            running = book.balance_as_at(account,
                                        types::Date::from_day_number(
                                            types::Date::day_number(row.date) - 1));
            started = true;
        }
        running += row.amount;
        if (!row.has_balance) continue;
        BalanceAtRow one;
        one.line = row.line;
        one.row_balance = row.balance;
        one.computed = running;
        one.agrees = one.row_balance == one.computed;
        out.push_back(one);
    }
    return out;
}

}  // namespace csv
