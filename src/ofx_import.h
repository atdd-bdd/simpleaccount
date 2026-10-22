#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

#include "chart.h"
#include "ledger.h"
#include "money.h"
#include "ofx_reader.h"
#include "payee_rules.h"
#include "transaction.h"
#include "transaction_id.h"

// Deciding what to do with each downloaded transaction: create it, recognise it
// as one already imported, match it to one entered by hand, or offer it as a
// possible duplicate. See ImportOfx.spectable and the claim-pairing rule in
// ImportCsv.spectable.
namespace ofx {

// Four, where there were five. Matched and Duplicate were two answers to one
// question -- this transaction is already here -- and the only thing that told
// them apart was whether the one already here was cleared, which says where it
// came from and nothing about whether it is the same transaction. So both are
// Duplicate now, and what was typed is said in a note.
//
// Completed is the one that creates something: this row is a payment whose
// other half is already in the book, waiting with its own side unassigned. The
// transaction already there gains an account, and this row is written as the
// side the file described. See the payment rule in ImportOfx.spectable.
enum class Disposition { New, Duplicate, Possible, Completed };

inline std::string to_string(Disposition d) {
    switch (d) {
        case Disposition::New:       return "New";
        case Disposition::Duplicate: return "Duplicate";
        case Disposition::Possible:  return "Possible";
        case Disposition::Completed: return "Completed";
    }
    return "New";
}

// What was seen about a duplicate that did not fit in its answer. Kept on the
// transaction rather than against the import, because the question it answers
// -- why is this cleared, why is it spelt like that -- is asked long after the
// import has been put away. Several can be true of one duplicate, and each is
// kept once. See what the note on a duplicate says in ImportOfx.spectable.
enum class DuplicateNote {
    None,
    DetailsDiffer,
    CheckNumberSame,
    FitIdAdded,
    PayeeUnmatched,
    Uncleared,
};

inline std::string to_string(DuplicateNote n) {
    switch (n) {
        case DuplicateNote::None:            return "None";
        case DuplicateNote::DetailsDiffer:   return "DetailsDiffer";
        case DuplicateNote::CheckNumberSame: return "CheckNumberSame";
        case DuplicateNote::FitIdAdded:      return "FitIdAdded";
        case DuplicateNote::PayeeUnmatched:  return "PayeeUnmatched";
        case DuplicateNote::Uncleared:       return "Uncleared";
    }
    return "None";
}

struct Decided {
    Transaction downloaded;
    Disposition disposition = Disposition::New;
    // Which transaction already in the book it claimed, where it claimed one,
    // by identity rather than by label. Empty where it claimed nothing.
    std::string claimed_id;
    // The same transaction's label, for saying which one in a message. Never
    // used to find it again.
    std::string claimed_ref;
    // Every transaction already in the book that this row was decided against:
    // one for a duplicate or a completion, and for a possible all of the ones
    // it could be, because nothing says which. The review carries these so
    // that the claim can be looked at before it is believed. See the
    // show-the-duplicate section of UserInterface.spectable.
    std::vector<std::string> claim_ids;
    // What was seen about this duplicate, in the order a reader wants it: what
    // settled it first, then what the import changed, then what it added.
    std::vector<DuplicateNote> notes;
};

struct Summary {
    int New = 0;
    int Duplicate = 0;
    int Possible = 0;
    int Completed = 0;
};

// Something the import saw and could not decide. Kept against the import and
// shown when it finishes: see the check-number rule in ImportOfx.spectable.
// Modelled on qif::Note, which does the same job for a migration.
struct Note {
    std::string kind;
    std::string detail;
};

struct Imported {
    std::vector<Decided> decided;
    Summary summary;
    // Written where the program could not decide something and a person has to
    // look. A note nobody is shown is a note nobody acts on, so an import that
    // wrote any says so at the end.
    std::vector<Note> notes;
    bool refused = false;
    std::string refusal;
};

namespace detail {

inline std::string fold(const std::string& s) {
    std::string out;
    bool last_was_blank = false;
    for (char c : s) {
        const bool blank = std::isspace(static_cast<unsigned char>(c)) != 0;
        if (blank) {
            last_was_blank = true;
            continue;
        }
        if (last_was_blank && !out.empty()) out.push_back(' ');
        last_was_blank = false;
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

// The date and the amount, which is the key everything here is matched on. The
// payee is deliberately not in it: a history brought over from Quicken says
// Harris Teeter where the download says HARRIS TEETER #231, and a key holding
// both would match neither. What the payee decides is the note. See the
// Fingerprint term in ImportCsv.spectable.
inline std::string fingerprint(const types::Date& on, const Money& amount) {
    return on.iso() + "|" + std::to_string(amount.cents());
}

// A payee reduced to what two files might agree on: upper case, punctuation
// gone, runs of blanks collapsed, and a trailing run of all-digit words taken
// off. The last of those is the store number -- HARRIS TEETER #231 is the
// Harris Teeter on Franklin Street, not a different shop, and JOE COFFEE 02 is
// the second till.
//
// It stops there on purpose. SHELL OIL does not reduce to SHELL and AMZN Mktp
// US*2X4 does not reduce to Amazon, and no amount of trimming would get there
// -- only a payee rule knows those. A resemblance score would reach them and
// reach a dozen wrong things with them, and a number saying two names are
// eighty per cent alike is not something a person can check or a specification
// can state. See two payees are compared after the rules and then loosely.
inline std::string loose_payee(const std::string& s) {
    std::string plain;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) != 0) {
            plain.push_back(static_cast<char>(std::toupper(u)));
        } else {
            plain.push_back(' ');
        }
    }
    std::vector<std::string> words;
    std::string word;
    for (char c : plain) {
        if (c == ' ') {
            if (!word.empty()) words.push_back(word);
            word.clear();
            continue;
        }
        word.push_back(c);
    }
    if (!word.empty()) words.push_back(word);

    const auto all_digits = [](const std::string& w) {
        for (char c : w)
            if (std::isdigit(static_cast<unsigned char>(c)) == 0) return false;
        return !w.empty();
    };
    // Only a trailing run, and never the whole name: a payee called 7 ELEVEN
    // has a number at the front and a shop called 02 is still a shop.
    while (words.size() > 1 && all_digits(words.back())) words.pop_back();

