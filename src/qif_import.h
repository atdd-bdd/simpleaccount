#pragma once
#include <algorithm>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "chart.h"
#include "date_parse.h"
#include "ledger.h"
#include "money.h"
#include "transaction.h"
#include "transaction_id.h"
#include "qif_reader.h"

// Turning the records of a QIF file into a book: accounts, categories,
// transactions and a report of everything that was not obvious. See
// ImportQif.spectable.
namespace qif {

// One record after the fields have been read, which is the form the
// specification's tables are written in.
struct ReadRecord {
    std::string account;
    types::Date date;
    Money amount;
    std::string payee;
    std::string category_text;
    std::string memo;
    std::string check_no;
    std::string cleared_text;
};

struct Note {
    std::string kind;
    std::string detail;
};

// An invoice that was never collected. Its amount is owed and none of it is
// income, so this list is the only place the difference between what was
// invoiced and what was earned can be read. See the cleared marker rule in
// ImportQif.spectable.
struct UncollectedInvoice {
    types::Date date;
    std::string payee;
    Money amount;
    std::string category;      // the first category it bills for
    std::string note;
};

struct Summary {
    int created = 0;      // transactions created
    int duplicate = 0;    // halves recognised as the other side of one already made
    int matched = 0;
    int possible = 0;
};

// The statement balance an !Account block carries, and the date it was taken
// at. Not an opening balance: the figure to check the import against.
struct StatementBalance {
    Money balance;
    types::Date as_of;
};

// How much of an account's balance this round did not import, because its
// securities side was skipped.
struct IncompleteBalance {
    std::string reason;
    int skipped_records = 0;
};

struct TransferPairingReport {
    int bracketed_fields = 0;
    int pairs_formed = 0;
    int unpaired = 0;
    int near_misses = 0;
};

struct MigrationReport {
    int accounts_created = 0;
    int categories_created = 0;
    int transactions = 0;
    int postings = 0;
    int transfers_paired = 0;
    int unsupported = 0;
    int notes = 0;
    bool balanced = false;
};

struct BalanceCheck {
    Money cleared_balance;
    Money stated_bal;
    Money difference;
    bool agrees = false;
    bool has_stated = false;
};

// How an incomplete balance is offered to a reader: never as a figure that
// looks like a valuation, because the securities side behind it was not
// imported this round.
struct BalanceDisplay {
    bool complete = true;
    std::string shown_as;
    std::string caution;
};

inline BalanceDisplay balance_display(bool incomplete) {
    BalanceDisplay d;
    if (incomplete) {
        d.complete = false;
        d.shown_as = "Transfers only";
        d.caution = "Securities not imported; not a market value";
    }
    return d;
}

struct Imported {
    chart::Chart accounts;
    ledger::Ledger book;
    std::vector<ReadRecord> records;
    std::vector<chart::Account> accounts_created;
    std::vector<ledger::DatedPosting> postings;
    std::vector<Note> notes;
    std::vector<UncollectedInvoice> uncollected;
    Summary summary;
    std::map<std::string, Money> credit_limits;
    std::map<std::string, StatementBalance> statement_balances;
    std::map<std::string, IncompleteBalance> incomplete_balances;
    TransferPairingReport transfer_pairing;
    bool refused = false;
    std::string refusal;

    // The transactions made, in the order they were made. A transaction is
    // identified by its index, written T1, T2 and so on, which is how the
    // specification's tables refer to them.
    std::vector<ledger::Transaction> transactions;
    // A split's category lines, found by the transaction's id. Empty for an
    // ordinary transaction, which names its one other side directly.
    std::vector<ledger::TransactionSplit> splits;

    // The balance an account's statement carries, checked against what the
    // postings add up to as at that date. Empty where there is nothing to
    // check: a migration of a file with no account block carries no balance.
    BalanceCheck check_balance(const std::string& account) const {
        BalanceCheck out;
        const auto at = statement_balances.find(account);
        if (at == statement_balances.end()) return out;
        out.has_stated = true;
        out.stated_bal = at->second.balance;
        out.cleared_balance = book.balance_as_at(account, at->second.as_of);
        out.difference = out.cleared_balance - out.stated_bal;
        out.agrees = out.difference.cents() == 0;
        return out;
    }

    // Where there is exactly one statement balance to check, which is the only
    // case these tables are written for.
    BalanceCheck check_the_one_balance() const {
        if (statement_balances.size() != 1) return BalanceCheck();
        return check_balance(statement_balances.begin()->first);
    }

