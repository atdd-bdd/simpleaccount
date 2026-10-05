// Imports a QIF file and prints a category report, so that the import and the
// report can be checked against Quicken's own figures at full scale rather than
// only against the worked examples.
//
//   sa_report <file.qif> <from:YYYY-MM-DD> <to:YYYY-MM-DD> [--rows]
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "qif_import.h"
#include "report.h"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: sa_report <file.qif> <from> <to>"
                             " [--rows] [--quicken] [--category <path>]\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    qif::Importer importer(types::DateOrder::MDY);
    const qif::Imported book = importer.run(buffer.str());
    if (book.refused) { std::fprintf(stderr, "refused: %s\n", book.refusal.c_str()); return 1; }

    reports::Spec spec;
    const auto from = types::Date::from_iso(argv[2]);
    const auto to = types::Date::from_iso(argv[3]);
    if (!from || !to) { std::fprintf(stderr, "dates must be YYYY-MM-DD\n"); return 2; }
    spec.from = *from;
    spec.to = *to;

    // Expenses negative, as Quicken writes them, so a figure can be read
    // straight against a Quicken report without negating a column by hand.
    for (int i = 4; i < argc; ++i)
        if (std::string(argv[i]) == "--quicken") spec.quicken_signs = true;

    const reports::CategoryReport r = reports::category_report(book.accounts, book.book, spec);

    std::printf("file         %s\n", argv[1]);
    std::printf("records      %zu\n", book.records.size());
    std::printf("transactions %zu\n", book.transactions.size());
    std::printf("postings     %zu\n", book.postings.size());
    std::printf("accounts     %zu\n", book.accounts.all().size());
    std::printf("notes        %zu\n", book.notes.size());
    std::printf("transfers paired %d\n", book.summary.duplicate);
    std::printf("\n%s..%s\n", argv[2], argv[3]);
    std::printf("  INCOME    %16s\n", r.totals.income.in_register().c_str());
    // Printed as the report gives it. This used to negate the figure so the
    // line read the Quicken way whatever the report said, which hid the sign
    // convention rather than showing it; --quicken now decides.
    std::printf("  EXPENSES  %16s\n", r.totals.expenses.in_register().c_str());
    std::printf("  NET       %16s\n", r.totals.net.in_register().c_str());

    // Dumps every posting the report counted for one category, with the ref of
    // the transaction it came from, so that a report figure can be compared
    // posting by posting with the register of the same category.
    for (int i = 4; i + 1 < argc; ++i) {
        if (std::string(argv[i]) != "--category") continue;
        const std::string want = argv[i + 1];
        Money sum;
        int n = 0;
        for (const ledger::DatedPosting& p : book.book.postings()) {
            if (p.date < spec.from || spec.to < p.date) continue;
            if (!chart::Chart::is_descendant_or_self(p.account.value(), want)) continue;
            std::printf("%s  %-44s %14s  ref %s\n", p.date.iso().c_str(),
                        p.account.value().c_str(), p.amount.in_register().c_str(),
                        p.ref.c_str());
            sum += p.amount;
            ++n;
        }
        std::printf("%d postings, raw sum %s\n", n, sum.in_register().c_str());
    }

    // The invoices that were never collected, which is the only place the
    // difference between what was invoiced and what was earned can be read.
    for (int i = 4; i < argc; ++i) {
        if (std::string(argv[i]) != "--uncollected") continue;
        std::printf("\nuncollected invoices (%zu)\n", book.uncollected.size());
        for (const qif::UncollectedInvoice& u : book.uncollected)
            std::printf("  %s %-24s %12s  %-36s %s\n", u.date.iso().c_str(),
                        u.payee.c_str(), u.amount.in_register().c_str(),
                        u.category.c_str(), u.note.c_str());
    }

    // Scanned rather than read at a fixed position, so the flags can be given
    // in any order alongside --quicken and --category.
    bool rows = false;
    for (int i = 4; i < argc; ++i)
        if (std::string(argv[i]) == "--rows") rows = true;
    if (rows) {
        std::printf("\n%-46s %16s %s\n", "account", "amount", "subtotal");
        for (const reports::Row& row : r.rows)
            std::printf("%-46s %16s %s\n", row.account.c_str(),
                        row.amount.in_register().c_str(), row.is_subtotal ? "yes" : "");
    }
    return 0;
}
