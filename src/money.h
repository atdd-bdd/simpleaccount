#pragma once
#include <cstdlib>
#include <stdexcept>
#include <string>

// A signed amount in the currency of the book, held as whole cents so that a
// column of them sums exactly. See CoreTypes.spectable.
//
// Parsing accepts what a person or a bank file writes: a dollar sign, thousands
// separators, a leading minus, or accounting parentheses. It does not round: a
// third decimal place is rejected rather than quietly turned into a cent that
// nobody wrote.
class Money {
public:
    Money() = default;

    static Money from_cents(long long cents) {
        Money m;
        m.cents_ = cents;
        return m;
    }

    // Throws std::invalid_argument on anything the rule does not allow.
    explicit Money(const std::string& text) : cents_(to_cents(text)) {}

    static bool is_valid(const std::string& text) {
        try {
            to_cents(text);
            return true;
        } catch (const std::invalid_argument&) {
            return false;
        }
    }

    long long cents() const { return cents_; }

    // The register writes a negative amount with a leading minus.
    std::string in_register() const { return grouped(cents_ < 0, "-", ""); }

    // A report writes it in parentheses, which is the convention there.
    std::string in_report() const { return grouped(cents_ < 0, "(", ")"); }

    Money operator-() const { return from_cents(-cents_); }
    Money operator+(const Money& o) const { return from_cents(cents_ + o.cents_); }
    Money operator-(const Money& o) const { return from_cents(cents_ - o.cents_); }
    Money& operator+=(const Money& o) { cents_ += o.cents_; return *this; }
    bool operator==(const Money& o) const { return cents_ == o.cents_; }
    bool operator!=(const Money& o) const { return cents_ != o.cents_; }
    bool operator<(const Money& o) const { return cents_ < o.cents_; }

private:
    long long cents_ = 0;

    static long long to_cents(const std::string& raw) {
        std::string s = trim(raw);
        if (s.empty()) throw std::invalid_argument("an amount is required");

        bool negative = false;
        // Accounting parentheses mean negative, and wrap everything else.
        if (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
            negative = true;
            s = trim(s.substr(1, s.size() - 2));
        }
        if (!s.empty() && (s.front() == '-' || s.front() == '+')) {
            if (s.front() == '-') negative = !negative;
            s.erase(0, 1);
        }
        if (!s.empty() && s.front() == '$') s.erase(0, 1);
        if (s.empty()) throw std::invalid_argument("no digits in the amount");

        std::string whole;
        std::string frac;
        bool seen_point = false;
        for (char c : s) {
            if (c == ',') {
                // A separator belongs between digits of the whole part only.
                if (seen_point || whole.empty())
                    throw std::invalid_argument("misplaced thousands separator");
                continue;
            }
            if (c == '.') {
                if (seen_point) throw std::invalid_argument("two decimal points");
                seen_point = true;
                continue;
            }
            if (c < '0' || c > '9') throw std::invalid_argument("not a number");
            (seen_point ? frac : whole) += c;
        }
        if (whole.empty() && frac.empty()) throw std::invalid_argument("not a number");
        if (frac.size() > 2) throw std::invalid_argument("more than two decimal places");

        // One decimal place is tenths, none is whole units.
        while (frac.size() < 2) frac += '0';

        const long long units = whole.empty() ? 0 : std::stoll(whole);
        const long long hundredths = std::stoll(frac);
        const long long total = units * 100 + hundredths;
        return negative ? -total : total;
    }

    std::string grouped(bool negative, const std::string& open,
                        const std::string& close) const {
        const long long abs_cents = cents_ < 0 ? -cents_ : cents_;
        const long long units = abs_cents / 100;
        const long long rest = abs_cents % 100;

        std::string digits = std::to_string(units);
        for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
            digits.insert(static_cast<std::size_t>(i), ",");

        std::string out = digits + ".";
        out += static_cast<char>('0' + rest / 10);
        out += static_cast<char>('0' + rest % 10);
        return negative ? open + out + close : out;
    }

    static std::string trim(const std::string& s) {
        std::size_t b = s.find_first_not_of(" \t");
        if (b == std::string::npos) return "";
        std::size_t e = s.find_last_not_of(" \t");
        return s.substr(b, e - b + 1);
    }
};
