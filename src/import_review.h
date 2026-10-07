#pragma once
#include <string>
#include <vector>
#include "chart.h"
#include "csv_import.h"
#include "csv_reader.h"
#include "ofx_import.h"
#include "payee_rules.h"
#include "posting.h"
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
    // How many rows the import decided it had already seen, and how many it
    // matched against something entered by hand. Neither is added again.
    int duplicates = 0;
    int matched = 0;
};

namespace detail {

// One review row per decided row. A row that will be added is read off the
// transaction the importer prepared for it, which is where the rules, the
// category and the identifier have already been worked out -- so the review
// shows what will be stored rather than a second guess at it.
//
// The prepared transactions are in the order of the rows that produced them, and
// only the New rows produce one, which is what lets them be zipped.
inline void add_row(ui::ReviewRow* row, const ledger::Transaction& prepared,
                    const std::string& account) {
    row->payee = prepared.payee.value();
    row->raw_name = prepared.raw_name;
    row->memo = prepared.memo;
    row->check_no = prepared.check_no.value();
    for (const ledger::Posting& p : prepared.postings) {
        if (p.account.value() == account) {
            row->cleared = p.cleared;
            if (!p.import_ids.empty()) {
                row->identifier = p.import_ids.front().id;
                row->source = p.import_ids.front().source;
            }
            continue;
        }
        row->category = p.account.value();
    }
}

}  // namespace detail

// An OFX or QFX download. Every statement in the file is read, and they are all
// for the same account: the file is downloaded per account.
inline Prepared from_ofx(const std::string& text, const std::string& account,
                         const std::vector<ledger::Transaction>& existing,
                         chart::Chart* accounts,
                         const std::vector<payees::Rule>& rules) {
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
        const ofx::Imported decided =
            ofx::decide(statement, account, existing, *accounts);
        // Asked with the rules in force, so the review shows the payee and the
        // category the user will actually get.
        const std::vector<ledger::Transaction> prepared = ofx::transactions_for(
            decided, account, existing.size(), accounts, rules);

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
            if (one.disposition == ofx::Disposition::New && next < prepared.size())
                detail::add_row(&row, prepared[next++], account);
            if (one.disposition == ofx::Disposition::Duplicate) ++out.duplicates;
            if (one.disposition == ofx::Disposition::Matched) ++out.matched;
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
                         const std::vector<payees::Rule>& rules) {
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

    const csv::Imported decided = csv::decide(rows.rows, account, existing);
    const std::vector<ledger::Transaction> prepared = csv::transactions_for(
        decided, account, existing.size(), accounts, rules);

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
        row.cleared = types::ClearedStatus::Uncleared;
        row.disposition = ofx::to_string(one.disposition);
        if (one.disposition == ofx::Disposition::New && next < prepared.size())
            detail::add_row(&row, prepared[next++], account);
        if (one.disposition == ofx::Disposition::Duplicate) ++out.duplicates;
        if (one.disposition == ofx::Disposition::Matched) ++out.matched;
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

inline Prepared from_file(const std::string& text, const std::string& account,
                          const std::vector<ledger::Transaction>& existing,
                          chart::Chart* accounts,
                          const std::vector<payees::Rule>& rules) {
    return looks_like_a_csv(text)
               ? from_csv(text, account, existing, accounts, rules)
               : from_ofx(text, account, existing, accounts, rules);
}

}  // namespace review