    MigrationReport migration_report() const {
        MigrationReport out;
        for (const chart::Account& a : accounts_created) {
            const bool category = a.type == types::AccountType::Income ||
                                  a.type == types::AccountType::Expense;
            if (category) ++out.categories_created;
            else if (!a.placeholder) ++out.accounts_created;
        }
        out.transactions = static_cast<int>(transactions.size());
        out.transfers_paired = summary.duplicate;
        out.notes = static_cast<int>(notes.size());
        out.balanced = true;
        for (const ledger::Transaction& t : transactions) {
            const std::vector<ledger::Side> sides = ledger::sides_of(t, splits);
            out.postings += static_cast<int>(sides.size());
            std::vector<Money> amounts;
            for (const ledger::Side& s : sides) amounts.push_back(s.amount);
            if (!ledger::balances(amounts)) { out.balanced = false; break; }
        }
        return out;
    }
};

// What kind of file this is, by what it holds rather than by what it is called.
// A QIF opens with a bang directive -- !Type:Bank, !Account, !Option -- before
// anything else, which neither a CSV nor an OFX can start with. Asked because
// the one import item takes all three kinds and each has its own reader.
inline bool looks_like_a_qif(const std::string& text) {
    for (const char c : text) {
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        return c == '!';
    }
    return false;
}

// One line of a split that is itself a transfer: the split names another account
// in brackets instead of a category. Such a line posts both sides on its own --
// the split's own T covers this account and the line covers the other -- so a
// whole record in that other account naming this one is the same transfer
// written twice.
struct SplitLeg {
    std::string owner;      // the account whose split this is
    std::string target;     // the account the line names
    Money to_target;        // what the line posts to that account
    types::Date date;
    bool used = false;
};

class Importer {
public:
    explicit Importer(types::DateOrder order = types::DateOrder::MDY) : order_(order) {}

    // An existing chart to import into, where there is one.
    void start_from(const chart::Chart& existing) { result_.accounts = existing; }
    void import_into(const std::string& account) { target_ = account; }
    // This file is a statement, not the history. A statement is a download that
    // happens to be in Quicken's format, so its rows arrive cleared like any
    // other download's and the C field in them is not read. Only the history
    // keeps its markers, which is why that is the default. See the arrives
    // cleared rule in ImportOfx.
    void as_a_statement() { a_statement_ = true; }

    Imported run(const std::string& text) {
        const File file = read(decode(text), target_);
        // The target is where every record with no account block posts, so it
        // has to resolve to a ref the same as any other account -- a file with
        // no !Account block of its own never otherwise names it. The target
        // itself is not a migration note -- it is where the import was
        // pointed, not something the import discovered -- but a placeholder
        // filled in above it is new all the same, and is reported as such.
        if (!target_.empty() && !result_.accounts.has(target_)) {
            std::vector<chart::Account> created;
            result_.accounts.add(types::AccountPath(target_), types::AccountType::Checking,
                                 &created);
            for (const chart::Account& a : created)
                if (a.path.value() != target_) result_.accounts_created.push_back(a);
        }
        declare_accounts(file);
        declare_categories(file);
        read_records(file);
        if (result_.refused) return result_;
        build_transactions();
        report_guessed_categories();
        result_.transfer_pairing.bracketed_fields = bracketed_fields_;
        result_.transfer_pairing.pairs_formed = result_.summary.duplicate;
        for (const Note& n : result_.notes) {
            if (n.kind == "UnpairedTransfer") ++result_.transfer_pairing.unpaired;
            if (n.kind == "NearTransfer") ++result_.transfer_pairing.near_misses;
        }
        for (const auto& entry : skipped_by_account_) {
            result_.incomplete_balances[entry.first] =
                {"The securities side is not imported this round", entry.second};
            note("SecuritiesSkipped", std::to_string(entry.second) +
                 " investment records not imported this round; " + entry.first +
                 " holds an incomplete balance");
        }
        if (any_account_block_ && result_.statement_balances.empty())
            note("NoStatedBalance", "No account block carries a statement balance; "
                                    "the import was not checked against one");
        if (tags_kept_ > 0)
            note("TagsKept", std::to_string(tags_kept_) + " class" +
                             (tags_kept_ == 1 ? "" : "es") +
                             " kept as a tag; no report groups by tag in this release");
        if (invoice_records_with_detail_ > 0)
            note("InvoiceDetail",
                 std::to_string(invoice_records_with_detail_) + " invoice record" +
                 (invoice_records_with_detail_ == 1 ? "" : "s") + ": " +
                 std::to_string(dropped_x_fields_) + " X fields and " +
                 std::to_string(dropped_address_lines_) +
                 " address lines not imported");
        return result_;
    }

private:
    types::DateOrder order_;
    std::string target_;
    bool a_statement_ = false;
    Imported result_;
    std::map<std::string, char> category_kind_;   // name -> 'I' or 'E'
    std::map<std::string, std::string> account_paths_;  // QIF name -> path
    // An undeclared category, with how often it was used and the path the sign
    // gave it. The count is per distinct name rather than per transaction.
    std::map<std::string, std::pair<int, std::string>> guessed_;

