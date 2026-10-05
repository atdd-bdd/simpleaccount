// Prints the register of one account as tab-separated lines, so it can be
// compared with the register report Quicken exports.
//
//   sa_register <file.qif> <account path>
//   sa_register <file.qif> --list
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "qif_import.h"
#include "register_lines.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: sa_register <file.qif> <account path>|--list\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    qif::Importer importer(types::DateOrder::MDY);
    const qif::Imported book = importer.run(buffer.str());
    if (book.refused) { std::fprintf(stderr, "refused: %s\n", book.refusal.c_str()); return 1; }

    if (std::string(argv[2]) == "--list") {
        for (const chart::Account& a : book.accounts.all())
            std::printf("%s\n", a.path.value().c_str());
        return 0;
    }

    const std::vector<reg::Line> lines =
        reg::lines_for(book.accounts, book.transactions, argv[2]);
    std::printf("date\tnum\tpayee\tcategory\tamount\tbalance\n");
    for (const reg::Line& line : lines) {
        const Money amount = line.payment.cents() != 0 ? -line.payment : line.deposit;
        std::printf("%s\t%s\t%s\t%s\t%lld\t%lld\n", line.date.iso().c_str(),
                    line.check_no.c_str(), line.payee.c_str(), line.category.c_str(),
                    static_cast<long long>(amount.cents()),
                    static_cast<long long>(line.balance.cents()));
    }
    return 0;
}
