// Prints the balance of every account after importing a QIF file, so it can be
// compared with Quicken's Account Balances report.
#include <cstdio>
#include <fstream>
#include <sstream>
#include "qif_import.h"

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: sa_balances <file.qif>\n"); return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    qif::Importer importer(types::DateOrder::MDY);
    const qif::Imported book = importer.run(buffer.str());
    if (book.refused) { std::fprintf(stderr, "refused: %s\n", book.refusal.c_str()); return 1; }
    std::printf("account\ttype\traw_cents\n");
    for (const chart::Account& a : book.accounts.all())
        std::printf("%s\t%s\t%lld\n", a.path.value().c_str(),
                    types::to_string(a.type).c_str(),
                    static_cast<long long>(book.book.raw_balance(a.path.value()).cents()));
    std::fprintf(stderr, "bracketed=%d paired=%d unpaired_notes=%d\n", 0,
                 book.summary.duplicate,
                 (int)std::count_if(book.notes.begin(), book.notes.end(),
                                    [](const qif::Note& n){ return n.kind=="UnpairedTransfer"; }));
    return 0;
}