    // In a bank or card record the dollar lines are split lines. In an
    // investment record the dollar field is the amount transferred to the
    // account named in L, which is a different thing entirely -- so reading it
    // as a split made the reader ignore L and then skip the record as having no
    // category, losing the bank side of every transfer out of a brokerage.
    static bool has_split_lines(const Record& r) {
        return r.has('$') && r.section != Section::InvestmentTxns;
    }

    // A QIF account name becomes a path under the root its type implies. A colon
    // in the name would split it into two levels, so it is replaced and reported.
    void declare_accounts(const File& file) {
        any_account_block_ = any_account_block_ || !file.accounts.empty();
        for (const AccountRecord& a : file.accounts) {
            if (a.name.empty()) continue;
            const std::string path = account_path_of(a.qif_type, a.name);
            account_paths_[a.name] = path;
            const auto mapping = account_mapping_of(a.qif_type);
            add_account(path, mapping ? mapping->type : types::AccountType::Checking);
            if (a.name.find(':') != std::string::npos)
                note("NameChanged", "Account \"" + a.name + "\" became \"" +
                                    types::name_of(types::AccountPath(path)) + "\"");
            if (a.credit_limit) result_.credit_limits[path] = *a.credit_limit;
            if (a.stated_balance) {
                const auto as_of = dates::parse_qif(a.stated_balance_date_text, order_);
                if (as_of) result_.statement_balances[path] = {*a.stated_balance, *as_of};
            }
        }
    }

    // The category list is read before any transaction, because it is the only
    // thing in the file that says whether a name is income or an expense.
    void declare_categories(const File& file) {
        for (const CategoryRecord& c : file.categories)
            category_kind_[c.name] = c.is_income ? 'I' : 'E';
        // Declared in the list means it can be posted to, so it is not a
        // placeholder even where it is also a parent.
        for (const CategoryRecord& c : file.categories)
            add_account(category_path(c.name, Money::from_cents(c.is_income ? 1 : -1)),
                        c.is_income ? types::AccountType::Income
                                    : types::AccountType::Expense);
    }

    void read_records(const File& file) {
        for (const Record& r : file.records) {
            // This round imports the investment records that move cash somewhere
            // outside the account and leaves the securities side alone. The 21
            // records that name a category are why the section is in scope at
            // all; the other 848 are dividends reinvested, buys, sells and shares
            // in and out, which carry no category, are internal to the account,
            // and change no figure in a spending report. Including them put 2021
            // income at 1,430,167 against a plausible 130,000 to 190,000.
            if (r.section == Section::InvestmentTxns) {
                const std::string l = r.first('L');
                if (detail::trim(l).empty()) {
                    ++skipped_securities_;
                    ++skipped_by_account_[path_for_qif_account(r.account)];
                    continue;
                }
            }
            const std::string date_text = r.first('D');
            const auto date = dates::parse_qif(date_text, order_);
            if (!date) {
                // All or nothing: a book half imported is neither the old data
                // nor the new, with no way to tell which arrived.
                result_.refused = true;
                result_.refusal = "Line " + std::to_string(r.line) + ": " +
                                  date_text + " is not a date; nothing was imported";
                return;
            }
            // T is used and U is the same field written twice; a disagreement is
            // reported rather than resolved.
            const std::string t_text = r.has('T') ? r.first('T') : r.first('U');
            const auto amount = parse_amount(t_text, false);
            if (!amount) {
                result_.refused = true;
                result_.refusal = "Line " + std::to_string(r.line) + ": " + t_text +
                                  " is not an amount; nothing was imported";
                return;
            }
            if (r.has('T') && r.has('U') && r.first('T') != r.first('U')) {
                const auto u = parse_amount(r.first('U'), false);
                if (u && u->cents() != amount->cents())
                    note("AmountMismatch", date->iso() + " " + r.first('P') + ": U is " +
                                           u->in_register() + " and T is " +
                                           amount->in_register() + "; used " +
                                           amount->in_register());
            }

            ReadRecord out;
            out.account = path_for_qif_account(r.account);
            out.date = *date;
            out.amount = *amount;
            // An investment record writes its amount unsigned and says which way
            // it goes in the action: XIn is cash arriving, XOut is cash leaving.
            // Every one of the nineteen records in the business file that carries
            // both T and the dollar field has them equal, so the dollar field
            // adds nothing -- but reading the amount as already signed sent every
            // XOut the wrong way.
            if (r.section == Section::InvestmentTxns) {
                const std::string action = detail::lower(detail::trim(r.first('N')));
                const Money magnitude = out.amount.cents() < 0 ? -out.amount : out.amount;
                if (action == "xout" || action == "sellx" || action == "divx")
                    out.amount = -magnitude;
                else if (action == "xin" || action == "buyx")
                    out.amount = magnitude;
            }
            if (in_invoice_section(r)) {
                int x = 0;
                int a = 0;
                for (const std::string& v : r.all('X')) { (void)v; ++x; }
                for (const std::string& v : r.all('A'))
                    if (!detail::trim(v).empty()) ++a;
                if (x > 0 || a > 0) {
                    ++invoice_records_with_detail_;
                    dropped_x_fields_ += x;
                    dropped_address_lines_ += a;
                }
            }
            out.payee = r.first('P');
            out.memo = r.first('M');
            out.check_no = r.first('N');
            out.cleared_text = r.first('C');
            // The split lines decide the categories whenever there are any, so
            // the L field is only the category when there are none. That covers
            // both the literal "--Split--" and the records that carry a real
            // category alongside their splits.
            out.category_text = has_split_lines(r) ? std::string() : r.first('L');
            if (out.category_text == kSplitMarker) out.category_text.clear();
            result_.records.push_back(out);
            raw_.push_back(r);
            const std::string l = has_split_lines(r) ? std::string() : r.first('L');
            if (is_transfer_category(l)) {
                bracketed_by_account_[out.account].push_back(result_.records.size() - 1);
                ++bracketed_fields_;
            }
        }
    }