    std::string out;
    for (const std::string& one : words) {
        if (!out.empty()) out.push_back(' ');
        out += one;
    }
    return out;
}

// Whether the book and the file are naming the same payee.
//
// The name the file wrote, kept on the transaction when it was imported, is
// asked first and settles it: it is the only field two readings of the same
// statement are certain to agree on. The book holds Harris Teeter and the file
// says HARRIS TEETER #231 DURHAM NC, and they are the same row only because
// that field says so -- compared on the converted name they would look like
// different shops and the statement would be imported twice.
//
// Where there is no such name -- a transaction typed by hand, or migrated from
// Quicken -- the rules come next, because they are the only thing that can know
// AMZN Mktp US*2X4 is Amazon, and a renamed name is what gets compared. Where
// no rule applies either, the two are compared loosely.
inline bool payees_agree(const std::string& in_book, const std::string& in_book_raw,
                         const std::string& in_file,
                         const std::vector<payees::Rule>& rules) {
    if (!in_book_raw.empty() && loose_payee(in_book_raw) == loose_payee(in_file))
        return true;
    const payees::Applied named = payees::apply(rules, in_file);
    return loose_payee(in_book) == loose_payee(named.payee);
}

// One posting already in the book, in the account being imported into, with
// whatever is needed to decide a claim against it.
struct Existing {
    // What the transaction is, as opposed to what it is labelled. Anything that
    // acts on a claim acts through this: a ref is a label and two transactions
    // can share one, so finding a transaction by ref would act on whichever
    // came first.
    std::string id;
    std::string ref;
    types::Date date;
    Money amount;
    std::string payee;
    // What the file wrote when this was imported, which is what the next
    // reading of the same statement is compared against. Empty on anything
    // typed by hand.
    std::string raw_name;
    std::string check_no;
    // What this posting is called by the kind of file being read, if
    // anything. An identifier from any other kind of file is not consulted: a
    // CSV reference would not match a FITID, and treating it as one would make
    // every download look new. See an identifier is only an identifier to the
    // file that gave it, in ImportOfx.spectable.
    std::string source_id;
    types::ClearedStatus cleared = types::ClearedStatus::Uncleared;
    // Set once a row has settled against it. A transaction already in the book
    // settles once: two identical rows against two identical transactions are
    // two duplicates, not one transaction recognised twice. Without this the
    // test answers the same way however many there are, and the second coffee
    // of the day is silently lost.
    bool claimed = false;
};

// Three ways into what the account already holds, built once per import
// instead of scanned once per row. A book of twenty years and a download of a
// hundred rows was four scans of the book per row, each comparison normalising
// a payee, which is the whole of why an import of nothing took a noticeable
// time. Every bucket is in book order, which is the order a scan would have
// found them in, so the transaction a test settles against is the same one.
struct Index {
    explicit Index(const std::vector<Existing>& rows) {
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (!rows[i].source_id.empty()) by_source_id.emplace(rows[i].source_id, i);
            if (!rows[i].check_no.empty()) by_check_no[rows[i].check_no].push_back(i);
            by_fingerprint[fingerprint(rows[i].date, rows[i].amount)].push_back(i);
        }
    }

    // An identifier is unique within the account, so the first wins and a
    // second is a book somebody edited by hand.
    std::unordered_map<std::string, std::size_t> by_source_id;
    std::unordered_map<std::string, std::vector<std::size_t>> by_check_no;
    std::unordered_map<std::string, std::vector<std::size_t>> by_fingerprint;

    static const std::vector<std::size_t>& bucket(
            const std::unordered_map<std::string, std::vector<std::size_t>>& in,
            const std::string& key) {
        static const std::vector<std::size_t> none;
        const auto at = in.find(key);
        return at == in.end() ? none : at->second;
    }
};

}  // namespace detail

// What the account already holds, in the form the decision needs. The
// identifier read is the one for the kind of file being imported: see the
// source_id field.
inline std::vector<detail::Existing> existing_in(
        const chart::Chart& accounts,
        const std::vector<ledger::Transaction>& transactions,
        const std::string& account,
        types::ImportSource source = types::ImportSource::Ofx) {
    std::vector<detail::Existing> out;
    const chart::Ref account_ref = accounts.ref_for_path(account);
    for (const ledger::Transaction& t : transactions) {
        if (t.source_account != account_ref) continue;
        detail::Existing one;
        one.id = t.id.value();
        one.ref = t.ref.value();
        one.date = t.date;
        one.amount = t.amount;
        one.payee = t.payee.value();
        one.raw_name = t.raw_name;
        one.check_no = t.check_no.value();
        if (const std::string* id = ledger::id_from(t, source))
            one.source_id = *id;
        one.cleared = t.cleared;
        out.push_back(one);
    }
    return out;
}

// Five days of lag between the day a payment was entered and the day the bank
// posted it. The Ofx importer no longer reaches across that: inside an import
// all it has is an amount and a date, and reaching days on that much is
// guessing, so a same-day likeness is offered to be answered and anything
// further is a second transaction. The window belongs to the questions asked
// of two dates from two different places -- the Csv importer, which has no
// identifier to work with, and the pass that pairs one register's transaction
// against another's once an import has been accepted. See the date-window rule
// in ImportOfx.spectable.
inline constexpr long long kMatchWindow = 5;

// A check number is the strongest thing in a bank file -- it is written on the
// check itself, so the book and the download are naming one piece of paper --
// but only for as long as the number means what it says. Check numbers are
// reused: a new book of checks starts where the old one left off, and over
// twenty years the same number comes round again. So a number settles a match
// only within a day, and beyond that it is treated as coincidence. The window
// is a setting on the book; this is only the default a new book starts with.
inline constexpr long long kCheckWindow = 1;

// Which SimpleAccount account a downloaded account number belongs to. Recorded
// once, on the first import, and used silently thereafter. See the account-
// mapping rule in ImportOfx.spectable.
struct AccountMapping {
    std::string bank_id;
    std::string ofx_account_id;
    std::string account_path;
};

// Only the digits, because the account's number is kept as somebody wrote it
// -- 0012-3456, or a card number in groups of four -- and the file gives
// nothing but digits.
inline std::string digits_of(const std::string& text) {
    std::string out;
    for (char c : text)
        if (c >= '0' && c <= '9') out += c;
    return out;
}

// Four digits is the floor. Three digits is a month or a sequence number, and
// matching an account on three digits would put a statement in the wrong
// account for a living. The same floor the file-name rule uses.
inline constexpr std::size_t kNumberFloor = 4;

// Equal digits is the ordinary case. Beyond that, the digits of one ending the
// digits of the other count, which is what lets an account labelled with the
// last four of a card meet the full number a download carries.
inline bool numbers_agree(const std::string& from_file, const std::string& on_account) {
    const std::string a = digits_of(from_file);
    const std::string b = digits_of(on_account);
    if (a.empty() || b.empty()) return false;
    if (a == b) return true;
    const std::string& longer = a.size() > b.size() ? a : b;
    const std::string& shorter = a.size() > b.size() ? b : a;
    if (shorter.size() < kNumberFloor) return false;
    return longer.compare(longer.size() - shorter.size(), shorter.size(), shorter) == 0;
}

