#pragma once
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

#include "date.h"
#include "money.h"

// Reading an OFX or QFX file into statements and transactions. See
// ImportOfx.spectable.
//
// One tokeniser serves both shapes the format comes in. OFX 1 is SGML: a tag
// opens, its value runs to the next tag, and the closing tag is optional. OFX 2
// is XML and closes everything. Reading both as "a tag, then whatever text
// follows it" costs nothing and means the version never has to be detected.
namespace ofx {

enum class AcctType { Checking, Savings, MoneyMarket, CreditLine, CreditCard, Investment };

inline std::string to_string(AcctType t) {
    switch (t) {
        case AcctType::Checking:    return "CHECKING";
        case AcctType::Savings:     return "SAVINGS";
        case AcctType::MoneyMarket: return "MONEYMRKT";
        case AcctType::CreditLine:  return "CREDITLINE";
        case AcctType::CreditCard:  return "CREDITCARD";
        case AcctType::Investment:  return "INVESTMENT";
    }
    return "CHECKING";
}

inline AcctType acct_type_from(const std::string& text) {
    if (text == "SAVINGS")    return AcctType::Savings;
    if (text == "MONEYMRKT")  return AcctType::MoneyMarket;
    if (text == "CREDITLINE") return AcctType::CreditLine;
    if (text == "CREDITCARD") return AcctType::CreditCard;
    if (text == "INVESTMENT") return AcctType::Investment;
    return AcctType::Checking;
}

struct Transaction {
    std::string trn_type = "DEBIT";
    types::Date date_posted{2024, 1, 1};
    Money amount;
    std::string fit_id;
    std::string name;
    std::string memo;
    std::string check_num;
};

struct Statement {
    std::string bank_id;
    std::string account_id;
    AcctType acct_type = AcctType::Checking;
    std::string currency = "USD";
    types::Date start_date{2024, 1, 1};
    types::Date end_date{2024, 1, 31};
    Money ledger_balance;
    types::Date ledger_balance_at{2024, 1, 31};
    std::vector<Transaction> transactions;
};

struct Read {
    std::vector<Statement> statements;
    bool refused = false;
    std::string refusal;
};

namespace detail {

struct Token {
    std::string tag;      // upper case, without the angle brackets or the slash
    bool closing = false;
    std::string value;    // the text that followed an opening tag
};

inline std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

inline std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

inline std::vector<Token> tokenise(const std::string& text) {
    std::vector<Token> out;
    std::size_t at = 0;
    while (true) {
        const std::size_t open = text.find('<', at);
        if (open == std::string::npos) break;
        const std::size_t close = text.find('>', open);
        if (close == std::string::npos) break;

        std::string inside = text.substr(open + 1, close - open - 1);
        at = close + 1;
        if (inside.empty() || inside[0] == '?' || inside[0] == '!') continue;

        Token token;
        if (inside[0] == '/') {
            token.closing = true;
            inside.erase(0, 1);
        }
        // An XML tag may carry attributes; nothing here needs them.
        const std::size_t space = inside.find_first_of(" \t\r\n");
        if (space != std::string::npos) inside.erase(space);
        if (!inside.empty() && inside.back() == '/') inside.pop_back();   // <TAG/>
        token.tag = upper(trim(inside));
        if (token.tag.empty()) continue;

        if (!token.closing) {
            const std::size_t next = text.find('<', at);
            token.value = trim(text.substr(at, next == std::string::npos
                                                   ? std::string::npos : next - at));
        }
        out.push_back(token);
    }
    return out;
}

// OFX writes a date as YYYYMMDD and may append a time and a time zone:
// 20240115120000[-5:EST]. The day is what matters here, and the time zone is
// deliberately ignored -- shifting a posting date by a zone would move a
// transaction between months, and the bank means the day it printed.
inline std::optional<types::Date> date_of(const std::string& text) {
    std::string digits;
    for (char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) break;
        digits.push_back(c);
    }
    if (digits.size() < 8) return std::nullopt;
    const int year = std::stoi(digits.substr(0, 4));
    const int month = std::stoi(digits.substr(4, 2));
    const int day = std::stoi(digits.substr(6, 2));
    if (month < 1 || month > 12 || day < 1 || day > 31) return std::nullopt;
    return types::Date{year, month, day};
}

inline std::optional<Money> amount_of(const std::string& text) {
    std::string cleaned;
    for (char c : text) {
        if (c == ',' || c == ' ') continue;
        cleaned.push_back(c);
    }
    if (cleaned.empty()) return std::nullopt;
    try {
        return Money(cleaned);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}  // namespace detail

// A file may hold more than one statement: a download covering a chequing
// account and a card arrives as one file with two STMTRS blocks.
inline Read read(const std::string& text) {
    Read out;
    const std::vector<detail::Token> tokens = detail::tokenise(text);

    Statement* current = nullptr;
    Transaction* line = nullptr;
    bool in_balance = false;

    const auto start_statement = [&]() {
        out.statements.push_back(Statement());
        current = &out.statements.back();
        line = nullptr;
    };

    for (const detail::Token& t : tokens) {
        // A statement begins at its own block rather than at the account block,
        // because a card statement names the account in CCACCTFROM and a bank
        // one in BANKACCTFROM.
        if (!t.closing && (t.tag == "STMTRS" || t.tag == "CCSTMTRS")) {
            start_statement();
            if (t.tag == "CCSTMTRS") current->acct_type = AcctType::CreditCard;
            continue;
        }
        if (current == nullptr) continue;

        if (!t.closing && t.tag == "STMTTRN") {
            current->transactions.push_back(Transaction());
            line = &current->transactions.back();
            continue;
        }
        if (t.closing && t.tag == "STMTTRN") {
            line = nullptr;
            continue;
        }
        if (!t.closing && t.tag == "LEDGERBAL") {
            in_balance = true;
            continue;
        }
        if (t.closing && t.tag == "LEDGERBAL") {
            in_balance = false;
            continue;
        }
        if (t.closing) continue;

        if (line != nullptr) {
            if (t.tag == "TRNTYPE") line->trn_type = detail::upper(t.value);
            else if (t.tag == "DTPOSTED") {
                if (const auto on = detail::date_of(t.value)) line->date_posted = *on;
                else {
                    out.refused = true;
                    out.refusal = t.value + " is not a date; nothing was imported";
                    return out;
                }
            } else if (t.tag == "TRNAMT") {
                if (const auto amount = detail::amount_of(t.value)) line->amount = *amount;
                else {
                    out.refused = true;
                    out.refusal = t.value + " is not an amount; nothing was imported";
                    return out;
                }
            }
            else if (t.tag == "FITID")    line->fit_id = t.value;
            else if (t.tag == "NAME")     line->name = t.value;
            else if (t.tag == "MEMO")     line->memo = t.value;
            else if (t.tag == "CHECKNUM") line->check_num = t.value;
            continue;
        }

        if (in_balance) {
            if (t.tag == "BALAMT") {
                if (const auto amount = detail::amount_of(t.value))
                    current->ledger_balance = *amount;
            } else if (t.tag == "DTASOF") {
                if (const auto on = detail::date_of(t.value)) current->ledger_balance_at = *on;
            }
            continue;
        }

        if (t.tag == "BANKID")        current->bank_id = t.value;
        else if (t.tag == "ACCTID")   current->account_id = t.value;
        else if (t.tag == "ACCTTYPE") current->acct_type = acct_type_from(detail::upper(t.value));
        else if (t.tag == "CURDEF")   current->currency = t.value;
        else if (t.tag == "DTSTART") {
            if (const auto on = detail::date_of(t.value)) current->start_date = *on;
        } else if (t.tag == "DTEND") {
            if (const auto on = detail::date_of(t.value)) current->end_date = *on;
        }
    }

    if (out.statements.empty()) {
        out.refused = true;
        out.refusal = "no statement was found in the file; nothing was imported";
    }
    return out;
}

}  // namespace ofx
