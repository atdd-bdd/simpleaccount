#pragma once
#include <sqlite3.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <system_error>
#include <string>
#include <unordered_map>
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
// this when that shape changes, and add the upgrade it needs to open_at below --
// a book already on disk was made by kSchema as it read on an earlier version,
// and has to be brought forward in place rather than recreated.
inline constexpr int kSchemaVersion = 10;

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
  check_window_days INTEGER NOT NULL DEFAULT 1,
  -- The number the id counter last handed out, for accounts and categories
  -- together. Recorded rather than worked out from the chart, so a deleted
  -- account's number is never given to the next one created. See the counter
  -- rule in Accounts.spectable.
  last_id_used      INTEGER NOT NULL DEFAULT 0);

CREATE TABLE accounts (
  -- The identity a posting or a transaction names instead of the path --
  -- see chart::Ref. Zero only transiently, between being created in memory
  -- and first written; every row written by this store carries a real one.
  -- Unique, because it is an identity: two accounts sharing one would make
  -- every reference to it ambiguous.
  id            INTEGER NOT NULL DEFAULT 0 UNIQUE,
  path          TEXT    NOT NULL PRIMARY KEY,
  type          TEXT    NOT NULL,
  placeholder   INTEGER NOT NULL DEFAULT 0,
  hidden        INTEGER NOT NULL DEFAULT 0,
  -- A short way to type the account when categorising, and what a payment to
  -- it is called on the paying account's own statement. See the finding rule
  -- in Accounts.spectable and the payment-payee rule in ImportOfx.
  alias         TEXT    NOT NULL DEFAULT '',
  payment_payee TEXT    NOT NULL DEFAULT '',
  -- What is kept about the account from its own dialog: which institution,
  -- and the number on the statement, kept exactly as written.
  bank            TEXT  NOT NULL DEFAULT '',
  account_number  TEXT  NOT NULL DEFAULT '');

CREATE TABLE categories (
  id            INTEGER NOT NULL DEFAULT 0 UNIQUE,
  path          TEXT    NOT NULL PRIMARY KEY,
  type          TEXT    NOT NULL,
  placeholder   INTEGER NOT NULL DEFAULT 0,
  hidden        INTEGER NOT NULL DEFAULT 0,
  -- A short way to type it; see the finding rule in Accounts.spectable.
  alias         TEXT    NOT NULL DEFAULT '');

-- The width each register column was dragged to, one row per column that
-- somebody resized. Not a setting of the book row: these are a measurement per
-- column rather than one named thing, and a width for a column nobody
-- recognises is simply ignored. See the width rule in UserInterface.spectable.
CREATE TABLE register_column_width (
  name  TEXT    NOT NULL PRIMARY KEY,
  width INTEGER NOT NULL);

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
  tag       TEXT NOT NULL DEFAULT '',
  -- What the transaction is, by id rather than path -- see chart::Ref -- so
  -- a rename or a move never has to rewrite one. See Transactions.spectable.
  source_ref     INTEGER NOT NULL DEFAULT 0,
  destination_ref INTEGER NOT NULL DEFAULT 0,
  amount_cents   INTEGER NOT NULL DEFAULT 0,
  -- Per entry under this shape: see the Cleared attribute on Transaction in
  -- Transactions.spectable.
  cleared        TEXT    NOT NULL DEFAULT 'Uncleared');

CREATE TABLE transaction_splits (
  -- A split's parts under the new shape, kept apart from postings rather
  -- than folded into them. See TransactionSplit in Transactions.spectable.
  transaction_id  TEXT    NOT NULL REFERENCES transactions(id) ON DELETE CASCADE,
  line            INTEGER NOT NULL,
  destination_ref INTEGER NOT NULL,
  amount_cents    INTEGER NOT NULL,
  memo            TEXT    NOT NULL DEFAULT '',
  PRIMARY KEY (transaction_id, line));

CREATE TABLE transaction_import_ids (
  -- At most one per source, since a transaction is one account's own
  -- download and has exactly one home account to carry an identifier for.
  transaction_id  TEXT NOT NULL REFERENCES transactions(id) ON DELETE CASCADE,
  source          TEXT NOT NULL,
  id              TEXT NOT NULL,
  PRIMARY KEY (transaction_id, source));

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

