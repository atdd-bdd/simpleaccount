#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "chart.h"
#include "ledger.h"
#include "money.h"
#include "ofx_reader.h"
#include "posting.h"
#include "transaction_id.h"

// Deciding what to do with each downloaded transaction: create it, recognise it
// as one already imported, match it to one entered by hand, or offer it as a
// possible duplicate. See ImportOfx.spectable and the claim-pairing rule in
// ImportCsv.spectable.
namespace ofx {

enum class Disposition { New, Duplicate, Matched, Possible };

inline std::string to_string(Disposition d) {
    switch (d) {
        case Disposition::New:       return "New";
        case Disposition::Duplicate: return "Duplicate";
        case Disposition::Matched:   return "Matched";
        case Disposition::Possible:  return "Possible";
    }
    return "New";
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
};

struct Summary {
    int New = 0;
    int Duplicate = 0;
    int Matched = 0;
    int Possible = 0;
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

// The date, amount and payee taken together, folded. It stands in for the FITID
// that a file without identifiers does not have; see the Fingerprint term in
// ImportCsv.spectable.
inline std::string fingerprint(const types::Date& on, const Money& amount,
                               const std::string& payee) {
    return on.iso() + "|" + std::to_string(amount.cents()) + "|" + fold(payee);
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
    std::string check_no;
    // What this posting is called by an OFX file, if anything. An identifier
    // from any other kind of file is not consulted here: it would not match a
    // FITID, and treating it as one would make every download look new.
    std::string ofx_id;
    types::ClearedStatus cleared = types::ClearedStatus::Uncleared;
    // Set once something has claimed it. A transaction already in the book may
    // be claimed once: the first matching row takes it and the next finds
    // nothing left. Without this the test answers the same way however many
    // there are, and the second coffee of the day is silently lost.
    bool claimed = false;
};

}  // namespace detail

// What the account already holds, in the form the decision needs. The FITID is
// carried on the posting in the statement's own account.
inline std::vector<detail::Existing> existing_in(
        const std::vector<ledger::Transaction>& transactions,
        const std::string& account) {
    std::vector<detail::Existing> out;
    for (const ledger::Transaction& t : transactions) {
        for (const ledger::Posting& p : t.postings) {
            if (p.account.value() != account) continue;
            detail::Existing one;
            one.id = t.id.value();
            one.ref = t.ref.value();
            one.date = t.date;
            one.amount = p.amount;
            one.payee = t.payee.value();
            one.check_no = t.check_no.value();
            if (const std::string* id = ledger::id_from(p, types::ImportSource::Ofx))
                one.ofx_id = *id;
            one.cleared = p.cleared;
            out.push_back(one);
        }
    }
    return out;
}

inline constexpr long long kMatchWindow = 5;

// A check number is the strongest thing in a bank file -- it is written on the
// check itself, so the book and the download are naming one piece of paper --
// but only for as long as the number means what it says. Check numbers are
// reused: a new book of checks starts where the old one left off, and over
// twenty years the same number comes round again. So a number settles a match
// only within a day, and beyond that it is treated as coincidence.
inline constexpr long long kCheckWindow = 1;

// The tests in order; the first that applies decides. Tests two to four have no
// identifier behind them, so each claims at most one of what is already there.
inline Imported decide(const Statement& statement, const std::string& account,
                       const std::vector<ledger::Transaction>& transactions) {
    Imported out;
    std::vector<detail::Existing> already = existing_in(transactions, account);

    for (const Transaction& downloaded : statement.transactions) {
        Decided decided;
        decided.downloaded = downloaded;

        // 1. The same FITID in this account. Unique within the account, so it
        // needs no claiming: a second import of one file does nothing.
        if (!downloaded.fit_id.empty()) {
            const auto same = std::find_if(already.begin(), already.end(),
                [&](const detail::Existing& e) { return e.ofx_id == downloaded.fit_id; });
            if (same != already.end()) {
                decided.disposition = Disposition::Duplicate;
                decided.claimed_id = same->id;
                decided.claimed_ref = same->ref;
                ++out.summary.Duplicate;
                out.decided.push_back(decided);
                continue;
            }
        }

        const auto unclaimed = [&](const detail::Existing& e) { return !e.claimed; };

        // 2. An uncleared posting with the same amount and check number, dated
        // within a day: the check written by hand, now reached the bank.
        auto found = already.end();
        if (!downloaded.check_num.empty()) {
            const auto same_number = [&](const detail::Existing& e) {
                return unclaimed(e) && e.cleared == types::ClearedStatus::Uncleared &&
                       e.amount.cents() == downloaded.amount.cents() &&
                       !e.check_no.empty() && e.check_no == downloaded.check_num;
            };
            const auto within_a_day = [&](const detail::Existing& e) {
                return std::abs(types::Date::days_between(e.date, downloaded.date_posted))
                       <= kCheckWindow;
            };
            found = std::find_if(already.begin(), already.end(),
                [&](const detail::Existing& e) { return same_number(e) && within_a_day(e); });

            // The number matched and the day did not. Either the check really
            // took that long to clear, in which case the book is about to hold
            // two of it, or the number came round again and creating one is
            // right. Nothing here can tell those apart, so it does not try.
            if (found == already.end()) {
                const auto stale = std::find_if(already.begin(), already.end(), same_number);
                if (stale != already.end())
                    out.notes.push_back(Note{
                        "CheckNumberStale",
                        "Check " + downloaded.check_num + " is dated " + stale->date.iso() +
                            " here and " + downloaded.date_posted.iso() + " in the file"});
            }
        }
        // 3. An uncleared posting with the same amount within five days.
        if (found == already.end()) {
            found = std::find_if(already.begin(), already.end(),
                [&](const detail::Existing& e) {
                    return unclaimed(e) && e.cleared == types::ClearedStatus::Uncleared &&
                           e.amount.cents() == downloaded.amount.cents() &&
                           std::abs(types::Date::days_between(e.date, downloaded.date_posted))
                               <= kMatchWindow;
                });
        }
        if (found != already.end()) {
            found->claimed = true;
            decided.disposition = Disposition::Matched;
            decided.claimed_id = found->id;
            decided.claimed_ref = found->ref;
            ++out.summary.Matched;
            out.decided.push_back(decided);
            continue;
        }

        // 4. Something with the same fingerprint already there: offered rather
        // than skipped, because two genuine visits to one shop on one day for
        // one amount are not rare.
        const std::string print =
            detail::fingerprint(downloaded.date_posted, downloaded.amount, downloaded.name);
        found = std::find_if(already.begin(), already.end(),
            [&](const detail::Existing& e) {
                return unclaimed(e) && e.ofx_id.empty() &&
                       detail::fingerprint(e.date, e.amount, e.payee) == print;
            });
        if (found != already.end()) {
            found->claimed = true;
            decided.disposition = Disposition::Possible;
            decided.claimed_id = found->id;
            decided.claimed_ref = found->ref;
            ++out.summary.Possible;
            out.decided.push_back(decided);
            continue;
        }

        // 5. Nothing above applies.
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
        std::size_t already_in_book, chart::Chart* accounts) {
    std::vector<ledger::Transaction> out;
    for (const Decided& one : decided.decided) {
        if (one.disposition != Disposition::New) continue;
        const Money amount = one.downloaded.amount;
        // Chosen by the sign of the side that is known, which is the bank's own
        // amount: money in is income, money out is an expense. Passing its
        // opposite put every payment under Income and every deposit under
        // Expenses, which reads plausibly in a summary and is backwards.
        const std::string other = ledger::uncategorized_for(amount).value();
        accounts->add(types::AccountPath(other),
                      amount.cents() < 0 ? types::AccountType::Expense
                                         : types::AccountType::Income);

        ledger::Transaction t;
        t.id = ledger::new_id();
        t.ref = types::TransactionRef(
            "T" + std::to_string(already_in_book + out.size() + 1));
        t.date = one.downloaded.date_posted;
        t.payee = types::PayeeName(one.downloaded.name);
        t.check_no = types::CheckNumber(one.downloaded.check_num);
        t.memo = one.downloaded.memo;

        ledger::Posting here;
        here.account = types::AccountPath(account);
        here.amount = amount;
        // Kept on the posting in the account the statement is for, and named
        // for the kind of file that gave it, which is what makes a second
        // import of this file a duplicate rather than a possible one.
        ledger::stamp(&here, types::ImportSource::Ofx, one.downloaded.fit_id);
        here.cleared = types::ClearedStatus::Cleared;
        ledger::Posting there;
        there.account = types::AccountPath(other);
        there.amount = -amount;
        t.postings = {here, there};
        out.push_back(t);
    }
    return out;
}

// A match is not a new transaction: the one entered by hand is the transaction,
// and what the download adds is the knowledge that the bank has seen it. So the
// posting in that account is marked cleared and stamped with the identifier.
//
// The stamp is what makes the difference on a second import. Without it the
// same row matches the same hand-entered transaction again every time the file
// is read, and a transaction that has already been reconciled goes on offering
// itself for ever.
inline int apply_matches(const Imported& decided, const std::string& account,
                         std::vector<ledger::Transaction>* transactions) {
    int cleared = 0;
    for (const Decided& one : decided.decided) {
        if (one.disposition != Disposition::Matched) continue;
        for (ledger::Transaction& t : *transactions) {
            if (t.id.value() != one.claimed_id) continue;
            for (ledger::Posting& p : t.postings) {
                if (p.account.value() != account) continue;
                p.cleared = types::ClearedStatus::Cleared;
                ledger::stamp(&p, types::ImportSource::Ofx, one.downloaded.fit_id);
                ++cleared;
            }
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
    count(result.summary.Matched, "matched");
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
inline bool answer_the_same(const Decided& offered, const std::string& account,
                            std::vector<ledger::Transaction>* transactions) {
    for (ledger::Transaction& t : *transactions) {
        if (t.id.value() != offered.claimed_id) continue;
        for (ledger::Posting& p : t.postings) {
            if (p.account.value() != account) continue;
            p.cleared = types::ClearedStatus::Cleared;
            ledger::stamp(&p, types::ImportSource::Ofx, offered.downloaded.fit_id);
            return true;
        }
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
    one.decided.push_back(as_new);
    const std::vector<ledger::Transaction> made =
        transactions_for(one, account, already_in_book, accounts);
    return made.empty() ? ledger::Transaction() : made.front();
}

}  // namespace ofx