// The routing numbers have to agree whenever both are stated. A card statement
// states none, and an account that has not been told its bank's id states
// none; neither of those is a disagreement, because nothing was claimed.
inline bool bank_ids_agree(const std::string& from_file, const std::string& on_account) {
    if (from_file.empty() || on_account.empty()) return true;
    return digits_of(from_file) == digits_of(on_account);
}

// One account saying yes. See "When the number in a download is the number on
// an account" in ImportOfx.spectable.
inline bool account_matches(const std::string& statement_bank_id,
                            const std::string& statement_account_id,
                            const std::string& account_bank_id,
                            const std::string& account_number) {
    return bank_ids_agree(statement_bank_id, account_bank_id) &&
           numbers_agree(statement_account_id, account_number);
}

// Which account in the chart a download belongs in. One account saying yes is
// the account it goes to; none, and the import asks. Two is also asked, because
// a number that names two accounts has named neither -- so the answer is empty
// in both of those cases and the caller cannot tell them apart, which is
// right: the dialog is the same dialog.
inline std::string place_statement(const chart::Chart& accounts,
                                   const std::string& bank_id,
                                   const std::string& account_id) {
    std::string found;
    int saying_yes = 0;
    for (const chart::Account& a : accounts.all()) {
        if (a.placeholder) continue;
        if (!accounts.is_real(accounts.ref_for_path(a.path.value()))) continue;
        if (!account_matches(bank_id, account_id, a.bank_id, a.account_number)) continue;
        found = a.path.value();
        ++saying_yes;
    }
    return saying_yes == 1 ? found : std::string();
}

// What the first download confirmed for an account tells it. Only what is
// blank is filled in: a number a person typed is what the account dialog shows
// them, and a download that disagreed with it would be rewriting a label they
// chose, in a place they were not looking. So a disagreement leaves the
// account alone and says so, and the account dialog is where it is settled.
struct Teaching {
    bool learned = false;
    std::vector<Note> notes;
};

inline Teaching teach_account(chart::Chart* accounts, const std::string& path,
                              const std::string& bank_id,
                              const std::string& account_id) {
    Teaching out;
    const chart::Account* was = accounts->find(path);
    if (was == nullptr) return out;
    chart::Account now = *was;
    if (!bank_id.empty()) {
        if (now.bank_id.empty()) {
            now.bank_id = bank_id;
            out.learned = true;
        } else if (!bank_ids_agree(bank_id, now.bank_id)) {
            out.notes.push_back(Note{"BankIdDiffers",
                                     path + " says " + now.bank_id +
                                         " and the download says " + bank_id});
        }
    }
    if (!account_id.empty()) {
        if (now.account_number.empty()) {
            now.account_number = account_id;
            out.learned = true;
        } else if (!numbers_agree(account_id, now.account_number)) {
            out.notes.push_back(Note{"AccountNumberDiffers",
                                     path + " says " + now.account_number +
                                         " and the download says " + account_id});
        }
    }
    if (out.learned) accounts->put(now);
    return out;
}

inline const AccountMapping* mapping_for(const std::vector<AccountMapping>& mappings,
                                         const std::string& bank_id,
                                         const std::string& ofx_account_id) {
    for (const AccountMapping& m : mappings)
        if (m.bank_id == bank_id && m.ofx_account_id == ofx_account_id) return &m;
    return nullptr;
}

// A multi-file import names its account by OfxAccountId alone, since a single
// combined table has no room for a BankId per row; the id is unique enough on
// its own for that purpose. Empty where nothing maps it, which is how an
// account not yet set up is told apart without asking mid-batch.
inline std::string account_for_id(const std::vector<AccountMapping>& mappings,
                                  const std::string& ofx_account_id) {
    for (const AccountMapping& m : mappings)
        if (m.ofx_account_id == ofx_account_id) return m.account_path;
    return std::string();
}

// Two halves of one electronic transfer are a day or two apart at most, and
// every extra day is another chance to pair two payments that were never one
// transfer. Narrower than the window for matching a download against something
// entered by hand, and the two are deliberately separate: see the two-windows
// rule in ImportOfx.spectable.
inline constexpr long long kTransferWindow = 2;

// The tests in order; the first that applies decides. Tests two to four have no
// identifier behind them, so each claims at most one of what is already there.
// There is no search here for the other half of a payment already in the book.
// There was, and it looked only at a shortlist of accounts named by the payee --
// which meant a payment nobody had recorded a payee for was never found at all.
// The search is now as wide as the chart and its answer is asked for rather
// than assumed, which cannot be done while a row is being judged: see
// transfers::halves_for and the transfer question in UserInterface.spectable.

// One row of any kind of file, reduced to what the matching rules ask about.
// Written down once so the OFX and CSV importers decide the same way: the only
// difference between them is that a CSV usually has nothing for source_id, and
// a test that has nothing to work on simply does not apply.
struct Candidate {
    types::Date date{2024, 1, 1};
    Money amount;
    std::string payee;
    std::string check_no;
    std::string source_id;
};

// What one row came to, before anything is done about it.
struct Judgement {
    Disposition disposition = Disposition::New;
    std::string claimed_id;
    std::string claimed_ref;
    // Every transaction the row was decided against, which is more than one
    // only for a possible duplicate.
    std::vector<std::string> claim_ids;
    std::vector<DuplicateNote> notes;
};

