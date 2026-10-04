#pragma once
#include <optional>
#include <stdexcept>
#include <string>

namespace types {

// Which of the first two numbers in a date is the month. Stated in a profile
// rather than detected: a file whose dates are all before the thirteenth of the
// month is genuinely ambiguous, and guessing wrong moves a history by up to
// eleven months without saying so. See ImportCsv.spectable.
enum class DateOrder { MDY, DMY, YMD };

inline DateOrder date_order_from_string(const std::string& s) {
    if (s == "MDY") return DateOrder::MDY;
    if (s == "DMY") return DateOrder::DMY;
    if (s == "YMD") return DateOrder::YMD;
    throw std::invalid_argument("unknown date order: " + s);
}

inline std::string to_string(DateOrder o) {
    switch (o) {
        case DateOrder::MDY: return "MDY";
        case DateOrder::DMY: return "DMY";
        case DateOrder::YMD: return "YMD";
    }
    throw std::invalid_argument("unknown date order");
}

// A date to the day. A register is kept by date, so no time is carried: keeping
// one would make the same transaction sort differently depending on the reader's
// timezone.
class Date {
public:
    Date() = default;
    Date(int year, int month, int day) : y_(year), m_(month), d_(day) {
        if (!is_valid(year, month, day))
            throw std::invalid_argument("not a date: " + iso());
    }

    static bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

    static int days_in_month(int y, int m) {
        static const int len[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (m < 1 || m > 12) return 0;
        if (m == 2 && is_leap(y)) return 29;
        return len[m - 1];
    }

    static bool is_valid(int y, int m, int d) {
        if (y < 1 || m < 1 || m > 12 || d < 1) return false;
        return d <= days_in_month(y, m);
    }

    // The one written form, which is also how a spec table writes a date.
    std::string iso() const {
        std::string s = pad(y_, 4);
        s += '-';
        s += pad(m_, 2);
        s += '-';
        s += pad(d_, 2);
        return s;
    }

    static std::optional<Date> from_iso(const std::string& text) {
        if (text.size() != 10 || text[4] != '-' || text[7] != '-') return std::nullopt;
        for (std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u})
            if (text[i] < '0' || text[i] > '9') return std::nullopt;
        const int y = std::stoi(text.substr(0, 4));
        const int m = std::stoi(text.substr(5, 2));
        const int d = std::stoi(text.substr(8, 2));
        if (!is_valid(y, m, d)) return std::nullopt;
        return Date(y, m, d);
    }

    int year() const { return y_; }
    int month() const { return m_; }
    int day() const { return d_; }

    // Comparison is lexicographic on the three parts, which is also date order.
    bool operator==(const Date& o) const { return key() == o.key(); }
    bool operator!=(const Date& o) const { return key() != o.key(); }
    bool operator<(const Date& o) const { return key() < o.key(); }
    bool operator<=(const Date& o) const { return key() <= o.key(); }
    bool operator>(const Date& o) const { return key() > o.key(); }
    bool operator>=(const Date& o) const { return key() >= o.key(); }

    // Days between two dates, by day number, so a window can be measured
    // without caring which side of a month end it falls.
    static long long days_between(const Date& a, const Date& b) {
        return day_number(b) - day_number(a);
    }

    Date plus_days(long long n) const { return from_day_number(day_number(*this) + n); }

    // The first and last day of the month this date falls in.
    Date start_of_month() const { return Date(y_, m_, 1); }
    Date end_of_month() const { return Date(y_, m_, days_in_month(y_, m_)); }

    // Days since an arbitrary fixed point, by the usual civil-calendar formula.
    static long long day_number(const Date& dt) {
        long long y = dt.y_;
        long long m = dt.m_;
        if (m <= 2) { y -= 1; m += 12; }
        const long long era = (y >= 0 ? y : y - 399) / 400;
        const long long yoe = y - era * 400;
        const long long doy = (153 * (m - 3) + 2) / 5 + dt.d_ - 1;
        const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }

    static Date from_day_number(long long z) {
        z += 719468;
        const long long era = (z >= 0 ? z : z - 146096) / 146097;
        const long long doe = z - era * 146097;
        const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const long long y = yoe + era * 400;
        const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const long long mp = (5 * doy + 2) / 153;
        const long long d = doy - (153 * mp + 2) / 5 + 1;
        const long long m = mp < 10 ? mp + 3 : mp - 9;
        return Date(static_cast<int>(m <= 2 ? y + 1 : y), static_cast<int>(m),
                    static_cast<int>(d));
    }

private:
    int y_ = 2024, m_ = 1, d_ = 1;

    long long key() const { return (y_ * 10000LL) + (m_ * 100LL) + d_; }

    static std::string pad(int v, int width) {
        std::string s = std::to_string(v);
        while (static_cast<int>(s.size()) < width) s.insert(s.begin(), '0');
        return s;
    }
};

// A two-digit year, which needs a window. Years 70 to 99 are nineteen hundreds
// and 00 to 69 are two thousands: a file old enough to write dates this way will
// not contain 2070. See ImportCsv.spectable.
inline int year_from_two_digits(int n) { return n >= 70 ? 1900 + n : 2000 + n; }

}  // namespace types