    // A transfer is written once in each account, so both halves are in the file
    // and one transaction has to come of them. Nothing weaker than all four tests
    // is safe: two 500.00 movements between one pair of accounts on one day are a
    // real thing, and collapsing those would lose money.
    void build_transactions() {
        collect_split_legs();
        std::vector<bool> used(result_.records.size(), false);
        for (std::size_t i = 0; i < result_.records.size(); ++i) {
            if (used[i]) continue;
            const ReadRecord& rec = result_.records[i];
            const Record& raw = raw_[i];

            // An invoice that was never collected. The amount owed goes on the
            // books, because it is owed, but none of it is income: the reports
            // are on a cash basis and no money came. A paid invoice falls through
            // to the ordinary category path below, which posts it in its own
            // period -- measured, that is where the reports being compared
            // against put it.
            if (is_document_record(i) && !was_paid(raw)) {
                // Listed whatever it bills for: the question "is anything
                // outstanding" is about what is owed, not about what counts as
                // income. Four of the nine in the business file are
                // inter-company claims worth 36,900.36, and leaving them off
                // the list would answer that question wrongly.
                const bool income = earns_income(raw);
                UncollectedInvoice open;
                open.date = rec.date;
                open.payee = rec.payee;
                open.amount = rec.amount;
                open.category = first_category_of(raw, rec);
                // A negative one is a credit note and a zero one is neither, so
                // calling either an unpaid invoice would mislead.
                open.note = rec.amount.cents() < 0 ? "A credit note, unapplied"
                          : rec.amount.cents() == 0 ? "A zero invoice"
                          : earns_income(raw)       ? "Not cleared"
                                                    : "Not cleared; an expense claim";
                result_.uncollected.push_back(open);
                note("InvoiceNotCollected",
                     rec.date.iso() + " " + rec.payee + " " +
                     abs_of(rec.amount).in_register() + ": " + open.note +
                     (income ? "" : "; it claims an expense, which is still"
                                    " recognised"));
                // Only income waits for the money. An uncollected claim for an
                // expense already paid falls through to the ordinary category
                // path below and credits it, which is what Quicken does.
                if (income) {
                    commit({posting(rec, rec.account, rec.amount),
                            posting(rec, holding_account_for(raw), -rec.amount)}, rec);
                    used[i] = true;
                    continue;
                }
            }

            const std::string bracketed = has_split_lines(raw) ? std::string() : raw.first('L');
            const bool is_transfer = is_transfer_category(bracketed);
            const std::string other =
                is_transfer ? path_for_qif_account(transfer_account_of(bracketed))
                            : std::string();

            // Quicken writes the opening balance of an account as a transfer from
            // the account to itself. Left alone that would be refused for posting
            // twice to one account, so it goes to Equity instead.
            if (is_transfer && other == rec.account) {
                add_account("Equity:Opening Balances", types::AccountType::Equity);
                commit({posting(rec, rec.account, rec.amount),
                        posting(rec, "Equity:Opening Balances", -rec.amount)}, rec);
                used[i] = true;
                continue;
            }

            if (is_transfer) {
                add_account_if_missing_for_transfer(other);
                // A split line in the other account may already be this very
                // transfer, in which case this record adds nothing and posting
                // it would count the movement twice in both accounts.
                if (const auto leg = find_split_leg(rec, other)) {
                    split_legs_[*leg].used = true;
                    ++result_.summary.duplicate;
                    used[i] = true;
                    continue;
                }
                const std::optional<std::size_t> partner = find_partner(i, other, used);
                if (partner) {
                    used[*partner] = true;
                    ++result_.summary.duplicate;
                    const long long gap = std::abs(types::Date::days_between(
                        rec.date, result_.records[*partner].date));
                    if (gap > kQuietGap)
                        note("NearTransfer", rec.date.iso() + " and " +
                                             result_.records[*partner].date.iso() +
                                             " between " + rec.account + " and " + other +
                                             " for " + abs_of(rec.amount).in_register() +
                                             " were paired " + std::to_string(gap) +
                                             " days apart");
                }
                // The payee and date are taken from the account the money left.
                const bool money_left_here = rec.amount.cents() < 0;
                const ReadRecord& from = money_left_here || !partner
                                             ? rec : result_.records[*partner];
                commit({posting(from, rec.account, rec.amount),
                        posting(from, other, -rec.amount)}, from);
                if (!partner)
                    note("UnpairedTransfer", rec.date.iso() + " " + rec.account + " to " +
                                             other + " " + abs_of(rec.amount).in_register() +
                                             ": only one half was in the file");
                used[i] = true;
                continue;
            }

            // Splits, or a single category, or neither.
            Part own_posting;
            own_posting.account = result_.accounts.ref_for_path(real_posting_account(rec.account));
            own_posting.amount = rec.amount;
            own_posting.memo = rec.memo;
            // This side is the record's own account by construction, so a
            // statement's row is cleared here whatever its C field says.
            own_posting.cleared = a_statement_ ? types::ClearedStatus::Cleared
                                               : cleared_of(rec.cleared_text);
            std::vector<Part> postings{own_posting};
            if (has_split_lines(raw)) {
                const std::vector<std::string>& cats = raw.all('S');
                const std::vector<std::string>& amounts = raw.all('$');
                const std::vector<std::string>& memos = raw.all('E');
                Money allocated;
                for (std::size_t k = 0; k < amounts.size(); ++k) {
                    const auto dollar = parse_amount(amounts[k], false);
                    if (!dollar) continue;
                    const Money line = posting_amount_of_split_line(*dollar);
                    allocated += *dollar;
                    const std::string cat = k < cats.size() ? cats[k] : std::string();
                    Part p = posting(rec, side_account(cat, line), line);
                    if (k < memos.size()) p.memo = memos[k];
                    postings.push_back(p);
                }
                // A split that does not add up is reported and balanced, because
                // refusing it would lose the record.
                const Money shortfall = rec.amount - allocated;
                if (shortfall.cents() != 0) {
                    postings.push_back(posting(rec, uncategorized(-shortfall), -shortfall));
                    note("SplitShort", rec.date.iso() + " " + rec.payee +
                                       ": split lines are " +
                                       abs_of(shortfall).in_register() + " short of " +
                                       abs_of(rec.amount).in_register() +
                                       "; put to Uncategorized");
                }
            } else {
                const Money other_side = -rec.amount;
                postings.push_back(
                    posting(rec, side_account(rec.category_text, other_side), other_side));
            }
            commit(postings, rec, tag_of(rec.category_text));
            used[i] = true;
        }
    }