// Tests one to four of the disposition rule, over a whole file at once --
// which it has to be, because tests three and four are a count and a count is
// not a property of one row. A row the tests do not settle comes back New, and
// what the caller does with it then is its own business: the OFX importer asks
// whether it completes a payment already here, and the CSV importer does not.
//
// `already` is modified: a transaction that has been settled against is marked
// so the next row cannot settle against it too.
//
// See how a downloaded transaction is identified, and how many rows match how
// many transactions in ImportCsv.spectable, which is the same rule read from
// the side of a file without identifiers.
inline std::vector<Judgement> judge(std::vector<detail::Existing>* already,
                                    const std::vector<Candidate>& rows,
                                    bool is_checking,
                                    const std::vector<payees::Rule>& rules,
                                    long long check_window,
                                    std::vector<Note>* notes) {
    std::vector<Judgement> out(rows.size());
    std::vector<bool> settled(rows.size(), false);
    const detail::Index index(*already);

    const auto note_once = [](Judgement* j, DuplicateNote n) {
        for (DuplicateNote one : j->notes)
            if (one == n) return;
        j->notes.push_back(n);
    };
    // What any duplicate may have to say, after whatever settled it: that the
    // import has cleared something a person typed, and that it has written an
    // identifier where there was none. Both are things the import changed, so
    // both are worth a note; neither changes the answer.
    const auto and_what_changed = [&](Judgement* j, const detail::Existing& e,
                                      const Candidate& row) {
        if (e.cleared == types::ClearedStatus::Uncleared)
            note_once(j, DuplicateNote::Uncleared);
        if (!row.source_id.empty() && e.source_id.empty())
            note_once(j, DuplicateNote::FitIdAdded);
    };

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Candidate& row = rows[i];

        // 1. A transaction in this account already carries this row's
        // identifier, from a file of this kind. Unique within the account, so
        // it needs no counting: a second read of one file does nothing.
        if (!row.source_id.empty()) {
            const auto at = index.by_source_id.find(row.source_id);
            if (at != index.by_source_id.end()) {
                detail::Existing* same = &(*already)[at->second];
                same->claimed = true;
                out[i].disposition = Disposition::Duplicate;
                out[i].claimed_id = same->id;
                out[i].claimed_ref = same->ref;
                out[i].claim_ids.push_back(same->id);
                // The identifier is the bank's own word for this transaction,
                // so it settles the question whatever else disagrees -- and if
                // something does disagree, that is worth saying.
                if (same->date != row.date ||
                    same->amount.cents() != row.amount.cents() ||
                    !detail::payees_agree(same->payee, same->raw_name, row.payee,
                                          rules))
                    note_once(&out[i], DuplicateNote::DetailsDiffer);
                and_what_changed(&out[i], *same, row);
                settled[i] = true;
                continue;
            }
        }

        // 2. The same check number, amount and payee, within the book's
        // check-number window. A check number is written on the check itself,
        // so the book and the file are naming one piece of paper -- but only
        // of a checking account, because a card reference is not a check
        // number, and only for as long as the number means what it says.
        if (!is_checking || row.check_no.empty()) continue;
        const auto same_check = [&](const detail::Existing& e) {
            return !e.claimed && !e.check_no.empty() && e.check_no == row.check_no &&
                   e.amount.cents() == row.amount.cents() &&
                   detail::payees_agree(e.payee, e.raw_name, row.payee, rules);
        };
        const std::vector<std::size_t>& numbered =
            detail::Index::bucket(index.by_check_no, row.check_no);
        detail::Existing* found = nullptr;
        for (std::size_t at : numbered) {
            detail::Existing& e = (*already)[at];
            if (same_check(e) &&
                std::abs(types::Date::days_between(e.date, row.date)) <= check_window) {
                found = &e;
                break;
            }
        }
        if (found != nullptr) {
            found->claimed = true;
            out[i].disposition = Disposition::Duplicate;
            out[i].claimed_id = found->id;
            out[i].claimed_ref = found->ref;
            out[i].claim_ids.push_back(found->id);
            note_once(&out[i], DuplicateNote::CheckNumberSame);
            and_what_changed(&out[i], *found, row);
            settled[i] = true;
            continue;
        }

        // The number matched and the day did not. Either the check really took
        // that long to clear, in which case the book is about to hold two of
        // it, or the number came round again and creating one is right.
        // Nothing here can tell those apart, so it does not try.
        const detail::Existing* stale = nullptr;
        for (std::size_t at : numbered)
            if (same_check((*already)[at])) {
                stale = &(*already)[at];
                break;
            }
        if (stale != nullptr && notes != nullptr)
            notes->push_back(Note{
                "CheckNumberStale",
                "Check " + row.check_no + " is dated " + stale->date.iso() +
                    " here and " + row.date.iso() + " in the file"});
    }

    // 3 and 4. One test counted two ways, over the date and the amount, in
    // this account, across what tests one and two have not already settled.
    // Where the file has as many as the book does, every one is a duplicate.
    // Where it does not, every one is a question: nothing says which row goes
    // with which transaction, and deciding either way doubles a transaction or
    // loses one.
    std::vector<std::string> keys;
    std::unordered_map<std::string, std::vector<std::size_t>> grouped;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (settled[i]) continue;
        const std::string key = detail::fingerprint(rows[i].date, rows[i].amount);
        std::vector<std::size_t>& group = grouped[key];
        if (group.empty()) keys.push_back(key);
        group.push_back(i);
    }
    for (const std::string& key : keys) {
        const std::vector<std::size_t>& group = grouped[key];
        std::vector<detail::Existing*> here;
        for (std::size_t at : detail::Index::bucket(index.by_fingerprint, key))
            if (!(*already)[at].claimed) here.push_back(&(*already)[at]);

        // Nothing like it is here, so this is not the test that decides.
        if (here.empty()) continue;

        if (here.size() == group.size()) {
            for (std::size_t k = 0; k < group.size(); ++k) {
                detail::Existing& e = *here[k];
                Judgement& j = out[group[k]];
                const Candidate& row = rows[group[k]];
                e.claimed = true;
                j.disposition = Disposition::Duplicate;
                j.claimed_id = e.id;
                j.claimed_ref = e.ref;
                j.claim_ids.push_back(e.id);
                // The payee is not part of the key, so it cannot refuse the
                // duplicate -- the same date and the same amount in one
                // account is enough to say this is the same transaction. What
                // it decides is whether the book's name for it is worth a
                // second look.
                if (!detail::payees_agree(e.payee, e.raw_name, row.payee, rules))
                    note_once(&j, DuplicateNote::PayeeUnmatched);
                and_what_changed(&j, e, row);
                settled[group[k]] = true;
            }
            continue;
        }

        // The counts disagree. Every row of the group is offered, including
        // the one a pairing would have settled: it is no better evidenced than
        // the one the pairing would have left over. Nothing is claimed, since
        // nothing has been settled -- what each row points at is the first
        // transaction like it, so a person can be shown what the question is
        // about.
        for (std::size_t i : group) {
            out[i].disposition = Disposition::Possible;
            out[i].claimed_id = here.front()->id;
            out[i].claimed_ref = here.front()->ref;
            // All of them, not just the first: the question the row asks is
            // which of these it is, and it cannot be answered by somebody
            // shown only one.
            for (const detail::Existing* e : here) out[i].claim_ids.push_back(e->id);
            settled[i] = true;
        }
    }
    return out;
}

