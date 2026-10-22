#pragma once
#include <string>
#include <vector>
#include "money.h"
#include "register_lines.h"

namespace edit {

// Saving a split. A split is edited in a window of its own, on a transaction
// that already has an amount, so the window knows what its lines are supposed
// to come to. That total is checked when the window is saved and a split that
// does not match it is not quietly written. See "A split is saved only when its
// lines match the total it was opened on" in Transactions.spectable.
//
// The total is a magnitude rather than the transaction's signed amount: a
// payment of 184.50 is written -184.50 and its lines are positive, and the
// question the window asks is about 184.50 either way.

// What the user answers when a split does not match its total. Cancel and edit
// are both "do not save this", and the difference between them is the only
// thing about this dialog anyone has to think about: edit keeps the typed lines
// and stays, cancel discards them and leaves.
enum class SplitMismatchChoice { CancelSave, EditSplit, AddRemainder };

inline const char* kUncategorized = "Expenses:Uncategorized";

inline std::string to_string(SplitMismatchChoice choice) {
    switch (choice) {
        case SplitMismatchChoice::CancelSave:   return "CancelSave";
        case SplitMismatchChoice::EditSplit:    return "EditSplit";
        case SplitMismatchChoice::AddRemainder: return "AddRemainder";
    }
    return "CancelSave";
}

inline SplitMismatchChoice split_choice_from_string(const std::string& text) {
    if (text == "EditSplit") return SplitMismatchChoice::EditSplit;
    if (text == "AddRemainder") return SplitMismatchChoice::AddRemainder;
    return SplitMismatchChoice::CancelSave;
}

// What the window says at the moment of saving. Matching is the ordinary case
// and says nothing; the rest is the exception.
struct SplitCheck {
    bool matches = true;
    Money total;
    Money allocated;
    // What is left to allocate: the total less the lines, so short by thirty
    // is 30.00 and over by thirty is -30.00. A line of -30.00 under
    // Uncategorized is honest about where the arithmetic went.
    Money remainder;
    // Said rather than only reported: the user is about to choose between the
    // two figures, so both are in the sentence.
    std::string message;
    std::string options;
};

inline Money allocated_in(const std::vector<reg::SplitLine>& lines) {
    Money out;
    for (const reg::SplitLine& line : lines) out = out + line.amount;
    return out;
}

inline SplitCheck split_save_check(const Money& total, const Money& allocated) {
    SplitCheck out;
    out.total = total.cents() < 0 ? -total : total;
    out.allocated = allocated;
    out.remainder = out.total - out.allocated;
    out.matches = out.remainder.cents() == 0;
    if (out.matches) return out;
    out.message = "Not matching: the lines come to " + out.allocated.in_register() +
                  " of " + out.total.in_register();
    out.options = "Cancel save, Edit the split, Add the remainder";
    return out;
}

inline SplitCheck split_save_check(const Money& total,
                                  const std::vector<reg::SplitLine>& lines) {
    return split_save_check(total, allocated_in(lines));
}

// Adding the remainder: the difference becomes a line of its own, which
// balances the split and leaves the amount visible in a category instead of
// hidden in a rounding nobody can find. It goes to Uncategorized rather than
// to Unassigned -- a split line is a category line, so a remainder nobody has
// categorised is the unknown that appears on a spending report and asks to be
// given a heading.
inline std::vector<reg::SplitLine> with_remainder(
        const std::vector<reg::SplitLine>& lines, const Money& remainder) {
    std::vector<reg::SplitLine> out = lines;
    if (remainder.cents() == 0) return out;
    reg::SplitLine line;
    line.category = kUncategorized;
    line.amount = remainder;
    out.push_back(line);
    return out;
}

// What becomes of the typed lines, per answer. Saved says whether the split is
// written; still_open says whether the window stays, with those lines in it.
struct SplitAnswer {
    bool save = false;
    bool still_open = false;
    std::vector<reg::SplitLine> lines;
};

inline SplitAnswer answered(SplitMismatchChoice choice,
                            const std::vector<reg::SplitLine>& typed,
                            const Money& remainder) {
    SplitAnswer out;
    switch (choice) {
        case SplitMismatchChoice::CancelSave:
            // The window closes, the typed lines go with it, and the split is
            // the one it was opened on. Nothing is undone because nothing was
            // written.
            return out;
        case SplitMismatchChoice::EditSplit:
            out.still_open = true;
            out.lines = typed;
            return out;
        case SplitMismatchChoice::AddRemainder:
            out.save = true;
            out.lines = with_remainder(typed, remainder);
            return out;
    }
    return out;
}

}  // namespace edit
