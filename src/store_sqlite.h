#pragma once
#include <sqlite3.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <system_error>
#include <string>
#include <vector>

#include "account_type.h"
#include "chart.h"
#include "date.h"
#include "ledger.h"
#include "money.h"
#include "payee_rules.h"
#include "posting.h"
#include "text_types.h"

// A book on disk. One book is one SQLite database with a .sadb name, and they
// all live in one folder; see the storage rules in Books.spectable.
//
// No Qt here on purpose. The schema is the thing most worth testing, so it has
// to be reachable from the headless spec suite and from the command line, not
// only from the window.
namespace store {

// The schema is read off the specification rather than designed beside it: every
// Entity becomes a row and the Collection it belongs to becomes the table. Bump
// this when that shape changes.
inline constexpr int kSchemaVersion = 1;

struct Failure {
    bool refused = false;
    std::string reason;
};

// What a book holds about itself. There is no table of books anywhere: the
// folder is the collection, so this is one row.
struct BookInfo {
    std::string name;
    int fiscal_year_start = 1;
    // One day unless it is changed: in a book whose checks are never entered
    // before the bank reports them nothing is outstanding, so a number that
    // matches anything older is more likely a number reused.
    int check_window_days = 1;
};

namespace detail {

// sqlite3_close is safe on null, so one deleter covers every path out.
struct Close {
    void operator()(sqlite3* db) const noexcept { sqlite3_close(db); }
};
using Handle = std::unique_ptr<sqlite3, Close>;

inline std::string message(sqlite3* db) {
    const char* text = sqlite3_errmsg(db);
    return text != nullptr ? text : "the database reported no reason";
}

// Every statement goes through here, so a failure cannot be read as a success
// anywhere. Nothing in this file builds SQL by concatenating a value.
class Statement {
public:
    Statement(sqlite3* db, const std::string& sql) : db_(db) {
        ok_ = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) == SQLITE_OK;
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    bool ok() const { return ok_; }
    std::string why() const { return message(db_); }

    void bind(int at, const std::string& value) {
        sqlite3_bind_text(stmt_, at, value.c_str(), -1, SQLITE_TRANSIENT);
    }
    void bind(int at, long long value) { sqlite3_bind_int64(stmt_, at, value); }
    void bind(int at, int value) { sqlite3_bind_int(stmt_, at, value); }

    // Between uses of a prepared statement, so one INSERT serves every row.
    void reset() { sqlite3_reset(stmt_); }

    // True while there is a row; false when there are no more.
    bool step_row() { return sqlite3_step(stmt_) == SQLITE_ROW; }
    // For a statement that returns nothing.
    bool run() { return sqlite3_step(stmt_) == SQLITE_DONE; }

    std::string text(int column) const {
        const unsigned char* value = sqlite3_column_text(stmt_, column);
        return value != nullptr ? reinterpret_cast<const char*>(value) : "";
    }
    long long integer(int column) const { return sqlite3_column_int64(stmt_, column); }

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
    bool ok_ = false;
};

inline bool exec(sqlite3* db, const std::string& sql, std::string* why) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error) == SQLITE_OK) return true;
    if (why != nullptr) *why = error != nullptr ? error : message(db);
    sqlite3_free(error);
    return false;
}

// Amounts are whole cents in an integer column. A binary floating-point column
// cannot hold 0.10, and a book that cannot add up is the one thing this program
// may not be.
inline const char* kSchema = R"sql(
CREATE TABLE schema (version INTEGER NOT NULL);

CREATE TABLE book (
  name              TEXT    NOT NULL,
  fiscal_year_start INTEGER NOT NULL DEFAULT 1,
  -- How long a check number is trusted to settle a match. See the
  -- check-number rule in ImportOfx.spectable.
  check_window_days INTEGER NOT NULL DEFAULT 1);

CREATE TABLE accounts (
  path        TEXT    NOT NULL PRIMARY KEY,
  type        TEXT    NOT NULL,
  placeholder INTEGER NOT NULL DEFAULT 0,
  hidden      INTEGER NOT NULL DEFAULT 0);