// The chart is needed to tell a payment from a payment to nobody: whether a
// payee names an account this book keeps is the question that decides whether
// the other side is an account or a category.
inline Imported decide(const Statement& statement, const std::string& account,
                       const std::vector<ledger::Transaction>& transactions,
                       const chart::Chart& accounts,
                       long long check_window = kCheckWindow,
                       const std::vector<payees::Rule>& rules = {}) {
    Imported out;
    std::vector<detail::Existing> already =
        existing_in(accounts, transactions, account, types::ImportSource::Ofx);
    // Check numbers are asked about only here. Nothing else in a book is paid
    // by check, and a card reference in the CheckNum field is not a number
    // written on anything.
    bool is_checking = false;
    if (const chart::Account* mine = accounts.find(account))
        is_checking = mine->type == types::AccountType::Checking;

    std::vector<Candidate> rows;
    for (const Transaction& downloaded : statement.transactions) {
        Candidate row;
        row.date = downloaded.date_posted;
        row.amount = downloaded.amount;
        row.payee = downloaded.name;
        row.check_no = downloaded.check_num;
        row.source_id = downloaded.fit_id;
        rows.push_back(row);
    }
    const std::vector<Judgement> judged =
        judge(&already, rows, is_checking, rules, check_window, &out.notes);

    for (std::size_t i = 0; i < statement.transactions.size(); ++i) {
        const Transaction& downloaded = statement.transactions[i];
        Decided decided;
        decided.downloaded = downloaded;
        decided.disposition = judged[i].disposition;
        decided.claimed_id = judged[i].claimed_id;
        decided.claimed_ref = judged[i].claimed_ref;
        decided.claim_ids = judged[i].claim_ids;
        decided.notes = judged[i].notes;

        if (decided.disposition == Disposition::Duplicate) {
            ++out.summary.Duplicate;
            out.decided.push_back(decided);
            continue;
        }
        if (decided.disposition == Disposition::Possible) {
            ++out.summary.Possible;
            out.decided.push_back(decided);
            continue;
        }

        // 5. Nothing above applies. A row that is the other half of a payment
        // already in the book is one of these: on the evidence of this file it
        // cannot be told from money that simply left, so it is an ordinary new
        // row with a category, and the pairing is asked about after the review
        // is prepared rather than decided here.
        decided.disposition = Disposition::New;
        ++out.summary.New;
        out.decided.push_back(decided);
    }
    return out;
}

// The transactions to add for what was decided New. Nothing else is created:
// a duplicate adds nothing, and a match clears what is already there rather
// than writing a second copy of it.
//
// The other side goes to Uncategorized, chosen by the sign, until a payee rule
// or the user says otherwise.
inline std::vector<ledger::Transaction> transactions_for(
        const Imported& decided, const std::string& account,
        std::size_t already_in_book, chart::Chart* accounts,
        const std::vector<payees::Rule>& rules = {},
        const std::vector<payees::Conversion>& conversions = {},
        const std::vector<payees::Categorisation>& categories = {}) {
    std::vector<ledger::Transaction> out;
    for (const Decided& one : decided.decided) {
        if (one.disposition != Disposition::New) continue;
        const Money amount = one.downloaded.amount;
        // Chosen by the sign of the side that is known, which is the bank's own
        // amount: money in is income, money out is an expense. Passing its
        // opposite put every payment under Income and every deposit under
        // Expenses, which reads plausibly in a summary and is backwards.
        // What the two tables and the rules make of the name the bank sent: both
        // payee fields, and where any of them says so the category to post the
        // other side to. A rule or a categorisation that names one is the user
        // having answered this question once already, so it is not asked again.
        const payees::Naming named =
            payees::name_for(rules, conversions, categories, one.downloaded.name);

        // A row a rule has categorised goes where the rule says; anything else
        // goes to a category chosen by the sign. Nothing is held in a staging
        // account: a payment whose other side is not known yet is not a
        // different kind of thing from one whose category is not known yet, and
        // it is a finished transaction either way.
        std::string other = ledger::uncategorized_for(amount).value();
        if (!named.category.empty()) other = named.category;
        accounts->add(types::AccountPath(other),
                      amount.cents() < 0 ? types::AccountType::Expense
                                         : types::AccountType::Income);

        ledger::Transaction t;
        t.id = ledger::new_id();
        t.ref = types::TransactionRef(
            "T" + std::to_string(already_in_book + out.size() + 1));
        t.date = one.downloaded.date_posted;
        t.payee = types::PayeeName(named.payee);
        t.raw_name = named.raw_name;
        t.check_no = types::CheckNumber(one.downloaded.check_num);
        t.memo = one.downloaded.memo;

        t.source_account = accounts->ref_for_path(account);
        t.destination = accounts->ref_for_path(other);
        t.amount = amount;
        t.cleared = types::ClearedStatus::Cleared;
        // Named for the kind of file that gave it, which is what makes a
        // second import of this file a duplicate rather than a possible one.
        ledger::stamp(&t, types::ImportSource::Ofx, one.downloaded.fit_id);
        out.push_back(t);
    }
    return out;
}

// Filling in the account a payment went to, on the transaction that was
// waiting for it, and adding this account's own row of the pair -- a
// transaction is one account's own download, so the other half of a payment
// that only now arrived is a row of its own, stamped with this file's
// identifier, not a rewrite of the one already there.
//
// The payee of the existing row is not touched. Each side of a payment has
// its own word for it -- a bank says CHASEBANK, a card says PAYMENT RECEIVED
// -- and the first is both the more useful and the one already on the
// screen, so overwriting it would be a surprise with nothing to recommend it.
struct Completed {
    int completed = 0;
    std::vector<ledger::Transaction> created;
};

inline Completed apply_completions(const chart::Chart& accounts, const Imported& decided,
                                   const std::string& account,
                                   std::size_t already_in_book,
                                   std::vector<ledger::Transaction>* transactions) {
    Completed out;
    const chart::Ref account_ref = accounts.ref_for_path(account);
    for (const Decided& one : decided.decided) {
        if (one.disposition != Disposition::Completed) continue;
        for (ledger::Transaction& t : *transactions) {
            if (t.id.value() != one.claimed_id) continue;
            t.destination = account_ref;

            ledger::Transaction mine;
            mine.id = ledger::new_id();
            mine.ref = types::TransactionRef(
                "T" + std::to_string(already_in_book + out.created.size() + 1));
            mine.date = one.downloaded.date_posted;
            mine.payee = types::PayeeName(one.downloaded.name);
            mine.raw_name = one.downloaded.name;
            mine.check_no = types::CheckNumber(one.downloaded.check_num);
            mine.memo = one.downloaded.memo;
            mine.source_account = account_ref;
            mine.destination = t.source_account;
            mine.amount = -t.amount;
            mine.cleared = types::ClearedStatus::Cleared;
            ledger::stamp(&mine, types::ImportSource::Ofx, one.downloaded.fit_id);
            out.created.push_back(mine);

            ++out.completed;
            break;
        }
    }
    return out;
}

