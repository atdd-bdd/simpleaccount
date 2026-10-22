#pragma once
#include <algorithm>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>
#include "chart.h"
#include "date.h"
#include "money.h"
#include "transaction.h"

// Finding the other half of a transfer among the transactions the book already
// has.
//
// A row being imported and a transaction already recorded can be the two sides
// of one payment, and the amount and the date are the whole of the evidence for
// it. That is weaker evidence than a payee recorded against the account in
// advance would have been, which is why nothing here decides anything: it
// finds the halves that could pair and the user is asked about each. See the
// rule about a half already in the book in ImportOfx.spectable and the transfer
// question in UserInterface.spectable.
namespace transfers {

// How far apart the two sides of one payment are allowed to be. The autopay in
// testdata left the checking account two days after it reached the card, which
// is the pair that sets this.
inline constexpr long long kWindow = 2;

// One half already in the book that could be the other side of a row being
// imported. Read off that transaction rather than guessed at, because the
// question names it back to the person who entered it.
struct Half {
    std::string transaction_id;
    std::string account;
    types::Date date;
    std::string payee;
    long long days_apart = 0;
};

// The book arranged by the one thing a pairing needs exactly: the amount, to
// the cent. A transfer is equal and opposite, so a row of -91.40 can only pair
// with +91.40, and asking that question of every transaction in the book -- for
// every row of a download -- was the cost that made an import of a hundred rows
// take a noticeable time over a book of twenty years. Built once per import and
// then asked per row.
struct Book {
    explicit Book(const std::vector<ledger::Transaction>& existing) : all(&existing) {
        for (std::size_t i = 0; i < existing.size(); ++i)
            by_cents[existing[i].amount.cents()].push_back(i);
    }

    std::unordered_map<long long, std::vector<std::size_t>> by_cents;
    const std::vector<ledger::Transaction>* all = nullptr;