    // The /Class suffix Quicken appends to a category, kept as a tag because
    // reporting by class is not in this release and discarding it during a
    // migration that runs once would lose it for good.
    static std::string tag_of(const std::string& category_text) {
        const std::string text = detail::trim(category_text);
        const auto slash = text.find('/');
        return slash == std::string::npos ? std::string() : text.substr(slash + 1);
    }


    static bool in_invoice_section(const Record& r) {
        return r.section == Section::InvoiceTxns || r.section == Section::BillTxns;
    }

    // Both kinds of record share the section and are told apart by L: a payment
    // names an account in brackets, a document names a category or --Split--.
    bool is_document_record(std::size_t i) const {
        const Record& raw = raw_[i];
        if (!in_invoice_section(raw)) return false;
        const std::string l = has_split_lines(raw) ? std::string() : raw.first('L');
        return !is_transfer_category(l);
    }

    // Whether the invoice was ever paid, which the export says outright: Quicken
    // marks a document cleared once the money is in and leaves the C field off
    // one still outstanding. 203 of the 212 documents in the business file carry
    // it; the nine that do not carry 975.00 of Consulting, 378.75 of
    // Uncategorized Income - Business and no Royalty, which is each of the three
    // figures the all-dates report gives, exactly.
    //
    // Worth recording what this replaced. Reconstructing which payment settled
    // which invoice -- by the N field, by N with its instalment suffix stripped,
    // by payee and amount, by letting a combined payment run on to the next open
    // invoice -- was tried twelve ways, and the best of them still put
    // uncollected Consulting at -80.65 against the 975.00 that is wanted. The
    // link is in the file but not reliably enough to post from. The cleared
    // marker is, and it is one field.
    static bool was_paid(const Record& raw) {
        return !detail::trim(raw.first('C')).empty();
    }