CREATE INDEX import_ids_by_id ON transaction_import_ids(source, id);
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
        if (version < kSchemaVersion) {
            std::string why;
            if (!upgrade(db.get(), static_cast<int>(version), &why))
                return {true, label + " could not be brought up to date: " + why};
        }

        into->db_ = std::move(db);
        into->path_ = path;
        return {};
    }

    // Brings a book written by an earlier version up to the current schema, in
    // place. Each step adds to what is there; nothing already on disk is ever
    // read differently by an older step, because new columns always default
    // to the value an old book's absence of them already meant.
    static bool upgrade(sqlite3* db, int from, std::string* why) {
        if (from < 2) {
            // alias and payment_payee existed in the program before this,
            // but the column to hold them was never added -- so every book
            // written before version 2 has neither, which is exactly the
            // empty default a new column gets.
            static const char* const columns[] = {
                "ALTER TABLE accounts ADD COLUMN alias TEXT NOT NULL DEFAULT ''",
                "ALTER TABLE accounts ADD COLUMN payment_payee TEXT NOT NULL DEFAULT ''",
                "ALTER TABLE accounts ADD COLUMN bank TEXT NOT NULL DEFAULT ''",
                "ALTER TABLE accounts ADD COLUMN account_number TEXT NOT NULL DEFAULT ''",
            };
            for (const char* sql : columns)
                if (!detail::exec(db, sql, why)) return false;
        }
        if (from < 3) {
            // Category did not exist as its own collection before this: a
            // book written before version 3 has no categories table at all,
            // which is exactly the empty collection a new book starts with.
            static const char* const sql =
                "CREATE TABLE categories ("
                "  path          TEXT    NOT NULL PRIMARY KEY,"
                "  type          TEXT    NOT NULL,"
                "  placeholder   INTEGER NOT NULL DEFAULT 0,"
                "  hidden        INTEGER NOT NULL DEFAULT 0,"
                "  alias         TEXT    NOT NULL DEFAULT '')";
            if (!detail::exec(db, sql, why)) return false;
        }
        if (from < 4) {
            // SourceAccount, Destination and Amount did not exist on a
            // transaction before this, and TransactionSplit did not exist as
            // its own collection -- a book written before version 4 has
            // neither, which is exactly what an empty default/table means.
            static const char* const columns[] = {
                "ALTER TABLE transactions ADD COLUMN source_ref INTEGER NOT NULL DEFAULT 0",
                "ALTER TABLE transactions ADD COLUMN destination_ref INTEGER NOT NULL DEFAULT 0",
                "ALTER TABLE transactions ADD COLUMN amount_cents INTEGER NOT NULL DEFAULT 0",
            };
            for (const char* sql : columns)
                if (!detail::exec(db, sql, why)) return false;
            static const char* const splits =
                "CREATE TABLE transaction_splits ("
                "  transaction_id  TEXT    NOT NULL REFERENCES transactions(id) ON DELETE CASCADE,"
                "  line            INTEGER NOT NULL,"
                "  destination_ref INTEGER NOT NULL,"
                "  amount_cents    INTEGER NOT NULL,"
                "  memo            TEXT    NOT NULL DEFAULT '',"
                "  PRIMARY KEY (transaction_id, line))";
            if (!detail::exec(db, splits, why)) return false;
        }
        if (from < 5) {
            // Cleared did not exist on a transaction before this -- it lived
            // only on a posting -- so a book written before version 5 has
            // every transaction reading as Uncleared under this shape, which
            // is exactly the new column's default.
            static const char* const sql =
                "ALTER TABLE transactions ADD COLUMN cleared TEXT NOT NULL DEFAULT 'Uncleared'";
            if (!detail::exec(db, sql, why)) return false;
        }
        if (from < 6) {
            // A posting's account moves from path text to an id -- see
            // chart::Ref -- which is not a column type ALTER can carry a
            // value across. The author does not need the old rows kept: a
            // book from before this version is re-imported from its QIF
            // rather than opened, so the column is simply remade empty.
            static const char* const sql[] = {
                "DELETE FROM posting_import_ids",
                "DROP TABLE postings",
                "CREATE TABLE postings ("
                "  transaction_id  TEXT    NOT NULL REFERENCES transactions(id) ON DELETE CASCADE,"
                "  line            INTEGER NOT NULL,"
                "  account_ref     INTEGER NOT NULL,"
                "  amount_cents    INTEGER NOT NULL,"
                "  memo            TEXT    NOT NULL DEFAULT '',"
                "  cleared         TEXT    NOT NULL DEFAULT 'Uncleared',"
                "  PRIMARY KEY (transaction_id, line))",
                "DROP TABLE IF EXISTS transaction_splits",
                "CREATE TABLE transaction_splits ("
                "  transaction_id  TEXT    NOT NULL REFERENCES transactions(id) ON DELETE CASCADE,"
                "  line            INTEGER NOT NULL,"
                "  destination_ref INTEGER NOT NULL,"
                "  amount_cents    INTEGER NOT NULL,"
                "  memo            TEXT    NOT NULL DEFAULT '',"
                "  PRIMARY KEY (transaction_id, line))",
            };
            for (const char* s : sql)
                if (!detail::exec(db, s, why)) return false;
        }
        if (from < 7) {
            // Postings go away entirely: a transaction is its own one side,
            // with a split's lines in transaction_splits, so nothing keyed
            // on postings(transaction_id, line) has anywhere left to point.
            // Same as the version 6 upgrade, a book from before this is
            // re-imported from its QIF rather than carried across.
            static const char* const sql[] = {
                "DROP TABLE IF EXISTS posting_import_ids",
                "DROP TABLE IF EXISTS postings",
                "CREATE TABLE transaction_import_ids ("
                "  transaction_id  TEXT NOT NULL REFERENCES transactions(id) ON DELETE CASCADE,"
                "  source          TEXT NOT NULL,"
                "  id              TEXT NOT NULL,"
                "  PRIMARY KEY (transaction_id, source))",
            };
            for (const char* s : sql)
                if (!detail::exec(db, s, why)) return false;
        }
        if (from < 8) {
            // Account and Category were never given an id column, so every
            // open reassigned fresh ones in path order and silently
            // orphaned every source_ref/destination_ref already written.
            // Caught before any real book had been through a save/reopen
            // cycle under the new shape, so there is nothing to backfill --
            // the accounts and categories below get their ids the normal
            // way, from read()'s own put()/put_category() calls, the next
            // time this book is opened after this ALTER runs.
            static const char* const sql[] = {
                "ALTER TABLE accounts ADD COLUMN id INTEGER NOT NULL DEFAULT 0",
                "ALTER TABLE categories ADD COLUMN id INTEGER NOT NULL DEFAULT 0",
            };
            for (const char* s : sql)
                if (!detail::exec(db, s, why)) return false;
        }
        if (from < 9) {
            // The counter was worked out from the largest id in the chart,
            // which drops back when the highest account is deleted. It is
            // recorded now. An older book's ids are assigned at read() and
            // written on the next save, so there is nothing to backfill: the
            // column starts at zero and the first save raises it.
            static const char* const sql[] = {
                "ALTER TABLE book ADD COLUMN last_id_used INTEGER NOT NULL DEFAULT 0",
                "CREATE UNIQUE INDEX accounts_id ON accounts (id)",
                "CREATE UNIQUE INDEX categories_id ON categories (id)",
            };
            for (const char* s : sql)
                if (!detail::exec(db, s, why)) return false;
        }
        if (from < 10) {
            // Column widths were not kept at all before this, so there is
            // nothing to carry over: the table starts empty, which is exactly
            // what "nobody has resized anything" already meant.
            static const char* const sql =
                "CREATE TABLE register_column_width ("
                "  name  TEXT    NOT NULL PRIMARY KEY,"
                "  width INTEGER NOT NULL)";
            if (!detail::exec(db, sql, why)) return false;
        }
        detail::Statement set(db, "UPDATE schema SET version = ?");
        if (!set.ok()) { if (why) *why = set.why(); return false; }
        set.bind(1, kSchemaVersion);
        if (!set.run()) { if (why) *why = set.why(); return false; }
        return true;
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
                               "INSERT INTO book (name, fiscal_year_start, check_window_days, "
                               "last_id_used) VALUES (?, 1, 1, 0)");
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

    // Writes the book. One database transaction around the lot, so an
    // interrupted save leaves the file as it was rather than half of each.
    //
    // The chart and the rules are rewritten outright -- there are hundreds of
    // them, not tens of thousands. The transactions are not: the author's book
    // holds 37,000 and a save happens after nearly every edit, so rewriting
    // them all meant a few seconds of inserts to record one typed amount.
    // Instead each transaction is compared against what this object last saw
    // in the file and only the ones that differ are touched. What the file
    // holds afterwards is the same either way, which is how the rule in
    // Books.spectable is stated: about the state of the file, not about which
    // rows were written.
    Failure write(const chart::Chart& accounts,
                  const std::vector<ledger::Transaction>& transactions,
                  const std::vector<payees::Rule>& rules = {},
                  const std::vector<ledger::TransactionSplit>& splits = {},
                  const std::map<std::string, int>& column_widths = {}) {
        // The lines of one transaction, found once for all of them. This was a
        // scan of every split line for every transaction, which on a book with
        // a few thousand splits was tens of millions of comparisons per save.
        std::unordered_map<std::string, std::vector<const ledger::TransactionSplit*>> lines;
        for (const ledger::TransactionSplit& one : splits)
            lines[one.transaction_id.value()].push_back(&one);

        // What the book is asking the file to hold. Built before anything is
        // written, so a transaction that cannot be stored is refused while the
        // file is still untouched.
        std::unordered_map<std::string, std::string> wanted;
        wanted.reserve(transactions.size());
        const std::vector<const ledger::TransactionSplit*> no_lines;
        for (const ledger::Transaction& t : transactions) {
            // A transaction reaching the store without a name has not been
            // created properly. Giving it one here would hide where that
            // happened and would hand it a name from the wrong moment, so it
            // is refused instead.
            if (t.id.value().empty())
                return {true, "transaction " + t.ref.value() + " has no id"};
            const auto at = lines.find(t.id.value());
            // Two transactions under one id cannot both be stored -- id is the
            // primary key. The whole-book rewrite learned this from the UNIQUE
            // constraint partway through writing; saying it here names the
            // transaction instead of the column.
            if (!wanted.emplace(t.id.value(),
                                state_of(t, at == lines.end() ? no_lines : at->second)).second)
                return {true, "transaction " + t.ref.value() + " is stored twice"};
        }

        // Once per opened book: after this the save keeps it up to date itself.
        if (!stored_known_) {
            const Failure no = look_at_what_is_stored();
            if (no.refused) return no;
        }

        std::string why;
        if (!detail::exec(db_.get(), "BEGIN IMMEDIATE", &why)) return {true, why};

        const auto fail = [&](const std::string& reason) -> Failure {
            detail::exec(db_.get(), "ROLLBACK", nullptr);
            return {true, reason};
        };

        // The small tables. A rewrite of a few hundred rows costs about a
        // millisecond, and the chart has no stable key to diff on that is
        // worth the trouble while that is true. The transactions are dealt
        // with below, row by row.
        if (!detail::exec(db_.get(), "DELETE FROM accounts; DELETE FROM categories; "
                                     "DELETE FROM payee_rules;",
                          &why))
            return fail(why);

        detail::Statement account(db_.get(),
            "INSERT INTO accounts (id, path, type, placeholder, hidden, alias, "
            "payment_payee, bank, account_number) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        if (!account.ok()) return fail(account.why());
        for (const chart::Account& a : accounts.all()) {
            account.reset();
            account.bind(1, a.id.value());
            account.bind(2, a.path.value());
            account.bind(3, types::to_string(a.type));
            account.bind(4, a.placeholder ? 1 : 0);
            account.bind(5, a.hidden ? 1 : 0);
            account.bind(6, a.alias);
            account.bind(7, a.payment_payee);
            account.bind(8, a.bank);
            account.bind(9, a.account_number);
            if (!account.run()) return fail(account.why());
        }

        detail::Statement category(db_.get(),
            "INSERT INTO categories (id, path, type, placeholder, hidden, alias) "
            "VALUES (?, ?, ?, ?, ?, ?)");
        if (!category.ok()) return fail(category.why());
        for (const chart::Category& c : accounts.all_categories()) {
            category.reset();
            category.bind(1, c.id.value());
            category.bind(2, c.path.value());
            category.bind(3, types::to_string(c.type));
            category.bind(4, c.placeholder ? 1 : 0);
            category.bind(5, c.hidden ? 1 : 0);
            category.bind(6, c.alias);
            if (!category.run()) return fail(category.why());
        }

        detail::Statement header(db_.get(),
            "INSERT INTO transactions "
            "(id, ref, date, payee, raw_name, check_no, memo, tag, "
            "source_ref, destination_ref, amount_cents, cleared) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        if (!header.ok()) return fail(header.why());
        detail::Statement mark(db_.get(),
            "INSERT INTO transaction_import_ids (transaction_id, source, id) "
            "VALUES (?, ?, ?)");
        if (!mark.ok()) return fail(mark.why());
        detail::Statement split(db_.get(),
            "INSERT INTO transaction_splits "
            "(transaction_id, line, destination_ref, amount_cents, memo) "
            "VALUES (?, ?, ?, ?, ?)");
        if (!split.ok()) return fail(split.why());

        // A transaction is replaced rather than updated in place: its row goes
        // and is written again, and so do its lines and its import marks. The
        // lines have no identity of their own -- the line number is a position
        // -- so there is nothing to update them by, and a transaction that
        // went from three lines to two would otherwise keep the third.
        //
        // The children are deleted here rather than left to the foreign keys.
        // The schema declares ON DELETE CASCADE, but nothing turns
        // PRAGMA foreign_keys on, so those clauses do nothing at all.
        detail::Statement forget_header(db_.get(), "DELETE FROM transactions WHERE id = ?");
        if (!forget_header.ok()) return fail(forget_header.why());
        detail::Statement forget_splits(db_.get(),
            "DELETE FROM transaction_splits WHERE transaction_id = ?");
        if (!forget_splits.ok()) return fail(forget_splits.why());
        detail::Statement forget_marks(db_.get(),
            "DELETE FROM transaction_import_ids WHERE transaction_id = ?");
        if (!forget_marks.ok()) return fail(forget_marks.why());

        const auto forget = [&](const std::string& id) -> std::string {
            forget_marks.reset();
            forget_marks.bind(1, id);
            if (!forget_marks.run()) return forget_marks.why();
            forget_splits.reset();
            forget_splits.bind(1, id);
            if (!forget_splits.run()) return forget_splits.why();
            forget_header.reset();
            forget_header.bind(1, id);
            if (!forget_header.run()) return forget_header.why();
            return {};
        };

        // Gone from the book, so gone from the file. This is the case a save
        // that only wrote what it had in front of it would get wrong, and it
        // is why the rule is stated about what the file holds afterwards.
        for (const auto& was : stored_)
            if (wanted.find(was.first) == wanted.end()) {
                const std::string why_not = forget(was.first);
                if (!why_not.empty()) return fail(why_not);
            }

        for (const ledger::Transaction& t : transactions) {
            const auto now = wanted.find(t.id.value());
            const auto before = stored_.find(t.id.value());
            if (before != stored_.end() && before->second == now->second) continue;

            if (before != stored_.end()) {
                const std::string why_not = forget(t.id.value());
                if (!why_not.empty()) return fail(why_not);
            }

            header.reset();
            header.bind(1, t.id.value());
            header.bind(2, t.ref.value());
            header.bind(3, t.date.iso());
            header.bind(4, t.payee.value());
            header.bind(5, t.raw_name);
            header.bind(6, t.check_no.value());
            header.bind(7, t.memo);
            header.bind(8, t.tag);
            header.bind(9, t.source_account.value());
            header.bind(10, t.destination.value());
            header.bind(11, static_cast<long long>(t.amount.cents()));
            header.bind(12, types::to_string(t.cleared));
            if (!header.run()) return fail(header.why());

            const auto at = lines.find(t.id.value());
            if (at != lines.end()) {
                int split_line = 0;
                for (const ledger::TransactionSplit* s : at->second) {
                    split.reset();
                    split.bind(1, t.id.value());
                    split.bind(2, split_line++);
                    split.bind(3, s->destination.value());
                    split.bind(4, static_cast<long long>(s->amount.cents()));
                    split.bind(5, s->memo);
                    if (!split.run()) return fail(split.why());
                }
            }

            for (const ledger::ImportId& one : t.import_ids) {
                mark.reset();
                mark.bind(1, t.id.value());
                mark.bind(2, types::to_string(one.source));
                mark.bind(3, one.id);
                if (!mark.run()) return fail(mark.why());
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

        // The widths are written over rather than cleared and rewritten, so a
        // save that was not about them -- which is nearly every save -- leaves
        // the ones already recorded alone.
        detail::Statement width(db_.get(),
            "INSERT INTO register_column_width (name, width) VALUES (?, ?) "
            "ON CONFLICT (name) DO UPDATE SET width = excluded.width");
        if (!width.ok()) return fail(width.why());
        for (const auto& one : column_widths) {
            width.reset();
            width.bind(1, one.first);
            width.bind(2, static_cast<long long>(one.second));
            if (!width.run()) return fail(width.why());
        }

        // The counter the chart is holding, so the next id this book hands out
        // after being opened again is one past the last it ever gave rather
        // than one past the largest it still holds. See the storage rule in
        // Books.spectable.
        detail::Statement counter(db_.get(), "UPDATE book SET last_id_used = ?");
        if (!counter.ok()) return fail(counter.why());
        counter.bind(1, accounts.last_id_used());
        if (!counter.run()) return fail(counter.why());

        if (!detail::exec(db_.get(), "COMMIT", &why)) return fail(why);

        // Only now, and only here. Every way out of this function above is a
        // refusal that rolled back, and a refused save has to leave this
        // holding what the file still holds -- otherwise the next save would
        // believe rows are stored that were never written, and skip them. That
        // is the "the save fails" line of the rule in Books.spectable.
        stored_ = std::move(wanted);
        stored_known_ = true;
        return {};
    }

    // Reads it back. The ledger is built from the postings rather than stored,
    // so a balance can never disagree with the register.
    Failure read(chart::Chart* accounts, ledger::Ledger* book,
                 std::vector<ledger::Transaction>* transactions,
                 std::vector<payees::Rule>* rules = nullptr,
                 std::vector<ledger::TransactionSplit>* splits = nullptr,
                 std::map<std::string, int>* column_widths = nullptr) const {
        *accounts = chart::Chart();
        *book = ledger::Ledger();
        transactions->clear();

        detail::Statement account(db_.get(),
            "SELECT id, path, type, placeholder, hidden, alias, payment_payee, "
            "bank, account_number FROM accounts ORDER BY path");
        if (!account.ok()) return {true, account.why()};
        while (account.step_row()) {
            chart::Account a;
            a.id = chart::AccountId(account.integer(0));
            a.path = types::AccountPath(account.text(1));
            a.type = types::account_type_from_string(account.text(2));
            a.placeholder = account.integer(3) != 0;
            a.hidden = account.integer(4) != 0;
            a.alias = account.text(5);
            a.payment_payee = account.text(6);
            a.bank = account.text(7);
            a.account_number = account.text(8);
            accounts->put(a);
        }

        detail::Statement category(db_.get(),
            "SELECT id, path, type, placeholder, hidden, alias FROM categories ORDER BY path");
        if (!category.ok()) return {true, category.why()};
        while (category.step_row()) {
            chart::Category c;
            c.id = chart::CategoryId(category.integer(0));
            c.path = types::AccountPath(category.text(1));
            c.type = types::account_type_from_string(category.text(2));
            c.placeholder = category.integer(3) != 0;
            c.hidden = category.integer(4) != 0;
            c.alias = category.text(5);
            accounts->put_category(c);
        }

        // After the chart, so that a row carrying a higher id than the book
        // recorded still raises the counter rather than being overwritten by
        // it. The counter never goes backwards; see Books.spectable.
        detail::Statement counter(db_.get(), "SELECT last_id_used FROM book");
        if (counter.ok() && counter.step_row())
            accounts->set_last_id_used(counter.integer(0));

        if (column_widths != nullptr) {
            column_widths->clear();
            detail::Statement width(db_.get(),
                "SELECT name, width FROM register_column_width");
            if (width.ok())
                while (width.step_row())
                    (*column_widths)[width.text(0)] = static_cast<int>(width.integer(1));
        }

        detail::Statement header(db_.get(),
            "SELECT id, ref, date, payee, raw_name, check_no, memo, tag, "
            "source_ref, destination_ref, amount_cents, cleared "
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
            t.source_account = chart::Ref(header.integer(8));
            t.destination = chart::Ref(header.integer(9));
            t.amount = Money::from_cents(header.integer(10));
            t.cleared = types::cleared_status_from_string(header.text(11));
            transactions->push_back(t);
        }

        // Read regardless of whether the caller wants the lines back: a
        // split's own sides are only in this table now, and the book has to
        // have all of them whether or not splits is null.
        //
        // Both of these were a statement per transaction -- a prepare, a bind
        // and a step for each of 37,000 transactions, twice over, which was
        // most of the time it took to open the author's own book. One query
        // each, grouped by the transaction it belongs to, says the same thing.
        // The lines are then appended transaction by transaction, in the order
        // the transactions are in, so the vector reads exactly as it did
        // before: by transaction in date order, and by line within one.
        std::map<std::string, std::vector<ledger::TransactionSplit>> split_lines;
        {
            detail::Statement s(db_.get(),
                "SELECT transaction_id, destination_ref, amount_cents, memo "
                "FROM transaction_splits ORDER BY transaction_id, line");
            if (!s.ok()) return {true, s.why()};
            while (s.step_row()) {
                ledger::TransactionSplit one;
                one.transaction_id = types::TransactionId(s.text(0));
                one.destination = chart::Ref(s.integer(1));
                one.amount = Money::from_cents(s.integer(2));
                one.memo = s.text(3);
                split_lines[one.transaction_id.value()].push_back(one);
            }
        }
        std::vector<ledger::TransactionSplit> all_splits;
        for (const ledger::Transaction& t : *transactions) {
            const auto at = split_lines.find(t.id.value());
            if (at == split_lines.end()) continue;
            all_splits.insert(all_splits.end(), at->second.begin(), at->second.end());
        }
        if (splits != nullptr) *splits = all_splits;

        std::map<std::string, std::vector<ledger::ImportId>> marks;
        {
            detail::Statement mark(db_.get(),
                "SELECT transaction_id, source, id FROM transaction_import_ids "
                "ORDER BY transaction_id, source");
            if (!mark.ok()) return {true, mark.why()};
            while (mark.step_row())
                marks[mark.text(0)].push_back(ledger::ImportId{
                    types::import_source_from_string(mark.text(1)), mark.text(2)});
        }
        for (ledger::Transaction& t : *transactions) {
            const auto at = marks.find(t.id.value());
            if (at == marks.end()) continue;
            t.import_ids = at->second;
        }
        ledger::add_to_book(book, *accounts, *transactions, all_splits);

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
    // Everything the file holds about one transaction, in one string: its own
    // row, its split lines in the order they are in, and its import marks.
    // Two saves of an unchanged transaction produce the same string, and a
    // difference in any field produces a different one.
    //
    // Each part is written with its length in front of it, so no run of
    // characters inside a memo can imitate the start of the next field. A memo
    // of "5:x" would otherwise be able to make two different transactions look
    // alike, and looking alike here means an edit is not written.
    static std::string state_of(const ledger::Transaction& t,
                                const std::vector<const ledger::TransactionSplit*>& lines) {
        std::string out;
        const auto add = [&out](const std::string& value) {
            out += std::to_string(value.size());
            out += ':';
            out += value;
        };
        add(t.ref.value());
        add(t.date.iso());
        add(t.payee.value());
        add(t.raw_name);
        add(t.check_no.value());
        add(t.memo);
        add(t.tag);
        add(std::to_string(t.source_account.value()));
        add(std::to_string(t.destination.value()));
        add(std::to_string(t.amount.cents()));
        add(types::to_string(t.cleared));
        for (const ledger::TransactionSplit* s : lines) {
            add(std::to_string(s->destination.value()));
            add(std::to_string(s->amount.cents()));
            add(s->memo);
        }
        // Between the lines and the marks, because a line writes three fields
        // and a mark writes two: without it, a transaction with one line and
        // no marks could spell the same string as one with no lines and marks.
        out += '|';
        // By source rather than in the order the book happens to hold them,
        // which is the order they come back in. A set of marks that is the
        // same set must read as the same string, or the first save after
        // opening would rewrite the transaction for no reason.
        std::vector<std::string> marks;
        marks.reserve(t.import_ids.size());
        for (const ledger::ImportId& one : t.import_ids)
            marks.push_back(types::to_string(one.source) + ":" + one.id);
        std::sort(marks.begin(), marks.end());
        for (const std::string& one : marks) add(one);
        return out;
    }

    // What the file holds, in the same strings a save would produce for it.
    // Read once, the first time this object writes.
    //
    // The rows are read back into the structures the book itself uses and
    // handed to the same state_of, rather than serialising them a second way
    // out of SQL. Two serialisers would have to agree forever; if they drifted
    // apart, every save would silently become a whole-book rewrite again, and
    // nothing would look wrong except the clock.
    Failure look_at_what_is_stored() {
        std::unordered_map<std::string, std::vector<ledger::TransactionSplit>> lines;
        {
            detail::Statement s(db_.get(),
                "SELECT transaction_id, destination_ref, amount_cents, memo "
                "FROM transaction_splits ORDER BY transaction_id, line");
            if (!s.ok()) return {true, s.why()};
            while (s.step_row()) {
                ledger::TransactionSplit one;
                one.transaction_id = types::TransactionId(s.text(0));
                one.destination = chart::Ref(s.integer(1));
                one.amount = Money::from_cents(s.integer(2));
                one.memo = s.text(3);
                lines[one.transaction_id.value()].push_back(one);
            }
        }

        std::unordered_map<std::string, std::vector<ledger::ImportId>> marks;
        {
            detail::Statement m(db_.get(),
                "SELECT transaction_id, source, id FROM transaction_import_ids "
                "ORDER BY transaction_id, source");
            if (!m.ok()) return {true, m.why()};
            while (m.step_row())
                marks[m.text(0)].push_back(ledger::ImportId{
                    types::import_source_from_string(m.text(1)), m.text(2)});
        }

        std::unordered_map<std::string, std::string> stored;
        detail::Statement header(db_.get(),
            "SELECT id, ref, date, payee, raw_name, check_no, memo, tag, "
            "source_ref, destination_ref, amount_cents, cleared FROM transactions");
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
            t.source_account = chart::Ref(header.integer(8));
            t.destination = chart::Ref(header.integer(9));
            t.amount = Money::from_cents(header.integer(10));
            t.cleared = types::cleared_status_from_string(header.text(11));
            const auto mark = marks.find(t.id.value());
            if (mark != marks.end()) t.import_ids = mark->second;

            std::vector<const ledger::TransactionSplit*> of_this;
            const auto line = lines.find(t.id.value());
            if (line != lines.end())
                for (const ledger::TransactionSplit& one : line->second)
                    of_this.push_back(&one);
            stored.emplace(t.id.value(), state_of(t, of_this));
        }

        stored_ = std::move(stored);
        stored_known_ = true;
        return {};
    }

    detail::Handle db_;
    std::string path_;
    // What this object last saw in the file, by transaction id. Empty and not
    // yet known on a book just opened; filled on the first save and kept in
    // step by every save after it.
    std::unordered_map<std::string, std::string> stored_;
    bool stored_known_ = false;
};

}  // namespace store
