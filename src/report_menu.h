#pragma once
#include <string>
#include <vector>

// What the Reports menu offers. See the balance-sheet section of
// UserInterface.spectable.
//
// One list, read twice: the menu is built from it and a report tab is named
// from it, so a report cannot be on the menu under one name and on its tab
// under another. What each one asks for is here too, because the kind of
// report decides it -- a period is two dates and a width of column, a
// position is one date -- and it is the kind that decides which page is
// drawn.
namespace reports {

struct Offered {
    std::string name;             // the menu item, and the tab it opens
    bool as_at_one_date = false;  // a position as at a date, not a period
};

inline std::vector<Offered> offered_reports() {
    return {{"Spending by category", false}, {"Balance sheet", true}};
}

// Whether the tab of this name is drawn up as at one date. A name that is not
// on the menu is not a report, and a report that is not a position is a
// period, so either way the answer is no.
inline bool report_is_as_at(const std::string& name) {
    for (const Offered& one : offered_reports())
        if (one.name == name) return one.as_at_one_date;
    return false;
}

}  // namespace reports