    // Whether the document bills for income or for an expense being claimed
    // back. The cleared marker governs income only: Quicken leaves an
    // uncollected invoice's income out of its cash-basis report but still
    // credits the expense an uncollected reimbursement invoice claims.
    //
    // Measured both ways. Treating every uncollected document alike moved 2025
    // expenses from -198,939.00, which agrees with Quicken to the cent, to
    // -212,342.46 -- out by 13,403.46, which is exactly the two reimbursement
    // invoices to Warp And Bytes and Ken Pugh, Inc. still outstanding at the
    // export. Applying it to income alone keeps 2025 exact and keeps all three
    // all-dates income figures exact as well.
    bool earns_income(const Record& raw) {
        if (!has_split_lines(raw)) {
            const std::string l = detail::trim(raw.first('L'));
            if (l.empty() || l == kSplitMarker) return true;   // Uncategorized
            return category_is_income(l);
        }
        // One kind or the other throughout, in every one of the 212 documents in
        // the business file, so the first line that says decides.
        for (const std::string& name : raw.all('S')) {
            const std::string text = detail::trim(name);
            if (!text.empty()) return category_is_income(text);
        }
        return true;
    }

    // The category list where the name is declared, and the chart where it is
    // not -- which is how a worked example says it, having been given a chart
    // rather than a !Type:Cat block. An unknown name counts as income, so an
    // uncollected invoice errs towards recognising nothing.
    bool category_is_income(const std::string& name) const {
        const std::string bare = name.substr(0, name.find('/'));
        const auto at = category_kind_.find(bare);
        if (at != category_kind_.end()) return at->second == 'I';
        if (result_.accounts.has("Expenses:" + bare)) return false;
        if (result_.accounts.has("Income:" + bare)) return true;
        return true;
    }

    // What the document bills for, for the uncollected list. The first line that
    // names something, because a document in these files bills for one kind.
    std::string first_category_of(const Record& raw, const ReadRecord& rec) {
        if (has_split_lines(raw)) {
            for (const std::string& name : raw.all('S')) {
                const std::string text = detail::trim(name);
                if (!text.empty())
                    return category_path(text.substr(0, text.find('/')), -rec.amount);
            }
            return std::string();
        }
        const std::string l = detail::trim(rec.category_text);
        if (l.empty()) return std::string();
        return category_path(l.substr(0, l.find('/')), -rec.amount);
    }

    // Where an invoice that was never collected is parked. A real account, so it
    // stays out of a spending report, and its balance is the whole of what was
    // invoiced and never earned -- 37,808.16 over twenty years.
    std::string holding_account_for(const Record& raw) {
        if (raw.section == Section::BillTxns) {
            add_account("Assets:Unrecognised Expense", types::AccountType::OtherAsset);
            return "Assets:Unrecognised Expense";
        }
        add_account("Liabilities:Unrecognised Income", types::AccountType::OtherDebt);
        return "Liabilities:Unrecognised Income";
    }

    // Every split line that names an account rather than a category.
    void collect_split_legs() {
        for (std::size_t i = 0; i < raw_.size(); ++i) {
            const Record& raw = raw_[i];
            if (!has_split_lines(raw)) continue;
            const std::vector<std::string>& cats = raw.all('S');
            const std::vector<std::string>& amounts = raw.all('$');
            for (std::size_t k = 0; k < amounts.size() && k < cats.size(); ++k) {
                if (!is_transfer_category(cats[k])) continue;
                const auto dollar = parse_amount(amounts[k], false);
                if (!dollar) continue;
                SplitLeg leg;
                leg.owner = result_.records[i].account;
                leg.target = path_for_qif_account(transfer_account_of(cats[k]));
                leg.to_target = posting_amount_of_split_line(*dollar);
                leg.date = result_.records[i].date;
                split_legs_.push_back(leg);
            }
        }
    }

    // A whole record is the same transfer as a split line when the line sits in
    // the account the record points at, names the record's own account back, and
    // posts the record's own amount to it. The nearest date wins, as for any
    // other pairing.
    std::optional<std::size_t> find_split_leg(const ReadRecord& rec,
                                              const std::string& other) {
        std::optional<std::size_t> best;
        long long best_gap = 0;
        for (std::size_t k = 0; k < split_legs_.size(); ++k) {
            SplitLeg& leg = split_legs_[k];
            if (leg.used) continue;
            if (leg.owner != other) continue;
            if (leg.target != rec.account) continue;
            if (leg.to_target.cents() != rec.amount.cents()) continue;
            const long long gap = std::abs(types::Date::days_between(rec.date, leg.date));
            if (!best || gap < best_gap) { best = k; best_gap = gap; }
        }
        return best;
    }