// A duplicate creates nothing. The transaction already in the book is the
// transaction, and the import brings it up to date rather than replacing it:
// the book's date, payee, category, memo and check number are kept, because a
// history brought over from Quicken has twenty years of categorising in it and
// a download knows none of it.
//
// Cleared is the exception, because cleared is not something a person chose --
// it is the bank saying the transaction happened, and the row being read is the
// bank saying it. The identifier is written for the source that gave it and
// alongside any the transaction already has, which is what lets one
// transaction be recognised by a QFX after a CSV.
//
// The stamp is also what makes the difference on a second import. Without it
// the same row reaches the same transaction by counting every time the file is
// read, which works but asks the question again the moment anything about the
// book changes.
//
// See what a duplicate does to the transaction already there, and what the
// note on a duplicate says, in ImportOfx.spectable.
inline int apply_duplicates(const chart::Chart& accounts, const Imported& decided,
                            const std::string& account,
                            std::vector<ledger::Transaction>* transactions) {
    int cleared = 0;
    const chart::Ref account_ref = accounts.ref_for_path(account);
    for (const Decided& one : decided.decided) {
        if (one.disposition != Disposition::Duplicate) continue;
        for (ledger::Transaction& t : *transactions) {
            if (t.id.value() != one.claimed_id || t.source_account != account_ref) continue;
            t.cleared = types::ClearedStatus::Cleared;
            ledger::stamp(&t, types::ImportSource::Ofx, one.downloaded.fit_id);
            for (DuplicateNote note : one.notes) {
                const std::string name = to_string(note);
                if (std::find(t.import_notes.begin(), t.import_notes.end(), name) ==
                    t.import_notes.end())
                    t.import_notes.push_back(name);
            }
            ++cleared;
        }
    }
    return cleared;
}

// What to do with a row the import could not settle. There are two answers and
// both are ordinary; see the rule in ImportOfx.spectable.
// What the import says when it is done. Only what happened is listed, so a
// clean import reads "4 new" rather than naming three kinds of nothing, and a
// note is never buried in a row of zeroes.
inline std::string completion_of(const Imported& result) {
    std::vector<std::string> parts;
    const auto count = [&](int many, const std::string& what) {
        if (many > 0) parts.push_back(std::to_string(many) + " " + what);
    };
    count(result.summary.New, "new");
    count(result.summary.Duplicate, "already here");
    count(result.summary.Completed, "completed");
    count(result.summary.Possible, "to answer");
    if (!result.notes.empty())
        parts.push_back(std::to_string(result.notes.size()) +
                        (result.notes.size() == 1 ? " note to look at" : " notes to look at"));
    if (parts.empty()) return "nothing to import";
    std::string out = parts.front();
    for (std::size_t i = 1; i < parts.size(); ++i) out += ", " + parts[i];
    return out;
}

enum class Answer { TheSame, Different };

// Answering that the offered row is the transaction already in the book. The one
// already there is the transaction; the download only tells us the bank has seen
// it, so it is cleared and stamped and nothing is created.
//
// The stamp is the point: without it the same file asks the same question for
// ever, and a transaction that has already been settled goes on offering itself.
inline bool answer_the_same(const chart::Chart& accounts, const Decided& offered,
                            const std::string& account,
                            std::vector<ledger::Transaction>* transactions) {
    const chart::Ref account_ref = accounts.ref_for_path(account);
    for (ledger::Transaction& t : *transactions) {
        if (t.id.value() != offered.claimed_id || t.source_account != account_ref) continue;
        t.cleared = types::ClearedStatus::Cleared;
        ledger::stamp(&t, types::ImportSource::Ofx, offered.downloaded.fit_id);
        return true;
    }
    return false;
}

// Answering that it is a second transaction the book does not have. Created like
// any new row, and it carries the identifier while the one already there does
// not -- so a later file naming it is a duplicate of this one only.
inline ledger::Transaction answer_different(const Decided& offered,
                                            const std::string& account,
                                            std::size_t already_in_book,
                                            chart::Chart* accounts) {
    Imported one;
    Decided as_new = offered;
    as_new.disposition = Disposition::New;
    as_new.claimed_id.clear();
    as_new.claimed_ref.clear();
    as_new.claim_ids.clear();
    one.decided.push_back(as_new);
    const std::vector<ledger::Transaction> made =
        transactions_for(one, account, already_in_book, accounts);
    return made.empty() ? ledger::Transaction() : made.front();
}

// ---------------------------------------------------------------------------
// Two downloaded transactions that are one transfer. See the pairing rules in
// ImportOfx.spectable: neither half is already in the book, both halves are
// new in the same import, and recognising them as one is a different problem
// from matching a download against something entered by hand.
// ---------------------------------------------------------------------------

// kTransferWindow, used by the pairing below, is declared earlier in this
// file -- it is the same two-day window the payment-completion rule already
// uses, and the two were kept as one constant on purpose.

enum class TransferCandidateState { Proposed, Approved, Rejected, Automatic };

inline std::string to_string(TransferCandidateState s) {
    switch (s) {
        case TransferCandidateState::Proposed:  return "Proposed";
        case TransferCandidateState::Approved:  return "Approved";
        case TransferCandidateState::Rejected:  return "Rejected";
        case TransferCandidateState::Automatic: return "Automatic";
    }
    return "Proposed";
}

inline TransferCandidateState transfer_candidate_state_from(const std::string& s) {
    if (s == "Approved")  return TransferCandidateState::Approved;
    if (s == "Rejected")  return TransferCandidateState::Rejected;
    if (s == "Automatic") return TransferCandidateState::Automatic;
    return TransferCandidateState::Proposed;
}

// One proposed pairing, with everything a person needs to judge it.
struct TransferCandidate {
    std::string from_account;
    types::Date from_date{2026, 1, 1};
    std::string from_name;
    std::string from_fit_id;
    std::string to_account;
    types::Date to_date{2026, 1, 1};
    std::string to_name;
    std::string to_fit_id;
    Money amount;
    long long days_apart = 0;
    TransferCandidateState state = TransferCandidateState::Proposed;
};

// A remembered approval: these two accounts, these two payee texts, matched by
// Contains against the name as a payee rule is. No amount, because an autopay
// is a different figure every month and that is the whole point of
// remembering it.
struct TransferMatchRule {
    std::string from_account;
    std::string from_pattern;
    std::string to_account;
    std::string to_pattern;
    bool enabled = true;
};

// Everything needed to turn a proposed pairing into one transaction, or into
// two, without having to search the book again.
struct TransferProposal {
    TransferCandidate candidate;
    std::string from_account;
    Transaction from_row;
    std::string to_account;
    Transaction to_row;
    // Which side's name becomes the transaction's payee: whichever file's row
    // was read first, same as the payment-completion rule above -- the first
    // side's word for it is both the more useful one and the one already on
    // the screen.
    bool from_read_first = true;
};