    const std::vector<std::size_t>& opposite(long long cents) const {
        static const std::vector<std::size_t> none;
        const auto at = by_cents.find(-cents);
        return at == by_cents.end() ? none : at->second;
    }
};

// Every half already in the book that could be the other side of one row:
// equal and opposite to the cent, in some other real account, within the
// window, and not already paired with anything. Closest in days first, so the
// likeliest pairing is the one asked about first.
//
// The payee is not consulted. Narrowing by payee is what the earlier rule did,
// and the cost of it was every payment nobody had recorded a payee for -- which
// is most of them, and was the case that sent that rule back to be rewritten.
inline std::vector<Half> halves_for(const std::string& into, const types::Date& date,
                                    const Money& amount, const Book& book,
                                    const chart::Chart& accounts,
                                    long long window = kWindow) {
    std::vector<Half> out;
    // Nothing is equal and opposite to nothing, and a row for no money is not
    // half of a payment.
    if (amount.cents() == 0) return out;
    for (std::size_t at : book.opposite(amount.cents())) {
        const ledger::Transaction& t = (*book.all)[at];
        // A transfer has two accounts. Two rows of one account that happen to
        // be equal and opposite are a payment and a refund, not a transfer.
        if (!accounts.is_real(t.source_account)) continue;
        const std::string mine = accounts.path_for_ref(t.source_account);
        if (mine.empty() || mine == into) continue;
        const long long gap = std::abs(types::Date::days_between(t.date, date));
        if (gap > window) continue;
        // Its other side already names an account rather than a category, so
        // this half is spoken for and asking about it again would unpick a
        // pairing somebody has already made.
        if (accounts.is_real(t.destination)) continue;
        Half half;
        half.transaction_id = t.id.value();
        half.account = mine;
        half.date = t.date;
        half.payee = t.payee.value();
        half.days_apart = gap;
        out.push_back(half);
    }
    std::stable_sort(out.begin(), out.end(), [](const Half& a, const Half& b) {
        return a.days_apart < b.days_apart;
    });
    return out;
}

// The same question asked of a book nobody has indexed yet, which is every
// caller with one row to ask about: a register line being edited, and the
// scenarios. One row over one index costs what the scan it replaces cost.
inline std::vector<Half> halves_for(const std::string& into, const types::Date& date,
                                    const Money& amount,
                                    const std::vector<ledger::Transaction>& existing,
                                    const chart::Chart& accounts,
                                    long long window = kWindow) {
    return halves_for(into, date, amount, Book(existing), accounts, window);
}

// The same question asked from the register instead of from an import: the
// destination column has just been given a real account, and that account may
// already have its own transaction for this payment. So it is halves_for
// narrowed to the one account named, which is the only one the person said
// anything about.
//
// A category is the whole answer and is never asked about, and Unassigned is
// not an account yet -- it is the question of which account, so pairing against
// it would be answering one question with another. Both are silent, because a
// dialog that says "there is nothing to ask you" is worse than no dialog. See
// the destination-column rule in UserInterface.spectable.
inline std::vector<Half> halves_in(const std::string& named, const std::string& own,
                                   const types::Date& date, const Money& amount,
                                   const std::vector<ledger::Transaction>& existing,
                                   const chart::Chart& accounts,
                                   long long window = kWindow) {
    std::vector<Half> out;
    if (named.empty() || named == ledger::kUnassigned) return out;
    if (!accounts.is_real(accounts.ref_for_path(named))) return out;
    for (const Half& half : halves_for(own, date, amount, existing, accounts, window))
        if (half.account == named) out.push_back(half);
    return out;
}

// Choosing one of them. A transfer is one transaction naming both accounts, so
// the pairing is a merge and not a link: the transaction being edited keeps its
// own date, payee and memo and the other one is gone. What it does not keep to
// itself is the cleared status -- a half the bank has reported stays reported,
// so the one that is left is cleared if either was -- or the identifier a
// download gave the other half, which is kept so that re-reading that file
// still finds this payment.
inline bool merge(const std::string& keeping, const std::string& half,
                  std::vector<ledger::Transaction>* transactions) {
    ledger::Transaction* keeper = nullptr;
    for (ledger::Transaction& t : *transactions)
        if (t.id.value() == keeping) keeper = &t;
    if (keeper == nullptr) return false;

    auto gone = transactions->begin();
    for (; gone != transactions->end(); ++gone)
        if (gone->id.value() == half) break;
    if (gone == transactions->end() || gone->id.value() == keeping) return false;

    if (static_cast<int>(gone->cleared) > static_cast<int>(keeper->cleared))
        keeper->cleared = gone->cleared;
    for (const ledger::ImportId& id : gone->import_ids)
        keeper->import_ids.push_back(id);
    transactions->erase(gone);
    return true;
}

// What the person said to one question. Yes and YesAndRemember both make the
// transfer; the difference is whether the pair of payees is written down, so
// that the next download of the same autopay is not asked about again. No is
// about the row rather than about the payee: two payments that happen to be
// equal and opposite are not evidence of anything to remember.
enum class Answer { Yes, No, YesAndRemember };

inline std::string to_string(Answer answer) {
    switch (answer) {
        case Answer::Yes:            return "Yes";
        case Answer::No:             return "No";
        case Answer::YesAndRemember: return "YesAndRemember";
    }
    return "Yes";
}

inline Answer answer_from_string(const std::string& text) {
    if (text == "No") return Answer::No;
    if (text == "YesAndRemember") return Answer::YesAndRemember;
    return Answer::Yes;
}

// The question as it stands: the two halves, what they come to, and how far
// apart they are. Shown is false when there is nothing to ask and after the
// last question has been answered, so that the window can be driven from this
// alone. See "What the transfer question says" in UserInterface.spectable.
struct Question {
    bool shown = true;
    std::string from_account;
    types::Date from_date;
    std::string from_payee;
    std::string to_account;
    types::Date to_date;
    std::string to_payee;
    Money amount;
    long long days_apart = 0;
};

inline const char* kQuestionHeading = "Are these the same payment";
inline const char* kQuestionAnswers = "Yes; No; Yes, and do not ask again for these two";

// The lines of it, in the order they are read. Named here rather than in the
// window so that a line cannot be added to the specification without one being
// added to the program.
inline std::vector<std::string> question_lines() {
    return {"Heading", "FirstHalf", "OtherHalf", "Gap", "Answers"};
}

// One half, as the question names it back: the account it is in, the day it
// fell on, and whatever payee was recorded against it. All three are quoted
// from the book or from the file -- none of it is this program's reasoning
// repeated back -- and a half with no payee reads as the two that are left
// rather than as a blank nobody can account for.
inline std::string half_reads(const std::string& account, const types::Date& date,
                              const std::string& payee) {
    std::string out = account + "  " + date.iso();
    if (!payee.empty()) out += "  " + payee;
    return out;
}

inline std::string gap_reads(long long days) {
    if (days == 0) return "The same day";
    if (days == 1) return "1 day apart";
    return std::to_string(days) + " days apart";
}

// The whole of what the window shows, by line. An unknown line reads as
// nothing, which is how the program says it has not been told about it.
inline std::string line_reads(const Question& question, const std::string& line) {
    if (line == "Heading") return kQuestionHeading;
    if (line == "FirstHalf")
        return half_reads(question.from_account, question.from_date, question.from_payee);
    if (line == "OtherHalf")
        return half_reads(question.to_account, question.to_date, question.to_payee);
    if (line == "Gap") return gap_reads(question.days_apart);
    if (line == "Answers") return kQuestionAnswers;
    return std::string();
}

}  // namespace transfers
