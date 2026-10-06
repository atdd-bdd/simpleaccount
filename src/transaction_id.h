#pragma once
#include <chrono>
#include <string>
#include <thread>

#include "text_types.h"

// The name a transaction is stored under. See the naming rule in
// Transactions.spectable.
//
// It is the moment the transaction was created, to the millisecond, and it is
// given once and never changed. The generator waits if the clock has not moved
// since the last name it gave out, so two names can never be the same however
// fast they are asked for.
//
// Why not the identifier the file gave it. A FITID or a CSV row number is a fact
// about a file, and a transaction can collect one of those from every kind of
// file it is ever recognised in -- a row imported from a CSV and later matched
// in a QFX has a name in each, which is why ImportIds is a collection. Identity
// cannot be one of several, and it cannot change when a later import learns
// something, so it is kept apart from all of them. The import identifiers still
// do the work they always did: they are what makes the same file read twice
// import nothing the second time.
//
// The cost is a millisecond for each transaction created in a run. That is
// nothing while a person types, and about forty-three seconds for the forty-
// three thousand transactions of the business QIF -- paid once, on the slowest
// thing the program already does. What it buys is that a transaction can be
// traced: a name carrying the minute it was entered answers "when did this
// appear, and what else appeared with it" without anything having had to record
// it.
namespace ledger {

// Monotonic to the millisecond, by waiting rather than by counting. A counter
// would have to be stored somewhere and would disagree with the clock after a
// restart; the clock is already there and is the thing being recorded.
class IdGenerator {
public:
    // The name for a transaction created now. Never the same twice from one
    // generator, because it will not return until the clock has moved past the
    // last name it gave.
    std::string next() {
        long long millis = now_millis();
        while (millis <= last_) {
            // A millisecond is the smallest wait that can change the answer, so
            // it is the whole of the delay. Sleeping is better than spinning
            // here: during a migration this runs tens of thousands of times and
            // a busy loop would take a core with it.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            millis = now_millis();
        }
        last_ = millis;
        return format(millis);
    }

    // The format, separately, so a test or a migration can state a moment
    // without waiting for it to arrive.
    static std::string format(long long millis) {
        const std::time_t seconds = static_cast<std::time_t>(millis / 1000);
        const int remainder = static_cast<int>(millis % 1000);
        std::tm when{};
#ifdef _WIN32
        localtime_s(&when, &seconds);
#else
        localtime_r(&seconds, &when);
#endif
        std::string out;
        out += pad(when.tm_year + 1900, 4);
        out += pad(when.tm_mon + 1, 2);
        out += pad(when.tm_mday, 2);
        out += 'T';
        out += pad(when.tm_hour, 2);
        out += pad(when.tm_min, 2);
        out += pad(when.tm_sec, 2);
        out += pad(remainder, 3);
        return out;
    }

private:
    long long last_ = 0;

    static long long now_millis() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    static std::string pad(int value, int width) {
        std::string s = std::to_string(value);
        while (static_cast<int>(s.size()) < width) s.insert(s.begin(), '0');
        return s;
    }
};

// The one generator a program uses. One per process rather than one per caller:
// two generators could each be waiting on their own last name and hand out the
// same one, which is the whole thing this is here to prevent.
inline IdGenerator& ids() {
    static IdGenerator one;
    return one;
}

inline types::TransactionId new_id() { return types::TransactionId(ids().next()); }

}  // namespace ledger