inline bool transfer_rule_matches(const TransferMatchRule& rule, const std::string& from_account,
                                  const std::string& from_name, const std::string& to_account,
                                  const std::string& to_name) {
    if (!rule.enabled) return false;
    if (rule.from_account != from_account || rule.to_account != to_account) return false;
    if (detail::fold(from_name).find(detail::fold(rule.from_pattern)) == std::string::npos)
        return false;
    if (detail::fold(to_name).find(detail::fold(rule.to_pattern)) == std::string::npos)
        return false;
    return true;
}

// One real account's rows from one combined import, grouped by the account
// they resolved to.
struct AccountRows {
    std::string account;
    std::vector<Transaction> rows;
};

struct MultiImported {
    Summary summary;
    std::vector<Note> notes;
    // Ready to write: the unpaired new rows, and any pair a remembered rule
    // already approved. Nothing here is a candidate any more.
    std::vector<ledger::Transaction> ready;
    // Every pairing found, proposed or already automatic, for the dialog and
    // for the approve/reject steps to act on by account and amount.
    std::vector<TransferProposal> candidates;
};

// Deciding several statements at once: each account's own duplicate, match
// and possible-duplicate tests run exactly as a single statement's would, and
// what is left over -- new rows nothing else claimed -- is checked for a
// transfer before any of it becomes a transaction of its own.
inline MultiImported decide_multi(const std::vector<AccountRows>& by_account,
                                  std::vector<ledger::Transaction>* transactions,
                                  chart::Chart* accounts,
                                  const std::vector<TransferMatchRule>& rules = {}) {
    MultiImported out;

    struct Pending {
        std::string account;
        Transaction row;
        std::size_t order = 0;
    };
    std::vector<Pending> news;
    std::size_t order_counter = 0;

    for (const AccountRows& ar : by_account) {
        // An account id nothing has mapped yet contributes nothing: there is
        // no account to decide against and no account to pair, and asking is
        // a different step from this one.
        if (ar.account.empty()) { order_counter += ar.rows.size(); continue; }
        Statement statement;
        statement.transactions = ar.rows;
        const Imported one = decide(statement, ar.account, *transactions, *accounts);
        out.summary.Duplicate += one.summary.Duplicate;
        out.summary.Possible += one.summary.Possible;
        out.summary.Completed += one.summary.Completed;
        for (const Note& n : one.notes) out.notes.push_back(n);
        apply_duplicates(*accounts, one, ar.account, transactions);
        const Completed done_completing =
            apply_completions(*accounts, one, ar.account, transactions->size(), transactions);
        for (const ledger::Transaction& t : done_completing.created)
            transactions->push_back(t);
        for (const Decided& d : one.decided) {
            if (d.disposition == Disposition::New)
                news.push_back({ar.account, d.downloaded, order_counter});
            ++order_counter;
        }
    }

    // Pair across different accounts, equal and opposite, within the transfer
    // window, closest gap first; a half once paired is not offered again.
    std::vector<bool> used(news.size(), false);
    struct Pair { std::size_t i; std::size_t j; long long gap; types::Date earliest; };
    std::vector<Pair> pairs;
    // Both sides are real accounts, which is condition 2 of the rule and not
    // only a different-account check: a mapping that points a download at a
    // category gives a half there is nothing to transfer to, and pairing it
    // would propose a transfer into an expense. A path the chart has not heard
    // of is an account this import is about to make, so it is left alone.
    const auto pairable = [accounts](const std::string& path) {
        const auto ref = accounts->ref_for_path(path);
        return ref.is_none() || accounts->is_real(ref);
    };
    for (std::size_t i = 0; i < news.size(); ++i) {
        if (!pairable(news[i].account)) continue;
        for (std::size_t j = i + 1; j < news.size(); ++j) {
            if (news[i].account == news[j].account) continue;
            if (!pairable(news[j].account)) continue;
            if (news[i].row.amount.cents() != -news[j].row.amount.cents()) continue;
            const long long gap = std::abs(
                types::Date::days_between(news[i].row.date_posted, news[j].row.date_posted));
            if (gap > kTransferWindow) continue;
            const types::Date earliest = news[i].row.date_posted < news[j].row.date_posted
                                             ? news[i].row.date_posted
                                             : news[j].row.date_posted;
            pairs.push_back({i, j, gap, earliest});
        }
    }
    std::stable_sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) {
        if (a.gap != b.gap) return a.gap < b.gap;
        return a.earliest < b.earliest;
    });

    for (const Pair& pc : pairs) {
        if (used[pc.i] || used[pc.j]) continue;
        used[pc.i] = used[pc.j] = true;
        const bool i_is_from = news[pc.i].row.amount.cents() < 0;
        const Pending& from_p = i_is_from ? news[pc.i] : news[pc.j];
        const Pending& to_p = i_is_from ? news[pc.j] : news[pc.i];

        TransferProposal prop;
        prop.from_account = from_p.account;
        prop.from_row = from_p.row;
        prop.to_account = to_p.account;
        prop.to_row = to_p.row;
        prop.from_read_first = from_p.order < to_p.order;

        TransferCandidate c;
        c.from_account = from_p.account;
        c.from_date = from_p.row.date_posted;
        c.from_name = from_p.row.name;
        c.from_fit_id = from_p.row.fit_id;
        c.to_account = to_p.account;
        c.to_date = to_p.row.date_posted;
        c.to_name = to_p.row.name;
        c.to_fit_id = to_p.row.fit_id;
        c.amount = from_p.row.amount.cents() < 0 ? -from_p.row.amount : from_p.row.amount;
        c.days_apart = pc.gap;
        c.state = TransferCandidateState::Proposed;
        prop.candidate = c;

        bool automatic = false;
        for (const TransferMatchRule& r : rules) {
            if (transfer_rule_matches(r, prop.from_account, prop.from_row.name,
                                      prop.to_account, prop.to_row.name)) {
                automatic = true;
                break;
            }
        }
        if (automatic) {
            prop.candidate.state = TransferCandidateState::Automatic;
            const std::size_t already_in_book = transactions->size() + out.ready.size();

            ledger::Transaction from_t;
            from_t.id = ledger::new_id();
            from_t.ref = types::TransactionRef("T" + std::to_string(already_in_book + 1));
            from_t.date = prop.from_row.date_posted;
            from_t.payee = types::PayeeName(prop.from_row.name);
            from_t.memo = prop.from_row.memo;
            from_t.source_account = accounts->ref_for_path(prop.from_account);
            from_t.destination = accounts->ref_for_path(prop.to_account);
            from_t.amount = prop.from_row.amount;
            from_t.cleared = types::ClearedStatus::Cleared;
            ledger::stamp(&from_t, types::ImportSource::Ofx, prop.from_row.fit_id);

            ledger::Transaction to_t;
            to_t.id = ledger::new_id();
            to_t.ref = types::TransactionRef("T" + std::to_string(already_in_book + 2));
            to_t.date = prop.to_row.date_posted;
            to_t.payee = types::PayeeName(prop.to_row.name);
            to_t.memo = prop.to_row.memo;
            to_t.source_account = accounts->ref_for_path(prop.to_account);
            to_t.destination = accounts->ref_for_path(prop.from_account);
            to_t.amount = prop.to_row.amount;
            to_t.cleared = types::ClearedStatus::Cleared;
            ledger::stamp(&to_t, types::ImportSource::Ofx, prop.to_row.fit_id);

            out.ready.push_back(from_t);
            out.ready.push_back(to_t);
            out.summary.New += 1;
        }
        out.candidates.push_back(prop);
    }

    // Unpaired new rows become ordinary transactions, exactly as a single
    // statement would create them.
    for (std::size_t i = 0; i < news.size(); ++i) {
        if (used[i]) continue;
        Imported one;
        Decided d;
        d.downloaded = news[i].row;
        d.disposition = Disposition::New;
        one.decided.push_back(d);
        const std::vector<ledger::Transaction> made = transactions_for(
            one, news[i].account, transactions->size() + out.ready.size(), accounts);
        for (const ledger::Transaction& t : made) {
            out.ready.push_back(t);
            ++out.summary.New;
        }
    }
    return out;
}

