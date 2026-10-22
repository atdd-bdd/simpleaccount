#pragma once
#include <string>
#include <vector>
#include "chart.h"
#include "csv_import.h"
#include "csv_reader.h"
#include "import_target.h"
#include "ofx_import.h"
#include "ofx_reader.h"
#include "payee_rules.h"
#include "transaction.h"
#include "ui_model.h"

// Turning a downloaded file into the rows of an import review.
//
// The importers already decide everything that matters -- what is new, what the
// payee is called once the rules have had it, which category the other side goes
// to, what identifier the row carries. This only arranges their answers into the
// rows the review pane shows, so that the window has no import logic of its own.
//
// What the review shows and what accepting it does is in UserInterface.spectable;
// how a row is decided is in ImportOfx.spectable and ImportCsv.spectable.
namespace review {

// Everything the review needs, or the reason there is nothing to review.
struct Prepared {
    bool refused = false;
    std::string refusal;
    // Said out loud rather than swallowed: columns the alias table could not
    // place, rows that could not be read, a check number that matched on the
    // number but not the day.
    std::vector<std::string> notes;
    std::vector<ui::ReviewRow> rows;
    // The account the import is for, which the file names rather than the reader.
    std::string into;
    // How many rows the import decided it had already seen, and how many
    // filled in the other half of a payment already here. A duplicate adds
    // nothing; a completion adds the side the file described.
    int duplicates = 0;
    int completed = 0;
};

namespace detail {

// One review row per decided row. A row that will be added is read off the
// transaction the importer prepared for it, which is where the rules, the
// category and the identifier have already been worked out -- so the review
// shows what will be stored rather than a second guess at it.
//
// The prepared transactions are in the order of the rows that produced them, and
// only the New rows produce one, which is what lets them be zipped.
inline void add_row(const chart::Chart& accounts, ui::ReviewRow* row,
                    const ledger::Transaction& prepared) {
    row->payee = prepared.payee.value();
    row->raw_name = prepared.raw_name;
    row->memo = prepared.memo;
    row->check_no = prepared.check_no.value();
    row->cleared = prepared.cleared;
    row->category = accounts.path_for_ref(prepared.destination);
    if (!prepared.import_ids.empty()) {
        row->identifier = prepared.import_ids.front().id;
        row->source = prepared.import_ids.front().source;
    }
}

}  // namespace detail

// An OFX or QFX download. Every statement in the file is read, and they are all
// for the same account: the file is downloaded per account.
inline Prepared from_ofx(const std::string& text, const std::string& account,
                         const std::vector<ledger::Transaction>& existing,
                         chart::Chart* accounts,
                         const std::vector<payees::Rule>& rules,
                         const std::vector<payees::Conversion>& conversions = {},
                         const std::vector<payees::Categorisation>& categories = {}) {
    Prepared out;
    out.into = account;
    const ofx::Read parsed = ofx::read(text);
    if (parsed.refused) {
        out.refused = true;
        out.refusal = parsed.refusal;
        return out;
    }

    int line = 0;
    for (const ofx::Statement& statement : parsed.statements) {
        // Asked with the rules in force, because comparing two payees is the
        // first thing the rules are for: only a rule can know that AMZN Mktp
        // US*2X4 and Amazon are one shop.
        const ofx::Imported decided =
            ofx::decide(statement, account, existing, *accounts, ofx::kCheckWindow, rules);
        // Asked with the rules in force, so the review shows the payee and the
        // category the user will actually get.
        const std::vector<ledger::Transaction> prepared = ofx::transactions_for(
            decided, account, existing.size(), accounts, rules, conversions, categories);

        std::size_t next = 0;
        for (const ofx::Decided& one : decided.decided) {
            ui::ReviewRow row;
            row.line = ++line;
            row.date = one.downloaded.date_posted;
            row.payee = one.downloaded.name;
            row.raw_name = one.downloaded.name;
            row.account = account;
            row.amount = one.downloaded.amount;
            row.memo = one.downloaded.memo;
            row.check_no = one.downloaded.check_num;
            row.identifier = one.downloaded.fit_id;
            row.source = types::ImportSource::Ofx;
            row.disposition = ofx::to_string(one.disposition);
            row.claimed_ids = one.claim_ids;
            for (const ofx::DuplicateNote& note : one.notes)
                row.notes.push_back(ofx::to_string(note));
            if (one.disposition == ofx::Disposition::New && next < prepared.size())
                detail::add_row(*accounts, &row, prepared[next++]);
            if (one.disposition == ofx::Disposition::Duplicate) ++out.duplicates;
            if (one.disposition == ofx::Disposition::Completed) ++out.completed;
            out.rows.push_back(row);
        }
        for (const ofx::Note& note : decided.notes)
            out.notes.push_back(note.kind + ": " + note.detail);
    }
    return out;
}

// A delimited file. Its columns have to be understood before anything can be
// decided, and anything the alias table cannot place is reported rather than
// guessed at.
inline Prepared from_csv(const std::string& text, const std::string& account,
                         const std::vector<ledger::Transaction>& existing,
                         chart::Chart* accounts,
                         const std::vector<payees::Rule>& rules,
                         bool clear_what_is_imported = true,
                         const std::vector<payees::Conversion>& conversions = {},
                         const std::vector<payees::Categorisation>& categories = {}) {
    Prepared out;
    out.into = account;
    const csv::Read file = csv::read(text);
    if (file.refused) {
        out.refused = true;
        out.refusal = file.refusal;
        return out;
    }
    const csv::Matching matching = csv::match_headers(file.headings);
    if (matching.refused) {
        out.refused = true;
        out.refusal = matching.refusal;
        return out;
    }
    for (const csv::Ambiguity& one : matching.ambiguous) {
        out.refused = true;
        out.refusal = "two columns both look like " + csv::to_string(one.field) +
                      ": " + csv::detail::joined(one.headers);
        return out;
    }
    for (const csv::Field missing : matching.missing) {
        out.refused = true;
        out.refusal = "no column holds the " + csv::to_string(missing) +
                      ", which is required";
        return out;
    }
    for (const csv::HeaderMatch& one : matching.unmatched)
        out.notes.push_back("column " + std::to_string(one.position) + ", " +
                            one.source_header + ", was not recognised");

    const csv::StyleChoice style = csv::style_for(
        matching.for_field(csv::Field::Amount) != nullptr,
        matching.for_field(csv::Field::Debit) != nullptr,
        matching.for_field(csv::Field::Credit) != nullptr);
    const csv::Rows rows = csv::rows_of(file, matching, types::DateOrder::MDY,
                                        style.style, csv::OutwardSign::Negative);
    for (const csv::RowError& bad : rows.rejected)
        out.notes.push_back("line " + std::to_string(bad.line) +
                            " was not imported: " + bad.reason);

    // Asked with the rules in force, for the same reason the OFX import is:
    // only a rule can know that two spellings are one payee.
    const csv::Imported decided =
        csv::decide(*accounts, rows.rows, account, existing, rules);
    const std::vector<ledger::Transaction> prepared = csv::transactions_for(
        decided, account, existing.size(), accounts, rules, clear_what_is_imported,
        conversions, categories);

    int line = 0;
    std::size_t next = 0;
    for (const csv::Decided& one : decided.decided) {
        ui::ReviewRow row;
        row.line = ++line;
        row.date = one.row.date;
        row.payee = one.row.payee;
        row.raw_name = one.row.payee;
        row.account = account;
        row.amount = one.row.amount;
        row.memo = one.row.memo;
        row.check_no = one.row.check_no;
        // The only name a CSV row has is the line it came from, which is what
        // the importer stamps. Not an OFX identifier and never compared with one.
        row.identifier = std::to_string(one.row.line);
        row.source = types::ImportSource::Csv;
        // A download, so cleared; a New row has this read off the prepared
        // transaction below in any case. See the arrives cleared rule in
        // ImportOfx.
        row.cleared = types::ClearedStatus::Cleared;
        row.disposition = ofx::to_string(one.disposition);
        row.claimed_ids = one.claim_ids;
        for (const ofx::DuplicateNote& note : one.notes)
            row.notes.push_back(ofx::to_string(note));
        if (one.disposition == ofx::Disposition::New && next < prepared.size())
            detail::add_row(*accounts, &row, prepared[next++]);
        if (one.disposition == ofx::Disposition::Duplicate) ++out.duplicates;
        if (one.disposition == ofx::Disposition::Completed) ++out.completed;
        out.rows.push_back(row);
    }
    return out;
}

// What kind of file this is, by what it holds. An OFX file opens a tag before
// anything else; anything else is read as delimited text.
inline bool looks_like_a_csv(const std::string& text) {
    for (const char c : text) {
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        return c != '<' && c != 'O';     // '<' opens OFX 2, 'O' starts OFXHEADER
    }
    return false;
}

// The book's CSV cleared option is passed here rather than asked for inside,
// because nothing below this knows which book it is working in. An OFX file
// states a cleared status of its own and so is never told.
inline Prepared from_file(const std::string& text, const std::string& account,
                          const std::vector<ledger::Transaction>& existing,
                          chart::Chart* accounts,
                          const std::vector<payees::Rule>& rules,
                          bool clear_what_a_csv_brings_in = true,
                          const std::vector<payees::Conversion>& conversions = {},
                          const std::vector<payees::Categorisation>& categories = {}) {
    return looks_like_a_csv(text)
               ? from_csv(text, account, existing, accounts, rules,
                          clear_what_a_csv_brings_in, conversions, categories)
               : from_ofx(text, account, existing, accounts, rules, conversions,
                          categories);
}

// The two things a download carries that came from the bank rather than from
// this program, as the file wrote them and not reformatted, so that they can be
// read against a statement. A CSV and a QIF say nothing of the kind, so both
// are empty; a QFX states them whether or not an account in this book carries
// the number, which is the case where they matter most -- they are then the
// whole of what the person has to go on when asked which account the download
// belongs to. See the confirmation attributes in UserInterface.spectable.
struct FileNumbers {
    std::string bank_id;
    std::string account_number;
};

inline FileNumbers numbers_in_file(const std::string& text) {
    if (looks_like_a_csv(text)) return {};
    const ofx::Read read = ofx::read(text);
    if (read.refused || read.statements.empty()) return {};
    const ofx::Statement& s = read.statements.front();
    return {s.bank_id, s.account_id};
}

// What the file itself says about the account it is for, asked before a row of
// it is read. A QFX names the institution and the account number, and the
// mappings turn that pair into a path; a CSV names nothing, so the answer is
// empty and the name of the file is all there will be to go on.
//
// An account number written on the chart is the fallback, because a mapping is
// only made the first time a download is placed by hand -- and the number is
// often already there from setting the account up. Matched on the tail, since a
// download gives the last four of a number as often as the whole of it.
inline std::string account_in_file(const std::string& text, const chart::Chart& accounts,
                                   const std::vector<ofx::AccountMapping>& mappings) {
    if (looks_like_a_csv(text)) return {};

    const ofx::Read read = ofx::read(text);
    if (read.refused || read.statements.empty()) return {};
    const ofx::Statement& s = read.statements.front();

    if (const ofx::AccountMapping* m = ofx::mapping_for(mappings, s.bank_id, s.account_id))
        return m->account_path;
    const std::string by_id = ofx::account_for_id(mappings, s.account_id);
    if (!by_id.empty()) return by_id;

    const std::string wanted = importing::detail::only_digits(s.account_id);
    if (wanted.empty()) return {};
    std::string found;
    for (const chart::Account& a : accounts.all()) {
        if (!importing::can_be_imported_into(a)) continue;
        const std::string digits = importing::detail::only_digits(a.account_number);
        if (digits.empty()) continue;
        const std::string& shorter = digits.size() < wanted.size() ? digits : wanted;
        const std::string& longer = digits.size() < wanted.size() ? wanted : digits;
        if (shorter.size() < 4) continue;
        if (longer.compare(longer.size() - shorter.size(), shorter.size(), shorter) != 0)
            continue;
        if (!found.empty()) return {};
        found = a.path.value();
    }
    return found;
}

}  // namespace review