CREATE TABLE transactions (
  -- The name the transaction was given when it was created, and what it is
  -- stored under for the rest of its life. UNIQUE because the generator
  -- promises it, and declared so here anyway: a promise the database enforces
  -- cannot be broken by a bug upstream of it.
  id        TEXT NOT NULL PRIMARY KEY,
  -- What the register groups by, and nothing the database relies on. It was the
  -- key once, assigned as "T" plus the number of transactions, and a delete of
  -- any but the last made the count disagree with the names -- so the next
  -- transaction collided with one still there and the whole save was refused.
  -- Names that are positions cannot be keys.
  ref       TEXT NOT NULL,
  date      TEXT NOT NULL,
  payee     TEXT NOT NULL DEFAULT '',
  -- What the bank sent, where a rule renamed it. A rule written next year has
  -- to be able to match what the bank sent this year.
  raw_name  TEXT NOT NULL DEFAULT '',
  check_no  TEXT NOT NULL DEFAULT '',
  memo      TEXT NOT NULL DEFAULT '',
  tag       TEXT NOT NULL DEFAULT '');

CREATE TABLE postings (
  transaction_id  TEXT    NOT NULL REFERENCES transactions(id) ON DELETE CASCADE,
  line            INTEGER NOT NULL,
  account         TEXT    NOT NULL,
  amount_cents    INTEGER NOT NULL,
  memo            TEXT    NOT NULL DEFAULT '',
  cleared         TEXT    NOT NULL DEFAULT 'Uncleared',
  PRIMARY KEY (transaction_id, line));

CREATE TABLE posting_import_ids (
  transaction_id  TEXT    NOT NULL,
  line            INTEGER NOT NULL,
  source          TEXT    NOT NULL,
  id              TEXT    NOT NULL,
  PRIMARY KEY (transaction_id, line, source),
  FOREIGN KEY (transaction_id, line)
    REFERENCES postings(transaction_id, line) ON DELETE CASCADE);

CREATE TABLE payee_rules (
  pattern    TEXT    NOT NULL,
  match_type TEXT    NOT NULL DEFAULT 'Contains',
  payee      TEXT    NOT NULL,
  category   TEXT    NOT NULL DEFAULT '',
  enabled    INTEGER NOT NULL DEFAULT 1,
  PRIMARY KEY (pattern, match_type));

CREATE TABLE payee_split_rules (
  pattern    TEXT NOT NULL,
  match_type TEXT NOT NULL DEFAULT 'Contains',
  payee      TEXT NOT NULL,
  shape      TEXT NOT NULL DEFAULT 'Amounts',
  PRIMARY KEY (pattern, match_type));

CREATE TABLE ofx_account_mappings (
  bank_id        TEXT NOT NULL DEFAULT '',
  ofx_account_id TEXT NOT NULL,
  account_path   TEXT NOT NULL,
  PRIMARY KEY (bank_id, ofx_account_id));

CREATE TABLE transfer_match_rules (
  from_account TEXT    NOT NULL,
  from_pattern TEXT    NOT NULL,
  to_account   TEXT    NOT NULL,
  to_pattern   TEXT    NOT NULL,
  enabled      INTEGER NOT NULL DEFAULT 1);

CREATE TABLE csv_profiles (
  name           TEXT    NOT NULL PRIMARY KEY,
  date_order     TEXT    NOT NULL DEFAULT 'MDY',
  has_header     INTEGER NOT NULL DEFAULT 1,
  decimal_comma  INTEGER NOT NULL DEFAULT 0);

CREATE TABLE csv_header_matches (
  profile TEXT NOT NULL REFERENCES csv_profiles(name) ON DELETE CASCADE,
  heading TEXT NOT NULL,
  field   TEXT NOT NULL,
  PRIMARY KEY (profile, heading));

CREATE INDEX postings_by_account ON postings(account);
CREATE INDEX import_ids_by_id ON posting_import_ids(source, id);
CREATE INDEX transactions_by_date ON transactions(date);
)sql";

}  // namespace detail

// Where the books are. One folder, machine-wide, holding as many .sadb files as
// the user has made; a book is chosen from the file names in it with the
// extension taken off, so the list is made by looking rather than maintained.
//
// SIMPLEACCOUNT_BOOKS overrides it. That is what lets a test start from an empty
// folder every time, which is the only way a forced sequence of scenarios --
// make a book, add accounts, enter transactions, import a file -- can say
// anything about the second run as well as the first.
inline std::string books_folder() {
    if (const char* from_env = std::getenv("SIMPLEACCOUNT_BOOKS"))
        if (from_env[0] != '\0') return from_env;
    const char* data = std::getenv("ProgramData");
    const std::string root = data != nullptr && data[0] != '\0' ? data : ".";
    return root + "\\SimpleAccount";
}

