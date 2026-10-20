#pragma once
#include <algorithm>

namespace ui {

// Where on a scroll bar somebody clicked. The indicator is the part that is
// dragged; the pages are the empty bar above and below it, and the arrows are
// the buttons at the ends.
enum class ScrollBarPlace { AboveIndicator, BelowIndicator, OnIndicator, OnArrow };

// How far the view moves for a click in that place, in lines. A page is one
// screenful less a line, so the line that was at the top before a page back is
// at the bottom after it: the overlap is what keeps a reader's place. See the
// scroll bar rule in UserInterface.spectable.
inline int scroll_lines(ScrollBarPlace place, int lines_on_screen) {
    const int page = std::max(1, lines_on_screen - 1);
    switch (place) {
        case ScrollBarPlace::AboveIndicator:
            return -page;
        case ScrollBarPlace::BelowIndicator:
            return page;
        case ScrollBarPlace::OnArrow:
            return 1;
        case ScrollBarPlace::OnIndicator:
            break;
    }
    // The indicator does nothing until it is dragged, and a drag is not a click.
    return 0;
}

}  // namespace ui