    // The accounts have to cross-match and the amounts have to be equal and
    // opposite. The dates do not have to agree, and requiring that was wrong:
    // of the 4,025 pairs in the business file only 3,053 share a date, 558 are a
    // day apart, 180 are three days apart and 19 are more than ten. Requiring
    // the same date left 972 pairs unmatched, and an unmatched half still posts
    // both of its sides -- so every one of them counted its transfer twice in
    // both accounts. That is what made the card balances tens of thousands out.
    //
    // The nearest date wins, so two separate transfers of one amount between one
    // pair of accounts still pair with the right halves.
    std::optional<std::size_t> find_partner(std::size_t self, const std::string& other,
                                            const std::vector<bool>& used) const {
        const ReadRecord& a = result_.records[self];
        const auto at = bracketed_by_account_.find(other);
        if (at == bracketed_by_account_.end()) return std::nullopt;
        std::optional<std::size_t> best;
        long long best_gap = 0;
        for (std::size_t j : at->second) {
            if (j == self || used[j]) continue;
            const ReadRecord& b = result_.records[j];
            if (a.account != partner_target(j)) continue;
            if (b.amount.cents() != -a.amount.cents()) continue;
            const long long gap = std::abs(types::Date::days_between(a.date, b.date));
            if (!best || gap < best_gap) { best = j; best_gap = gap; }
        }
        return best;
    }

    // How far apart a pair may be before it is worth mentioning. Five days takes
    // in all but nineteen of the pairs in the business file.
    static constexpr long long kQuietGap = 5;

    std::string partner_target(std::size_t index) const {
        const Record& raw = raw_[index];
        const std::string l = has_split_lines(raw) ? std::string() : raw.first('L');
        return is_transfer_category(l) ? path_for_qif_account(transfer_account_of(l))
                                       : std::string();
    }

    // A posting cannot go to a placeholder. An investment account that is a
    // placeholder holds its cash in a Cash child created for it; a record whose
    // target is such a placeholder posts there instead.
    std::string real_posting_account(const std::string& path) const {
        const chart::Account* a = result_.accounts.find(path);
        if (!a || !a->placeholder) return path;
        const std::string cash = path + ":Cash";
        return result_.accounts.has(cash) ? cash : path;
    }

    // The side the other posting goes to: a category where there is one, and
    // otherwise Uncategorized chosen by the sign of that posting.
    std::string side_account(const std::string& category_text, const Money& amount) {
        const std::string text = detail::trim(category_text);
        if (text.empty() || text == kSplitMarker) return uncategorized(amount);
        if (is_transfer_category(text)) {
            const std::string path = path_for_qif_account(transfer_account_of(text));
            add_account_if_missing_for_transfer(path);
            return path;
        }
        const std::string name = text.substr(0, text.find('/'));  // drop the tag
        const std::string path = category_path(name, amount);
        // Reported only where the sign really did decide it. A name the chart
        // already places was not guessed, and saying it was sent a reader
        // looking for a problem that is not there.
        if (!category_known(name)) {
            auto& entry = guessed_[name];
            ++entry.first;
            entry.second = path;
        }
        add_account(path, amount.cents() > 0 ? types::AccountType::Expense
                                             : types::AccountType::Income);
        return path;
    }

    std::string uncategorized(const Money& amount) {
        const std::string path = amount.cents() > 0 ? "Expenses:Uncategorized"
                                                    : "Income:Uncategorized";
        add_account(path, amount.cents() > 0 ? types::AccountType::Expense
                                             : types::AccountType::Income);
        return path;
    }

    // The root comes from the category list where the name is declared, and from
    // the sign of the amount where it is not -- and every use of that fallback is
    // reported, because an undeclared category is worth looking at once.
    //
    // Declared in the category list only: an account created by an earlier
    // guess at this same name is not evidence that this one was not guessed
    // too, or a second use of the fallback for one category would read as if
    // it only happened once.
    bool category_known(const std::string& name) const {
        return category_kind_.count(name) != 0;
    }

    std::string category_path(const std::string& name, const Money& amount) const {
        const auto at = category_kind_.find(name);
        if (at != category_kind_.end())
            return (at->second == 'I' ? "Income:" : "Expenses:") + name;
        // Not declared in the category list. A chart that already places it is
        // better evidence than the sign of one posting: a refund to an expense
        // category is a credit, and the sign alone would read that as income.
        if (result_.accounts.has("Expenses:" + name)) return "Expenses:" + name;
        if (result_.accounts.has("Income:" + name)) return "Income:" + name;
        return (amount.cents() < 0 ? "Income:" : "Expenses:") + name;
    }

    std::string path_for_qif_account(const std::string& name) const {
        if (name.empty()) return target_;
        const auto at = account_paths_.find(name);
        if (at != account_paths_.end()) return at->second;
        // Already a path, as the import target is.
        if (name.find(':') != std::string::npos) return name;
        return "Assets:" + name;
    }