inline std::string extension() { return ".sadb"; }

inline std::string path_for(const std::string& name) {
    return books_folder() + "\\" + name + extension();
}

// Made on demand, the first time a book is created. A process running as an
// ordinary user may create a subfolder of ProgramData and owns what it puts
// there, so this needs no installer and no elevation.
inline Failure ensure_folder() {
    std::error_code bad;
    std::filesystem::create_directories(books_folder(), bad);
    if (bad && !std::filesystem::is_directory(books_folder()))
        return {true, books_folder() + " could not be made: " + bad.message()};
    return {};
}

// Every book in the folder, by name. A file that is not a book is not offered
// as one.
inline std::vector<std::string> book_names() {
    std::vector<std::string> out;
    std::error_code bad;
    for (const auto& entry : std::filesystem::directory_iterator(books_folder(), bad)) {
        if (bad) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != extension()) continue;
        out.push_back(entry.path().stem().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// A book held open. Everything that can fail says why rather than throwing, so
// that the window and the command line report the same words.
class Book {
public:
    // Opens an existing book. A path that is not there is refused rather than
    // created: a mistyped name must not look like a book that lost everything.
    // Opening by name, which is how a person asks for a book: the folder is
    // known and the name is what they chose. Refusals name the book rather than
    // the file, because a path tells them where the program looked and a name
    // tells them what they asked for.
    static Failure open_named(const std::string& name, Book* into) {
        return open_at(path_for(name), name, into);
    }

    static Failure open(const std::string& path, Book* into) {
        return open_at(path, path, into);
    }

private:
    static Failure open_at(const std::string& path, const std::string& label, Book* into) {
        std::FILE* probe = std::fopen(path.c_str(), "rb");
        if (probe == nullptr)
            return {true, "There is no book named " + label};
        std::fclose(probe);

        sqlite3* raw = nullptr;
        if (sqlite3_open_v2(path.c_str(), &raw, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
            const std::string why = raw != nullptr ? detail::message(raw) : "it could not be opened";
            sqlite3_close(raw);
            return {true, path + " could not be opened: " + why};
        }
        detail::Handle db(raw);

        // The version is read before anything else, so that a file from a later
        // program is refused untouched rather than written back in a shape that
        // drops whatever that version added.
        detail::Statement read(db.get(), "SELECT version FROM schema");
        if (!read.ok() || !read.step_row())
            return {true, label + " is not a SimpleAccount book"};
        const long long version = read.integer(0);
        if (version > kSchemaVersion)
            return {true, label + " was written by a later version of SimpleAccount"};

        into->db_ = std::move(db);
        into->path_ = path;
        return {};
    }

public:
    // Creates and initialises one. An existing file is refused: overwriting
    // twenty years of history because a name was reused is not a mistake worth
    // making available.
    static Failure create(const std::string& path, const std::string& name, Book* into) {
        std::FILE* probe = std::fopen(path.c_str(), "rb");
        if (probe != nullptr) {
            std::fclose(probe);
            return {true, "A book named " + name + " already exists"};
        }

        sqlite3* raw = nullptr;
        if (sqlite3_open_v2(path.c_str(), &raw,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
            const std::string why = raw != nullptr ? detail::message(raw) : "it could not be made";
            sqlite3_close(raw);
            return {true, path + " could not be made: " + why};
        }
        detail::Handle db(raw);

        std::string why;
        if (!detail::exec(db.get(), detail::kSchema, &why))
            return {true, "the schema could not be made: " + why};

        detail::Statement version(db.get(), "INSERT INTO schema (version) VALUES (?)");
        if (!version.ok()) return {true, version.why()};
        version.bind(1, kSchemaVersion);
        if (!version.run()) return {true, version.why()};

        detail::Statement info(db.get(),
                               "INSERT INTO book (name, fiscal_year_start, check_window_days) VALUES (?, 1, 1)");
        if (!info.ok()) return {true, info.why()};
        info.bind(1, name);
        if (!info.run()) return {true, info.why()};

        into->db_ = std::move(db);
        into->path_ = path;
        return {};
    }

    const std::string& path() const { return path_; }
    bool is_open() const { return db_ != nullptr; }

    BookInfo info() const {
        BookInfo out;
        detail::Statement read(db_.get(), "SELECT name, fiscal_year_start FROM book");
        if (read.ok() && read.step_row()) {
            out.name = read.text(0);
            out.fiscal_year_start = static_cast<int>(read.integer(1));
        }
        return out;
    }

    // Writes the whole book. One transaction around the lot, so an interrupted
    // save leaves the file as it was rather than half of each.
    Failure write(const chart::Chart& accounts,
                  const std::vector<ledger::Transaction>& transactions,
                  const std::vector<payees::Rule>& rules = {}) {
        std::string why;
        if (!detail::exec(db_.get(), "BEGIN IMMEDIATE", &why)) return {true, why};

        const auto fail = [&](const std::string& reason) -> Failure {
            detail::exec(db_.get(), "ROLLBACK", nullptr);
            return {true, reason};
        };

        // Still a whole-book rewrite, which is the next thing to go: the store
        // should be the record and a save should touch the rows that changed.
        // See the note in development.txt.
        if (!detail::exec(db_.get(), "DELETE FROM posting_import_ids; "
                                     "DELETE FROM postings; DELETE FROM transactions; "
                                     "DELETE FROM accounts; DELETE FROM payee_rules;",
                          &why))
            return fail(why);

        detail::Statement account(db_.get(),
            "INSERT INTO accounts (path, type, placeholder, hidden) VALUES (?, ?, ?, ?)");
        if (!account.ok()) return fail(account.why());
        for (const chart::Account& a : accounts.all()) {
            account.reset();
            account.bind(1, a.path.value());
            account.bind(2, types::to_string(a.type));
            account.bind(3, a.placeholder ? 1 : 0);
            account.bind(4, a.hidden ? 1 : 0);
            if (!account.run()) return fail(account.why());
        }

        detail::Statement header(db_.get(),
            "INSERT INTO transactions "
            "(id, ref, date, payee, raw_name, check_no, memo, tag) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
        if (!header.ok()) return fail(header.why());
        detail::Statement line(db_.get(),
            "INSERT INTO postings "
            "(transaction_id, line, account, amount_cents, memo, cleared) "
            "VALUES (?, ?, ?, ?, ?, ?)");
        if (!line.ok()) return fail(line.why());
        detail::Statement mark(db_.get(),
            "INSERT INTO posting_import_ids (transaction_id, line, source, id) "
            "VALUES (?, ?, ?, ?)");
        if (!mark.ok()) return fail(mark.why());

        for (const ledger::Transaction& t : transactions) {
            header.reset();
            // A transaction reaching the store without a name has not been
            // created properly. Giving it one here would hide where that
            // happened and would hand it a name from the wrong moment, so it is
            // refused instead.
            if (t.id.value().empty())
                return fail("transaction " + t.ref.value() + " has no id");
            header.bind(1, t.id.value());
            header.bind(2, t.ref.value());
            header.bind(3, t.date.iso());
            header.bind(4, t.payee.value());
            header.bind(5, t.raw_name);
            header.bind(6, t.check_no.value());
            header.bind(7, t.memo);
            header.bind(8, t.tag);
            if (!header.run()) return fail(header.why());

            // The line number keeps the order the postings were entered in, so
            // a register reopened reads the way it was written.
            int at = 0;
            for (const ledger::Posting& p : t.postings) {
                line.reset();
                line.bind(1, t.id.value());
                line.bind(2, at++);
                line.bind(3, p.account.value());
                line.bind(4, static_cast<long long>(p.amount.cents()));
                line.bind(5, p.memo);
                line.bind(6, types::to_string(p.cleared));
                if (!line.run()) return fail(line.why());
                for (const ledger::ImportId& one : p.import_ids) {
                    mark.reset();
                    mark.bind(1, t.id.value());
                    mark.bind(2, at - 1);
                    mark.bind(3, types::to_string(one.source));
                    mark.bind(4, one.id);
                    if (!mark.run()) return fail(mark.why());
                }
            }
        }

        detail::Statement rule(db_.get(),
            "INSERT INTO payee_rules (pattern, match_type, payee, category, enabled) "
            "VALUES (?, ?, ?, ?, ?)");
        if (!rule.ok()) return fail(rule.why());
        for (const payees::Rule& one : rules) {
            rule.reset();
            rule.bind(1, one.pattern);
            rule.bind(2, payees::to_string(one.match_type));
            rule.bind(3, one.payee);
            rule.bind(4, one.category);
            rule.bind(5, one.enabled ? 1 : 0);
            if (!rule.run()) return fail(rule.why());
        }

        if (!detail::exec(db_.get(), "COMMIT", &why)) return fail(why);
        return {};
    }

    // Reads it back. The ledger is built from the postings rather than stored,
    // so a balance can never disagree with the register.
    Failure read(chart::Chart* accounts, ledger::Ledger* book,
                 std::vector<ledger::Transaction>* transactions,
                 std::vector<payees::Rule>* rules = nullptr) const {
        *accounts = chart::Chart();
        *book = ledger::Ledger();
        transactions->clear();

        detail::Statement account(db_.get(),
            "SELECT path, type, placeholder, hidden FROM accounts ORDER BY path");
        if (!account.ok()) return {true, account.why()};
        while (account.step_row()) {
            chart::Account a;
            a.path = types::AccountPath(account.text(0));
            a.type = types::account_type_from_string(account.text(1));
            a.placeholder = account.integer(2) != 0;
            a.hidden = account.integer(3) != 0;
            accounts->put(a);
        }

        detail::Statement header(db_.get(),
            "SELECT id, ref, date, payee, raw_name, check_no, memo, tag "
            "FROM transactions "
            "ORDER BY date, rowid");
        if (!header.ok()) return {true, header.why()};
        while (header.step_row()) {
            ledger::Transaction t;
            t.id = types::TransactionId(header.text(0));
            t.ref = types::TransactionRef(header.text(1));
            const auto on = types::Date::from_iso(header.text(2));
            if (!on) return {true, header.text(2) + " is not a date"};
            t.date = *on;
            t.payee = types::PayeeName(header.text(3));
            t.raw_name = header.text(4);
            t.check_no = types::CheckNumber(header.text(5));
            t.memo = header.text(6);
            t.tag = header.text(7);
            transactions->push_back(t);
        }

        for (ledger::Transaction& t : *transactions) {
            detail::Statement line(db_.get(),
                "SELECT account, amount_cents, memo, cleared, line FROM postings "
                "WHERE transaction_id = ? ORDER BY line");
            if (!line.ok()) return {true, line.why()};
            line.bind(1, t.id.value());
            while (line.step_row()) {
                ledger::Posting p;
                p.account = types::AccountPath(line.text(0));
                p.amount = Money::from_cents(line.integer(1));
                p.memo = line.text(2);
                p.cleared = types::cleared_status_from_string(line.text(3));

                detail::Statement mark(db_.get(),
                    "SELECT source, id FROM posting_import_ids "
                    "WHERE transaction_id = ? AND line = ? ORDER BY source");
                if (!mark.ok()) return {true, mark.why()};
                mark.bind(1, t.id.value());
                mark.bind(2, line.integer(4));
                while (mark.step_row())
                    p.import_ids.push_back(ledger::ImportId{
                        types::import_source_from_string(mark.text(0)), mark.text(1)});
                t.postings.push_back(p);
                book->add({t.date, p.account, p.amount, t.ref.value()});
            }
        }

        if (rules != nullptr) {
            rules->clear();
            detail::Statement rule(db_.get(),
                "SELECT pattern, match_type, payee, category, enabled "
                "FROM payee_rules ORDER BY rowid");
            if (!rule.ok()) return {true, rule.why()};
            while (rule.step_row()) {
                payees::Rule one;
                one.pattern = rule.text(0);
                one.match_type = payees::match_type_from_string(rule.text(1));
                one.payee = rule.text(2);
                one.category = rule.text(3);
                one.enabled = rule.integer(4) != 0;
                rules->push_back(one);
            }
        }
        return {};
    }

private:
    detail::Handle db_;
    std::string path_;
};

}  // namespace store