// Turning a proposed pairing into the two transactions it describes, one for
// each account: both real, both cleared, each carrying the identifier from
// the file that described it, each naming the other as Destination. Dated
// and named from the side the money left, unless the other side's file was
// read first -- see TransferProposal::from_read_first.
inline std::pair<ledger::Transaction, ledger::Transaction> merge_transfer(
        chart::Chart* accounts, const TransferProposal& p, std::size_t already_in_book) {
    ledger::Transaction from_t;
    from_t.id = ledger::new_id();
    from_t.ref = types::TransactionRef("T" + std::to_string(already_in_book + 1));
    from_t.date = p.from_row.date_posted;
    from_t.payee = types::PayeeName(p.from_row.name);
    from_t.memo = p.from_row.memo;
    from_t.source_account = accounts->ref_for_path(p.from_account);
    from_t.destination = accounts->ref_for_path(p.to_account);
    from_t.amount = p.from_row.amount;
    from_t.cleared = types::ClearedStatus::Cleared;
    ledger::stamp(&from_t, types::ImportSource::Ofx, p.from_row.fit_id);

    ledger::Transaction to_t;
    to_t.id = ledger::new_id();
    to_t.ref = types::TransactionRef("T" + std::to_string(already_in_book + 2));
    to_t.date = p.to_row.date_posted;
    to_t.payee = types::PayeeName(p.to_row.name);
    to_t.memo = p.to_row.memo;
    to_t.source_account = accounts->ref_for_path(p.to_account);
    to_t.destination = accounts->ref_for_path(p.from_account);
    to_t.amount = p.to_row.amount;
    to_t.cleared = types::ClearedStatus::Cleared;
    ledger::stamp(&to_t, types::ImportSource::Ofx, p.to_row.fit_id);

    return {from_t, to_t};
}

// Rejecting a pairing: each side becomes its own ordinary new transaction,
// with the Uncategorized or Unassigned side it would have had if it had
// never been proposed at all.
inline std::vector<ledger::Transaction> split_transfer(const TransferProposal& p,
                                                        std::size_t already_in_book,
                                                        chart::Chart* accounts) {
    std::vector<ledger::Transaction> out;
    for (const auto& side : {std::make_pair(p.from_account, p.from_row),
                             std::make_pair(p.to_account, p.to_row)}) {
        Imported one;
        Decided d;
        d.downloaded = side.second;
        d.disposition = Disposition::New;
        one.decided.push_back(d);
        const std::vector<ledger::Transaction> made =
            transactions_for(one, side.first, already_in_book + out.size(), accounts);
        for (const ledger::Transaction& t : made) out.push_back(t);
    }
    return out;
}

// The rule offered after an approval. Not learned automatically: whether a
// recurring pairing should always be approved is a judgement about the
// accounts, and the offer is made, not taken.
inline TransferMatchRule rule_offered_for(const TransferCandidate& c) {
    TransferMatchRule r;
    r.from_account = c.from_account;
    r.from_pattern = c.from_name;
    r.to_account = c.to_account;
    r.to_pattern = c.to_name;
    r.enabled = true;
    return r;
}

// What the import says before anything is written: a batch of decisions is
// one, and the candidates, the duplicates and the matches are all shown
// together before it is committed.
struct ImportPreview {
    int new_transactions = 0;
    int duplicates = 0;
    int matched_to_existing = 0;
    int transfer_candidates = 0;
    bool committed = false;
};

inline ImportPreview preview_of(const MultiImported& m) {
    ImportPreview p;
    p.new_transactions = m.summary.New;
    p.duplicates = m.summary.Duplicate;
    // What joined itself to something already in the book: the payment halves,
    // now that a duplicate is counted as a duplicate.
    p.matched_to_existing = m.summary.Completed;
    int pending = 0;
    for (const TransferProposal& c : m.candidates)
        if (c.candidate.state == TransferCandidateState::Proposed) ++pending;
    p.transfer_candidates = pending;
    p.committed = false;
    return p;
}

// Checking an import's result against what the bank itself says the balance
// is. Only what has cleared counts: a check written but not yet presented is
// in the register and not at the bank, and counting it would make every
// import look wrong. See the ledger-balance rule in ImportOfx.spectable.
struct LedgerBalanceResult {
    Money cleared_balance;
    Money stated_bal;
    Money difference;
    bool agrees = false;
};

inline LedgerBalanceResult check_ledger_balance(
        const chart::Chart& accounts,
        const std::vector<ledger::Transaction>& transactions, const std::string& account,
        const Money& stated_bal) {
    const chart::Ref account_ref = accounts.ref_for_path(account);
    Money cleared;
    for (const ledger::Transaction& t : transactions)
        if (t.source_account == account_ref && t.cleared == types::ClearedStatus::Cleared)
            cleared += t.amount;
    LedgerBalanceResult r;
    r.cleared_balance = cleared;
    r.stated_bal = stated_bal;
    r.difference = cleared - stated_bal;
    r.agrees = r.difference.cents() == 0;
    return r;
}

}  // namespace ofx
