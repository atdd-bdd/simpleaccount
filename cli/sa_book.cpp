// Driving a book from the command line: make one, add accounts, enter
// transactions, read the registers and the reports back.
//
// This is not a convenience wrapper round the window. It is how a sequence of
// scenarios is run without one -- make a book, add accounts, enter
// transactions, import a file, check the figures -- so the behaviour can be
// scripted and compared. The window is a second way of reaching the same
// src/ code, not the only way.
//
//   sa_book list
//   sa_book new      <book>
//   sa_book show     <book>
//   sa_book account  <book> <path> <Type> [opening] [YYYY-MM-DD]
//   sa_book entry    <book> <YYYY-MM-DD> <payee> <account> <category> <amount>
//   sa_book register <book> <account>
//   sa_book import   <book> <file.qfx> <account> [--accept] [--same <id>...]
//                                               [--different <id>...]
//   sa_book delete   <book> <ref>
//   sa_book report   <book> <from> <to> [--quicken]
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <fstream>
#include <sstream>

#include "ofx_import.h"
#include "register_lines.h"
#include "report.h"
#include "store_sqlite.h"
#include "transaction_id.h"

namespace {

int usage() {
    std::fprintf(stderr,
        "usage:\n"
        "  sa_book list\n"
        "  sa_book new      <book>\n"
        "  sa_book show     <book>\n"
        "  sa_book account  <book> <path> <Type> [opening] [YYYY-MM-DD]\n"
        "  sa_book entry    <book> <YYYY-MM-DD> <payee> <account> <category> <amount>\n"
        "  sa_book register <book> <account>\n"
        "  sa_book import   <book> <file.qfx> <account> [--accept]\n"
        "                   [--same <id>] [--different <id>]   resolve a Possible\n"
        "  sa_book delete   <book> <ref>\n"
        "  sa_book report   <book> <from> <to> [--quicken]\n"
        "\nBooks live in %s\n", store::books_folder().c_str());
    return 2;
}

int complain(const store::Failure& no) {
    std::fprintf(stderr, "%s\n", no.reason.c_str());
    return 1;
}

// Every command but new and list opens the book and reads it whole. A book of
// this size is read in a blink, and the alternative is a second way to be wrong
// about what is in it.
struct Loaded {
    store::Book book;
    chart::Chart accounts;
    ledger::Ledger ledger;
    std::vector<ledger::Transaction> transactions;
};

bool load(const std::string& name, Loaded* into, store::Failure* no) {
    *no = store::Book::open_named(name, &into->book);
    if (no->refused) return false;
    *no = into->book.read(&into->accounts, &into->ledger, &into->transactions);
    return !no->refused;
}

std::string next_ref(const std::vector<ledger::Transaction>& transactions) {
    return "T" + std::to_string(transactions.size() + 1);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string command = argv[1];

    if (command == "list") {
        const std::vector<std::string> names = store::book_names();
        if (names.empty()) {
            std::printf("no books in %s\n", store::books_folder().c_str());
            return 0;
        }
        for (const std::string& name : names) std::printf("%s\n", name.c_str());
        return 0;
    }

    if (command == "new") {
        if (argc < 3) return usage();
        const std::string name = argv[2];
        store::Failure no = store::ensure_folder();
        if (no.refused) return complain(no);
        store::Book book;
        no = store::Book::create(store::path_for(name), name, &book);
        if (no.refused) return complain(no);
        std::printf("made %s\n", book.path().c_str());
        return 0;
    }

    if (argc < 3) return usage();
    const std::string name = argv[2];
    Loaded open;
    store::Failure no;
    if (!load(name, &open, &no)) return complain(no);

    if (command == "show") {
        const store::BookInfo info = open.book.info();
        std::printf("name         %s\n", info.name.c_str());
        std::printf("file         %s\n", open.book.path().c_str());
        std::printf("accounts     %zu\n", open.accounts.all().size());
        std::printf("transactions %zu\n", open.transactions.size());
        // Every posting of every transaction sums to zero, so the book as a
        // whole does. Printed because it is the one number that says the file
        // is trustworthy at all.
        Money total;
        for (const ledger::DatedPosting& p : open.ledger.postings()) total += p.amount;
        std::printf("balances     %s\n", total.cents() == 0 ? "yes" : "NO");
        if (!open.accounts.all().empty()) {
            std::printf("\n%-44s %-11s %14s\n", "account", "type", "balance");
            for (const chart::Account& a : open.accounts.all()) {
                if (a.placeholder) continue;
                const Money held = open.ledger.display_balance(a.path.value(), a.type);
                std::printf("%-44s %-11s %14s\n", a.path.value().c_str(),
                            types::to_string(a.type).c_str(), held.in_register().c_str());
            }
        }
        return 0;
    }

    if (command == "account") {
        if (argc < 5) return usage();
        const std::string path = argv[3];
        types::AccountType kind;
        try {
            kind = types::account_type_from_string(argv[4]);
        } catch (const std::exception& bad) {
            std::fprintf(stderr, "%s\n", bad.what());
            return 2;
        }
        const chart::Rejection refused =
            open.accounts.add(types::AccountPath(path), kind);
        if (refused.refused) {
            std::fprintf(stderr, "%s\n", refused.reason.c_str());
            return 1;
        }
        // An opening balance is a transaction against Equity, so the book
        // balances from the account's first day rather than starting out of it.
        if (argc > 5) {
            const Money stated{argv[5]};
            if (stated.cents() != 0) {
                const std::string on = argc > 6 ? argv[6] : "2026-01-01";
                const auto when = types::Date::from_iso(on);
                if (!when) {
                    std::fprintf(stderr, "%s is not a date\n", on.c_str());
                    return 2;
                }
                open.accounts.add(types::AccountPath("Equity:Opening Balances"),
                                  types::AccountType::Equity);
                ledger::Transaction t;
                t.id = ledger::new_id();
                t.ref = types::TransactionRef(next_ref(open.transactions));
                t.date = *when;
                t.payee = types::PayeeName("Opening Balance");
                for (const ledger::DatedPosting& p : ledger::Ledger::opening_balance_postings(
                         types::AccountPath(path), kind, stated, *when)) {
                    ledger::Posting posting;
                    posting.account = p.account;
                    posting.amount = p.amount;
                    t.postings.push_back(posting);
                }
                open.transactions.push_back(t);
            }
        }
        no = open.book.write(open.accounts, open.transactions);
        if (no.refused) return complain(no);
        std::printf("added %s\n", path.c_str());
        return 0;
    }

    if (command == "entry") {
        if (argc < 8) return usage();
        const auto when = types::Date::from_iso(argv[3]);
        if (!when) {
            std::fprintf(stderr, "%s is not a date\n", argv[3]);
            return 2;
        }
        const std::string account = argv[5];
        const std::string category = argv[6];
        const Money amount{argv[7]};
        if (!open.accounts.has(account)) {
            std::fprintf(stderr, "There is no account named %s\n", account.c_str());
            return 1;
        }
        if (!open.accounts.has(category)) {
            std::fprintf(stderr, "There is no account named %s\n", category.c_str());
            return 1;
        }
        // Two postings that sum to zero: the amount as given on the account
        // named, and its opposite on the category. The sign is the user's --
        // money out is negative -- because that is how a register reads.
        ledger::Transaction t;
        t.id = ledger::new_id();
        t.ref = types::TransactionRef(next_ref(open.transactions));
        t.date = *when;
        t.payee = types::PayeeName(argv[4]);
        ledger::Posting here;
        here.account = types::AccountPath(account);
        here.amount = amount;
        ledger::Posting there;
        there.account = types::AccountPath(category);
        there.amount = -amount;
        t.postings = {here, there};
        open.transactions.push_back(t);

        no = open.book.write(open.accounts, open.transactions);
        if (no.refused) return complain(no);
        std::printf("%s  %s  %s %s  %s\n", t.ref.value().c_str(), when->iso().c_str(),
                    account.c_str(), amount.in_register().c_str(), category.c_str());
        return 0;
    }

    if (command == "register") {
        if (argc < 4) return usage();
        const std::string account = argv[3];
        if (!open.accounts.has(account)) {
            std::fprintf(stderr, "There is no account named %s\n", account.c_str());
            return 1;
        }
        std::printf("%-10s %-24s %-30s %12s %12s %14s\n", "date", "payee", "category",
                    "payment", "deposit", "balance");
        for (const reg::Line& line :
             reg::lines_for(open.accounts, open.transactions, account))
            std::printf("%-10s %-24s %-30s %12s %12s %14s\n", line.date.iso().c_str(),
                        line.payee.c_str(), line.category.c_str(),
                        line.payment.cents() == 0 ? "" : line.payment.in_register().c_str(),
                        line.deposit.cents() == 0 ? "" : line.deposit.in_register().c_str(),
                        line.balance.in_register().c_str());
        return 0;
    }

    if (command == "delete") {
        if (argc < 4) return usage();
        const std::string ref = argv[3];
        // Always the whole transaction: removing one side would leave the book
        // out of balance. See the delete scenario in Transactions.spectable.
        // An id outright, or a ref that names exactly one transaction. A ref
        // that names two is refused rather than guessed at: both are real
        // transactions and only the person asking knows which they meant.
        std::string id = ref;
        if (ref.size() != 18) {
            const std::vector<std::string> named =
                ledger::ids_labelled(open.transactions, ref);
            if (named.empty()) {
                std::fprintf(stderr, "There is no transaction %s\n", ref.c_str());
                return 1;
            }
            if (named.size() > 1) {
                std::fprintf(stderr, "%s names %zu transactions; say which:\n",
                             ref.c_str(), named.size());
                for (const std::string& one : named)
                    std::fprintf(stderr, "  %s\n", one.c_str());
                return 1;
            }
            id = named.front();
        }
        if (!ledger::erase_transaction(&open.transactions, id)) {
            std::fprintf(stderr, "There is no transaction %s\n", id.c_str());
            return 1;
        }
        no = open.book.write(open.accounts, open.transactions);
        if (no.refused) return complain(no);
        std::printf("deleted %s\n", ref.c_str());
        return 0;
    }

    if (command == "import") {
        if (argc < 5) return usage();
        const std::string file = argv[3];
        const std::string account = argv[4];
        if (!open.accounts.has(account)) {
            std::fprintf(stderr, "There is no account named %s\n", account.c_str());
            return 1;
        }
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "%s could not be read\n", file.c_str());
            return 1;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();

        const ofx::Read parsed = ofx::read(buffer.str());
        if (parsed.refused) {
            std::fprintf(stderr, "%s\n", parsed.refusal.c_str());
            return 1;
        }

        bool accept = false;
        // Which offered rows have been answered, by the identifier the file gave
        // them. Nothing happens to a row that was not answered, which is what
        // makes it safe to put an import down and come back to it.
        std::vector<std::string> say_same;
        std::vector<std::string> say_different;
        for (int i = 5; i < argc; ++i) {
            if (std::strcmp(argv[i], "--accept") == 0) accept = true;
            else if (std::strcmp(argv[i], "--same") == 0 && i + 1 < argc)
                say_same.push_back(argv[++i]);
            else if (std::strcmp(argv[i], "--different") == 0 && i + 1 < argc)
                say_different.push_back(argv[++i]);
        }
        const auto was_answered = [](const std::vector<std::string>& answers,
                                     const std::string& id) {
            return !id.empty() &&
                   std::find(answers.begin(), answers.end(), id) != answers.end();
        };

        // Nothing reaches the book without --accept, so the decision can be
        // read first. That is the shape the review pane in the window has: an
        // import is looked at before it changes anything.
        ofx::Summary total;
        int matched_clear = 0;
        int answered_same = 0;
        int answered_different = 0;
        int left_open = 0;
        std::vector<ledger::Transaction> adding;
        for (const ofx::Statement& statement : parsed.statements) {
            std::printf("statement %s %s  %s..%s  ledger %s\n",
                        statement.bank_id.c_str(), statement.account_id.c_str(),
                        statement.start_date.iso().c_str(), statement.end_date.iso().c_str(),
                        statement.ledger_balance.in_register().c_str());
            const ofx::Imported decided =
                ofx::decide(statement, account, open.transactions);
            std::printf("%-10s %-30s %12s %-11s %s\n", "date", "payee", "amount",
                        "decision", "claims");
            for (const ofx::Decided& one : decided.decided)
                std::printf("%-10s %-30s %12s %-11s %s\n",
                            one.downloaded.date_posted.iso().c_str(),
                            one.downloaded.name.c_str(),
                            one.downloaded.amount.in_register().c_str(),
                            ofx::to_string(one.disposition).c_str(),
                            one.claimed_ref.c_str());
            total.New += decided.summary.New;
            total.Duplicate += decided.summary.Duplicate;
            total.Matched += decided.summary.Matched;
            total.Possible += decided.summary.Possible;

            // A match clears the transaction already there rather than making a
            // second copy of it.
            matched_clear += ofx::apply_matches(decided, account, &open.transactions);

            // Then whatever was answered about the rows it could not settle.
            for (const ofx::Decided& one : decided.decided) {
                if (one.disposition != ofx::Disposition::Possible) continue;
                const std::string& id = one.downloaded.fit_id;
                if (was_answered(say_same, id)) {
                    if (ofx::answer_the_same(one, account, &open.transactions)) {
                        ++answered_same;
                        std::printf("  %s is the same as %s\n", id.c_str(),
                                    one.claimed_ref.c_str());
                    }
                } else if (was_answered(say_different, id)) {
                    ledger::Transaction made = ofx::answer_different(
                        one, account, open.transactions.size() + adding.size(),
                        &open.accounts);
                    if (!made.postings.empty()) {
                        open.transactions.push_back(made);
                        adding.push_back(made);
                        ++answered_different;
                        std::printf("  %s is a different transaction, %s\n", id.c_str(),
                                    made.ref.value().c_str());
                    }
                } else {
                    ++left_open;
                }
            }

            // The decision for the next statement must see what this one added,
            // or two statements naming one transaction would both create it.
            std::vector<ledger::Transaction> made = ofx::transactions_for(
                decided, account, open.transactions.size() + adding.size(), &open.accounts);
            for (ledger::Transaction& t : made) {
                open.transactions.push_back(t);
                adding.push_back(t);
            }
        }
        std::printf("\nNew %d  Duplicate %d  Matched %d  Possible %d\n",
                    total.New, total.Duplicate, total.Matched, total.Possible);
        if (total.Possible > 0)
            std::printf("of the possible: %d answered the same, %d different, "
                        "%d left to answer\n",
                        answered_same, answered_different, left_open);

        if (!accept) {
            std::printf("nothing written; pass --accept to keep it\n");
            return 0;
        }
        no = open.book.write(open.accounts, open.transactions);
        if (no.refused) return complain(no);
        std::printf("added %zu transactions, cleared %d already there\n",
                    adding.size(), matched_clear + answered_same);
        return 0;
    }

    if (command == "report") {
        if (argc < 5) return usage();
        reports::Spec spec;
        const auto from = types::Date::from_iso(argv[3]);
        const auto to = types::Date::from_iso(argv[4]);
        if (!from || !to) {
            std::fprintf(stderr, "the dates must be YYYY-MM-DD\n");
            return 2;
        }
        spec.from = *from;
        spec.to = *to;
        for (int i = 5; i < argc; ++i)
            if (std::strcmp(argv[i], "--quicken") == 0) spec.quicken_signs = true;

        const reports::CategoryReport r =
            reports::category_report(open.accounts, open.ledger, spec);
        std::printf("%s  %s..%s\n", name.c_str(), argv[3], argv[4]);
        for (const reports::Row& row : r.rows)
            std::printf("%-46s %16s %s\n", row.account.c_str(),
                        row.amount.in_register().c_str(), row.is_subtotal ? "subtotal" : "");
        std::printf("\n  INCOME    %16s\n", r.totals.income.in_register().c_str());
        std::printf("  EXPENSES  %16s\n", r.totals.expenses.in_register().c_str());
        std::printf("  NET       %16s\n", r.totals.net.in_register().c_str());
        return 0;
    }

    return usage();
}
