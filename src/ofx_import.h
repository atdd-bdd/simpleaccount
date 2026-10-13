#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "chart.h"
#include "ledger.h"
#include "money.h"
#include "ofx_reader.h"
#include "payee_rules.h"
#include "posting.h"
#include "transaction_id.h"

// Deciding what to do with each downloaded transaction: create it, recognise it
// as one already imported, match it to one entered by hand, or offer it as a
// possible duplicate. See ImportOfx.spectable and the claim-pairing rule in
// ImportCsv.spectable.
namespace ofx {

// Completes is the fifth: this row is the other half of a payment already in the
// book, waiting with its account unassigned. Nothing is created and nothing is
// cleared on this row's behalf -- the transaction already there gains an account
// and this file's identifier. See the payment rule in ImportOfx.spectable.
enum class Disposition { New, Duplicate, Matched, Possible, Completes };

inline std::string to_string(Disposition d) {
    switch (d) {
        case Disposition::New:       return "New";
        case Disposition::Duplicate: return "Duplicate";
        case Disposition::Matched:   return "Matched";
        case Disposition::Possible:  return "Possible";
        case Disposition::Completes: return "Completes";
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
    // Whether this row's other side should wait in Unassigned rather than go to
    // a category: true only where something in the book could be the other half.
    bool other_side_waits = false;
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
        const chart::Chart& accounts,
        const std::vector<ledger::Transaction>& transactions,
        const std::string& account) {
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
        one.check_no = t.check_no.value();
        if (const std::string* id = ledger::id_from(t, types::ImportSource::Ofx))
            one.ofx_id = *id;
        one.cleared = t.cleared;
        out.push_back(one);
    }
    return out;
}

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
// The accounts a payment with this payee could be paying. One payee can name
// several -- several cards paid from one bank all say CHASEBANK -- so this
// narrows the search and decides nothing. Empty where the payee names no
// account, which means the row is not a payment at all: the money went to
// somebody the book does not keep, and its other side is a category.
inline std::vector<std::string> accounts_paid_by(const chart::Chart& accounts,
                                                 const std::string& payee) {
    std::vector<std::string> out;
    if (payee.empty()) return out;
    for (const chart::Account& a : accounts.all()) {
        if (a.payment_payee.empty()) continue;
        if (detail::fold(a.payment_payee) == detail::fold(payee))
            out.push_back(a.path.value());
    }
    return out;
}

// A transaction already in the book whose Destination is waiting to be
// filled in with this row's account. Equal and opposite amount, within the
// transfer window.
struct Waiting {
    std::string transaction_id;
    bool found = false;
};

inline Waiting waiting_for(const chart::Chart& accounts,
                           const std::vector<ledger::Transaction>& transactions,
                           const Transaction& downloaded, const std::string& account) {
    Waiting out;
    const chart::Ref account_ref = accounts.ref_for_path(account);
    for (const ledger::Transaction& t : transactions) {
        // The row being imported describes one account's own side; the side
        // that is waiting is the other account's, so a transaction already
        // naming this account as its own is this row's own transaction and
        // not a half to complete.
        if (t.source_account == account_ref) continue;
        if (!ledger::is_unassigned(accounts, t.destination)) continue;
        if (t.amount.cents() != -downloaded.amount.cents()) continue;
        if (std::abs(types::Date::days_between(t.date, downloaded.date_posted))
            > kTransferWindow)
            continue;
        out.transaction_id = t.id.value();
        out.found = true;
        return out;
    }
    return out;
}

// The chart is needed to tell a payment from a payment to nobody: whether a
// payee names an account this book keeps is the question that decides whether
// the other side is an account or a category.
inline Imported decide(const Statement& statement, const std::string& account,
                       const std::vector<ledger::Transaction>& transactions,
                       const chart::Chart& accounts,
                       long long check_window = kCheckWindow) {
    Imported out;
    std::vector<detail::Existing> already = existing_in(accounts, transactions, account);
    // What a payment to the account being imported into is called elsewhere, so
    // a card's own statement can recognise a payment received as a payment.
    std::string statement_payee;
    if (const chart::Account* mine = accounts.find(account))
        statement_payee = mine->payment_payee;

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
                       <= check_window;
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

        // 5. A transaction already here is waiting for this side. The other
        // half of a payment, downloaded from the other account's statement on
        // another day -- which is the ordinary way a payment arrives, since the
        // two sides are two files read whenever each is downloaded.
        //
        // Only asked where the payee names an account this book keeps, or where
        // this row's own account is one such. Without that, two unrelated
        // payments of the same amount in the same week would complete each
        // other, and the narrowing is the whole protection against it.
        const bool could_be_a_payment =
            !accounts_paid_by(accounts, downloaded.name).empty() ||
            !accounts_paid_by(accounts, statement_payee).empty();
        if (could_be_a_payment) {
            const Waiting waiting = waiting_for(accounts, transactions, downloaded, account);
            if (waiting.found) {
                decided.disposition = Disposition::Completes;
                decided.claimed_id = waiting.transaction_id;
                // Counted as a match: it joined this row to a transaction
                // already here, which is what Matched means to a reader. The
                // disposition stays its own thing because the action differs --
                // a match clears a posting, a completion fills in an account.
                ++out.summary.Matched;
                out.decided.push_back(decided);
                continue;
            }
        }

        // 6. Nothing above applies.
        decided.disposition = Disposition::New;
        // The other side is Unassigned only where something in the book could be
        // the other half. Otherwise the money went to somebody the book does not
        // keep, and that is a category -- a paycheck is not a failed transfer.
        decided.other_side_waits = could_be_a_payment;
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
        const std::vector<payees::Rule>& rules = {}) {
    std::vector<ledger::Transaction> out;
    for (const Decided& one : decided.decided) {
        if (one.disposition != Disposition::New) continue;
        const Money amount = one.downloaded.amount;
        // Chosen by the sign of the side that is known, which is the bank's own
        // amount: money in is income, money out is an expense. Passing its
        // opposite put every payment under Income and every deposit under
        // Expenses, which reads plausibly in a summary and is backwards.
        // What the rules make of the name the bank sent: the payee to record and,
        // where a rule says so, the category to post the other side to. A rule
        // that names a category is the user having answered this question once
        // already, so it is not asked again.
        const payees::Applied named = payees::apply(rules, one.downloaded.name);

        // A payment waits in Unassigned for the account it went to; a row a rule
        // has categorised goes where the rule says; anything else goes to a
        // category chosen by the sign.
        std::string other = one.other_side_waits
                                ? ledger::unassigned().value()
                                : ledger::uncategorized_for(amount).value();
        if (!one.other_side_waits && !named.category.empty()) other = named.category;
        accounts->add(types::AccountPath(other),
                      one.other_side_waits
                          ? types::AccountType::OtherAsset
                          : (amount.cents() < 0 ? types::AccountType::Expense
                                                : types::AccountType::Income));

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
        if (one.disposition != Disposition::Completes) continue;
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

// A match is not a new transaction: the one entered by hand is the transaction,
// and what the download adds is the knowledge that the bank has seen it. So the
// posting in that account is marked cleared and stamped with the identifier.
//
// The stamp is what makes the difference on a second import. Without it the
// same row matches the same hand-entered transaction again every time the file
// is read, and a transaction that has already been reconciled goes on offering
// itself for ever.
inline int apply_matches(const chart::Chart& accounts, const Imported& decided,
                         const std::string& account,
                         std::vector<ledger::Transaction>* transactions) {
    int cleared = 0;
    const chart::Ref account_ref = accounts.ref_for_path(account);
    for (const Decided& one : decided.decided) {
        if (one.disposition != Disposition::Matched) continue;
        for (ledger::Transaction& t : *transactions) {
            if (t.id.value() != one.claimed_id || t.source_account != account_ref) continue;
            t.cleared = types::ClearedStatus::Cleared;
            ledger::stamp(&t, types::ImportSource::Ofx, one.downloaded.fit_id);
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
        out.summary.Matched += one.summary.Matched;
        out.summary.Possible += one.summary.Possible;
        for (const Note& n : one.notes) out.notes.push_back(n);
        apply_matches(*accounts, one, ar.account, transactions);
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
    for (std::size_t i = 0; i < news.size(); ++i) {
        for (std::size_t j = i + 1; j < news.size(); ++j) {
            if (news[i].account == news[j].account) continue;
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
    p.matched_to_existing = m.summary.Matched;
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