    // Brackets mean an account, so a bracketed name that is not in the account
    // list is an account that was not exported rather than a category.
    void add_account_if_missing_for_transfer(const std::string& path) {
        if (result_.accounts.has(path)) return;
        add_account(path, types::AccountType::Checking);
        note("AccountGuessed", "\"[" + types::name_of(types::AccountPath(path)) +
                               "]\" was not in the account list; made " + path +
                               " of type Bank");
    }

    void add_account(const std::string& path, types::AccountType type) {
        if (path.empty() || result_.accounts.has(path)) return;
        std::vector<chart::Account> created;
        result_.accounts.add(types::AccountPath(path), type, &created);
        for (const chart::Account& a : created) result_.accounts_created.push_back(a);
    }

    // One side of a record being built, before it becomes either the whole
    // of a transaction (exactly two parts) or its source plus a split's
    // lines (more than two). Posting was the stored shape for this; it no
    // longer is, but the record-reading code below still finds it out one
    // side at a time, which is all this is for.
    struct Part {
        chart::Ref account;
        Money amount;
        std::string memo;
        types::ClearedStatus cleared = types::ClearedStatus::Uncleared;
    };

    Part posting(const ReadRecord& rec, const std::string& account,
                const Money& amount) const {
        Part p;
        p.account = result_.accounts.ref_for_path(account);
        p.amount = amount;
        p.memo = rec.memo;
        // The cleared marker belongs to the account whose section the record is
        // in; the other side of the transaction is left uncleared. A statement
        // states nothing about it either way -- the bank sent it, so it
        // happened -- and the marker in the file is not read.
        //
        // A statement is where the record's own account can be unnamed: a
        // download of one account has no !Account block, so the side the file
        // is about is the account the import was pointed at. The history always
        // names its accounts, and is left reading exactly what it read before.
        const bool own_side = account == rec.account ||
                              (rec.account.empty() && account == target_);
        p.cleared = a_statement_
                        ? (own_side ? types::ClearedStatus::Cleared
                                    : types::ClearedStatus::Uncleared)
                        : (account == rec.account ? cleared_of(rec.cleared_text)
                                                  : types::ClearedStatus::Uncleared);
        return p;
    }

    void commit(const std::vector<Part>& parts, const ReadRecord& from,
                const std::string& tag = {}) {
        ledger::Transaction t;
        t.id = ledger::new_id();
        t.ref = types::TransactionRef("T" + std::to_string(result_.transactions.size() + 1));
        t.date = from.date;
        t.payee = types::PayeeName(from.payee);
        t.check_no = types::CheckNumber(from.check_no);
        t.memo = from.memo;
        t.tag = tag;
        t.source_account = parts.front().account;
        t.amount = parts.front().amount;
        t.cleared = parts.front().cleared;
        if (parts.size() == 2) {
            t.destination = parts[1].account;
        } else {
            t.destination = chart::split_ref();
        }
        if (!tag.empty()) ++tags_kept_;
        result_.transactions.push_back(t);
        ++result_.summary.created;
        if (parts.size() > 2)
            for (std::size_t i = 1; i < parts.size(); ++i) {
                ledger::TransactionSplit s;
                s.transaction_id = t.id;
                s.destination = parts[i].account;
                s.amount = parts[i].amount;
                s.memo = parts[i].memo;
                result_.splits.push_back(s);
            }
        for (const Part& p : parts) {
            const ledger::DatedPosting dp{from.date, types::AccountPath(result_.accounts.path_for_ref(p.account)),
                                          p.amount, t.ref.value()};
            result_.postings.push_back(dp);
            result_.book.add(dp);
        }
    }

    static Money abs_of(const Money& m) { return m.cents() < 0 ? -m : m; }

    void note(const std::string& kind, const std::string& detail) {
        result_.notes.push_back({kind, detail});
    }

    void report_guessed_categories() {
        for (const auto& entry : guessed_)
            note("CategoryGuessed",
                 "\"" + entry.first + "\" was not in the category list; made " +
                 entry.second.second + " from the sign, " +
                 std::to_string(entry.second.first) + " transactions");
    }

    std::vector<Record> raw_;
    std::vector<SplitLeg> split_legs_;
    // Which records are transfer halves, by the account they sit in.
    std::map<std::string, std::vector<std::size_t>> bracketed_by_account_;
    // What an invoice record carries that this round does not import: the X
    // extension fields and the customer's address. Counted rather than dropped
    // quietly, because a reader should know what was left behind.
    int invoice_records_with_detail_ = 0;
    int dropped_x_fields_ = 0;
    int dropped_address_lines_ = 0;

    int skipped_securities_ = 0;
    std::map<std::string, int> skipped_by_account_;
    bool any_account_block_ = false;
    int bracketed_fields_ = 0;
    int tags_kept_ = 0;
};

}  // namespace qif
