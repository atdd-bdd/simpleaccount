#pragma once
#include <string>

namespace progress {

// What the window says about work that is taking a while. There is no dialog
// here and no thread: a Report is the strip's text and the two numbers behind
// it, and whatever is doing the work hands one back as it goes. Keeping it to
// a value means the words can be stated in a scenario without a real file and
// a real clock. See the noticeable-time rule in UserInterface.spectable.
struct Report {
    std::string what;
    long long done = 0;
    long long total = 0;
    // What is being counted, where it is not rows: a report over twenty years
    // counts years. Empty is the usual case and the count then reads as a bare
    // pair of numbers.
    std::string unit;
    bool finished = false;
};

// The thousand separators a person reads by. Money does its own grouping over
// cents; this is the same idea over a count, and the two are kept apart rather
// than one borrowing the other's decimal point.
inline std::string grouped(long long value) {
    const bool negative = value < 0;
    std::string digits = std::to_string(negative ? -value : value);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
        digits.insert(static_cast<std::size_t>(i), ",");
    return negative ? "-" + digits : digits;
}

// Where there is a total the count goes towards it, because a count with a
// total is a time estimate and a count alone is not. Work with no total says
// what it is doing and stops there: a number with nothing to be a fraction of
// tells the reader nothing they can use.
inline std::string reads(const Report& report) {
    if (report.total <= 0) return report.what;
    std::string out = report.what + " - " + grouped(report.done) + " of " +
                      grouped(report.total);
    if (!report.unit.empty()) out += " " + report.unit;
    return out;
}

// A strip that appears and goes in the same blink is worse than none at all --
// the person sees a flicker and looks for what it said. So the report is asked
// for at the start and shown only if the work is still going a second later.
inline const double kThresholdSeconds = 1.0;

inline bool shows(const Report& report, double seconds_running) {
    return !report.finished && seconds_running >= kThresholdSeconds;
}

// How far it has got. Reaching the total is the end of the work, and a
// finished report is no report: the strip is not a log, and what the work did
// is said in the work's own notes.
inline Report advanced(const Report& report, long long done) {
    Report out = report;
    out.done = done;
    if (out.total > 0 && out.done >= out.total) out.finished = true;
    return out;
}

// Stopping. The only thing the strip offers is a way to stop, and a stopped
// job is over the same as a finished one. Nothing is undone here because
// nothing was written: the work computes, and it is the writing at the end
// that changes the book.
inline Report stopped(const Report& report) {
    Report out = report;
    out.finished = true;
    return out;
}

}  // namespace progress
