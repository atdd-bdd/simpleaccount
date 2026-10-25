#include "MainWindow.h"

#include "category_search.h"
#include "transaction_id.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QFontDialog>
#include <QSettings>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QListWidget>
#include <QModelIndex>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSlider>
#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <thread>
#include <fstream>
#include <sstream>

#include "import_review.h"
#include "register_entry.h"
#include "report.h"
#include "balance_sheet.h"
#include "report_menu.h"
#include "report_ranges.h"
#include "payee_rules.h"
#include "qif_import.h"
#include "register_lines.h"
#include "register_view.h"
#include "scrolling.h"
#include "tags.h"
#include "transaction_edit.h"
#include "transfer_match.h"

namespace {

QString money(const Money& m) { return QString::fromStdString(m.in_register()); }

// A window built here lays its content out on itself, so content taller than
// the screen has nowhere to go: the buttons sit below the bottom edge and the
// window cannot be made small enough to bring them back. This gives the
// dialog a scroll area filling it and returns the widget the content goes
// into, so the content stays as tall as it wants to be while the window does
// not. The register's scroll bar is the same promise made about a pane; see
// the scroll bar rule in UserInterface.spectable.
//
// It is called in place of the dialog as the layout's parent --
// new QFormLayout(scrollingBody(&dialog)) -- and nothing else changes: a
// widget made with the dialog as its parent is reparented when it is added
// to a layout, so every pointer the caller already holds stays good.
QWidget* scrollingBody(QDialog* dialog) {
    auto* area = new QScrollArea(dialog);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget(area);
    area->setWidget(body);
    auto* outer = new QVBoxLayout(dialog);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(area);
    return body;
}

// Never larger than the screen it opened on, with room left for the frame and
// the task bar. A window that asks for more than this is what the scroll area
// above is for; one that asks for less is left at the size it asked for.
void fitToScreen(QDialog* dialog) {
    const QScreen* screen = dialog->screen();
    if (screen == nullptr) return;
    const QRect room = screen->availableGeometry();
    const QSize wanted = dialog->sizeHint().expandedTo(dialog->size());
    dialog->resize(qMin(wanted.width(), room.width() - 48),
                   qMin(wanted.height(), room.height() - 96));
}

// A negative figure is the one worth seeing at a glance, so it is the only thing
// coloured. Nothing else here depends on colour.
void setAmount(QTableWidget* table, int row, int column, const Money& m) {
    auto* item = new QTableWidgetItem(m.cents() == 0 ? QString() : money(m));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (m.cents() < 0) item->setForeground(QColor(170, 30, 30));
    table->setItem(row, column, item);
}

// Enter records the line being typed. No Q_OBJECT: it declares no signals or
// slots, so nothing here needs moc.
class EnterRecords : public QObject {
public:
    explicit EnterRecords(QObject* parent, std::function<void()> record)
        : QObject(parent), record_(std::move(record)) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                record_();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> record_;
};

// Clicking the empty part of a scroll bar pages the view. The style decides
// what a click on the bar means -- which of the four places it landed in --
// and ui::scroll_lines decides how far the view then moves; a page is a
// screenful less a line. Qt's own paging moves a whole screenful, leaving
// nothing in common between the two views, which is what this replaces. See
// the scroll bar rule in UserInterface.spectable.
class PagesTheView : public QObject {
public:
    explicit PagesTheView(QScrollBar* bar) : QObject(bar), bar_(bar) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() != QEvent::MouseButtonPress)
            return QObject::eventFilter(watched, event);
        auto* click = static_cast<QMouseEvent*>(event);
        if (click->button() != Qt::LeftButton)
            return QObject::eventFilter(watched, event);

        QStyleOptionSlider option;
        option.initFrom(bar_);
        option.orientation = bar_->orientation();
        option.minimum = bar_->minimum();
        option.maximum = bar_->maximum();
        option.sliderPosition = bar_->sliderPosition();
        option.sliderValue = bar_->value();
        option.singleStep = bar_->singleStep();
        option.pageStep = bar_->pageStep();
        option.subControls = QStyle::SC_All;
        if (bar_->orientation() == Qt::Horizontal)
            option.state |= QStyle::State_Horizontal;
        const QStyle::SubControl where = bar_->style()->hitTestComplexControl(
            QStyle::CC_ScrollBar, &option, click->position().toPoint(), bar_);

        // The indicator and the arrows are left to Qt: dragging is its own
        // thing, and an arrow already moves one line.
        if (where != QStyle::SC_ScrollBarAddPage && where != QStyle::SC_ScrollBarSubPage)
            return QObject::eventFilter(watched, event);

        const ui::ScrollBarPlace place = where == QStyle::SC_ScrollBarAddPage
                                             ? ui::ScrollBarPlace::BelowIndicator
                                             : ui::ScrollBarPlace::AboveIndicator;
        bar_->setValue(bar_->value() + ui::scroll_lines(place, bar_->pageStep()));
        return true;
    }

private:
    QScrollBar* bar_;
};

// A table whose scroll bar pages rather than jumps. Per-item scrolling is what
// makes a page a number of lines rather than a number of pixels.
void pagesTheView(QAbstractItemView* view) {
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    QScrollBar* bar = view->verticalScrollBar();
    bar->installEventFilter(new PagesTheView(bar));
}

// A figure typed by a person, which may be anything at all. Nothing is reported
// for text that is not a number: the cell simply holds no amount, which is the
// same as not having typed one.
Money moneyTyped(const QString& text) {
    const QString said = text.trimmed();
    if (said.isEmpty()) return Money();
    try {
        return Money(said.toStdString());
    } catch (const std::exception&) {
        return Money();
    }
}

types::Date asDate(const QDate& date) {
    return types::Date(date.year(), date.month(), date.day());
}

QTableWidgetItem* text(const std::string& s) {
    return new QTableWidgetItem(QString::fromStdString(s));
}

// A free-text column is the one that gets cut off, so it carries the whole of
// what it holds as a tooltip. Given separately where the tooltip says more than
// the cell does -- a split line shows --Split-- and hovers its categories.
QTableWidgetItem* text(const std::string& s, const std::string& tooltip) {
    auto* item = new QTableWidgetItem(QString::fromStdString(s));
    item->setToolTip(QString::fromStdString(tooltip.empty() ? s : tooltip));
    return item;
}

// A form for one rule. No Q_OBJECT: it declares no signals or slots of its own,
// and its buttons are connected to lambdas.
class RuleForm : public QDialog {
public:
    // pick, when it is given, is what opens the category picker: it is handed
    // the category the form holds and returns the one chosen, or nothing. The
    // form cannot open it itself -- the picker is declared below this -- and a
    // callback keeps the two in one direction rather than in a circle.
    RuleForm(QWidget* parent, const QString& title, const payees::Rule& start,
             std::function<std::string(const std::string&)> pick = {})
        : QDialog(parent) {
        setWindowTitle(title);
        auto* form = new QFormLayout(scrollingBody(this));

        pattern_ = new QLineEdit(QString::fromStdString(start.pattern));
        // Said here because it is the one thing about a rule that is not obvious:
        // the part of the name that repeats is what a rule should look for.
        pattern_->setToolTip(
            "The part of the name that identifies the payee. SHELL OIL rather "
            "than SHELL OIL 3344, so the next visit matches too.");
        form->addRow("Looks for", pattern_);

        match_ = new QComboBox;
        match_->addItems({"Contains", "StartsWith", "Exact", "Regex"});
        match_->setCurrentText(QString::fromStdString(payees::to_string(start.match_type)));
        form->addRow("How", match_);

        payee_ = new QLineEdit(QString::fromStdString(start.payee));
        payee_->setToolTip("The name to record instead of what the bank sent.");
        form->addRow("Payee", payee_);

        category_ = new QLineEdit(QString::fromStdString(start.category));
        category_->setToolTip("Left empty, the rule tidies the name and leaves "
                              "the category alone.");
        // Chosen from the same list the register's Recategorize offers, rather
        // than typed. A path typed by hand is how a rule ends up naming a
        // category the book has not got, which reads afterwards as a rule that
        // does nothing at all.
        if (pick) {
            auto* row = new QWidget(this);
            auto* across = new QHBoxLayout(row);
            across->setContentsMargins(0, 0, 0, 0);
            across->addWidget(category_, 1);
            auto* choose = new QPushButton("Choose...", row);
            across->addWidget(choose);
            connect(choose, &QPushButton::clicked, this, [this, pick]() {
                const std::string chosen =
                    pick(category_->text().trimmed().toStdString());
                if (!chosen.empty()) category_->setText(QString::fromStdString(chosen));
            });
            form->addRow("Category", row);
        } else {
            form->addRow("Category", category_);
        }

        enabled_ = new QCheckBox("Apply this rule");
        enabled_->setChecked(start.enabled);
        form->addRow(QString(), enabled_);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                             QDialogButtonBox::Cancel);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        fitToScreen(this);
    }

    payees::Rule rule() const {
        payees::Rule out;
        out.pattern = pattern_->text().trimmed().toStdString();
        out.match_type = payees::match_type_from_string(
            match_->currentText().toStdString());
        out.payee = payee_->text().trimmed().toStdString();
        out.category = category_->text().trimmed().toStdString();
        out.enabled = enabled_->isChecked();
        return out;
    }

private:
    QLineEdit* pattern_ = nullptr;
    QComboBox* match_ = nullptr;
    QLineEdit* payee_ = nullptr;
    QLineEdit* category_ = nullptr;
    QCheckBox* enabled_ = nullptr;
};

// Choosing a category by typing part of it. Only an account already in the
// book can be chosen: inventing one here would turn a typo into a heading on
// every report from then on. The list is the accounts that can take a posting,
// so a placeholder and a hidden account are both out of it.
//
// It opens on the category the line already has, because most changes are a
// correction of something close by.
//
// The matching rule is chart::offered_for_typed: any segment of a path,
// case-insensitive, bringing its children with it; an alias standing for
// exactly one category; a category nobody has posted to in two years left
// out unless it still matches by alias. See the finding-a-category section
// of Accounts.spectable.
//
// What the list shows is not the path. A category reads as its name back to
// its section with the section as a suffix -- Auto:Fuel (Expense), not
// Expenses:Auto:Fuel -- and a real account as its own name in brackets. The
// path is still what the dialog returns; chosen() reads it off the item's
// data, never off the text on screen.
class CategoryPick : public QDialog {
public:
    CategoryPick(QWidget* parent, const chart::Chart& accounts,
                 const ledger::Ledger& book, const std::string& start)
        : QDialog(parent), accounts_(accounts), book_(book),
          latest_(chart::last_used_dates(book)) {
        setWindowTitle("Choose a category");
        resize(460, 420);
        auto* layout = new QVBoxLayout(this);

        typed_ = new QLineEdit;
        typed_->setPlaceholderText("Type part of a category");
        layout->addWidget(typed_);

        list_ = new QListWidget;
        layout->addWidget(list_, 1);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                             QDialogButtonBox::Cancel);
        ok_ = buttons->button(QDialogButtonBox::Ok);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        connect(typed_, &QLineEdit::textChanged, this, [this]() { filter(); });
        connect(list_, &QListWidget::currentRowChanged, this, [this](int) {
            ok_->setEnabled(list_->currentItem() != nullptr);
        });
        // Double-clicking a line is choosing it.
        connect(list_, &QListWidget::itemDoubleClicked, this, &QDialog::accept);

        filter();
        // Opened on the category the line already has, if it is still
        // offered -- which is what makes correcting one a matter of a few
        // keystrokes rather than retyping it.
        for (int i = 0; i < list_->count(); ++i)
            if (list_->item(i)->data(Qt::UserRole).toString().toStdString() == start) {
                list_->setCurrentRow(i);
                break;
            }
        ok_->setEnabled(list_->currentItem() != nullptr);
        typed_->setFocus();
        fitToScreen(this);
    }

    // Empty when nothing was chosen, which the caller treats as a refusal.
    std::string chosen() const {
        return list_->currentItem() == nullptr
                   ? std::string()
                   : list_->currentItem()->data(Qt::UserRole).toString().toStdString();
    }

private:
    void filter() {
        const std::string typed = typed_->text().trimmed().toStdString();
        const types::Date today = asDate(QDate::currentDate());
        // The dates are read once, when the dialog opens, and not again:
        // asking the book for them on every keystroke is one pass over thirty
        // thousand postings per letter typed, which is what made this dialog
        // slow to open and slower to type in. Nobody posts anything while the
        // dialog is up, so the answer cannot have changed.
        std::vector<chart::Account> offered =
            chart::offered_for_typed(accounts_, latest_, typed, false, today);
        std::sort(offered.begin(), offered.end(),
                 [](const chart::Account& a, const chart::Account& b) {
                     return a.path.value() < b.path.value();
                 });
        list_->clear();
        for (const chart::Account& a : offered) {
            auto* item = new QListWidgetItem(
                QString::fromStdString(accounts_.displayed(a.path.value())));
            item->setData(Qt::UserRole, QString::fromStdString(a.path.value()));
            list_->addItem(item);
        }
        if (list_->count() > 0) list_->setCurrentRow(0);
    }

    const chart::Chart& accounts_;
    const ledger::Ledger& book_;
    // Read once at construction. See filter().
    chart::LastUsedDates latest_;
    QLineEdit* typed_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* ok_ = nullptr;
};

}  // namespace

MainWindow::MainWindow() {
    setWindowTitle("SimpleAccount");
    resize(1280, 760);

    accounts_ = new QTreeWidget;
    accounts_->setColumnCount(2);
    accounts_->setHeaderLabels({"Account", "Balance"});
    accounts_->setRootIsDecorated(false);
    accounts_->setUniformRowHeights(true);
    accounts_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    accounts_->setMinimumWidth(260);
    connect(accounts_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::accountClicked);
    accounts_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(accounts_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::treeMenu);

    tabs_ = new QTabWidget;
    tabs_->setTabsClosable(true);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::tabClosed);
    connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::tabChanged);

    // The import review, shown in the panel below the register only while an
    // import is waiting -- one of these, not one per tab. See the
    // bottom-panel section of UserInterface.spectable.
    reviewPanel_ = new QWidget;
    auto* reviewLayout = new QVBoxLayout(reviewPanel_);
    reviewHeading_ = new QLabel;
    reviewLayout->addWidget(reviewHeading_);
    reviewTable_ = new QTableWidget;
    reviewTable_->setColumnCount(7);
    reviewTable_->setHorizontalHeaderLabels(
        {"", "Date", "Payee", "Account", "Category", "Amount", "Import says"});
    reviewTable_->verticalHeader()->setVisible(false);
    reviewTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    reviewTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(reviewTable_, &QTableWidget::customContextMenuRequested, this,
            &MainWindow::reviewMenu);
    reviewTable_->setMinimumHeight(1);
    reviewTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    reviewTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    reviewLayout->addWidget(reviewTable_);
    auto* reviewButtons = new QHBoxLayout;
    reviewButtons->addStretch();
    auto* acceptButton = new QPushButton("Accept ticked rows");
    auto* cancelButton = new QPushButton("Cancel");
    connect(acceptButton, &QPushButton::clicked, this, &MainWindow::acceptImport);
    connect(cancelButton, &QPushButton::clicked, this, &MainWindow::cancelImport);
    reviewButtons->addWidget(cancelButton);
    reviewButtons->addWidget(acceptButton);
    reviewLayout->addLayout(reviewButtons);
    connect(reviewTable_, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem* item) {
                if (item->column() != 0 || refreshing_) return;
                const int line = item->data(Qt::UserRole).toInt();
                if (item->checkState() == Qt::Checked) workspace_->tick(line);
                else workspace_->untick(line);
                refreshReviewPanel();
            });
    // What a find matched, the panel's other tab. A result is read the way a
    // register line is read -- under an account, with the other side of the
    // transaction beside it -- because that is the shape a person recognises.
    findPanel_ = new QWidget;
    auto* findLayout = new QVBoxLayout(findPanel_);
    findHeading_ = new QLabel;
    findLayout->addWidget(findHeading_);
    findTable_ = new QTableWidget;
    findTable_->setColumnCount(5);
    findTable_->setHorizontalHeaderLabels(
        {"Date", "Payee", "Account", "Category", "Amount"});
    findTable_->verticalHeader()->setVisible(false);
    findTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    findTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // A table asks for room for several rows, and a splitter honours what its
    // children ask for, which is what stopped the panel being dragged small.
    // An explicit minimum of one pixel is what overrides that -- a minimum of
    // zero is read as "not set" and the asked-for height applies again -- and
    // the table scrolls at whatever height it is given.
    findTable_->setMinimumHeight(1);
    findTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    findTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    findLayout->addWidget(findTable_);
    // A result is read in its register, so clicking one goes there: the
    // register the result names, not whichever one is open, and the line in it.
    // The results stay where they are -- a find is a list to work down.
    connect(findTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (refreshing_) return;
        if (!workspace_->go_to_find_result(row + 1)) return;
        refresh();
    });
    findTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(findTable_, &QTableWidget::customContextMenuRequested, this,
            &MainWindow::findResultMenu);

    // The panel below the register. The import review is a tab of it, so that
    // the register it is about to change stays in view above, and so that more
    // than one thing can wait to be judged at once -- the results of a find
    // are the other tab it can hold. Which tabs are in it is the workspace's
    // to say, so the close button asks it rather than deciding here: closing
    // the review is cancelling the import, closing the results is only
    // closing them.
    bottomTabs_ = new QTabWidget;
    bottomTabs_->setTabsClosable(true);
    connect(bottomTabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        workspace_->close_bottom_tab_at(index + 1);
        refresh();
    });
    connect(bottomTabs_, &QTabWidget::currentChanged, this, [this](int index) {
        if (refreshing_ || index < 0) return;
        workspace_->select_bottom_tab(index + 1);
        refresh();
    });
    bottomTabs_->setVisible(false);

    rightSplit_ = new QSplitter(Qt::Vertical);
    rightSplit_->addWidget(tabs_);
    rightSplit_->addWidget(bottomTabs_);
    rightSplit_->setStretchFactor(0, 2);
    rightSplit_->setStretchFactor(1, 1);

    outer_ = new QSplitter(Qt::Horizontal);
    outer_->addWidget(accounts_);
    outer_->addWidget(rightSplit_);
    outer_->setStretchFactor(0, 0);
    outer_->setStretchFactor(1, 1);
    setCentralWidget(outer_);

    status_ = new QLabel;
    statusBar()->addWidget(status_);

    // Hidden until there is something long to say. Permanent rather than
    // ordinary widgets so that the strip keeps its place at the right-hand end
    // and the status text is not pushed about when it appears.
    progressText_ = new QLabel;
    progressBar_ = new QProgressBar;
    progressBar_->setMaximumWidth(220);
    progressBar_->setTextVisible(false);
    // The only thing the strip offers. Hidden with the rest of it, and shown
    // only when the work is on a thread of its own, because on this thread the
    // click could not arrive until the work it would stop had finished.
    progressStop_ = new QPushButton("Stop");
    progressText_->hide();
    progressBar_->hide();
    progressStop_->hide();
    statusBar()->addPermanentWidget(progressText_);
    statusBar()->addPermanentWidget(progressBar_);
    statusBar()->addPermanentWidget(progressStop_);
    connect(progressStop_, &QPushButton::clicked, this, [this] {
        if (!progressStopping_) return;
        progressStopping_->stop();
        progressStop_->setEnabled(false);
        progressStop_->setText("Stopping");
    });

    buildMenus();

    // Start about twice the size the toolkit would give, which is small on a
    // modern screen for a window that is read for long stretches. A font chosen
    // from the menu is remembered, so it is only chosen once.
    QSettings settings("SimpleAccount", "SimpleAccount");
    const QString remembered = settings.value("font").toString();
    QFont start = QApplication::font();
    if (!remembered.isEmpty() && start.fromString(remembered)) {
        // kept as chosen
    } else if (start.pointSizeF() > 0) {
        start.setPointSizeF(start.pointSizeF() * 2.0);
    } else {
        start.setPixelSize(start.pixelSize() * 2);
    }
    applyFont(start);

    rebuildWorkspace();
    refresh();
}

void MainWindow::applyFont(const QFont& font) {
    appFont_ = font;
    QApplication::setFont(appFont_);
    // An app font reaches only the widgets that have not been given one of their
    // own, so the two that have are set again here, and the rows are made tall
    // enough for the new size.
    const QFontMetrics metrics(appFont_);
    const int row = metrics.height() + 8;
    QFont bold = appFont_;
    bold.setBold(true);
    for (TabWidgets& t : tabWidgets_) {
        if (t.reg != nullptr) {
            t.reg->setFont(appFont_);
            t.reg->verticalHeader()->setDefaultSectionSize(row);
        }
        if (t.reportTable != nullptr) {
            t.reportTable->setFont(appFont_);
            t.reportTable->verticalHeader()->setDefaultSectionSize(row);
        }
    }
    reviewTable_->setFont(appFont_);
    reviewTable_->verticalHeader()->setDefaultSectionSize(row);
    reviewHeading_->setFont(bold);
    accounts_->setFont(appFont_);
    if (status_) status_->setFont(appFont_);
    menuBar()->setFont(appFont_);
    // The account list is rebuilt rather than restyled, because its headings
    // carry a bold of their own that is derived from the font in force.
    if (workspace_) refreshAccounts();
}

// The register's columns, named as the specification names them rather than as
// the headings read: Payment and Deposit are headed differently for different
// account types, and the width is not.
static std::string registerColumnName(int column) {
    static const char* const names[] = {"Date",    "CheckNo", "Payee",   "Category",
                                        "Memo",    "Tag",     "Payment", "Deposit",
                                        "Cleared", "Balance"};
    if (column < 0 || column >= static_cast<int>(sizeof(names) / sizeof(names[0])))
        return {};
    return names[column];
}

void MainWindow::applyColumnWidths() {
    const bool was = refreshing_;
    refreshing_ = true;
    for (const TabWidgets& t : tabWidgets_) {
        if (t.isReport || t.reg == nullptr) continue;
        for (int column = 0; column < t.reg->columnCount(); ++column) {
            if (t.reg->horizontalHeader()->sectionResizeMode(column) !=
                QHeaderView::Interactive)
                continue;
            const auto at = columnWidths_.find(registerColumnName(column));
            if (at != columnWidths_.end()) t.reg->setColumnWidth(column, at->second);
        }
    }
    refreshing_ = was;
}

// One line per node, every node, in path order, with no group headings and
// nothing hidden left out: the tree is the opposite on all four counts, and
// that is what this window is for. Double-clicking a line opens its editor.
void MainWindow::showAccountListing() {
    if (!store_.is_open()) {
        QMessageBox::information(this, "SimpleAccount",
                                 "Open a book first, or make one with File > New book.");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle("Accounts");
    dialog.resize(820, 520);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel("Double-click a line to edit the account.", &dialog));
    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels(
        {"Account", "Type", "Group", "Hidden", "Bank", "Number", "Balance"});
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->setFont(appFont_);
    layout->addWidget(table);

    const auto fill = [this, table]() {
        const std::vector<ui::ListingRow> rows = ui::account_listing(chart_, book_);
        table->setRowCount(static_cast<int>(rows.size()));
        for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
            const ui::ListingRow& row = rows[static_cast<std::size_t>(r)];
            table->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(row.path)));
            table->setItem(r, 1, new QTableWidgetItem(
                                     QString::fromStdString(types::to_string(row.type))));
            table->setItem(r, 2, new QTableWidgetItem(row.placeholder ? "group" : ""));
            table->setItem(r, 3, new QTableWidgetItem(row.hidden ? "hidden" : ""));
            table->setItem(r, 4, new QTableWidgetItem(QString::fromStdString(row.bank)));
            table->setItem(r, 5,
                           new QTableWidgetItem(QString::fromStdString(row.account_number)));
            auto* balance = new QTableWidgetItem(QString::fromStdString(row.balance.in_register()));
            balance->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table->setItem(r, 6, balance);
        }
    };
    fill();

    connect(table, &QTableWidget::cellDoubleClicked, &dialog, [this, table, fill](int row, int) {
        QTableWidgetItem* item = table->item(row, 0);
        if (item == nullptr) return;
        editAccountSettings(item->text());
        fill();
    });
    fitToScreen(&dialog);
    dialog.exec();
}

// The categories, which no other window shows: the tree leaves them out, so
// without this a category hidden by mistake could not be found again.
void MainWindow::showCategoryListing() {
    if (!store_.is_open()) {
        QMessageBox::information(this, "SimpleAccount",
                                 "Open a book first, or make one with File > New book.");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle("Categories");
    dialog.resize(700, 520);
    auto* layout = new QVBoxLayout(&dialog);
    // Adding one from here rather than from the account dialog, which is where
    // the only way to add a category used to be -- a list of categories with
    // no way to add one is a list that sends a person somewhere else to use
    // it.
    auto* across = new QHBoxLayout;
    auto* addOne = new QPushButton("Add a category...", &dialog);
    across->addWidget(addOne);
    across->addStretch(1);
    across->addWidget(new QLabel("Double-click a line to edit the category.", &dialog));
    layout->addLayout(across);
    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({"Category", "Type", "Group", "Hidden", "Alias"});
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->setFont(appFont_);
    layout->addWidget(table);

    const auto fill = [this, table]() {
        const std::vector<ui::ListingRow> rows = ui::category_listing(chart_);
        table->setRowCount(static_cast<int>(rows.size()));
        for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
            const ui::ListingRow& row = rows[static_cast<std::size_t>(r)];
            table->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(row.path)));
            table->setItem(r, 1, new QTableWidgetItem(
                                     QString::fromStdString(types::to_string(row.type))));
            table->setItem(r, 2, new QTableWidgetItem(row.placeholder ? "group" : ""));
            table->setItem(r, 3, new QTableWidgetItem(row.hidden ? "hidden" : ""));
            table->setItem(r, 4, new QTableWidgetItem(QString::fromStdString(row.alias)));
        }
    };
    fill();

    connect(table, &QTableWidget::cellDoubleClicked, &dialog, [this, table, fill](int row, int) {
        QTableWidgetItem* item = table->item(row, 0);
        if (item == nullptr) return;
        editCategorySettings(item->text());
        fill();
    });

    connect(addOne, &QPushButton::clicked, &dialog, [this, &dialog, fill]() {
        if (addCategoryFromListing(&dialog)) fill();
    });
    fitToScreen(&dialog);
    dialog.exec();
}

// What is kept about one account: which bank it is at and the number on the
// statement. See the settings section of Accounts.spectable.
void MainWindow::editAccountSettings(const QString& path) {
    const chart::Account* existing = chart_.find(path.toStdString());
    if (existing == nullptr) return;

    QDialog dialog(this);
    dialog.setWindowTitle("Account settings");
    auto* form = new QFormLayout(scrollingBody(&dialog));
    form->addRow("Account", new QLabel(path, &dialog));
    auto* bank = new QLineEdit(QString::fromStdString(existing->bank), &dialog);
    auto* number = new QLineEdit(QString::fromStdString(existing->account_number), &dialog);
    form->addRow("Bank", bank);
    form->addRow("Number on the statement", number);

    // The starting balance is shown here rather than only at creation, because
    // the figure a statement opens with is often not known on the day the
    // account is made. Blank, or zero, is an account with none: the one
    // transaction against opening equity goes rather than being left at zero,
    // which is what set_starting_balance does.
    const edit::Starting was = edit::starting_balance_of(chart_, transactions_,
                                                         path.toStdString());
    auto* starting = new QLineEdit(&dialog);
    starting->setPlaceholderText("0.00");
    if (was.exists) starting->setText(QString::fromStdString(was.stated.in_register()));
    auto* startingOn = new QDateEdit(
        was.exists ? QDate(was.on.year(), was.on.month(), was.on.day())
                   : QDate::currentDate(),
        &dialog);
    startingOn->setCalendarPopup(true);
    startingOn->setDisplayFormat("yyyy-MM-dd");
    form->addRow("Starting balance", starting);
    form->addRow("As at", startingOn);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    fitToScreen(&dialog);
    if (dialog.exec() != QDialog::Accepted) return;

    chart::Account a = *existing;
    // Kept exactly as typed, with only the blanks at either end taken off: a
    // number on a statement is a label and not an amount.
    a.bank = bank->text().trimmed().toStdString();
    a.account_number = number->text().trimmed().toStdString();
    chart_.put(a);

    const QString said = starting->text().trimmed();
    const Money stated = moneyTyped(said);
    if (!said.isEmpty() && stated.cents() == 0 && said != "0" && said != "0.00") {
        QMessageBox::warning(this, "SimpleAccount",
                             QString("\"%1\" is not an amount; the starting balance "
                                     "was left as it was.").arg(said));
    } else if (was.exists || stated.cents() != 0) {
        const edit::Outcome done = edit::set_starting_balance(
            &chart_, &transactions_, path.toStdString(), stated,
            asDate(startingOn->date()));
        if (!done.changed && !done.reason.empty())
            QMessageBox::warning(this, "SimpleAccount",
                                 QString::fromStdString(done.reason));
    }
    save();
    rebuildWorkspace();
    refresh();
}

// Adding a category from the listing it will turn up in. It asks what the
// editor asks, less the path, which is the parent and the name together. The
// chart settles where a category with no parent goes -- under its own root --
// because nothing in the program can find one that is anywhere else.
bool MainWindow::addCategoryFromListing(QWidget* parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Add a category");
    auto* form = new QFormLayout(scrollingBody(&dialog));

    auto* name = new QLineEdit(&dialog);
    name->setPlaceholderText("Fuel");
    form->addRow("Name", name);

    // Only the three category trees are offered as a parent. A category under
    // a checking account is a category no report can reach, and offering it is
    // offering a mistake.
    auto* under = new QComboBox(&dialog);
    under->addItem("(top of its own tree)", QString());
    std::vector<chart::Account> everything = chart_.all();
    std::sort(everything.begin(), everything.end(),
             [](const chart::Account& x, const chart::Account& y) {
                 return x.path.value() < y.path.value();
             });
    for (const chart::Account& a : everything)
        if (a.type == types::AccountType::Income || a.type == types::AccountType::Expense ||
            a.type == types::AccountType::Equity)
            under->addItem(QString::fromStdString(a.path.value()),
                           QString::fromStdString(a.path.value()));
    form->addRow("Under", under);

    auto* kind = new QComboBox(&dialog);
    for (types::AccountType one : {types::AccountType::Income,
                                   types::AccountType::Expense,
                                   types::AccountType::Equity})
        kind->addItem(QString::fromStdString(types::to_string(one)),
                      QString::fromStdString(types::to_string(one)));
    kind->setCurrentIndex(1);   // an expense, which is what nearly all of them are
    form->addRow("Type", kind);

    auto* alias = new QLineEdit(&dialog);
    alias->setToolTip("A short way to type this category when categorising, "
                      "written with an @ in front of it.");
    form->addRow("Alias", alias);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    name->setFocus();
    fitToScreen(&dialog);
    if (dialog.exec() != QDialog::Accepted) return false;

    const QString wanted = name->text().trimmed();
    if (wanted.isEmpty()) return false;
    const chart::Rejection no = workspace_->add_category_from_listing(
        types::AccountName(wanted.toStdString()),
        under->currentData().toString().toStdString(),
        types::account_type_from_string(kind->currentData().toString().toStdString()),
        alias->text().trimmed().toStdString());
    if (no.refused) {
        QMessageBox::warning(&dialog, "SimpleAccount", QString::fromStdString(no.reason));
        return false;
    }
    // The workspace made it, so the window's chart follows it rather than
    // adding a second copy of the same category.
    chart_ = workspace_->accounts();
    save();
    refresh();
    return true;
}

// A category's own editor. It asks what the dialog that adds one asks -- the
// name, the parent, the kind, the alias and whether it is out of the way --
// because it held the alias and nothing else to begin with, which made it the
// one editor in the program that could not correct what somebody had got
// wrong. Each of the five is the chart's own operation; see the
// what-is-kept-about-a-category section of Accounts.spectable.
void MainWindow::editCategorySettings(const QString& path) {
    const std::string which = path.toStdString();
    const std::optional<chart::CategorySettings> was = chart_.category_settings(which);
    if (!was.has_value()) {
        // A category that is only in the categories table and not in the chart
        // has nothing but its alias to edit, which is what this used to be.
        const chart::Category* asCategory = chart_.find_category(which);
        if (asCategory == nullptr) return;
        bool said = false;
        const QString now = QInputDialog::getText(
            this, "Category settings", "Alias", QLineEdit::Normal,
            QString::fromStdString(asCategory->alias), &said).trimmed();
        if (!said) return;
        chart::Category c = *asCategory;
        c.alias = now.toStdString();
        chart_.put_category(c);
        save();
        rebuildWorkspace();
        refresh();
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Category settings");
    auto* form = new QFormLayout(scrollingBody(&dialog));
    form->addRow("Category", new QLabel(path, &dialog));

    auto* name = new QLineEdit(QString::fromStdString(was->name.value()), &dialog);
    form->addRow("Name", name);

    // The parent, chosen from what is there rather than typed. (top level)
    // means the top of its own tree -- Income, Expenses or Equity -- because a
    // category outside those is invisible to every report there is.
    auto* parent = new QComboBox(&dialog);
    parent->addItem("(top level)", QString());
    std::vector<chart::Account> everything = chart_.all();
    std::sort(everything.begin(), everything.end(),
             [](const chart::Account& x, const chart::Account& y) {
                 return x.path.value() < y.path.value();
             });
    for (const chart::Account& a : everything) {
        // Not under itself, and not under one of its own children: either one
        // is a move that cannot be made, and offering it is offering a
        // refusal.
        if (chart::Chart::is_descendant_or_self(a.path.value(), which)) continue;
        parent->addItem(QString::fromStdString(a.path.value()),
                        QString::fromStdString(a.path.value()));
    }
    const int at = parent->findData(QString::fromStdString(was->under));
    if (at >= 0) parent->setCurrentIndex(at);
    form->addRow("Under", parent);

    // The same three the adding dialog offers. A category entered as an
    // expense when it was income reads as the wrong sign on every report that
    // covers it, so this is askable rather than fixed.
    auto* kind = new QComboBox(&dialog);
    for (types::AccountType one : {types::AccountType::Income,
                                   types::AccountType::Expense,
                                   types::AccountType::Equity})
        kind->addItem(QString::fromStdString(types::to_string(one)),
                      QString::fromStdString(types::to_string(one)));
    const int kindAt = kind->findData(QString::fromStdString(types::to_string(was->type)));
    if (kindAt >= 0) kind->setCurrentIndex(kindAt);
    form->addRow("Type", kind);

    auto* alias = new QLineEdit(QString::fromStdString(was->alias), &dialog);
    alias->setToolTip("A short way to type this category when categorising, "
                      "written with an @ in front of it.");
    form->addRow("Alias", alias);

    auto* hidden = new QCheckBox("Keep it out of the pickers", &dialog);
    hidden->setChecked(was->hidden);
    form->addRow(QString(), hidden);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    fitToScreen(&dialog);
    if (dialog.exec() != QDialog::Accepted) return;

    const QString wanted = name->text().trimmed();
    if (wanted.isEmpty()) return;

    chart::CategorySettings now;
    now.path = types::AccountPath(which);
    now.name = types::AccountName(wanted.toStdString());
    now.under = parent->currentData().toString().toStdString();
    now.type = types::account_type_from_string(kind->currentData().toString().toStdString());
    now.alias = alias->text().trimmed().toStdString();
    now.hidden = hidden->isChecked();

    const chart::Rejection no = chart_.apply_category_settings(now);
    if (no.refused) {
        QMessageBox::warning(this, "SimpleAccount", QString::fromStdString(no.reason));
        return;
    }
    save();
    rebuildWorkspace();
    refresh();
}

void MainWindow::hideAccount(const QString& path) {
    const chart::Account* existing = chart_.find(path.toStdString());
    if (existing == nullptr) return;
    chart::Account a = *existing;
    // Hiding keeps the history: the account stays in the chart and in every
    // total, and only its line in the tree goes.
    a.hidden = true;
    chart_.put(a);
    save();
    rebuildWorkspace();
    refresh();
}

void MainWindow::deleteAccount(const QString& path) {
    const chart::Rejection no = chart_.remove(types::AccountPath(path.toStdString()));
    if (no.refused) {
        QMessageBox::warning(this, "SimpleAccount", QString::fromStdString(no.reason));
        return;
    }
    save();
    rebuildWorkspace();
    refresh();
}

// The menu on a line of the tree. What is on it is the workspace's answer, so
// that the greyed Delete and the heading's single item are decided in one place
// and are the same here as in the specification.
void MainWindow::treeMenu(const QPoint& at) {
    QTreeWidgetItem* item = accounts_->itemAt(at);
    if (item == nullptr || !workspace_) return;
    const QString account = item->data(0, Qt::UserRole).toString();
    // A heading carries no account, so it is named by what it says on the page.
    const QString node = account.isEmpty() ? item->text(0) : account;

    QMenu menu(this);
    for (const ui::MenuEntry& entry : workspace_->tree_menu(node.toStdString())) {
        QAction* action = menu.addAction(QString::fromStdString(entry.item));
        action->setEnabled(entry.enabled);
    }
    QAction* chosen = menu.exec(accounts_->viewport()->mapToGlobal(at));
    if (chosen == nullptr) return;

    const QString what = chosen->text();
    if (what.startsWith("New account")) {
        newAccount();
    } else if (what.startsWith("Edit")) {
        if (chart_.find(node.toStdString()) != nullptr) editAccountSettings(node);
        else editCategorySettings(node);
    } else if (what.startsWith("Reconcile")) {
        reconcileAccount(node);
    } else if (what == "Hide") {
        hideAccount(node);
    } else if (what.startsWith("Delete")) {
        deleteAccount(node);
    }
}

void MainWindow::chooseFont() {
    bool chosen = false;
    const QFont font = QFontDialog::getFont(&chosen, appFont_, this, "Font for all windows");
    if (!chosen) return;
    applyFont(font);
    QSettings settings("SimpleAccount", "SimpleAccount");
    settings.setValue("font", font.toString());
}

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu("&File");
    file->addAction("&New book...", this, &MainWindow::newBook);
    file->addAction("&Open book...", this, &MainWindow::openBook);
    recent_ = file->addMenu("Open &recent");
    rebuildRecent();
    file->addSeparator();
    // One import, not three. A QIF is no longer a menu item of its own: the
    // history file is asked for when a book is made, and a QIF offered here is
    // read the same way a QFX or a CSV is -- into one account, through the
    // review panel. See the File menu scenario in UserInterface.spectable.
    file->addAction("&Import transactions...", this,
                    &MainWindow::importTransactions);
    file->addSeparator();
    file->addAction("E&xit", this, &QWidget::close);

    // Find is modeless and its results are a tab, so it belongs with the
    // things done to what is already in the book rather than with the file.
    QMenu* edit = menuBar()->addMenu("&Edit");
    edit->addAction("&Book settings...", this, &MainWindow::editBookSettings);
    edit->addSeparator();
    auto* find = edit->addAction("&Find...", this, &MainWindow::showFind);
    find->setShortcut(QKeySequence::Find);

    QMenu* accounts = menuBar()->addMenu("&Accounts");
    accounts->addAction("&New account...", this, &MainWindow::newAccount);
    accounts->addSeparator();
    accounts->addAction("Account &listing...", this, &MainWindow::showAccountListing);
    accounts->addAction("&Category listing...", this, &MainWindow::showCategoryListing);
    accounts->addSeparator();
    accounts->addAction("Payee &rules...", this, &MainWindow::editRules);
    accounts->addAction("Payee &tables...", this, &MainWindow::editPayeeTables);
    accounts->addAction("&Tags...", this, &MainWindow::showTagListing);
    accounts->addSeparator();
    accounts->addAction("&Assign payees according to rules", this,
                        &MainWindow::assignPayeesAccordingToRules);
    accounts->addAction("&Convert the payees in the book", this,
                        &MainWindow::convertPayeesInTheBook);
    accounts->addAction("Assign the &categories in the book", this,
                        &MainWindow::assignCategoriesInTheBook);

    // One item per report the program offers, in the order it offers them.
    // The list is src/report_menu.h rather than this line, so that the menu
    // item, the tab it opens and the page drawn on it cannot drift apart.
    QMenu* reports = menuBar()->addMenu("&Reports");
    for (const reports::Offered& offered : reports::offered_reports()) {
        const std::string name = offered.name;
        QAction* item =
            reports->addAction(QString::fromStdString("&" + name + "..."));
        connect(item, &QAction::triggered, this, [this, name]() { openReport(name); });
    }

    QMenu* view = menuBar()->addMenu("&View");
    auto* font = view->addAction("&Font...");
    connect(font, &QAction::triggered, this, &MainWindow::chooseFont);
    view->addSeparator();
    auto* hidden = view->addAction("Show &hidden accounts");
    hidden->setCheckable(true);
    connect(hidden, &QAction::triggered, this, &MainWindow::toggleHidden);
}

// The book has changed, so the workspace is handed it. It is not built again:
// what is being read is no part of the book -- which registers are open, which
// report sits on which tab, which of the two is in front -- and a new workspace
// knew none of it. Register tabs used to be re-opened by name afterwards, which
// put them back one way or another; a report tab had no name to be re-opened by
// and was simply lost, which is why a recategorise made from a report took the
// report away with it.
//
// A workspace is built once, and again for a book that is not this one -- see
// newBook() and openBook(), which drop this one first.
void MainWindow::rebuildWorkspace() {
    if (workspace_) {
        workspace_->set_quicken_signs(quickenSigns_);
        workspace_->book_is_now(chart_, book_, transactions_, splits_);
        return;
    }
    workspace_ = std::make_unique<ui::Workspace>(chart_, book_, transactions_, splits_);
    workspace_->set_column_widths(columnWidths_);
    workspace_->set_quicken_signs(quickenSigns_);
}

std::string MainWindow::selectedAccount() const {
    const QList<QTreeWidgetItem*> chosen = accounts_->selectedItems();
    if (chosen.isEmpty()) return {};
    return chosen.first()->data(0, Qt::UserRole).toString().toStdString();
}

// A book with nothing in it. Opening balances and transactions are then entered
// by hand, which is the way to see whether the reports are right before trusting
// them with twenty years of imported history.
void MainWindow::newBook() {
    bool said = false;
    const QString name = QInputDialog::getText(
        this, "New book", "What is this book called?", QLineEdit::Normal, {}, &said)
                             .trimmed();
    if (!said || name.isEmpty()) return;

    const store::Failure folder = store::ensure_folder();
    if (folder.refused) {
        QMessageBox::warning(this, "SimpleAccount",
                             QString::fromStdString(folder.reason));
        return;
    }
    store::Book made;
    const store::Failure no = store::Book::create(
        store::path_for(name.toStdString()), name.toStdString(), &made);
    if (no.refused) {
        // An existing book is never overwritten: losing twenty years of history
        // because a name was reused is not a mistake worth making available.
        QMessageBox::warning(this, "SimpleAccount", QString::fromStdString(no.reason));
        return;
    }
    store_ = std::move(made);
    chart_ = chart::Chart();
    book_ = ledger::Ledger();
    transactions_.clear();
    rules_.clear();
    conversions_.clear();
    categorisations_.clear();
    tagVocabulary_.clear();
    hideBefore_.clear();
    quickenSigns_ = false;
    // Another book: the registers that were open were of accounts this one
    // does not have, so the workspace starts over rather than being handed
    // the new book.
    workspace_.reset();
    splits_.clear();
    rememberRecent(name);
    bookChanged(name);
    askTheNewBookQuestion(name);
}

// The question a new book opens with. A book with no accounts cannot be typed
// into at all, so the first thing asked is where its chart comes from: twenty
// years of Quicken, or the standard list. There is no Cancel, because a blank
// answer is itself an answer -- this is an entirely new book -- and a book left
// with no accounts would be of no use to anybody. See the new book scenarios in
// UserInterface.spectable.
void MainWindow::askTheNewBookQuestion(const QString& name) {
    workspace_->make_new_book(name.toStdString());

    QDialog dialog(this);
    dialog.setWindowTitle("New book");
    auto* layout = new QVBoxLayout(scrollingBody(&dialog));
    auto* asked = new QLabel(
        QString("%1 has nothing in it yet. Where do its accounts come from?").arg(name),
        &dialog);
    asked->setWordWrap(true);
    layout->addWidget(asked);

    auto* fromNothing = new QRadioButton("Start fresh, with the standard categories", &dialog);
    auto* fromHistory = new QRadioButton("Bring in a Quicken history file (QIF)", &dialog);
    fromNothing->setChecked(true);
    layout->addWidget(fromNothing);
    layout->addWidget(fromHistory);

    auto* row = new QHBoxLayout();
    auto* chosen = new QLineEdit(&dialog);
    chosen->setReadOnly(true);
    auto* browse = new QPushButton("Choose...", &dialog);
    chosen->setEnabled(false);
    browse->setEnabled(false);
    row->addWidget(chosen);
    row->addWidget(browse);
    layout->addLayout(row);

    connect(fromHistory, &QRadioButton::toggled, chosen, &QWidget::setEnabled);
    connect(fromHistory, &QRadioButton::toggled, browse, &QWidget::setEnabled);
    connect(browse, &QPushButton::clicked, &dialog, [this, chosen] {
        const QString file = chooseImportFile("History file", "Quicken export (*.qif *.QIF)");
        if (!file.isEmpty()) chosen->setText(file);
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    fitToScreen(&dialog);

    // The loop is for the two ways the answer can be no answer: the history
    // chosen without naming a file, and a file the importer would not read.
    for (;;) {
        // Closing the window is not a third answer: a book has to start from
        // something, so it starts from the standard list.
        const QString file = dialog.exec() == QDialog::Accepted && fromHistory->isChecked()
                                 ? chosen->text().trimmed()
                                 : QString();
        if (fromHistory->isChecked() && file.isEmpty() && dialog.result() == QDialog::Accepted) {
            QMessageBox::information(this, "SimpleAccount",
                                     "Name the QIF file, or start from the standard categories.");
            continue;
        }

        std::string text;
        if (!file.isEmpty()) {
            std::ifstream in(file.toStdString(), std::ios::binary);
            if (!in) {
                QMessageBox::warning(this, "SimpleAccount",
                                     QString("%1 could not be read").arg(file));
                continue;
            }
            std::ostringstream buffer;
            buffer << in.rdbuf();
            text = buffer.str();
        }

        const ui::NewBookStarted said =
            workspace_->answer_new_book_question(file.toStdString(), text, {});
        if (said.refused) {
            // All or nothing: a book half imported is neither the old data nor
            // the new, with no way to tell which arrived.
            QMessageBox::warning(this, "SimpleAccount", QString::fromStdString(said.refusal));
            continue;
        }

        // The workspace did the work, so the window takes the book back off it.
        chart_ = workspace_->accounts();
        book_ = workspace_->book();
        transactions_ = workspace_->transactions();
        splits_ = workspace_->splits();
        if (!save()) return;
        refresh();
        status_->setText(QString("%1  --  %2 transactions, %3 accounts")
                             .arg(name)
                             .arg(transactions_.size())
                             .arg(chart_.all().size()));
        return;
    }
}

void MainWindow::openBook() {
    QStringList names;
    for (const std::string& one : store::book_names())
        names << QString::fromStdString(one);
    if (names.isEmpty()) {
        QMessageBox::information(
            this, "SimpleAccount",
            QString("There are no books yet in %1.\n\nFile > New book makes one.")
                .arg(QString::fromStdString(store::books_folder())));
        return;
    }
    bool said = false;
    const QString name = QInputDialog::getItem(this, "Open book", "Which book?",
                                               names, 0, false, &said);
    if (!said || name.isEmpty()) return;
    openNamed(name);
}

void MainWindow::openNamed(const QString& name) {
    if (name.isEmpty()) return;
    store::Book opened;
    store::Failure no =
        store::Book::open_named(name.toStdString(), &opened);
    if (no.refused) {
        QMessageBox::warning(this, "SimpleAccount", QString::fromStdString(no.reason));
        return;
    }
    chart::Chart accounts;
    ledger::Ledger ledger;
    std::vector<ledger::Transaction> transactions;
    std::vector<payees::Rule> rules;
    std::vector<ledger::TransactionSplit> splits;
    std::map<std::string, int> widths;
    no = opened.read(&accounts, &ledger, &transactions, &rules, &splits, &widths);
    if (no.refused) {
        QMessageBox::warning(this, "SimpleAccount", QString::fromStdString(no.reason));
        return;
    }
    store_ = std::move(opened);
    chart_ = accounts;
    book_ = ledger;
    transactions_ = transactions;
    rules_ = rules;
    splits_ = splits;
    columnWidths_ = widths;
    // The side tables are read through the store rather than through read(),
    // which already takes five things: each is a list of its own, edited in a
    // window of its own, and nothing about the book changes when one does.
    conversions_ = store_.conversions();
    categorisations_ = store_.categorisations();
    tagVocabulary_ = store_.tags();
    // The one book setting a register reads, kept here rather than asked of
    // the store on every refresh.
    const store::BookInfo settings = store_.info();
    hideBefore_ = settings.hide_before;
    quickenSigns_ = settings.quicken_signs;
    workspace_.reset();   // another book, so another set of tabs
    rememberRecent(name);
    bookChanged(name);
    applyColumnWidths();
}

// A book is a file rather than a session, so anything that changes it writes it.
// Closing the window is not a way to lose a morning's entry.
bool MainWindow::save() {
    if (!store_.is_open()) return false;
    const store::Failure no =
        store_.write(chart_, transactions_, rules_, splits_, columnWidths_);
    if (no.refused) {
        QMessageBox::warning(this, "SimpleAccount", QString::fromStdString(no.reason));
        return false;
    }
    return true;
}

// A drag writes the widths and nothing else. The whole-book write rewrites
// every account, transaction, split and rule, which is too much work to record
// where a divider was left -- and it made the drag fail over anything else in
// the book that could not be stored. See decision 27 in questions.md.
bool MainWindow::saveColumnWidths() {
    if (!store_.is_open()) return false;
    const store::Failure no = store_.write_column_widths(columnWidths_);
    if (no.refused) {
        QMessageBox::warning(this, "SimpleAccount", QString::fromStdString(no.reason));
        return false;
    }
    return true;
}

// The list is of names rather than paths: a book lives in the books folder and
// is opened by name, so a path would be the same information written in a way
// that breaks if the folder moves.
void MainWindow::rebuildRecent() {
    if (recent_ == nullptr) return;
    recent_->clear();

    QSettings settings("SimpleAccount", "SimpleAccount");
    const QStringList remembered = settings.value("recentBooks").toStringList();

    // Only the books that are still there. One deleted outside the program
    // should not sit in the menu offering to fail.
    QStringList here;
    for (const std::string& one : store::book_names())
        here << QString::fromStdString(one);

    QStringList shown;
    for (const QString& name : remembered)
        if (here.contains(name) && !shown.contains(name)) shown << name;
    // Trimmed back if the folder has lost some, so the setting does not grow a
    // tail of names nobody can open.
    if (shown != remembered) settings.setValue("recentBooks", shown);

    if (shown.isEmpty()) {
        QAction* none = recent_->addAction("(none yet)");
        none->setEnabled(false);
        return;
    }
    for (const QString& name : shown)
        recent_->addAction(name, this, [this, name]() { openNamed(name); });
}

void MainWindow::rememberRecent(const QString& name) {
    if (name.isEmpty()) return;
    QSettings settings("SimpleAccount", "SimpleAccount");
    QStringList names = settings.value("recentBooks").toStringList();
    names.removeAll(name);
    names.prepend(name);          // most recent first, which is the useful order
    while (names.size() > 8) names.removeLast();
    settings.setValue("recentBooks", names);
    rebuildRecent();
}

void MainWindow::bookChanged(const QString& what) {
    rebuildWorkspace();
    refresh();
    status_->setText(QString("%1  --  %2 transactions, %3 accounts")
                         .arg(what)
                         .arg(transactions_.size())
                         .arg(chart_.all().size()));
}

// A real account picked with no parent is flat -- Checking, not
// Assets:Checking -- since the group above already says what kind of thing
// it is. A category still nests under its fixed root either way, parent or
// not, because the rest of the program finds it by that literal path. Every
// level between a chosen parent and the new account that is missing is
// filled in as a placeholder; see the chart rules in Accounts.
void MainWindow::newAccount() {
    if (!store_.is_open()) {
        QMessageBox::information(this, "SimpleAccount",
                                 "Open a book first, or make one with File > New book.");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle("New account");

    auto* name = new QLineEdit(&dialog);
    name->setPlaceholderText("Checking");

    // The seven groups, which is the question a person can answer while opening
    // an account. Whether it is a checking or a savings account is a detail of
    // the same answer and is settled afterwards; see the rule in CoreTypes.
    // Each item carries the type the account starts as.
    auto* type = new QComboBox(&dialog);
    int groups = 0;
    const types::AccountGroup* order = types::account_groups_in_order(&groups);
    for (int at = 0; at < groups; ++at)
        type->addItem(QString::fromStdString(types::heading_of(order[at])),
                      QString::fromStdString(
                          types::to_string(types::default_type_for(order[at]))));
    // The three categories, which are accounts here and have no group. They
    // stay on this list because it is the only way to add one until categories
    // have a dialog of their own.
    const int firstCategory = groups + 1;   // +1 for the separator just added
    type->insertSeparator(groups);
    for (types::AccountType kind : {types::AccountType::Income,
                                    types::AccountType::Expense,
                                    types::AccountType::Equity})
        type->addItem(QString::fromStdString(types::to_string(kind)),
                      QString::fromStdString(types::to_string(kind)));

    // Under an existing account, or at the top. A category's fixed root --
    // Income or Expenses -- is not on this list and is not optional: picking
    // "(top level)" for a category still nests it under its root, because
    // nothing else in the program knows how to find a category that is not.
    // A real account genuinely has no required parent any more, now that the
    // group above says what kind of thing it is.
    auto* parent = new QComboBox(&dialog);
    parent->addItem("(top level)", QString());
    std::vector<chart::Account> everything = chart_.picker();
    for (const chart::Account& a : chart_.all())
        if (a.placeholder) everything.push_back(a);
    std::sort(everything.begin(), everything.end(),
             [](const chart::Account& x, const chart::Account& y) {
                 return x.path.value() < y.path.value();
             });
    for (const chart::Account& a : everything)
        parent->addItem(QString::fromStdString(a.path.value()),
                        QString::fromStdString(a.path.value()));
    // Pre-selected from whatever is selected in the list, because a new
    // account is usually a sibling or a child of the one being looked at.
    const std::string chosen = selectedAccount();
    if (!chosen.empty()) {
        const int at = parent->findData(QString::fromStdString(chosen));
        if (at >= 0) parent->setCurrentIndex(at);
    }

    auto* opening = new QLineEdit(&dialog);
    opening->setPlaceholderText("0.00");
    // The bank and the number on the statement. Both are asked here because
    // the statement is usually on the desk at the moment an account is being
    // opened, and an account that carries its number from its first day is one
    // the first download can be placed into without anybody being asked which
    // account it belongs to. Neither is required; a cash account has neither.
    auto* bank = new QLineEdit(&dialog);
    bank->setPlaceholderText("The bank's name or id");
    auto* number = new QLineEdit(&dialog);
    number->setPlaceholderText("As it appears on the statement");
    auto* asAt = new QDateEdit(QDate::currentDate(), &dialog);
    asAt->setCalendarPopup(true);
    asAt->setDisplayFormat("yyyy-MM-dd");

    auto* form = new QFormLayout(scrollingBody(&dialog));
    form->addRow("Name", name);
    form->addRow("Type", type);
    form->addRow("Under", parent);
    form->addRow("Opening balance", opening);
    form->addRow("As at", asAt);
    form->addRow("Bank", bank);
    form->addRow("Number on the statement", number);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    name->setFocus();
    fitToScreen(&dialog);

    if (dialog.exec() != QDialog::Accepted) return;

    const std::string typedName = name->text().trimmed().toStdString();
    if (typedName.empty()) return;
    const types::AccountType kind =
        types::account_type_from_string(type->currentData().toString().toStdString());
    const std::string parentPath = parent->currentData().toString().toStdString();

    // A category always lives under its fixed root, parent or not -- the rest
    // of the program finds Expenses:Uncategorized and the two report roots by
    // that literal path, so a category with no root would be invisible to
    // them in exactly the way an earlier "Misc Income" was. A real account has
    // no such requirement: the group above already says what it is, so one
    // picked with no parent is simply a name of its own.
    std::string wanted;
    if (!parentPath.empty()) {
        wanted = parentPath + ":" + typedName;
    } else if (type->currentIndex() >= firstCategory) {
        const std::string root = kind == types::AccountType::Income ? "Income"
                                 : kind == types::AccountType::Expense ? "Expenses"
                                                                       : "Equity";
        wanted = root + ":" + typedName;
    } else {
        wanted = typedName;
    }

    std::vector<chart::Account> created;
    const chart::Rejection no = chart_.add(types::AccountPath(wanted), kind, &created);
    if (no.refused) {
        QMessageBox::warning(this, "SimpleAccount", QString::fromStdString(no.reason));
        return;
    }

    // Written onto the account the chart just made. add() takes a path and a
    // type and nothing else, so the two statement lines go on afterwards --
    // and only onto the account itself, never onto a placeholder the path
    // happened to create on the way down.
    const chart::Account* made = chart_.find(wanted);
    if (made != nullptr) {
        chart::Account withNumbers = *made;
        withNumbers.bank = bank->text().trimmed().toStdString();
        withNumbers.account_number = number->text().trimmed().toStdString();
        chart_.put(withNumbers);
    }

    // An opening balance is a transaction against Equity, so the book balances
    // from the account's first day rather than starting out of balance.
    const QString stated = opening->text().trimmed();
    if (!stated.isEmpty()) {
        Money amount;
        try {
            amount = Money(stated.toStdString());
        } catch (const std::exception&) {
            QMessageBox::warning(this, "SimpleAccount",
                                 QString("\"%1\" is not an amount; the account was "
                                         "made with no opening balance.").arg(stated));
            bookChanged(QString::fromStdString(wanted));
            return;
        }
        if (amount.cents() != 0) {
            chart_.add(types::AccountPath("Equity:Opening Balances"),
                       types::AccountType::Equity);
            const QDate on = asAt->date();
            const types::Date when{on.year(), on.month(), on.day()};
            const auto postings = ledger::Ledger::opening_balance_postings(
                types::AccountPath(wanted), kind, amount, when);

            ledger::Transaction t;
            t.id = ledger::new_id();
            t.ref = types::TransactionRef("T" + std::to_string(transactions_.size() + 1));
            t.date = when;
            t.payee = types::PayeeName("Opening Balance");
            if (postings.size() == 2) {
                t.source_account = chart_.ref_for_path(postings[0].account.value());
                t.destination = chart_.ref_for_path(postings[1].account.value());
                t.amount = postings[0].amount;
            }
            for (const ledger::DatedPosting& p : postings)
                book_.add({p.date, p.account, p.amount, t.ref.value()});
            transactions_.push_back(t);
        }
    }

    if (!save()) return;
    bookChanged(QString::fromStdString(wanted) + " added");
}

void MainWindow::importHistoryFrom(const QString& path) {
    std::ifstream in(path.toStdString(), std::ios::binary);
    if (!in) {
        QMessageBox::warning(this, "SimpleAccount", "That file could not be read.");
        return;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    // The history of a real book is the longest read the program does, so it
    // runs on a thread of its own with the strip showing and Stop live. Nothing
    // here is shared with the window: the importer builds its own chart.
    qif::Importer importer;
    const std::string text = buffer.str();
    qif::Imported imported;
    const bool ran = runWithProgress("Reading the history", "records",
                                     [&](const progress::Ticker& tick) {
                                         importer.reporting_to(tick);
                                         imported = importer.run(text);
                                     });
    if (!ran || imported.stopped) {
        // A stopped read leaves the book exactly as it was, because nothing was
        // written: the reading and the saving are separate, and this returns
        // before the saving.
        status_->setText("Reading stopped; the book is as it was");
        return;
    }
    if (imported.refused) {
        // All or nothing: a book half imported is neither the old data nor the
        // new, with no way to tell which arrived.
        QMessageBox::warning(this, "SimpleAccount",
                             QString::fromStdString(imported.refusal));
        return;
    }
    chart_ = imported.accounts;
    book_ = imported.book;
    transactions_ = imported.transactions;
    if (!save()) return;
    rebuildWorkspace();
    refresh();
    status_->setText(QString("%1  --  %2 transactions, %3 accounts, %4 transfers paired, %5 notes")
                         .arg(QFileInfo(path).fileName())
                         .arg(transactions_.size())
                         .arg(chart_.all().size())
                         .arg(imported.summary.duplicate)
                         .arg(imported.notes.size()));
}

// An import is shown in the split pane, beside the register it is about to
// change, rather than in a dialog of its own. Nothing reaches the book until it
// is accepted.
void MainWindow::importQifForReviewFrom(const QString& path) {
    std::ifstream in(path.toStdString(), std::ios::binary);
    if (!in) return;
    std::ostringstream buffer;
    buffer << in.rdbuf();

    qif::Importer importer;
    importer.start_from(chart_);
    importer.import_into(selectedAccount());
    // Reached from Import transactions, so this file is a statement and its
    // rows arrive cleared. The history is asked for once, when the book is
    // made, and keeps the markers it carries.
    importer.as_a_statement();
    // start_from and import_into are settled here, on this thread, and the
    // chart they copied is the importer's own from then on.
    const std::string text = buffer.str();
    qif::Imported imported;
    const bool ran = runWithProgress("Reading the file", "records",
                                     [&](const progress::Ticker& tick) {
                                         importer.reporting_to(tick);
                                         imported = importer.run(text);
                                     });
    if (!ran || imported.stopped) {
        status_->setText("Reading stopped; the book is as it was");
        return;
    }
    if (imported.refused) {
        QMessageBox::warning(this, "SimpleAccount",
                             QString::fromStdString(imported.refusal));
        return;
    }

    // Each two-posting transaction becomes one row: the account the money moved
    // in, and the other side as the import worked it out.
    std::vector<ui::ReviewRow> rows;
    int line = 0;
    std::string into;
    for (const ledger::Transaction& t : imported.transactions) {
        if (chart::is_split(t.destination)) continue;
        ui::ReviewRow row;
        row.line = ++line;
        row.date = t.date;
        row.payee = t.payee.value();
        row.account = chart_.path_for_ref(t.source_account);
        row.category = chart_.path_for_ref(t.destination);
        row.amount = t.amount;
        row.disposition = "New";
        if (into.empty()) into = row.account;
        rows.push_back(row);
    }
    if (rows.empty()) {
        QMessageBox::information(this, "SimpleAccount", "Nothing in that file to review.");
        return;
    }
    // Accounts the import had to invent are needed for the register to resolve
    // them once the rows are accepted.
    chart_ = imported.accounts;
    rebuildWorkspace();
    workspace_->review_import(rows, into);
    refresh();
}

void MainWindow::importTransactions() {
    if (!store_.is_open()) {
        QMessageBox::information(this, "SimpleAccount",
                                 "Open a book before importing into it.");
        return;
    }
    // The file is chosen first and the account second, because the file is the
    // better witness of the two: a QFX knows which account it was downloaded
    // for and a register in front of you only knows what you last looked at.
    // See the section on where an import is going in UserInterface.spectable.
    const QString path = chooseImportFile(
        "Import transactions",
        "Downloaded transactions (*.csv *.CSV *.qfx *.QFX *.ofx *.OFX *.qif *.QIF)"
        ";;All files (*)");
    if (path.isEmpty()) return;
    importTransactionsFrom(path);
}

// The program's own dialog, not the one Windows provides, because the order the
// files come in is the point: an import is nearly always the file just
// downloaded, and the native dialog sorts by name with no way to be told
// otherwise. See the import dialog rule in UserInterface.spectable.
QString MainWindow::chooseImportFile(const QString& title, const QString& filter) {
    QFileDialog dialog(this, title);
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setViewMode(QFileDialog::Detail);
    dialog.setNameFilter(filter);
    // Date Modified is the fourth column of the detail view, and descending
    // puts the newest at the top.
    if (auto* view = dialog.findChild<QTreeView*>("treeView"))
        view->sortByColumn(3, Qt::DescendingOrder);
    if (dialog.exec() != QDialog::Accepted) return {};
    const QStringList chosen = dialog.selectedFiles();
    return chosen.isEmpty() ? QString() : chosen.front();
}

// The dialog in front of an import. It proposes the account the file, its name
// or the tab in front pointed at, says which of the three that was, and lets it
// be changed -- a proposal is a proposal. Empty means cancelled, and cancelled
// reads nothing at all.
MainWindow::ImportAnswer MainWindow::confirmImportAccount(
        const ui::Workspace::ImportConfirmation& proposed) {
    QDialog dialog(this);
    dialog.setWindowTitle("Import");

    QString because;
    switch (proposed.source) {
        case importing::TargetSource::File:
            because = "The file says it is for this account.";
            break;
        case importing::TargetSource::FileName:
            because = "The name of the file fits this account.";
            break;
        case importing::TargetSource::ActiveTab:
            because = "The file does not say which account it is for, so this is the "
                      "register you are in.";
            break;
        case importing::TargetSource::NoneOffered:
            because = "Neither the file nor the name of it says which account it is "
                      "for, so there is nothing to propose. Choose it here.";
            break;
    }

    auto* layout = new QVBoxLayout(scrollingBody(&dialog));
    layout->addWidget(new QLabel(
        QString("Import %1 into:").arg(QString::fromStdString(proposed.file_name)), &dialog));

    // The two lines that came from the bank rather than from this program, and
    // so the two a person compares against the statement in their hand. A file
    // that stated neither leaves them out rather than showing an empty line.
    if (!proposed.bank_id.empty())
        layout->addWidget(new QLabel(
            QString("Bank id %1").arg(QString::fromStdString(proposed.bank_id)), &dialog));
    if (!proposed.account_number.empty())
        layout->addWidget(new QLabel(
            QString("Account number %1")
                .arg(QString::fromStdString(proposed.account_number)),
            &dialog));

    auto* accounts = new QComboBox(&dialog);
    for (const std::string& path : importing::candidates(chart_))
        accounts->addItem(QString::fromStdString(path));
    const int at = accounts->findText(QString::fromStdString(proposed.account));
    if (at >= 0) accounts->setCurrentIndex(at);
    layout->addWidget(accounts);

    auto* why = new QLabel(because, &dialog);
    why->setWordWrap(true);
    layout->addWidget(why);

    // What the bank says the account stands at. A QFX states it and this
    // arrives filled in; a CSV states nothing and the line is here to be
    // typed into off the bank's own website, which is the only way the
    // register can ever say whether the two agree. Left empty it is no
    // answer -- which is not the same thing as a balance of zero.
    auto* figures = new QFormLayout;
    auto* online = new QLineEdit(QString::fromStdString(proposed.online_balance), &dialog);
    online->setPlaceholderText("0.00");
    figures->addRow("On-line balance", online);
    auto* onlineAt = new QDateEdit(&dialog);
    onlineAt->setCalendarPopup(true);
    onlineAt->setDisplayFormat("yyyy-MM-dd");
    const std::optional<types::Date> stated =
        types::Date::from_iso(proposed.online_balance_at);
    onlineAt->setDate(stated.has_value()
                          ? QDate(stated->year(), stated->month(), stated->day())
                          : QDate::currentDate());
    figures->addRow("as at", onlineAt);
    layout->addLayout(figures);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Import");
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    fitToScreen(&dialog);

    if (dialog.exec() != QDialog::Accepted) return {};

    ImportAnswer answered;
    answered.account = accounts->currentText().toStdString();
    // An amount that cannot be read is no answer rather than a refusal: the
    // import is the thing being done here, and it is not worth stopping over
    // a figure that is only a check on it. The register says nothing about a
    // downloaded balance it was never given.
    const std::string typed = online->text().trimmed().toStdString();
    if (!typed.empty() && Money::is_valid(typed)) {
        answered.online_balance = Money(typed).in_csv();
        answered.online_balance_at = asDate(onlineAt->date()).iso();
    }
    return answered;
}

// Every pairing the import found between a row being reviewed and a half the
// book already has. One question at a time, and no answer writes anything:
// yes puts each half on the other's account in the review, and accepting the
// review is what writes. Each side is quoted as its own file wrote it, with
// the gap in days, which is the thing a person most needs in order to
// disagree -- see the transfer question in UserInterface.spectable.
void MainWindow::askAboutTransfers() {
    while (workspace_->transfer_question().shown) {
        const ui::Workspace::TransferQuestion q = workspace_->transfer_question();
        std::string said = "Are these the same payment?\n\n";
        said += q.from_account + "    " + q.from_date.iso() + "    " + q.from_payee + "\n";
        said += q.to_account + "    " + q.to_date.iso() + "    " + q.to_payee + "\n\n";
        said += q.amount.in_register() + ", " + std::to_string(q.days_apart) +
                (q.days_apart == 1 ? " day apart." : " days apart.");
        QMessageBox ask(this);
        ask.setWindowTitle("Import");
        ask.setIcon(QMessageBox::Question);
        ask.setText(QString::fromStdString(said));
        QPushButton* yes = ask.addButton("One payment", QMessageBox::YesRole);
        ask.addButton("Two payments", QMessageBox::NoRole);
        ask.exec();
        workspace_->answer_transfer_question(ask.clickedButton() == yes);
    }
}

void MainWindow::importTransactionsFrom(const QString& path) {
    std::ifstream in(path.toStdString(), std::ios::binary);
    if (!in) {
        QMessageBox::warning(this, "SimpleAccount",
                             QString("%1 could not be read").arg(path));
        return;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();

    // QIF has no menu item of its own any more, so it arrives here and is sent
    // on to the reader that understands it. The two differ in more than
    // parsing: a QIF names its own accounts, so there is no account to confirm.
    if (qif::looks_like_a_qif(text)) {
        importQifForReviewFrom(path);
        return;
    }

    // What the file knows about itself, before a row of it is read. The mapping
    // table is not read here yet, so a QFX is placed by the account number
    // written on the chart; the dialog is what covers the rest.
    const std::string in_file = review::account_in_file(text, chart_, {});
    const review::FileNumbers numbers = review::numbers_in_file(text);
    // The dialog opens whatever the file turned out to say, including when it
    // said nothing at all, which is the case the numbers are shown for.
    workspace_->choose_import_file(QFileInfo(path).fileName().toStdString(), in_file,
                                   numbers.bank_id, numbers.account_number,
                                   numbers.balance, numbers.balance_at);
    const ImportAnswer answered = confirmImportAccount(workspace_->import_confirmation());
    const std::string account = answered.account;
    if (account.empty()) {
        workspace_->cancel_import_confirmation();
        return;
    }

    // Everything about reading the file is production code with no Qt in it, so
    // what happens here is the same as what the command line does.
    chart::Chart accounts = chart_;
    // Whether a CSV arrives cleared is the book's own setting, read off its row
    // rather than remembered here. See the CSV cleared rule in ImportCsv.spectable.
    const bool clear_a_csv = !store_.is_open() || store_.info().set_cleared_on_csv_import;
    // On a thread of its own, over its own copy of the chart. The window's own
    // transactions are read but not written -- the reader compares against them
    // to find what it has seen before -- and nothing on this thread writes them
    // while it waits, because everything that could is disabled.
    //
    // Deciding which rows are duplicates happens before the first row is made,
    // and that is the slow half of a download; it reports nothing, so the strip
    // shows the words and a bar that says only that something is going on until
    // the rows start. Which is what the rule asks for: a count with no total is
    // not an estimate.
    review::Prepared prepared;
    const bool ran = runWithProgress(
        "Reading the download", "rows", [&](const progress::Ticker& tick) {
            prepared = review::from_file(text, account, transactions_, &accounts,
                                         rules_, clear_a_csv, conversions_,
                                         categorisations_, tick);
        });
    if (!ran || prepared.stopped) {
        workspace_->cancel_import_confirmation();
        status_->setText("Reading stopped; the book is as it was");
        return;
    }
    if (prepared.refused) {
        workspace_->cancel_import_confirmation();
        QMessageBox::warning(this, "SimpleAccount",
                             QString::fromStdString(prepared.refusal));
        return;
    }
    if (prepared.rows.empty()) {
        workspace_->cancel_import_confirmation();
        QMessageBox::information(this, "SimpleAccount",
                                 "There is nothing in that file to review.");
        return;
    }

    // Categories the import had to invent are needed for the register to resolve
    // them once the rows are accepted.
    chart_ = accounts;
    // The register of the account just confirmed becomes the active tab --
    // opened if it was not open, brought forward if it was -- and the rows open
    // in the panel beneath it, against the lines they are about to join. That
    // is the whole reason the review is a panel under a register rather than a
    // dialog of its own.
    rebuildWorkspace();
    const int wrote = workspace_->book_writes();
    workspace_->confirm_import_with(account, answered.online_balance,
                                    answered.online_balance_at, prepared.rows);
    askAboutTransfers();
    // The first download confirmed for an account teaches it the bank id and
    // the account number the file carried, and that is a change to the book
    // rather than to the review -- so it is written now, rather than waiting
    // for a review that may be cancelled. teach_account fills what is blank
    // and leaves what disagrees, so a second download writes nothing.
    if (workspace_->book_writes() != wrote) {
        chart_ = workspace_->accounts();
        save();
    }
    refresh();

    // Said out loud rather than swallowed. A note nobody is shown is a note
    // nobody acts on.
    if (!prepared.notes.empty()) {
        QString said;
        for (const std::string& note : prepared.notes)
            said += QString::fromStdString(note) + "\n";
        QMessageBox::information(this, "Worth a look", said.trimmed());
    }
}

// Builds the content of one report tab. Everything that used to live on the
// stack of a modal dialog now lives as long as the tab does, so every lambda
// below captures what it needs explicitly rather than by reference -- a "[&]"
// here would dangle the moment this function returned.
MainWindow::TabWidgets MainWindow::makeReportTab(int tabPosition, const std::string& title) {
    (void)tabPosition;
    TabWidgets t;
    t.isReport = true;
    auto* container = new QWidget;
    t.widget = container;
    auto* layout = new QVBoxLayout(container);

    // The periods Quicken offers, by the names it uses, because the figures are
    // read against its reports. All Dates runs from the first posting to today.
    t.period = new QComboBox;
    t.period->addItems({"YearToDate", "ThisYear", "LastYear", "ThisQuarter",
                        "ThisMonth", "MonthToDate", "LastMonth", "Last12Months",
                        "All"});
    t.from = new QDateEdit;
    t.to = new QDateEdit;
    for (QDateEdit* edit : {t.from, t.to}) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat("yyyy-MM-dd");
    }
    // How much of the tree to show. The three the report offers, in the order
    // they go from summary to everything. See the detail section of
    // Reports.spectable.
    t.detail = new QComboBox;
    t.detail->addItem("Categories - highest only",
                      QString::fromStdString(reports::to_string(reports::Detail::HighestOnly)));
    t.detail->addItem("Categories - all",
                      QString::fromStdString(reports::to_string(reports::Detail::AllCategories)));
    t.detail->addItem("Transactions",
                      QString::fromStdString(reports::to_string(reports::Detail::Transactions)));
    t.detail->setCurrentIndex(1);

    t.quicken = new QCheckBox("Quicken signs");
    t.quicken->setToolTip("Expenses negative, as Quicken writes them, so a figure "
                          "can be read straight against one of its reports.");
    // The book says how figures are read here, so it says how this report is
    // read to begin with; one run can still be asked for the other way without
    // the book setting moving. A report tab already open keeps the box it was
    // given -- it is this run's question, not the book's.
    t.quicken->setChecked(quickenSigns_);
    t.zero = new QCheckBox("Show empty categories");

    auto* chooser = new QHBoxLayout;
    chooser->addWidget(new QLabel("Period"));
    chooser->addWidget(t.period);
    chooser->addWidget(new QLabel("from"));
    chooser->addWidget(t.from);
    chooser->addWidget(new QLabel("to"));
    chooser->addWidget(t.to);
    chooser->addSpacing(12);
    chooser->addWidget(new QLabel("Show"));
    chooser->addWidget(t.detail);
    chooser->addStretch(1);
    chooser->addWidget(t.quicken);
    chooser->addWidget(t.zero);

    // The chooser is a single row and nothing in it wraps, so a narrow window
    // would squeeze the dates down to nothing and then hide the check boxes
    // altogether. It scrolls sideways instead, at the height one row needs
    // plus room for the bar; the report below it scrolls on its own.
    auto* chooserHolder = new QWidget;
    chooser->setContentsMargins(0, 0, 0, 0);
    chooserHolder->setLayout(chooser);
    auto* chooserScroll = new QScrollArea;
    chooserScroll->setWidgetResizable(true);
    chooserScroll->setFrameShape(QFrame::NoFrame);
    chooserScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chooserScroll->setWidget(chooserHolder);
    chooserScroll->setFixedHeight(
        chooserHolder->sizeHint().height() +
        chooserScroll->horizontalScrollBar()->sizeHint().height() + 2);
    layout->addWidget(chooserScroll);

    t.reportTable = new QTableWidget;
    t.reportTable->verticalHeader()->setVisible(false);
    t.reportTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(t.reportTable, 1);

    t.totals = new QLabel;
    QFont bold = appFont_;
    bold.setBold(true);
    t.totals->setFont(bold);
    layout->addWidget(t.totals);

    // The lines the table is showing, kept so the menu can turn a selected row
    // back into a transaction.
    t.shown = std::make_shared<std::vector<reports::DetailLine>>();

    // One step of indent per level, which is what makes a child read as part
    // of the line above it rather than as another category of its own.
    const auto indented = [](int level, const std::string& text) {
        return QString::fromStdString(
            std::string(static_cast<std::size_t>(level) * 2, ' ') + text);
    };

    QComboBox* period = t.period;
    QDateEdit* from = t.from;
    QDateEdit* to = t.to;
    QComboBox* detail = t.detail;
    QCheckBox* quicken = t.quicken;
    QCheckBox* zero = t.zero;
    QTableWidget* table = t.reportTable;
    QLabel* totals = t.totals;
    std::shared_ptr<std::vector<reports::DetailLine>> shown = t.shown;

    t.run = [this, container, from, to, detail, quicken, zero, table, totals, shown,
             indented, bold]() {
        reports::Spec spec;
        spec.from = asDate(from->date());
        spec.to = asDate(to->date());
        spec.detail = reports::detail_from_string(
            detail->currentData().toString().toStdString());
        spec.quicken_signs = quicken->isChecked();
        spec.zero_rows = zero->isChecked();

        // Showing the transactions adds a column for the individual amounts, to
        // the left of the column the category figures are read down. They are
        // two different kinds of figure and one column would invite adding a
        // transaction to the total that already contains it.
        const bool entries = spec.detail == reports::Detail::Transactions;
        table->setColumnCount(entries ? 3 : 2);
        table->setHorizontalHeaderLabels(entries
                                             ? QStringList{"Category", "Each", "Amount"}
                                             : QStringList{"Category", "Amount"});
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

        const reports::CategoryReport report =
            reports::category_report(chart_, book_, spec);

        if (entries) {
            const std::vector<reports::DetailLine> lines =
                reports::detail_report(chart_, book_, transactions_, splits_, spec);
            *shown = lines;
            table->setRowCount(static_cast<int>(lines.size()));
            for (int r = 0; r < static_cast<int>(lines.size()); ++r) {
                const reports::DetailLine& line = lines[static_cast<std::size_t>(r)];
                // A transaction reads as the date and who it was to; a category
                // by its own name, and the Other line by the name the report
                // gave it, which is not an account and has no name to take.
                QString label;
                switch (line.kind) {
                    case reports::LineKind::Transaction:
                        label = indented(line.level,
                                         line.date.iso() + "  " + line.payee);
                        break;
                    case reports::LineKind::Other:
                        label = indented(line.level, line.account);
                        break;
                    case reports::LineKind::Category:
                        label = indented(line.level,
                                         types::name_of(types::AccountPath(line.account)));
                        break;
                }
                auto* item = new QTableWidgetItem(label);
                item->setToolTip(QString::fromStdString(line.account));
                if (line.is_subtotal) item->setFont(bold);
                table->setItem(r, 0, item);
                // One figure per line and the other column left empty, which is
                // not the same as showing it as zero.
                if (line.has_each) setAmount(table, r, 1, line.each);
                if (line.has_amount) setAmount(table, r, 2, line.amount);
                if (line.is_subtotal && table->item(r, 2) != nullptr)
                    table->item(r, 2)->setFont(bold);
            }
        } else {
            shown->clear();
            table->setRowCount(static_cast<int>(report.rows.size()));
            for (int r = 0; r < static_cast<int>(report.rows.size()); ++r) {
                const reports::Row& line = report.rows[static_cast<std::size_t>(r)];
                auto* item = new QTableWidgetItem(
                    indented(line.level,
                             types::name_of(types::AccountPath(line.account))));
                item->setToolTip(QString::fromStdString(line.account));
                if (line.is_subtotal) item->setFont(bold);
                table->setItem(r, 0, item);
                setAmount(table, r, 1, line.amount);
                if (line.is_subtotal && table->item(r, 1) != nullptr)
                    table->item(r, 1)->setFont(bold);
            }
        }
        totals->setText(QString("Income %1        Expenses %2        Net %3")
                            .arg(money(report.totals.income))
                            .arg(money(report.totals.expenses))
                            .arg(money(report.totals.net)));

        // The figures on this tab are now the book's as it stands, which is
        // what spares the next redraw the work. Said from inside run() rather
        // than beside the one call that is gated, because changing the period
        // or the detail also works the report out -- those go straight to
        // run() and never through refreshPane(). The tab is found by its own
        // widget: a position captured when the tab was made goes wrong the
        // moment a tab to the left of it closes.
        if (workspace_) workspace_->report_worked_out(tabs_->indexOf(container) + 1);
    };
    std::function<void()> run = t.run;

    // The earliest posting in the book, which is where All Dates starts.
    types::Date earliest = asDate(QDate::currentDate());
    for (const ledger::DatedPosting& one : book_.postings())
        if (one.date < earliest) earliest = one.date;

    const auto takePeriod = [this, period, from, to, run, earliest]() {
        const auto range = reports::named_range(
            period->currentText().toStdString(), asDate(QDate::currentDate()),
            earliest);
        if (!range) return;
        const QSignalBlocker quietFrom(from);
        const QSignalBlocker quietTo(to);
        from->setDate(QDate(range->from.year(), range->from.month(), range->from.day()));
        to->setDate(QDate(range->to.year(), range->to.month(), range->to.day()));
        run();
    };

    connect(period, &QComboBox::currentTextChanged, container, takePeriod);
    // Typing a date of your own is not the same as asking for a named period,
    // so it runs the report without moving the period back.
    connect(from, &QDateEdit::dateChanged, container, run);
    connect(to, &QDateEdit::dateChanged, container, run);
    connect(detail, &QComboBox::currentIndexChanged, container, run);
    connect(quicken, &QCheckBox::toggled, container, run);
    connect(zero, &QCheckBox::toggled, container, run);

    // One transaction selected can be opened; several can only be given a
    // category together. A category line is neither, and offers nothing.
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // A report is paged by its scroll bar the same as a register is.
    pagesTheView(table);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QTableWidget::customContextMenuRequested, container,
            [this, table, shown, run](const QPoint& at) {
        std::vector<std::string> refs;
        for (const QModelIndex& index : table->selectionModel()->selectedRows()) {
            const std::size_t row = static_cast<std::size_t>(index.row());
            if (row >= shown->size()) continue;
            const reports::DetailLine& line = (*shown)[row];
            if (line.kind != reports::LineKind::Transaction) continue;
            refs.push_back(line.ref);
        }
        if (refs.empty()) return;

        QMenu menu(table);
        if (refs.size() == 1) {
            // A transfer is in two registers and neither is more right than the
            // other -- a payment to a card is in the current account's register
            // and in the card's -- so both are offered and the reader chooses.
            const std::vector<std::string> where =
                workspace_->registers_offered(refs.front());
            const auto open = [this, refs](const std::string& account) {
                if (!workspace_->go_to_transaction_in(refs.front(), account)) return;
                // The newly active tab is a register; syncTabs brings the Qt
                // tab bar into step with it.
                refresh();
            };
            if (where.size() == 1) {
                QAction* go = menu.addAction("Go to the transaction...");
                const std::string only = where.front();
                connect(go, &QAction::triggered, table, [open, only]() { open(only); });
            } else if (where.size() > 1) {
                QMenu* go = menu.addMenu("Go to the transaction in");
                for (const std::string& account : where) {
                    QAction* one = go->addAction(QString::fromStdString(account));
                    connect(one, &QAction::triggered, table,
                            [open, account]() { open(account); });
                }
            }
        }
        QAction* change = menu.addAction("Recategorize...");
        connect(change, &QAction::triggered, table, [this, refs]() {
            CategoryPick pick(this, chart_, book_, std::string());
            if (pick.exec() != QDialog::Accepted) return;
            const std::string into = pick.chosen();
            if (into.empty()) return;
            const ui::Recategorised done = workspace_->recategorise(refs, into);
            int changed = done.changed;
            // A transfer on a report line is the same question as a transfer in
            // a register selection, and is asked the same way. See the
            // transfer-warning rule in UserInterface.spectable.
            if (done.warned > 0) {
                QString ask = QString::fromStdString(done.warning);
                if (done.warned > 1)
                    ask += QString("\n\nand %1 more like it.").arg(done.warned - 1);
                ask += "\n\nGo ahead?";
                const bool accept =
                    QMessageBox::question(this, "Recategorize", ask,
                                          QMessageBox::Yes | QMessageBox::No,
                                          QMessageBox::No) == QMessageBox::Yes;
                changed += workspace_->answer_transfer_warning(accept).changed;
            }
            if (changed > 0) {
                transactions_ = workspace_->transactions();
                book_ = workspace_->book();
                save();
                rebuildWorkspace();
                // The figures have moved, which is the one case where
                // recategorising changes what is on the page -- and the
                // refresh works the report out again for exactly that
                // reason, so there is nothing to do here but ask for one.
                refresh();
            }
            if (done.refused > 0 && !done.reason.empty())
                QMessageBox::information(this, "Recategorize",
                                         QString::fromStdString(done.reason));
        });
        menu.exec(table->viewport()->mapToGlobal(at));
    });

    takePeriod();
    (void)title;
    return t;
}

// The name the Reports menu offered, out of the name a tab carries. A second
// tab of the same report is numbered -- Balance sheet 2 -- and is the same
// report, so the number is dropped before the name is looked up.
static std::string reportNameOf(const std::string& tab) {
    for (const reports::Offered& offered : reports::offered_reports())
        if (tab.rfind(offered.name, 0) == 0) return offered.name;
    return tab;
}

// Builds the content of one balance-sheet tab. What it asks for is one date,
// because a sheet is a position and not a period -- see the balance-sheet
// section of UserInterface.spectable. Everything else about the tab is what
// every report tab does: it is worked out when it is shown rather than when
// the book changed, and run() says that it has been.
MainWindow::TabWidgets MainWindow::makeBalanceSheetTab(int tabPosition,
                                                       const std::string& title) {
    (void)tabPosition;
    (void)title;
    TabWidgets t;
    t.isReport = true;
    auto* container = new QWidget;
    t.widget = container;
    auto* layout = new QVBoxLayout(container);

    t.asAt = new QDateEdit;
    t.asAt->setCalendarPopup(true);
    t.asAt->setDisplayFormat("yyyy-MM-dd");
    t.asAt->setDate(QDate::currentDate());
    // An account with nothing in it is left off, the same as a category with
    // nothing in it is left off the other report, unless it is asked for.
    t.zero = new QCheckBox("Show empty accounts");

    auto* chooser = new QHBoxLayout;
    chooser->setContentsMargins(0, 0, 0, 0);
    chooser->addWidget(new QLabel("As at"));
    chooser->addWidget(t.asAt);
    chooser->addStretch(1);
    chooser->addWidget(t.zero);
    auto* chooserHolder = new QWidget;
    chooserHolder->setLayout(chooser);
    layout->addWidget(chooserHolder);

    t.reportTable = new QTableWidget;
    t.reportTable->verticalHeader()->setVisible(false);
    t.reportTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t.reportTable->setColumnCount(2);
    t.reportTable->setHorizontalHeaderLabels({"Account", "Balance"});
    t.reportTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    t.reportTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    t.reportTable->setSelectionMode(QAbstractItemView::SingleSelection);
    pagesTheView(t.reportTable);
    layout->addWidget(t.reportTable, 1);

    t.totals = new QLabel;
    QFont bold = appFont_;
    bold.setBold(true);
    t.totals->setFont(bold);
    layout->addWidget(t.totals);

    t.sheet = std::make_shared<std::vector<reports::BalanceSheetRow>>();

    QDateEdit* asAt = t.asAt;
    QCheckBox* zero = t.zero;
    QTableWidget* table = t.reportTable;
    QLabel* totals = t.totals;
    std::shared_ptr<std::vector<reports::BalanceSheetRow>> sheet = t.sheet;

    t.run = [this, container, asAt, zero, table, totals, sheet, bold]() {
        const reports::BalanceSheet drawn = reports::balance_sheet(
            chart_, book_, asDate(asAt->date()), zero->isChecked(), quickenSigns_);
        *sheet = drawn.rows;
        table->setRowCount(static_cast<int>(drawn.rows.size()));
        for (int r = 0; r < static_cast<int>(drawn.rows.size()); ++r) {
            const reports::BalanceSheetRow& line =
                drawn.rows[static_cast<std::size_t>(r)];
            // A heading reads as the group it is the total of; a line reads as
            // the name of its account, indented under that heading.
            const std::string label =
                std::string(static_cast<std::size_t>(line.level) * 2, 32) +
                (line.account.empty()
                     ? line.group
                     : types::name_of(types::AccountPath(line.account)));
            auto* item = new QTableWidgetItem(QString::fromStdString(label));
            if (!line.account.empty())
                item->setToolTip(QString::fromStdString(line.account));
            const bool heavy = line.is_heading || line.is_subtotal;
            if (heavy) item->setFont(bold);
            table->setItem(r, 0, item);
            setAmount(table, r, 1, line.balance);
            if (heavy && table->item(r, 1) != nullptr) table->item(r, 1)->setFont(bold);
        }
        totals->setText(QString("Assets %1        Liabilities %2        Net worth %3")
                            .arg(money(drawn.totals.assets))
                            .arg(money(drawn.totals.liabilities))
                            .arg(money(drawn.totals.net)));
        if (workspace_) workspace_->report_worked_out(tabs_->indexOf(container) + 1);
    };
    std::function<void()> run = t.run;

    connect(asAt, &QDateEdit::dateChanged, container, run);
    connect(zero, &QCheckBox::toggled, container, run);

    // Every line of a sheet but a heading names a real account, and a real
    // account has a register, so the sheet is a way into the book and not only
    // a page to read. A single click is how a row is selected, so the one that
    // opens a register is the double click, and the same thing is on the
    // right-click menu, where the rest of the program says what a click does.
    const auto open = [this, sheet](int row) {
        if (row < 0 || row >= static_cast<int>(sheet->size())) return;
        const reports::BalanceSheetRow& line = (*sheet)[static_cast<std::size_t>(row)];
        if (line.is_heading || line.account.empty()) return;
        workspace_->select(line.account);
        refresh();
    };
    connect(table, &QTableWidget::cellDoubleClicked, container,
            [open](int row, int) { open(row); });
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QTableWidget::customContextMenuRequested, container,
            [this, table, sheet, open](const QPoint& at) {
        const int row = table->rowAt(at.y());
        if (row < 0 || row >= static_cast<int>(sheet->size())) return;
        const reports::BalanceSheetRow& line = (*sheet)[static_cast<std::size_t>(row)];
        if (line.is_heading || line.account.empty()) return;
        QMenu menu(table);
        QAction* go = menu.addAction(
            QString("Open the register for %1...")
                .arg(QString::fromStdString(line.account)));
        connect(go, &QAction::triggered, table, [open, row]() { open(row); });
        menu.exec(table->viewport()->mapToGlobal(at));
    });

    run();
    return t;
}

void MainWindow::openReport(const std::string& title) {
    if (!store_.is_open()) {
        QMessageBox::information(this, "SimpleAccount",
                                 "Open a book before reporting on it.");
        return;
    }
    workspace_->open_report(title);
    refresh();
}

void MainWindow::assignPayeesAccordingToRules() {
    if (!store_.is_open()) {
        QMessageBox::information(this, "SimpleAccount", "Open a book first.");
        return;
    }
    const payees::BookApplication done =
        payees::apply_to_book(rules_, chart_, &transactions_);
    book_ = ledger::Ledger();
    ledger::add_to_book(&book_, chart_, transactions_, splits_);
    save();
    rebuildWorkspace();
    refresh();
    QMessageBox::information(this, "Assign payees according to rules",
        QString("%1 matched, %2 already correct, %3 left alone")
            .arg(done.matched).arg(done.already_correct).arg(done.skipped));
}

void MainWindow::editRules() {
    if (!store_.is_open()) {
        QMessageBox::information(this, "SimpleAccount",
                                 "Open a book before working on its rules.");
        return;
    }

    QDialog box(this);
    box.setWindowTitle("Payee rules");
    box.resize(900, 520);
    auto* layout = new QVBoxLayout(&box);

    auto* explain = new QLabel(
        "In the order they are tried: a rule above another is the one that wins. "
        "A rule applies when a transaction is imported or categorised, so "
        "changing one here does not alter what it has already done.");
    explain->setWordWrap(true);
    layout->addWidget(explain);

    auto* table = new QTableWidget;
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({"Looks for", "How", "Payee", "Category", "On"});
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(table, 1);

    auto* row = new QHBoxLayout;
    auto* add = new QPushButton("&Add...");
    auto* edit = new QPushButton("&Edit...");
    auto* drop = new QPushButton("&Delete");
    auto* toggle = new QPushButton("Turn o&ff");
    auto* close = new QPushButton("&Close");
    close->setDefault(true);
    row->addWidget(add);
    row->addWidget(edit);
    row->addWidget(drop);
    row->addWidget(toggle);
    row->addStretch(1);
    row->addWidget(close);
    layout->addLayout(row);

    // The rules are held in the order they are tried, so a row of the table is
    // the rule at the same index. Sorting is stable and idempotent, so keeping
    // them sorted loses nothing: two rules the precedence rule cannot separate
    // stay in the order they were added.
    const auto fill = [&](int select) {
        rules_ = payees::in_order(rules_);
        table->setRowCount(static_cast<int>(rules_.size()));
        for (std::size_t i = 0; i < rules_.size(); ++i) {
            const payees::Rule& one = rules_[i];
            const int at = static_cast<int>(i);
            table->setItem(at, 0, text(one.pattern));
            table->setItem(at, 1, text(payees::to_string(one.match_type)));
            table->setItem(at, 2, text(one.payee));
            table->setItem(at, 3, text(one.category));
            table->setItem(at, 4, text(one.enabled ? "yes" : "no"));
            if (!one.enabled)
                for (int c = 0; c < 5; ++c)
                    table->item(at, c)->setForeground(QColor(130, 130, 130));
        }
        if (select >= 0 && select < table->rowCount()) table->selectRow(select);
        const bool any = table->currentRow() >= 0;
        edit->setEnabled(any);
        drop->setEnabled(any);
        toggle->setEnabled(any);
        if (any) {
            toggle->setText(rules_[static_cast<std::size_t>(table->currentRow())].enabled
                                ? "Turn o&ff" : "Turn o&n");
        }
    };

    const auto chosen = [&]() { return table->currentRow(); };

    QObject::connect(add, &QPushButton::clicked, &box, [&]() {
        RuleForm form(&box, "Add a rule", payees::Rule{}, [this](const std::string& start) {
            CategoryPick choose(this, chart_, book_, start);
            if (choose.exec() != QDialog::Accepted) return std::string();
            return choose.chosen();
        });
        if (form.exec() != QDialog::Accepted) return;
        const payees::Refusal no = payees::add(&rules_, form.rule());
        if (no.refused) {
            QMessageBox::warning(&box, "SimpleAccount",
                                 QString::fromStdString(no.reason));
            return;
        }
        save();
        fill(-1);
    });

    QObject::connect(edit, &QPushButton::clicked, &box, [&]() {
        const int at = chosen();
        if (at < 0) return;
        const payees::Rule was = rules_[static_cast<std::size_t>(at)];
        RuleForm form(&box, "Edit a rule", was, [this](const std::string& start) {
            CategoryPick choose(this, chart_, book_, start);
            if (choose.exec() != QDialog::Accepted) return std::string();
            return choose.chosen();
        });
        if (form.exec() != QDialog::Accepted) return;
        const payees::Refusal no =
            payees::change(&rules_, was.pattern, was.match_type, form.rule());
        if (no.refused) {
            QMessageBox::warning(&box, "SimpleAccount",
                                 QString::fromStdString(no.reason));
            return;
        }
        save();
        fill(at);
    });

    QObject::connect(drop, &QPushButton::clicked, &box, [&]() {
        const int at = chosen();
        if (at < 0) return;
        const payees::Rule one = rules_[static_cast<std::size_t>(at)];
        // Asked, because the usual move is to turn a rule off rather than lose
        // it: one that was wrong once is usually wanted again in a changed form.
        const QMessageBox::StandardButton said = QMessageBox::question(
            &box, "Delete this rule?",
            QString("Delete the rule looking for %1?\n\nTransactions it has "
                    "already categorised are left as they are. Turning it off "
                    "instead keeps it to come back to.")
                .arg(QString::fromStdString(one.pattern)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (said != QMessageBox::Yes) return;
        payees::remove(&rules_, one.pattern, one.match_type);
        save();
        fill(at - 1);
    });

    QObject::connect(toggle, &QPushButton::clicked, &box, [&]() {
        const int at = chosen();
        if (at < 0) return;
        const payees::Rule one = rules_[static_cast<std::size_t>(at)];
        payees::set_enabled(&rules_, one.pattern, one.match_type, !one.enabled);
        save();
        fill(at);
    });

    QObject::connect(table, &QTableWidget::itemSelectionChanged, &box, [&]() {
        const bool any = table->currentRow() >= 0;
        edit->setEnabled(any);
        drop->setEnabled(any);
        toggle->setEnabled(any);
        if (any)
            toggle->setText(rules_[static_cast<std::size_t>(table->currentRow())].enabled
                                ? "Turn o&ff" : "Turn o&n");
    });

    QObject::connect(close, &QPushButton::clicked, &box, &QDialog::accept);
    QObject::connect(table, &QTableWidget::doubleClicked, edit, &QPushButton::click);

    fill(rules_.empty() ? -1 : 0);
    box.exec();
}

// The converter tables. One window of two tabs: a conversion says what to call
// a name the bank keeps sending, a categorisation says where that payee's money
// goes, and the two are read one after the other -- convert, then categorise --
// which is why they are in one window. Two tables and not one because a single
// table would have a blank in one of its columns on nearly every row. See
// decisions.md and the converter section of Payees.spectable.
//
// Edited in place rather than through a row dialog, because these are lists
// somebody puts in an order and reorders: the row that acts is the first that
// matches, so moving a row is the fix for a pattern that is too broad, and that
// wants to be one click rather than a form.
void MainWindow::editPayeeTables() {
    if (!store_.is_open()) {
        QMessageBox::information(this, "SimpleAccount",
                                 "Open a book before working on its payee tables.");
        return;
    }

    QDialog box(this);
    box.setWindowTitle("Payee tables");
    box.resize(900, 560);
    auto* layout = new QVBoxLayout(&box);

    auto* explain = new QLabel(
        "The first row that matches is the one that acts, so a row above "
        "another wins. A pattern is matched against the whole name with * "
        "standing for any run of characters, and case is ignored.");
    explain->setWordWrap(true);
    layout->addWidget(explain);

    auto* tabs = new QTabWidget;
    layout->addWidget(tabs, 1);

    const auto makeTable = [](const QStringList& headings) {
        auto* t = new QTableWidget;
        t->setColumnCount(headings.size());
        t->setHorizontalHeaderLabels(headings);
        t->verticalHeader()->setVisible(false);
        t->setSelectionBehavior(QAbstractItemView::SelectRows);
        t->setSelectionMode(QAbstractItemView::SingleSelection);
        for (int c = 0; c < headings.size(); ++c)
            t->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Stretch);
        return t;
    };

    auto* conversions = makeTable({"The name in the file", "What to call it"});
    auto* categories = makeTable({"Payee", "Category"});
    for (std::size_t i = 0; i < conversions_.size(); ++i) {
        conversions->insertRow(static_cast<int>(i));
        conversions->setItem(static_cast<int>(i), 0, text(conversions_[i].match));
        conversions->setItem(static_cast<int>(i), 1, text(conversions_[i].converted));
    }
    for (std::size_t i = 0; i < categorisations_.size(); ++i) {
        categories->insertRow(static_cast<int>(i));
        categories->setItem(static_cast<int>(i), 0, text(categorisations_[i].payee));
        categories->setItem(static_cast<int>(i), 1, text(categorisations_[i].category));
    }

    // Each tab carries its own row buttons, so the buttons act on the table
    // being looked at and there is nothing to work out about which is which.
    const auto pageFor = [&](QTableWidget* table, const QString& applyWords) {
        auto* page = new QWidget;
        auto* down = new QVBoxLayout(page);
        down->addWidget(table, 1);
        auto* row = new QHBoxLayout;
        auto* add = new QPushButton("&Add row");
        auto* drop = new QPushButton("&Delete row");
        auto* up = new QPushButton("Move &up");
        auto* below = new QPushButton("Move do&wn");
        auto* apply = new QPushButton(applyWords);
        row->addWidget(add);
        row->addWidget(drop);
        row->addWidget(up);
        row->addWidget(below);
        row->addStretch(1);
        row->addWidget(apply);
        down->addLayout(row);

        QObject::connect(add, &QPushButton::clicked, page, [table]() {
            const int at = table->rowCount();
            table->insertRow(at);
            for (int c = 0; c < table->columnCount(); ++c)
                table->setItem(at, c, new QTableWidgetItem(QString()));
            table->setCurrentCell(at, 0);
            table->editItem(table->item(at, 0));
        });
        QObject::connect(drop, &QPushButton::clicked, page, [table]() {
            const int at = table->currentRow();
            if (at >= 0) table->removeRow(at);
        });
        // Moving a row is taking the text out and putting it back one place
        // along: a QTableWidget has no move, and swapping the items by pointer
        // leaves the selection on a row that is no longer the one moved.
        const auto swap = [table](int a, int b) {
            if (a < 0 || b < 0 || a >= table->rowCount() || b >= table->rowCount()) return;
            const int column = table->currentColumn() < 0 ? 0 : table->currentColumn();
            for (int c = 0; c < table->columnCount(); ++c) {
                const QString first = table->item(a, c) != nullptr
                                          ? table->item(a, c)->text() : QString();
                const QString second = table->item(b, c) != nullptr
                                           ? table->item(b, c)->text() : QString();
                table->setItem(a, c, new QTableWidgetItem(second));
                table->setItem(b, c, new QTableWidgetItem(first));
            }
            table->setCurrentCell(b, column);
        };
        QObject::connect(up, &QPushButton::clicked, page, [table, swap]() {
            swap(table->currentRow(), table->currentRow() - 1);
        });
        QObject::connect(below, &QPushButton::clicked, page, [table, swap]() {
            swap(table->currentRow(), table->currentRow() + 1);
        });
        return std::make_pair(page, apply);
    };

    const std::pair<QWidget*, QPushButton*> conversionPage =
        pageFor(conversions, "Apply to the &book...");
    const std::pair<QWidget*, QPushButton*> categoryPage =
        pageFor(categories, "Apply to the &book...");
    tabs->addTab(conversionPage.first, "Conversions");
    tabs->addTab(categoryPage.first, "Categories");

    auto* bottom = new QHBoxLayout;
    auto* close = new QPushButton("&Close");
    close->setDefault(true);
    bottom->addStretch(1);
    bottom->addWidget(close);
    layout->addLayout(bottom);

    // Reading the tables back. A row with nothing in either column is a row
    // somebody started and left, and is dropped rather than stored as a pattern
    // of nothing -- which would match every name.
    const auto readBack = [&]() {
        conversions_.clear();
        for (int r = 0; r < conversions->rowCount(); ++r) {
            const QString pattern = conversions->item(r, 0) != nullptr
                                        ? conversions->item(r, 0)->text().trimmed()
                                        : QString();
            const QString becomes = conversions->item(r, 1) != nullptr
                                        ? conversions->item(r, 1)->text().trimmed()
                                        : QString();
            if (pattern.isEmpty() || becomes.isEmpty()) continue;
            conversions_.push_back({pattern.toStdString(), becomes.toStdString()});
        }
        categorisations_.clear();
        QStringList unknown;
        for (int r = 0; r < categories->rowCount(); ++r) {
            const QString payee = categories->item(r, 0) != nullptr
                                      ? categories->item(r, 0)->text().trimmed()
                                      : QString();
            const QString into = categories->item(r, 1) != nullptr
                                     ? categories->item(r, 1)->text().trimmed()
                                     : QString();
            if (payee.isEmpty() || into.isEmpty()) continue;
            // Kept either way: a category typed before it exists is a row the
            // user will want to find again, and a silent drop would lose it.
            if (chart_.ref_for_path(into.toStdString()).is_none() && !unknown.contains(into))
                unknown.append(into);
            categorisations_.push_back({payee.toStdString(), into.toStdString()});
        }
        if (!unknown.isEmpty())
            QMessageBox::warning(&box, "SimpleAccount",
                                 QString("There is no category %1. The row is kept, "
                                         "and nothing is posted there until the "
                                         "category exists.")
                                     .arg(unknown.join(", ")));
        store::Failure no = store_.write_conversions(conversions_);
        if (no.refused) {
            QMessageBox::warning(&box, "SimpleAccount", QString::fromStdString(no.reason));
            return false;
        }
        no = store_.write_categorisations(categorisations_);
        if (no.refused) {
            QMessageBox::warning(&box, "SimpleAccount", QString::fromStdString(no.reason));
            return false;
        }
        return true;
    };

    QObject::connect(conversionPage.second, &QPushButton::clicked, &box, [&]() {
        if (!readBack()) return;
        convertPayeesInTheBook();
    });
    QObject::connect(categoryPage.second, &QPushButton::clicked, &box, [&]() {
        if (!readBack()) return;
        assignCategoriesInTheBook();
    });
    QObject::connect(close, &QPushButton::clicked, &box, &QDialog::accept);

    box.exec();
    readBack();
}

// The converter table over transactions already entered. Counted and reported,
// because a book of twenty years is too many to check by eye, and asked first,
// because it changes a name on every row it matches.
void MainWindow::convertPayeesInTheBook() {
    if (conversions_.empty()) {
        QMessageBox::information(this, "SimpleAccount",
                                 "There are no conversions to apply.");
        return;
    }
    const QMessageBox::StandardButton said = QMessageBox::question(
        this, "Convert the payees?",
        QString("Rename the payees of every transaction these %1 rows match?\n\n"
                "The name the file wrote is kept, and a reconciled transaction "
                "is left alone.")
            .arg(static_cast<int>(conversions_.size())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (said != QMessageBox::Yes) return;

    const int changed = payees::convert_payees(conversions_, &transactions_);
    if (changed > 0) {
        save();
        rebuildWorkspace();
        refresh();
    }
    QMessageBox::information(
        this, "SimpleAccount",
        QString("%1 transaction%2 renamed.").arg(changed).arg(changed == 1 ? "" : "s"));
}

void MainWindow::assignCategoriesInTheBook() {
    if (categorisations_.empty()) {
        QMessageBox::information(this, "SimpleAccount",
                                 "There are no categorisations to apply.");
        return;
    }
    const QMessageBox::StandardButton said = QMessageBox::question(
        this, "Assign the categories?",
        QString("Post every uncategorised transaction these %1 rows match?\n\n"
                "A category chosen by hand is a decision and is left alone, as "
                "are splits, transfers and reconciled transactions.")
            .arg(static_cast<int>(categorisations_.size())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (said != QMessageBox::Yes) return;

    const payees::BookApplication done =
        payees::assign_categories(categorisations_, chart_, &transactions_);
    if (done.matched > 0) {
        save();
        rebuildWorkspace();
        refresh();
    }
    QMessageBox::information(
        this, "SimpleAccount",
        QString("%1 categorised, %2 already in the right place, %3 left alone.")
            .arg(done.matched)
            .arg(done.already_correct)
            .arg(done.skipped));
}

// What the book holds about itself. Each of these is a statement about the book
// rather than about a register in it, which is why they are in one dialog here
// and not in the account settings. See the settings rules in Books.spectable.
void MainWindow::editBookSettings() {
    if (!store_.is_open()) {
        QMessageBox::information(this, "SimpleAccount",
                                 "Open a book before working on its settings.");
        return;
    }
    const store::BookInfo was = store_.info();

    QDialog dialog(this);
    dialog.setWindowTitle("Book settings");
    auto* form = new QFormLayout(scrollingBody(&dialog));
    form->addRow("Book", new QLabel(QString::fromStdString(was.name), &dialog));

    auto* fiscal = new QComboBox(&dialog);
    for (int month = 1; month <= 12; ++month)
        fiscal->addItem(QDate(2000, month, 1).toString("MMMM"), month);
    fiscal->setCurrentIndex(
        was.fiscal_year_start >= 1 && was.fiscal_year_start <= 12
            ? was.fiscal_year_start - 1
            : 0);

    auto* window = new QSpinBox(&dialog);
    window->setRange(0, 365);
    window->setValue(was.check_window_days);
    window->setSuffix(" days");

    auto* clears = new QCheckBox("A CSV import arrives cleared", &dialog);
    clears->setChecked(was.set_cleared_on_csv_import);

    auto* signs = new QCheckBox("Show what is owed as a negative number", &dialog);
    signs->setChecked(was.quicken_signs);
    signs->setToolTip("The Quicken reading of a card or a loan. Display only -- "
                      "nothing stored and nothing worked out changes, and "
                      "turning it off again restores the first reading exactly.");

    auto* hides = new QCheckBox("Hide everything before", &dialog);
    auto* before = new QDateEdit(&dialog);
    before->setCalendarPopup(true);
    before->setDisplayFormat("yyyy-MM-dd");
    const QDate said =
        QDate::fromString(QString::fromStdString(was.hide_before), "yyyy-MM-dd");
    hides->setChecked(said.isValid());
    before->setDate(said.isValid() ? said : QDate(QDate::currentDate().year(), 1, 1));
    before->setEnabled(said.isValid());
    QObject::connect(hides, &QCheckBox::toggled, before, &QDateEdit::setEnabled);

    form->addRow("Fiscal year starts", fiscal);
    form->addRow("A check number is trusted for", window);
    form->addRow(clears);
    form->addRow(signs);
    form->addRow(hides, before);

    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    fitToScreen(&dialog);
    if (dialog.exec() != QDialog::Accepted) return;

    store::BookInfo now = was;
    now.fiscal_year_start = fiscal->currentData().toInt();
    now.check_window_days = window->value();
    now.set_cleared_on_csv_import = clears->isChecked();
    now.quicken_signs = signs->isChecked();
    now.hide_before = hides->isChecked()
                          ? before->date().toString("yyyy-MM-dd").toStdString()
                          : std::string();
    const store::Failure no = store_.set_info(now);
    if (no.refused) {
        QMessageBox::warning(this, "SimpleAccount", QString::fromStdString(no.reason));
        return;
    }
    // The hide-before date changes what every register shows, so the registers
    // are built again rather than left showing years the book now hides.
    hideBefore_ = now.hide_before;
    quickenSigns_ = now.quicken_signs;
    rebuildWorkspace();
    refresh();
}

// The tag vocabulary and the use of it. A word is in the list because it is in
// the book's table or because a transaction carries it, and a word nothing
// carries is still a row: the list is the author's vocabulary, not a count of
// this year's work. See the tag table rule in Books.spectable.
void MainWindow::showTagListing() {
    if (!store_.is_open()) {
        QMessageBox::information(this, "SimpleAccount",
                                 "Open a book before working on its tags.");
        return;
    }

    QDialog box(this);
    box.setWindowTitle("Tags");
    box.resize(520, 480);
    auto* layout = new QVBoxLayout(&box);

    auto* table = new QTableWidget;
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({"Tag", "Transactions"});
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->setFont(appFont_);
    layout->addWidget(table, 1);

    std::vector<tags::Row> shown;
    const auto fill = [&](const QString& select) {
        std::vector<std::string> fields;
        fields.reserve(transactions_.size());
        for (const ledger::Transaction& t : transactions_) fields.push_back(t.tag);
        shown = tags::listing(tagVocabulary_, fields);
        table->setRowCount(static_cast<int>(shown.size()));
        for (std::size_t i = 0; i < shown.size(); ++i) {
            const int at = static_cast<int>(i);
            table->setItem(at, 0, text(shown[i].name));
            table->setItem(at, 1, text(std::to_string(shown[i].used)));
            if (QString::fromStdString(shown[i].name) == select) table->selectRow(at);
        }
    };
    fill(QString());

    auto* row = new QHBoxLayout;
    auto* add = new QPushButton("&Add...");
    auto* rename = new QPushButton("&Rename...");
    auto* close = new QPushButton("&Close");
    close->setDefault(true);
    row->addWidget(add);
    row->addWidget(rename);
    row->addStretch(1);
    row->addWidget(close);
    layout->addLayout(row);

    QObject::connect(add, &QPushButton::clicked, &box, [&]() {
        bool said = false;
        const QString word =
            QInputDialog::getText(&box, "Add a tag", "Tag", QLineEdit::Normal, QString(),
                                  &said)
                .trimmed();
        if (!said || word.isEmpty()) return;
        tagVocabulary_ = tags::accepted(tagVocabulary_, word.toStdString());
        store_.write_tags(tagVocabulary_);
        fill(word);
    });

    QObject::connect(rename, &QPushButton::clicked, &box, [&]() {
        const int at = table->currentRow();
        if (at < 0 || static_cast<std::size_t>(at) >= shown.size()) return;
        const std::string was = shown[static_cast<std::size_t>(at)].name;
        bool said = false;
        const QString now =
            QInputDialog::getText(&box, "Rename a tag", "Tag", QLineEdit::Normal,
                                  QString::fromStdString(was), &said)
                .trimmed();
        if (!said || now.isEmpty() || now.toStdString() == was) return;
        // A rename of a tag is a rename of every use of it, which is the whole
        // reason a transaction keeps the name and not an id.
        for (ledger::Transaction& t : transactions_)
            t.tag = tags::renamed_field(t.tag, was, now.toStdString());
        tagVocabulary_ = tags::renamed(tagVocabulary_, was, now.toStdString());
        store_.write_tags(tagVocabulary_);
        save();
        rebuildWorkspace();
        refresh();
        fill(now);
    });

    QObject::connect(close, &QPushButton::clicked, &box, &QDialog::accept);
    box.exec();
}

// The lines selected in a register, counting from one, in the order shown.
void MainWindow::registerMenu(int tabPosition, const QPoint& at) {
    TabWidgets& p = tabWidgets_[static_cast<std::size_t>(tabPosition - 1)];
    if (p.isReport || p.reg == nullptr) return;

    // The blank line at the foot is not a transaction and cannot be acted on.
    const int lines = p.reg->rowCount() - 1;
    std::vector<int> chosen;
    for (const QModelIndex& index : p.reg->selectionModel()->selectedRows())
        if (index.row() < lines) chosen.push_back(index.row() + 1);
    std::sort(chosen.begin(), chosen.end());
    if (chosen.empty()) return;

    // Only the active tab's register can have received this click, so the
    // selection is always of pane 1 -- the tab area -- from here on.
    workspace_->select_lines(chosen);

    QMenu menu(this);
    for (const ui::MenuEntry& entry : workspace_->menu_items()) {
        QAction* action = menu.addAction(QString::fromStdString(entry.item));
        action->setEnabled(entry.enabled);
        if (entry.item.rfind("Recategorize", 0) == 0)
            connect(action, &QAction::triggered, this,
                    [this]() { recategorise(1); });
        else if (entry.item.rfind("Match this transaction", 0) == 0)
            connect(action, &QAction::triggered, this,
                    [this]() { matchTransaction(1); });
        else if (entry.item.rfind("Delete", 0) == 0)
            connect(action, &QAction::triggered, this,
                    [this]() { deleteSelection(1); });
        else if (entry.item.rfind("Apply payee rules", 0) == 0)
            connect(action, &QAction::triggered, this,
                    [this]() { applyPayeeRulesToSelection(1); });
        else if (entry.item.rfind("Add payee rule", 0) == 0)
            connect(action, &QAction::triggered, this,
                    [this]() { addPayeeRuleFrom(1); });
        else if (entry.item.rfind("Split", 0) == 0)
            connect(action, &QAction::triggered, this,
                    [this]() { splitSelection(1); });
        // Nothing else is connected. The menu used to send everything it did
        // not recognise to the rule dialog, which was right while there were
        // three items and wrong the moment there was a fourth: an item nobody
        // has wired up should do nothing rather than something else.
    }
    menu.exec(p.reg->viewport()->mapToGlobal(at));
}

// The menu on a line of the import review. One item, which reads as the
// disposition does and is grey on a line that claims nothing. See the
// show-the-duplicate section of UserInterface.spectable.
void MainWindow::reviewMenu(const QPoint& at) {
    if (!workspace_ || !workspace_->review_showing()) return;
    const QModelIndex index = reviewTable_->indexAt(at);
    if (!index.isValid()) return;
    workspace_->select_review_line(index.row() + 1);

    QMenu menu(this);
    for (const ui::MenuEntry& entry : workspace_->review_menu_items()) {
        QAction* action = menu.addAction(QString::fromStdString(entry.item));
        action->setEnabled(entry.enabled);
        connect(action, &QAction::triggered, this, [this]() { showDuplicate(); });
    }
    menu.exec(reviewTable_->viewport()->mapToGlobal(at));
}

// The menu on a find result. Going to the transaction is what clicking the row
// already does; Show split is the item that earns the menu, a result row
// reading --Split-- and nothing more. It is left off a result that is not a
// split rather than offered and refused -- see the find-results menu section
// of UserInterface.spectable.
void MainWindow::findResultMenu(const QPoint& at) {
    if (!workspace_) return;
    const QModelIndex index = findTable_->indexAt(at);
    if (!index.isValid()) return;
    const int line = index.row() + 1;

    QMenu menu(this);
    for (const ui::MenuEntry& entry : workspace_->find_result_menu(line)) {
        QAction* action = menu.addAction(QString::fromStdString(entry.item));
        action->setEnabled(entry.enabled);
        if (entry.item.rfind("Show split", 0) == 0)
            connect(action, &QAction::triggered, this,
                    [this, line]() { showFindResultSplit(line); });
        else
            connect(action, &QAction::triggered, this, [this, line]() {
                if (!workspace_->go_to_find_result(line)) return;
                refresh();
            });
    }
    menu.exec(findTable_->viewport()->mapToGlobal(at));
}

// The split as it was typed, which is where the amount a find was given
// actually is. A look only: the register is where a split is changed.
void MainWindow::showFindResultSplit(int line) {
    if (!workspace_) return;
    const std::vector<reg::SplitLine> lines = workspace_->find_result_split(line);
    if (lines.empty()) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Split"));
    auto* layout = new QVBoxLayout(&dialog);

    auto* table = new QTableWidget(static_cast<int>(lines.size()), 3);
    table->setHorizontalHeaderLabels({tr("Category"), tr("Amount"), tr("Memo")});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setFont(appFont_);
    for (int r = 0; r < static_cast<int>(lines.size()); ++r) {
        const reg::SplitLine& row = lines[static_cast<std::size_t>(r)];
        table->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(row.category)));
        setAmount(table, r, 1, row.amount);
        table->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(row.memo)));
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(table);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* close = new QPushButton(tr("Close"));
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    dialog.resize(520, 260);
    fitToScreen(&dialog);
    dialog.exec();
}

// Splitting a transaction that is already in the register. The window is the
// one the entry side has in words -- category, amount, memo, and what is left
// to allocate -- on a transaction whose amount is already settled, so the
// figure to reach is the one it already has. See "Splitting a transaction that
// is already in the register" in UserInterface.spectable.
void MainWindow::splitSelection(int tabPosition) {
    (void)tabPosition;
    if (!workspace_) return;
    const std::vector<int> chosen = workspace_->selected_lines();
    if (chosen.size() != 1) return;
    const std::vector<reg::Line> lines = workspace_->register_lines(1);
    if (chosen.front() < 1 || chosen.front() > static_cast<int>(lines.size())) return;
    const reg::Line& line = lines[static_cast<std::size_t>(chosen.front() - 1)];
    // What the lines have to come to: the other side of the transaction, which
    // is the payment less the deposit. A payment of 184.50 is split into
    // positive lines and a deposit into negative ones, which is the rule the
    // entry side and the book itself use.
    const Money needed = line.payment - line.deposit;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Split"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        QString::fromStdString(line.date.iso() + "   " + line.payee + "   " +
                               needed.in_register()),
        &dialog));

    auto* table = new QTableWidget(4, 3, &dialog);
    table->setHorizontalHeaderLabels({tr("Category"), tr("Amount"), tr("Memo")});
    table->verticalHeader()->setVisible(false);
    table->setFont(appFont_);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(table, 1);

    auto* remaining = new QLabel(&dialog);
    layout->addWidget(remaining);

    // What the typed cells say, read back out of the table. Read again
    // whenever a cell changes, so the figure a person is working towards is
    // always the one on screen.
    const auto typedLines = [table]() {
        std::vector<reg::SplitLine> out;
        for (int r = 0; r < table->rowCount(); ++r) {
            const QTableWidgetItem* category = table->item(r, 0);
            const QTableWidgetItem* amount = table->item(r, 1);
            const QTableWidgetItem* memo = table->item(r, 2);
            const std::string path =
                category == nullptr ? std::string()
                                    : category->text().trimmed().toStdString();
            const std::string said =
                amount == nullptr ? std::string() : amount->text().trimmed().toStdString();
            if (path.empty() && said.empty()) continue;
            reg::SplitLine row;
            row.category = path;
            row.amount = moneyTyped(amount == nullptr ? QString() : amount->text());
            row.memo = memo == nullptr ? std::string() : memo->text().trimmed().toStdString();
            out.push_back(row);
        }
        return out;
    };
    const auto say = [&]() {
        Money allocated;
        for (const reg::SplitLine& row : typedLines()) allocated += row.amount;
        const Money left = needed - allocated;
        remaining->setText(tr("Remaining to allocate %1")
                               .arg(QString::fromStdString(left.in_register())));
    };
    say();
    connect(table, &QTableWidget::itemChanged, &dialog,
            [&say](QTableWidgetItem*) { say(); });

    auto* buttons = new QHBoxLayout;
    // The picker is the one Recategorize uses, so a category is found the same
    // way wherever it is being chosen.
    auto* pick = new QPushButton(tr("Category..."), &dialog);
    connect(pick, &QPushButton::clicked, &dialog, [&]() {
        const int row = table->currentRow() < 0 ? 0 : table->currentRow();
        const QTableWidgetItem* at = table->item(row, 0);
        CategoryPick choose(&dialog, chart_, book_,
                            at == nullptr ? std::string() : at->text().toStdString());
        if (choose.exec() != QDialog::Accepted) return;
        const std::string into = choose.chosen();
        if (into.empty()) return;
        table->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(into)));
    });
    buttons->addWidget(pick);
    auto* add = new QPushButton(tr("Add line"), &dialog);
    connect(add, &QPushButton::clicked, &dialog,
            [table]() { table->setRowCount(table->rowCount() + 1); });
    buttons->addWidget(add);
    buttons->addStretch();
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                     &dialog);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    buttons->addWidget(box);
    layout->addLayout(buttons);

    dialog.resize(620, 320);
    fitToScreen(&dialog);
    if (dialog.exec() != QDialog::Accepted) return;

    const ui::Workspace::SplitOutcome written = workspace_->split_selected(typedLines());
    if (!written.saved) {
        if (!written.reason.empty())
            QMessageBox::warning(this, tr("Split"),
                                 QString::fromStdString(written.reason));
        return;
    }
    chart_ = workspace_->accounts();
    transactions_ = workspace_->transactions();
    splits_ = workspace_->splits();
    book_ = workspace_->book();
    save();
    rebuildWorkspace();
    refresh();
}

// Reconciling an account against what the bank last said. The two figures and
// the difference are the register's own; what the window adds is the list of
// what is not cleared, with the option to tick it, and -- for a difference
// nobody can tick away -- the offer of a Balance Adjustment. See the
// reconciling section of UserInterface.spectable and decision 29.
void MainWindow::reconcileAccount(const QString& account) {
    if (!workspace_) return;
    const std::string path = account.toStdString();
    workspace_->open_reconcile(path);
    if (!workspace_->reconcile_state().shown) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Reconcile %1").arg(account));
    auto* layout = new QVBoxLayout(&dialog);
    auto* figures = new QLabel(&dialog);
    layout->addWidget(figures);

    auto* table = new QTableWidget(0, 3, &dialog);
    table->setHorizontalHeaderLabels({tr("Date"), tr("Payee"), tr("Amount")});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setFont(appFont_);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(table, 1);
    layout->addWidget(new QLabel(
        tr("Tick what the statement shows and press Clear ticked. Tag all ticks "
           "every line, for a statement most of which is on it."), &dialog));

    // The figures and the list are read from the workspace every time, because
    // a tick changes a transaction and the difference closing as lines are
    // ticked is the whole of the work.
    std::vector<recon::Item> offered;
    const auto show = [&]() {
        const recon::Figures f = workspace_->reconcile_state();
        std::string said = "Balance " + f.balance.in_register() + "    Cleared " +
                           f.cleared.in_register();
        if (f.online_at.has_value())
            said += "    On-line " + f.online.in_register() + " on " +
                    f.online_at->iso() + "    Difference " + f.difference.in_register();
        else
            said += "    No on-line balance has been downloaded for this account";
        if (f.agrees) said += "    --  agrees";
        figures->setText(QString::fromStdString(said));

        offered = workspace_->uncleared_offered();
        const QSignalBlocker quiet(table);
        table->setRowCount(static_cast<int>(offered.size()));
        for (int r = 0; r < static_cast<int>(offered.size()); ++r) {
            const recon::Item& item = offered[static_cast<std::size_t>(r)];
            auto* when = new QTableWidgetItem(QString::fromStdString(item.date.iso()));
            when->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            when->setCheckState(Qt::Unchecked);
            table->setItem(r, 0, when);
            table->setItem(r, 1,
                           new QTableWidgetItem(QString::fromStdString(item.payee)));
            setAmount(table, r, 2, item.amount);
        }
        table->resizeColumnToContents(0);
    };
    show();

    auto* buttons = new QHBoxLayout;
    // Tag all ticks and writes nothing: a statement most of whose lines are on
    // it is ticked in one press and the two that are not there are unticked,
    // rather than forty ticked one at a time. What it ticks is what the window
    // offers, which is what the workspace answers. See "Tagging every line
    // ticks all that is offered and nothing else" in UserInterface.spectable.
    auto* tagAll = new QPushButton(tr("Tag all"), &dialog);
    connect(tagAll, &QPushButton::clicked, &dialog, [&]() {
        const std::vector<std::string> tagged = workspace_->tag_all_in_reconcile();
        const QSignalBlocker quiet(table);
        for (int r = 0; r < table->rowCount(); ++r) {
            QTableWidgetItem* at = table->item(r, 0);
            if (at == nullptr) continue;
            if (static_cast<std::size_t>(r) >= offered.size()) continue;
            const std::string& ref = offered[static_cast<std::size_t>(r)].ref;
            if (std::find(tagged.begin(), tagged.end(), ref) == tagged.end()) continue;
            at->setCheckState(Qt::Checked);
        }
    });
    buttons->addWidget(tagAll);
    auto* clear = new QPushButton(tr("Clear ticked"), &dialog);
    connect(clear, &QPushButton::clicked, &dialog, [&]() {
        std::vector<std::string> refs;
        for (int r = 0; r < table->rowCount(); ++r) {
            const QTableWidgetItem* at = table->item(r, 0);
            if (at == nullptr || at->checkState() != Qt::Checked) continue;
            if (static_cast<std::size_t>(r) < offered.size())
                refs.push_back(offered[static_cast<std::size_t>(r)].ref);
        }
        if (refs.empty()) return;
        workspace_->clear_in_reconcile(refs);
        show();
    });
    buttons->addWidget(clear);
    buttons->addStretch();
    auto* finished = new QPushButton(tr("Done"), &dialog);
    connect(finished, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttons->addWidget(finished);
    layout->addLayout(buttons);

    dialog.resize(680, 400);
    fitToScreen(&dialog);
    dialog.exec();

    // What is left when there is nothing more to tick. Offered rather than
    // written: it is a figure nobody could explain, and whether to record it
    // is the person's decision.
    const recon::Adjustment offer = workspace_->balance_adjustment_offered();
    if (offer.shown) {
        const QString question =
            tr("The book and the bank differ by %1.\n\n"
               "Add a Balance Adjustment of %2 on %3, against %4?\n\n"
               "It is written uncleared, so the register goes on showing the "
               "difference until somebody clears it.")
                .arg(QString::fromStdString(
                    workspace_->reconcile_state().difference.in_register()))
                .arg(QString::fromStdString(offer.amount.in_register()))
                .arg(QString::fromStdString(offer.date.iso()))
                .arg(QString::fromStdString(offer.category));
        if (QMessageBox::question(this, tr("Balance Adjustment"), question,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) == QMessageBox::Yes)
            workspace_->accept_balance_adjustment();
    }

    chart_ = workspace_->accounts();
    transactions_ = workspace_->transactions();
    splits_ = workspace_->splits();
    book_ = workspace_->book();
    workspace_->close_reconcile();
    save();
    rebuildWorkspace();
    refresh();
}

// What the import decided the line already was, read off the book. A look, not
// a decision: the tick in the review is where the decision is made, so this
// changes nothing and only has to be closed.
void MainWindow::showDuplicate() {
    if (!workspace_) return;
    const ui::DuplicateLook look = workspace_->show_duplicate();
    if (!look.shown) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Transaction already in the book"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QString::fromStdString(look.heading)));

    auto* table = new QTableWidget(static_cast<int>(look.rows.size()), 7);
    table->setHorizontalHeaderLabels({tr("Date"), tr("Payee"), tr("Account"),
                                      tr("Category"), tr("Amount"), tr("Check"),
                                      tr("Cleared")});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setFont(appFont_);
    for (int r = 0; r < static_cast<int>(look.rows.size()); ++r) {
        const ui::DuplicateRow& row = look.rows[static_cast<std::size_t>(r)];
        table->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(row.date.iso())));
        table->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(row.payee)));
        table->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(row.account)));
        table->setItem(r, 3, new QTableWidgetItem(QString::fromStdString(row.category)));
        setAmount(table, r, 4, row.amount);
        table->setItem(r, 5, new QTableWidgetItem(QString::fromStdString(row.check_no)));
        table->setItem(r, 6, new QTableWidgetItem(
                                 QString::fromStdString(types::to_string(row.cleared))));
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(table);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(640, 240);
    fitToScreen(&dialog);
    dialog.exec();
    workspace_->close_duplicate_look();
}

void MainWindow::recategorise(int paneOneBased) {
    const std::vector<int>& chosen = workspace_->selected_lines();
    if (chosen.empty()) return;
    // Opened on the category the first selected line already has, which is
    // what makes correcting one a matter of a few keystrokes.
    const std::vector<reg::Line> lines = workspace_->register_lines(1);
    (void)paneOneBased;
    std::string start;
    if (chosen.front() >= 1 && chosen.front() <= static_cast<int>(lines.size())) {
        const reg::Line& line = lines[static_cast<std::size_t>(chosen.front() - 1)];
        // category_detail is the literal account, which is what the picker
        // needs; category is only a display string for a transfer (shown in
        // brackets) and reads "--Split--" for a split, neither of which is a
        // path the picker could pre-select.
        start = line.category_detail;
        if (start.empty() || start.find(static_cast<char>(10)) != std::string::npos)
            start.clear();
    }

    CategoryPick pick(this, chart_, book_, start);
    if (pick.exec() != QDialog::Accepted) return;
    const std::string into = pick.chosen();
    if (into.empty()) return;

    const ui::Recategorised done = workspace_->recategorise_selection(into);
    int changed = done.changed;
    int refused = done.refused;
    std::string reason = done.reason;

    // A transfer caught up in the selection is asked about before it is
    // touched, because a category there does not change a line -- it takes the
    // other account line away. Nothing of the transfers has been done yet; the
    // answer is what does it or leaves it. One question covers all of them,
    // because the selection was one gesture. See the transfer-warning rule in
    // UserInterface.spectable.
    if (done.warned > 0) {
        QString ask = QString::fromStdString(done.warning);
        if (done.warned > 1)
            ask += QString("\n\nand %1 more like it.").arg(done.warned - 1);
        ask += "\n\nGo ahead?";
        const bool accept =
            QMessageBox::question(this, "Recategorize", ask,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) == QMessageBox::Yes;
        const ui::Recategorised answered = workspace_->answer_transfer_warning(accept);
        changed += answered.changed;
        refused += answered.refused;
        if (reason.empty()) reason = answered.reason;
    }

    if (changed > 0) {
        transactions_ = workspace_->transactions();
        book_ = workspace_->book();
        save();
        rebuildWorkspace();
        refresh();
    }
    // Said out loud, because a selection of twenty is too many to check by eye.
    QString said = QString("%1 changed").arg(changed);
    if (refused > 0) {
        said += QString(", %1 left alone").arg(refused);
        if (!reason.empty()) said += "\n\n" + QString::fromStdString(reason);
    }
    if (refused > 0) QMessageBox::information(this, "Recategorize", said);
    else status_->setText(said + "  --  " + QString::fromStdString(into));
}

// The other half of the selected line, sought from the register menu rather
// than offered by the destination column. What it asks of a candidate is less
// than an import asks: equal and opposite to the cent and not already paired,
// but no window and no account, because whoever reaches for this does not know
// which account the other half is in. Closest in days is offered first, and
// choosing one is the merge the destination column does. See the menu-match
// rule in UserInterface.spectable.
void MainWindow::matchTransaction(int paneOneBased) {
    (void)paneOneBased;
    const std::vector<transfers::Half> halves = workspace_->match_candidates();
    // The menu item is grey when there is nothing to offer, so this is only
    // reached with something in hand. A dialog with nothing in it would be the
    // worse answer to the same question.
    if (halves.empty()) return;

    QStringList offered;
    for (const transfers::Half& half : halves)
        offered << QString("%1   %2   %3   (%4 day%5 apart)")
                       .arg(QString::fromStdString(half.account))
                       .arg(QString::fromStdString(half.date.iso()))
                       .arg(QString::fromStdString(
                           half.payee.empty() ? std::string("no payee") : half.payee))
                       .arg(half.days_apart)
                       .arg(half.days_apart == 1 ? "" : "s");
    const QString none = "None of them";
    offered << none;

    bool said = false;
    const QString chose = QInputDialog::getItem(
        this, "Match this transaction",
        QString("These lines elsewhere in the book are equal and opposite to\n"
                "this one. Is one of them the same payment?"),
        offered, 0, false, &said);
    // None changes nothing at all. Nothing was typed, so there is nothing to
    // leave behind: the line names no account before the menu was opened and
    // names none after.
    if (!said || chose == none) return;
    const int at = offered.indexOf(chose);
    if (at < 0 || static_cast<std::size_t>(at) >= halves.size()) return;
    const transfers::Half& half = halves[static_cast<std::size_t>(at)];
    const QString where = QString::fromStdString(half.account);
    const QString when = QString::fromStdString(half.date.iso());
    if (!workspace_->choose_match(half.transaction_id)) return;

    transactions_ = workspace_->transactions();
    book_ = workspace_->book();
    save();
    rebuildWorkspace();
    refresh();
    status_->setText(QString("Matched with the line of %1 in %2").arg(when).arg(where));
}

void MainWindow::deleteSelection(int paneOneBased) {
    (void)paneOneBased;
    const int count = static_cast<int>(workspace_->selected_lines().size());
    if (count == 0) return;
    const QString question = count == 1
        ? QString("Delete this transaction? This cannot be undone.")
        : QString("Delete these %1 transactions? This cannot be undone.").arg(count);
    if (QMessageBox::question(this, "Delete", question,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;
    const int removed = workspace_->delete_selected();
    if (removed == 0) return;
    transactions_ = workspace_->transactions();
    book_ = workspace_->book();
    save();
    rebuildWorkspace();
    refresh();
    status_->setText(QString("%1 transaction%2 deleted")
                         .arg(removed).arg(removed == 1 ? "" : "s"));
}

void MainWindow::addPayeeRuleFrom(int paneOneBased) {
    (void)paneOneBased;
    if (workspace_->selected_lines().empty()) return;
    const payees::Rule offered = workspace_->rule_offered();
    if (offered.pattern.empty()) {
        QMessageBox::information(this, "SimpleAccount",
                                 "There is no name on that line to make a rule from.");
        return;
    }
    RuleForm form(this, "Add payee rule", offered, [this](const std::string& start) {
            CategoryPick choose(this, chart_, book_, start);
            if (choose.exec() != QDialog::Accepted) return std::string();
            return choose.chosen();
        });
    if (form.exec() != QDialog::Accepted) return;
    const payees::Rule wanted = form.rule();
    const payees::Refusal no = payees::add(&rules_, wanted);
    if (no.refused) {
        QMessageBox::warning(this, "SimpleAccount", QString::fromStdString(no.reason));
        return;
    }
    save();
    status_->setText(QString("Rule added for %1")
                         .arg(QString::fromStdString(wanted.pattern)));
}

// The rules a person has already written, run over the lines in front of
// them. It is the same pass the import makes, so a month categorised by hand
// and a month downloaded come out looking the same -- and a rule written a
// minute ago is usually meant for the twenty lines it was written from.
void MainWindow::applyPayeeRulesToSelection(int paneOneBased) {
    (void)paneOneBased;
    if (workspace_->selected_lines().empty()) return;
    if (rules_.empty()) {
        QMessageBox::information(this, "SimpleAccount",
                                 "There are no payee rules yet. Add payee rule, on a "
                                 "line in the register, writes the first one.");
        return;
    }
    const payees::BookApplication done =
        workspace_->apply_payee_rules_to_selection(rules_);
    if (done.matched > 0) {
        transactions_ = workspace_->transactions();
        book_ = workspace_->book();
        save();
        rebuildWorkspace();
    }
    refresh();
    // Said in full, because the interesting number is usually not the first
    // one: a pass that changed nothing is either a selection the rules do not
    // cover or a selection that was already right, and those are different
    // answers to be given the same way.
    QString said = QString("%1 changed").arg(done.matched);
    if (done.already_correct > 0)
        said += QString(", %1 already right").arg(done.already_correct);
    if (done.skipped > 0) said += QString(", %1 left alone").arg(done.skipped);
    status_->setText(said);
}

void MainWindow::toggleHidden() {
    showHidden_ = !showHidden_;
    refreshAccounts();
}

void MainWindow::accountClicked() {
    // Re-selecting the account during a redraw fires this signal exactly as a
    // click does. Without this guard the redraw selects, which redraws, which
    // selects, and the window stops responding.
    if (refreshing_) return;
    const std::string account = selectedAccount();
    if (account.empty()) return;        // a group heading is not an account
    workspace_->select(account);
    refresh();
}

void MainWindow::acceptImport() {
    workspace_->accept_import();
    // The accepted rows are now in the workspace, so the window's own copies
    // follow it rather than the other way round.
    transactions_ = workspace_->transactions();
    book_ = workspace_->book();
    // The chart with them: accepting is when the balance the dialog was
    // answered with goes onto the account, and a chart left behind here is a
    // balance that is never written and never shown.
    chart_ = workspace_->accounts();
    // A book is a file rather than a session: an import that is not written is
    // an import that is lost when the window closes.
    save();
    refresh();
}

void MainWindow::cancelImport() {
    workspace_->cancel_import();
    refresh();
}

// A tab's position can shift as other tabs close; nothing here is handed one
// to remember, so every handler looks it up fresh from the widget Qt just
// told it about.
MainWindow::TabWidgets MainWindow::makeRegisterTab(int tabPosition) {
    (void)tabPosition;
    TabWidgets t;
    t.blank = reg::blank_line_on(asDate(QDate::currentDate()));

    t.reg = new QTableWidget;
    // A register is the table and the strip of controls above it. The strip
    // says which stretch of time is shown and what the register wants to say
    // about itself -- the balance brought forward, or why it is empty. Each
    // range is worked out from today when it is applied rather than stored,
    // so a book left open overnight into January shows the new year the next
    // time one is chosen. See the named-ranges section of
    // TransactionRegister.spectable.
    auto* container = new QWidget;
    auto* stack = new QVBoxLayout(container);
    stack->setContentsMargins(0, 0, 0, 0);
    auto* bar = new QHBoxLayout;
    bar->addWidget(new QLabel("Show", container));
    t.range = new QComboBox(container);
    t.range->addItems({"All dates", "This year", "Last year", "Past two years",
                       "Since a date"});
    bar->addWidget(t.range);
    t.since = new QDateEdit(QDate(QDate::currentDate().year(), 1, 1), container);
    t.since->setCalendarPopup(true);
    t.since->setDisplayFormat("yyyy-MM-dd");
    t.since->setVisible(false);
    bar->addWidget(t.since);
    t.note = new QLabel(container);
    bar->addWidget(t.note, 1);
    // What the account stands at, beside what the bank last said it stands
    // at. The difference between the two is the whole of reconciling, and it
    // was previously only arrived at by adding up a column by hand. See the
    // balances section of TransactionRegister.spectable.
    t.balances = new QLabel(container);
    bar->addWidget(t.balances);
    stack->addLayout(bar);
    stack->addWidget(t.reg, 1);
    t.widget = container;
    t.reg->setColumnCount(10);
    t.reg->setHorizontalHeaderLabels({"Date", "Num", "Payee", "Category", "Memo", "Tag",
                                      "Payment", "Deposit", "Cleared", "Balance"});
    t.reg->verticalHeader()->setVisible(false);
    t.reg->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Dragging a divider is how a width is set. It goes to the workspace, is
    // written with the book, and is put onto every other register -- which is
    // what refreshing_ guards: applying it would be heard as another drag.
    QHeaderView* header = t.reg->horizontalHeader();
    connect(header, &QHeaderView::sectionResized, this,
            [this, header](int column, int, int width) {
                if (refreshing_ || !workspace_) return;
                // A stretched column resizes itself whenever the table is laid
                // out, and that is not somebody dragging a divider. Recording
                // it would put a width in the book nobody asked for, and one
                // that can never be applied either, since the stretch decides.
                if (header->sectionResizeMode(column) != QHeaderView::Interactive) return;
                const std::string name = registerColumnName(column);
                if (name.empty()) return;
                workspace_->resize_column(name, width);
                // The width is recorded and nothing else happens yet. Applying
                // it lays every other open register out again and writing it
                // writes the whole book, and a drag of fifty pixels reports
                // fifty widths -- which is why dragging a divider across a
                // book of thirty thousand transactions took seconds. So the
                // work waits until the drag has stopped. See the column-width
                // section of UserInterface.spectable.
                if (resizeSettle_ == nullptr) {
                    resizeSettle_ = new QTimer(this);
                    resizeSettle_->setSingleShot(true);
                    resizeSettle_->setInterval(400);
                    connect(resizeSettle_, &QTimer::timeout, this, [this]() {
                        if (!workspace_ || !workspace_->finish_resize()) return;
                        columnWidths_ = workspace_->column_widths();
                        applyColumnWidths();
                        saveColumnWidths();
                    });
                }
                resizeSettle_->start();
            });
    // Typing is allowed, and only the blank line's cells carry the editable
    // flag, so the rest of a register stays read-only: a stray keypress must
    // not change a transaction from 2009.
    t.reg->setEditTriggers(QAbstractItemView::DoubleClicked |
                           QAbstractItemView::EditKeyPressed |
                           QAbstractItemView::AnyKeyPressed);
    QTableWidget* reg = t.reg;
    t.reg->installEventFilter(new EnterRecords(t.reg, [this, container]() {
        recordBlankLine(tabs_->indexOf(container) + 1);
    }));
    // Shift and control already extend and add to the selection; this is what
    // offers to do something with it. See the register-menu section of
    // UserInterface.spectable.
    t.reg->setSelectionMode(QAbstractItemView::ExtendedSelection);
    t.reg->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(t.reg, &QTableWidget::customContextMenuRequested, this,
            [this, container](const QPoint& at) {
                registerMenu(tabs_->indexOf(container) + 1, at);
            });
    // The heading is a toggle: a click sorts by that column and a second click
    // on the same one reverses it. See the sorting section of
    // TransactionRegister.spectable.
    connect(t.reg->horizontalHeader(), &QHeaderView::sectionClicked, this,
            [this, container](int column) {
                sortRegisterBy(tabs_->indexOf(container) + 1, column);
            });
    connect(t.range, &QComboBox::currentTextChanged, this,
            [this, container](const QString&) {
                registerRangeChanged(tabs_->indexOf(container) + 1);
            });
    connect(t.since, &QDateEdit::dateChanged, this,
            [this, container](const QDate&) {
                registerRangeChanged(tabs_->indexOf(container) + 1);
            });
    t.reg->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    t.reg->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    // Thirty thousand lines are reached by the scroll bar more than by
    // anything else, and the click made most is the one in the empty part of
    // it.
    pagesTheView(t.reg);

    // A tab made after the font was chosen has to be brought into step with
    // it: the app font reaches the table, but not the row heights, which are
    // worked out from the font in force.
    const QFontMetrics metrics(appFont_);
    t.reg->verticalHeader()->setDefaultSectionSize(metrics.height() + 8);
    return t;
}

// A click on a heading sorts the register by that column, and a second click on
// the same heading reverses it: the heading is a toggle, which is what every
// list in every program does. Payment and Deposit are two views of one signed
// number, so both sort on Amount; Balance sorts by date, because a running
// balance only means anything in date order. See "What each column sorts on"
// in TransactionRegister.spectable.
void MainWindow::sortRegisterBy(int tabPosition, int column) {
    if (refreshing_) return;
    if (tabPosition < 1 ||
        static_cast<std::size_t>(tabPosition - 1) >= tabWidgets_.size())
        return;
    TabWidgets& p = tabWidgets_[static_cast<std::size_t>(tabPosition - 1)];
    if (p.isReport || p.reg == nullptr) return;
    std::string name = registerColumnName(column);
    if (name.empty()) return;
    if (name == "Payment" || name == "Deposit") name = "Amount";
    if (name == "Balance") name = "Date";
    if (p.sortColumn == name)
        p.sortDirection = p.sortDirection == "Ascending" ? "Descending" : "Ascending";
    else {
        p.sortColumn = name;
        p.sortDirection = reg::first_direction_for(name);
    }
    refresh();
}

// The stretch of time the register shows, chosen from the strip above the
// table. Since a date is the only range that needs a date typed, so the date
// field is only there for that one -- the same reason the find panel's second
// amount field is only there for a range.
void MainWindow::registerRangeChanged(int tabPosition) {
    if (refreshing_) return;
    if (tabPosition < 1 ||
        static_cast<std::size_t>(tabPosition - 1) >= tabWidgets_.size())
        return;
    TabWidgets& p = tabWidgets_[static_cast<std::size_t>(tabPosition - 1)];
    if (p.isReport || p.reg == nullptr || p.range == nullptr) return;
    if (p.since != nullptr)
        p.since->setVisible(p.range->currentText() == "Since a date");
    refresh();
}

void MainWindow::refresh() {
    const bool was = refreshing_;
    refreshing_ = true;
    syncTabs();
    refreshAccounts();
    // One index for every open register, rather than one per register: they
    // are all read from the same book, and the sort and the three lookups a
    // register needs are the same for all of them.
    const reg::BookIndex book(chart_, transactions_, splits_);
    // Only the tab in front. The others are marked and filled when they are
    // shown, which is what tabChanged is for: a register nobody is looking at
    // shows nothing to be wrong about.
    const int showing = tabs_->currentIndex() + 1;
    for (int position = 1; position <= tabs_->count(); ++position) {
        const std::size_t at = static_cast<std::size_t>(position - 1);
        if (at >= tabWidgets_.size()) continue;
        if (position == showing) {
            refreshPane(position, book);
            tabWidgets_[at].stale = false;
        } else {
            tabWidgets_[at].stale = true;
        }
    }
    refreshReviewPanel();
    refreshFindPanel();
    refreshing_ = was;
}

// Makes the tab bar match workspace_->open_tabs(): a tab that just opened
// gets a widget, one that closed loses it, and every tab's title and active
// state are brought into step. Tabs are never reordered or rebuilt in place --
// only added at the end and removed -- which is enough because the workspace
// itself only ever appends a tab or removes one by position, never reorders.
void MainWindow::syncTabs() {
    if (!workspace_) return;
    const std::vector<ui::TabRow> rows = workspace_->open_tabs();

    while (static_cast<int>(tabWidgets_.size()) > static_cast<int>(rows.size())) {
        const int last = static_cast<int>(tabWidgets_.size()) - 1;
        tabs_->removeTab(last);
        tabWidgets_[static_cast<std::size_t>(last)].widget->deleteLater();
        tabWidgets_.pop_back();
    }
    for (std::size_t i = tabWidgets_.size(); i < rows.size(); ++i) {
        const ui::TabRow& row = rows[i];
        // Which page a report tab gets is decided by its name, because the
        // name is the report: a second balance sheet is called Balance sheet 2
        // and is the same page, so the name is read up to what the menu
        // offered.
        TabWidgets t;
        if (row.kind != ui::TabKind::Report)
            t = makeRegisterTab(static_cast<int>(i) + 1);
        else if (reports::report_is_as_at(reportNameOf(row.name)))
            t = makeBalanceSheetTab(static_cast<int>(i) + 1, row.name);
        else
            t = makeReportTab(static_cast<int>(i) + 1, row.name);
        tabs_->addTab(t.widget, QString::fromStdString(row.name));
        tabWidgets_.push_back(t);
    }
    for (std::size_t i = 0; i < rows.size(); ++i)
        tabs_->setTabText(static_cast<int>(i), QString::fromStdString(rows[i].name));

    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].active) {
            const QSignalBlocker quiet(tabs_);
            tabs_->setCurrentIndex(static_cast<int>(i));
        }

    syncBottomTabs();
    // A tab made just now has the widths the program gives it, not the ones
    // somebody set. The width belongs to the column, so it belongs to a
    // register opened a minute from now as much as to the one it was set in.
    applyColumnWidths();
}

void MainWindow::refreshAccounts() {
    const bool was = refreshing_;
    refreshing_ = true;
    const QSignalBlocker quiet(accounts_);
    const std::string chosen = selectedAccount();
    accounts_->clear();
    QTreeWidgetItem* heading = nullptr;
    for (const ui::AccountRow& row : workspace_->account_rows(showHidden_)) {
        auto* item = new QTreeWidgetItem;
        if (row.is_heading) {
            // The heading as it reads, which is not the group's name: two of
            // them carry an ampersand. See the group rule in UserInterface.
            item->setText(0, QString::fromStdString(types::heading_of(
                types::account_group_from_string(row.group))));
            QFont bold = appFont_;
            bold.setBold(true);
            item->setFont(0, bold);
            item->setFlags(Qt::ItemIsEnabled);   // a heading is not an account
            accounts_->addTopLevelItem(item);
            heading = item;
        } else {
            const QString indent = QString(2 * (row.indent - 1), QChar(' '));
            item->setText(0, indent + QString::fromStdString(row.name));
            item->setData(0, Qt::UserRole, QString::fromStdString(row.account));
            item->setToolTip(0, QString::fromStdString(row.account));
            if (heading) heading->addChild(item);
            else accounts_->addTopLevelItem(item);
        }
        item->setText(1, money(row.balance));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        if (row.balance.cents() < 0) item->setForeground(1, QColor(170, 30, 30));
        if (!row.is_heading && row.account == chosen) item->setSelected(true);
    }
    accounts_->expandAll();
    refreshing_ = was;
}

void MainWindow::refreshPane(int tabPosition, const reg::BookIndex& book) {
    const bool was = refreshing_;
    refreshing_ = true;
    TabWidgets& p = tabWidgets_[static_cast<std::size_t>(tabPosition - 1)];

    if (p.isReport) {
        // A report tab holds the figures it was worked out with, and a redraw
        // is not a reason to work them out again: this runs on every tab
        // click and every column resize, and a report over twenty years
        // totalled on each of those is the slowest thing in the program. The
        // book says when, and run() says it was done. See the
        // report-worked-out section of UserInterface.spectable.
        if (p.run && workspace_->report_wants_working_out(tabPosition)) p.run();
        refreshing_ = was;
        return;
    }

    const std::vector<ui::TabRow> rows = workspace_->open_tabs();
    const std::string account =
        static_cast<std::size_t>(tabPosition - 1) < rows.size()
            ? rows[static_cast<std::size_t>(tabPosition - 1)].account
            : std::string();
    const std::vector<reg::Line> all = reg::lines_for(book, account, quickenSigns_);
    // What this register shows, and the order it shows it in. The range is
    // applied first, because the balance brought forward is worked out by
    // walking the lines in date order -- which is the order they arrive in --
    // and the sort may take them out of it. A range that begins at the first
    // transaction has nothing before it, and nothing before it is no line
    // rather than a line reading zero.
    std::vector<reg::Line> lines = all;
    bool balanceShown = true;
    std::string says;
    if (p.range != nullptr) {
        const types::Date today = asDate(QDate::currentDate());
        const reg::DateRange range = reg::named_range(
            p.range->currentText().toStdString(), today, all,
            p.since == nullptr ? std::string() : asDate(p.since->date()).iso());
        const reg::Filtered kept = reg::in_range(all, range, hideBefore_);
        lines = kept.lines;
        says = kept.note;
        if (says.empty() && kept.has_opening)
            says = "Balance on " + kept.opening_as_at.iso() + "  " +
                   kept.opening.in_register();
        const reg::Sorted order = reg::sorted_by(lines, p.sortColumn, p.sortDirection);
        lines = order.lines;
        balanceShown = order.balance_shown;
        if (!order.reason.empty())
            says = says.empty() ? order.reason : says + "   --   " + order.reason;
    }
    if (p.note != nullptr) p.note->setText(QString::fromStdString(says));
    if (p.balances != nullptr) {
        // The figures are the register's own, worked out from the index the
        // lines were worked out from, so this is one more pass over this
        // account and not a second reading of the book.
        const reg::Balances figures = reg::balances_for(book, account, quickenSigns_);
        std::string said = "Balance " + figures.balance.in_register() +
                           "    Cleared " + figures.cleared.in_register();
        if (figures.downloaded_at.has_value())
            said += "    Downloaded " + figures.downloaded.in_register() + " on " +
                    figures.downloaded_at->iso() + "    Difference " +
                    figures.difference.in_register();
        else
            said += "    No downloaded balance";
        p.balances->setText(QString::fromStdString(said));
    }
    // The two amount columns are headed with the words the account uses: a
    // credit card is charged and paid, not paid and deposited into.
    const chart::Account* a = account.empty() ? nullptr : chart_.find(account);
    const reg::ColumnHeadings headings =
        reg::headings_for(a ? a->type : types::AccountType::Checking);
    p.reg->setHorizontalHeaderLabels({"Date", "Num", "Payee", "Category", "Memo", "Tag",
                                      QString::fromStdString(headings.out_column),
                                      QString::fromStdString(headings.in_column),
                                      "Cleared", "Balance"});
    p.reg->setRowCount(static_cast<int>(lines.size()));
    // Filling the cells is the wait a person actually sits through -- thirty
    // thousand lines of twenty years, ten cells each -- and it is a count with
    // a total, so the strip can say how far it has got. Nothing here is written
    // to the book: this is the painting of what the book already says.
    beginProgress("Showing " + (account.empty() ? std::string("the register") : account),
                  static_cast<long long>(lines.size()), "lines");
    for (int r = 0; r < static_cast<int>(lines.size()); ++r) {
        // Once every two hundred rows. Asking the clock and repainting on every
        // cell would be most of the cost of filling them.
        if (r % 200 == 0) advanceProgress(r);
        const reg::Line& line = lines[static_cast<std::size_t>(r)];
        p.reg->setItem(r, 0, text(line.date.iso()));
        p.reg->setItem(r, 1, text(line.check_no));
        p.reg->setItem(r, 2, text(line.payee, line.payee));
        p.reg->setItem(r, 3, text(line.category, line.category_detail));
        p.reg->setItem(r, 4, text(line.memo, line.memo));
        p.reg->setItem(r, 5, text(line.tag, line.tag));
        setAmount(p.reg, r, 6, line.payment);
        setAmount(p.reg, r, 7, line.deposit);
        // What the bank has seen: c for cleared, R for reconciled, and nothing
        // for a line the bank has not reported. Short marks rather than the
        // words, because the column is read down thirty thousand rows and the
        // handful of blanks are the ones being looked for. See the reconciling
        // section of UserInterface.spectable.
        p.reg->setItem(r, 8,
                       text(types::cleared_mark(line.cleared),
                            types::to_string(line.cleared)));
        // A running balance down a list that is not in date order is a column
        // of meaningless numbers, so it is emptied rather than filled in.
        if (balanceShown) setAmount(p.reg, r, 9, line.balance);
        else p.reg->setItem(r, 9, text(std::string()));
    }
    endProgress();
    // The blank line: one more row at the end, which is where a transaction is
    // entered. It is not in the book and nothing is written until an amount is
    // typed -- see the entry section of TransactionRegister.spectable.
    const bool canEnter = !account.empty() && a != nullptr &&
                          types::class_of(a->type) == types::AccountClass::Real;
    p.reg->setRowCount(static_cast<int>(lines.size()) + (canEnter ? 1 : 0));
    if (canEnter) {
        const int at = static_cast<int>(lines.size());
        const reg::BlankLine& line = p.blank;
        const auto typed = [](const std::string& value) {
            auto* item = new QTableWidgetItem(QString::fromStdString(value));
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            return item;
        };
        p.reg->setItem(at, 0, typed(line.date.iso()));
        p.reg->setItem(at, 1, typed(line.check_no));
        p.reg->setItem(at, 2, typed(line.payee));
        p.reg->setItem(at, 3, typed(line.category));
        p.reg->setItem(at, 4, typed(line.memo));
        p.reg->setItem(at, 5, typed(line.tag));
        p.reg->setItem(at, 6, typed(line.payment.cents() == 0
                                        ? std::string() : line.payment.in_register()));
        p.reg->setItem(at, 7, typed(line.deposit.cents() == 0
                                        ? std::string() : line.deposit.in_register()));
        // A line being typed is not cleared and is not typed into either: the
        // bank has not seen a transaction that is not in the book yet.
        auto* cleared = new QTableWidgetItem;
        cleared->setFlags(Qt::ItemIsEnabled);
        p.reg->setItem(at, 8, cleared);
        auto* balance = new QTableWidgetItem;
        balance->setFlags(Qt::ItemIsEnabled);
        p.reg->setItem(at, 9, balance);
        for (int c = 0; c <= 9; ++c)
            p.reg->item(at, c)->setBackground(QColor(248, 248, 230));
    }

    // The workspace's selected lines belong to pane 1 -- the active
    // register -- so only that tab's table reflects them; a click in the
    // register sets them the same way "Go to the transaction..." from a
    // report does, so both land here.
    const bool isActiveRegister =
        static_cast<std::size_t>(tabPosition - 1) < rows.size() &&
        rows[static_cast<std::size_t>(tabPosition - 1)].active;
    const std::vector<int> chosen =
        isActiveRegister ? workspace_->selected_lines() : std::vector<int>();
    if (!chosen.empty()) {
        p.reg->clearSelection();
        for (int line : chosen)
            if (line >= 1 && line <= static_cast<int>(lines.size()))
                p.reg->selectRow(line - 1);
        if (chosen.front() >= 1 && chosen.front() <= static_cast<int>(lines.size()))
            p.reg->scrollToItem(p.reg->item(chosen.front() - 1, 0));
    } else if (!lines.empty()) {
        // Opening on the most recent transactions: twenty years is tens of
        // thousands of lines, and starting at the top would mean scrolling
        // through 2005.
        p.reg->scrollToBottom();
    }
    refreshing_ = was;
}

// The tab bar of the panel below the register, brought into step with the
// workspace. The two pages are the window's own widgets, so a tab is taken
// out and put back rather than built again; a page that is not in the bar is
// kept as a child of the window, because removeTab leaves it parentless and
// an orphan widget is one nobody deletes.
void MainWindow::syncBottomTabs() {
    const std::vector<ui::BottomTabRow> rows = workspace_->bottom_panel_tabs();
    const QSignalBlocker quiet(bottomTabs_);
    while (bottomTabs_->count() > 0) {
        QWidget* page = bottomTabs_->widget(0);
        bottomTabs_->removeTab(0);
        page->setParent(this);
        page->hide();
    }
    for (const ui::BottomTabRow& row : rows) {
        if (row.kind == ui::BottomTabKind::ImportReview)
            bottomTabs_->addTab(reviewPanel_, "Import review");
        else
            bottomTabs_->addTab(findPanel_, "Find results");
    }
    for (const ui::BottomTabRow& row : rows)
        if (row.active) bottomTabs_->setCurrentIndex(row.tab - 1);
    bottomTabs_->setVisible(workspace_->bottom_panel().open);
}

// The import review, beside the active tab only while one is waiting. There
// is one of these, not one per tab.
void MainWindow::refreshReviewPanel() {
    if (!workspace_->review_showing()) return;
    const bool was = refreshing_;
    refreshing_ = true;
    const std::vector<ui::ReviewRow>& reviewRows = workspace_->review();
    reviewHeading_->setText(QString("Import into %1  --  %2 rows, %3 ticked")
                                .arg(QString::fromStdString(workspace_->review_account()))
                                .arg(reviewRows.size())
                                .arg(workspace_->accepted_count()));
    reviewTable_->setRowCount(static_cast<int>(reviewRows.size()));
    for (int r = 0; r < static_cast<int>(reviewRows.size()); ++r) {
        const ui::ReviewRow& row = reviewRows[static_cast<std::size_t>(r)];
        auto* tick = new QTableWidgetItem;
        tick->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        tick->setCheckState(row.accepted ? Qt::Checked : Qt::Unchecked);
        tick->setData(Qt::UserRole, row.line);
        reviewTable_->setItem(r, 0, tick);
        reviewTable_->setItem(r, 1, text(row.date.iso()));
        reviewTable_->setItem(r, 2, text(row.payee, row.payee));
        reviewTable_->setItem(r, 3, text(row.account, row.account));
        reviewTable_->setItem(r, 4, text(row.category, row.category));
        setAmount(reviewTable_, r, 5, row.amount);
        reviewTable_->setItem(
            r, 6, text(ui::disposition_reading(row.disposition, row.notes)));
    }
    reviewTable_->resizeColumnsToContents();
    reviewTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    refreshing_ = was;
}

// What the last find matched. Nothing clears this when the tab closes: the
// rows are only read, and the next find replaces them.
void MainWindow::refreshFindPanel() {
    const bool was = refreshing_;
    refreshing_ = true;
    // Going to a result refreshes the window, which rebuilds this table, so
    // the row that was clicked is put back afterwards -- otherwise choosing a
    // result clears the highlight from the thing just chosen.
    const int chosen = findTable_->currentRow();
    const std::vector<ui::FindResultRow>& rows = workspace_->find_results();
    findHeading_->setText(rows.empty()
                              ? QString("Nothing matched")
                              : QString("%1 matching line(s)").arg(rows.size()));
    findTable_->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        const ui::FindResultRow& row = rows[static_cast<std::size_t>(r)];
        findTable_->setItem(r, 0, text(row.date.iso()));
        findTable_->setItem(r, 1, text(row.payee, row.payee));
        findTable_->setItem(r, 2, text(row.account, row.account));
        findTable_->setItem(r, 3, text(row.category, row.category));
        setAmount(findTable_, r, 4, row.amount);
    }
    findTable_->resizeColumnsToContents();
    findTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    if (chosen >= 0 && chosen < static_cast<int>(rows.size()))
        findTable_->selectRow(chosen);
    refreshing_ = was;
}

// The Find dialog: modeless, and built once. A find is adjusted and run
// again more often than it is right first time, so the fields keep what was
// typed and the window stays open over its own results.
void MainWindow::buildFindDialog() {
    findDialog_ = new QDialog(this);
    findDialog_->setWindowTitle("Find");
    auto* form = new QFormLayout(findDialog_);
    findForm_ = form;
    findText_ = new QLineEdit;
    findDateFrom_ = new QLineEdit;
    findDateTo_ = new QLineEdit;
    findAmountMatch_ = new QComboBox;
    findAmount_ = new QLineEdit;
    findAmountTo_ = new QLineEdit;
    findAccount_ = new QLineEdit;
    findScope_ = new QComboBox;
    // A blank field is not a filter, so every one of them says so rather than
    // looking like something that was not filled in yet.
    findText_->setPlaceholderText("any payee or memo");
    findDateFrom_->setPlaceholderText("yyyy-mm-dd, or blank for no earliest");
    findDateTo_->setPlaceholderText("yyyy-mm-dd, or blank for no latest");
    findAmount_->setPlaceholderText("any amount");
    findAmountTo_->setPlaceholderText("no upper end");
    findAccount_->setPlaceholderText("any account or category");
    findAmountMatch_->addItem("Exact", QString("Exact"));
    findAmountMatch_->addItem("Range", QString("Range"));
    findScope_->addItem("This register", QString("CurrentRegister"));
    findScope_->addItem("Every register", QString("AllRegisters"));
    form->addRow("Text", findText_);
    form->addRow("Date from", findDateFrom_);
    form->addRow("Date to", findDateTo_);
    // The dropdown sits beside the amount, and the comparison is on the
    // amount itself rather than on the sign of it: somebody recalling a
    // transaction rarely remembers which way it went.
    auto* amountRow = new QWidget;
    auto* amountBox = new QHBoxLayout(amountRow);
    amountBox->setContentsMargins(0, 0, 0, 0);
    amountBox->addWidget(findAmountMatch_);
    amountBox->addWidget(findAmount_, 1);
    form->addRow("Amount", amountRow);
    findAmountLabel_ = qobject_cast<QLabel*>(form->labelForField(amountRow));
    form->addRow("to", findAmountTo_);
    findAmountToLabel_ = qobject_cast<QLabel*>(form->labelForField(findAmountTo_));
    form->setRowVisible(findAmountTo_, false);
    connect(findAmountMatch_, &QComboBox::currentIndexChanged,
            this, &MainWindow::findAmountMatchChanged);
    form->addRow("Account", findAccount_);
    form->addRow("Look in", findScope_);
    auto* buttons = new QDialogButtonBox;
    QPushButton* findButton = buttons->addButton("Find", QDialogButtonBox::AcceptRole);
    buttons->addButton("Close", QDialogButtonBox::RejectRole);
    form->addRow(buttons);
    connect(findButton, &QPushButton::clicked, this, &MainWindow::performFind);
    connect(buttons, &QDialogButtonBox::rejected, findDialog_, &QDialog::hide);
    // Return in any of the fields means Find, which is what it means in every
    // other search box.
    findButton->setDefault(true);
}

void MainWindow::showFind() {
    if (findDialog_ == nullptr) buildFindDialog();
    findDialog_->show();
    findDialog_->raise();
    findDialog_->activateWindow();
    findText_->setFocus();
    findText_->selectAll();
}

// Switching back to Exact hides the second field and clears it, so a number
// left behind cannot narrow a later find that no longer shows it. Switching to
// Range leaves the first field alone: the amount already typed is the sensible
// lower end.
void MainWindow::findAmountMatchChanged() {
    // How many fields there are and what they are called is ui::amount_fields
    // and not this window's own idea, so that the rule about the second field
    // in UserInterface.spectable is the one thing deciding it. The labels used
    // to be written out here as well, which is two places to change and one of
    // them with no test over it.
    const auto match = ui::amount_match_from(
        findAmountMatch_->currentData().toString().toStdString());
    const ui::AmountFields fields =
        ui::amount_fields(match.value_or(ui::AmountMatch::Exact));
    const bool range = fields.shown > 1;
    if (!range) findAmountTo_->clear();
    findForm_->setRowVisible(findAmountTo_, range);
    if (findAmountLabel_ != nullptr)
        findAmountLabel_->setText(QString::fromStdString(fields.first_label));
    if (findAmountToLabel_ != nullptr)
        findAmountToLabel_->setText(QString::fromStdString(fields.second_label));
}

void MainWindow::performFind() {
    ui::FindCriteria criteria;
    criteria.text = findText_->text().trimmed().toStdString();
    criteria.account = findAccount_->text().trimmed().toStdString();
    const auto date = [](const QLineEdit* field) -> std::optional<types::Date> {
        const QString typed = field->text().trimmed();
        if (typed.isEmpty()) return std::nullopt;
        return types::Date::from_iso(typed.toStdString());
    };
    const auto amount = [](const QLineEdit* field) -> std::optional<Money> {
        const QString typed = field->text().trimmed();
        if (typed.isEmpty()) return std::nullopt;
        return moneyTyped(typed);
    };
    criteria.date_from = date(findDateFrom_);
    criteria.date_to = date(findDateTo_);
    const std::optional<ui::AmountMatch> match =
        ui::amount_match_from(findAmountMatch_->currentData().toString().toStdString());
    criteria.amount_match = match ? *match : ui::AmountMatch::Exact;
    criteria.amount = amount(findAmount_);
    criteria.amount_to = amount(findAmountTo_);
    const std::optional<ui::FindScope> scope =
        ui::find_scope_from(findScope_->currentData().toString().toStdString());
    criteria.scope = scope ? *scope : ui::FindScope::CurrentRegister;
    workspace_->perform_find(criteria);
    refresh();
}

void MainWindow::tabClosed(int index) {
    workspace_->close_tab(index + 1);
    refresh();
}

void MainWindow::tabChanged(int index) {
    if (refreshing_ || index < 0) return;
    // A click on the tab bar names a position, not an account -- select_tab
    // is the step that matches that, for either kind of tab. Without it,
    // clicking back to a report that had lost focus did nothing, and the next
    // redraw snapped the bar back to whichever register was last read.
    workspace_->select_tab(index + 1);
    // A tab already showing what the book says needs nothing done to it. The
    // account list still follows the tab, because which register is in front
    // is what it highlights.
    const std::size_t at = static_cast<std::size_t>(index);
    if (at < tabWidgets_.size() && !tabWidgets_[at].stale) {
        const bool was = refreshing_;
        refreshing_ = true;
        refreshAccounts();
        refreshing_ = was;
        return;
    }
    refresh();
}

// What is on the blank line now, read back out of the table. Called before
// anything is done with it, so that a cell still being typed in is included.
void MainWindow::readBlankLine(int tabPosition) {
    TabWidgets& p = tabWidgets_[static_cast<std::size_t>(tabPosition - 1)];
    if (p.isReport || p.reg == nullptr) return;
    const int at = p.reg->rowCount() - 1;
    if (at < 0) return;
    const auto cell = [&](int column) {
        const QTableWidgetItem* item = p.reg->item(at, column);
        return item == nullptr ? QString() : item->text();
    };
    const auto date = types::Date::from_iso(cell(0).trimmed().toStdString());
    if (date) p.blank.date = *date;
    p.blank.check_no = cell(1).trimmed().toStdString();
    p.blank.payee = cell(2).trimmed().toStdString();
    p.blank.category = cell(3).trimmed().toStdString();
    p.blank.memo = cell(4).trimmed().toStdString();
    p.blank.tag = cell(5).trimmed().toStdString();
    p.blank.payment = moneyTyped(cell(6));
    p.blank.deposit = moneyTyped(cell(7));
}

void MainWindow::recordBlankLine(int tabPosition) {
    if (refreshing_) return;
    const std::vector<ui::TabRow> rows = workspace_->open_tabs();
    if (static_cast<std::size_t>(tabPosition - 1) >= rows.size()) return;
    const ui::TabRow& row = rows[static_cast<std::size_t>(tabPosition - 1)];
    if (row.kind != ui::TabKind::Register || row.account.empty()) return;
    readBlankLine(tabPosition);
    TabWidgets& p = tabWidgets_[static_cast<std::size_t>(tabPosition - 1)];

    const reg::Committed done = reg::commit(p.blank, row.account,
                                            ledger::transaction_count(transactions_), &chart_);
    if (!done.committed) {
        // A line with no amount is a line nobody meant, and is not an error.
        // Anything else is, and is worth saying.
        if (!done.reason.empty() && done.reason != "nothing was entered")
            QMessageBox::warning(this, "SimpleAccount",
                                 QString::fromStdString(done.reason));
        return;
    }
    transactions_.push_back(done.transaction);
    if (done.is_transfer) transactions_.push_back(done.other_side);
    // A real account in the destination column is a transfer, and the account
    // named may already have its own line for this payment -- entered by hand
    // or arrived in a download. So it is asked about here. A category and an
    // Unassigned destination are not transfers and are never asked about; see
    // the destination-column rule in UserInterface.spectable.
    if (done.is_transfer) offerTheOtherHalf(done, row.account);
    // A word typed in the Tag column that is not in the vocabulary joins it:
    // typing is the one place a tag is created, so the table need not be
    // opened first. See the tag rules in UserInterface.spectable.
    const std::vector<std::string> grown =
        tags::accepted(tagVocabulary_, done.transaction.tag);
    if (grown.size() != tagVocabulary_.size()) {
        tagVocabulary_ = grown;
        store_.write_tags(tagVocabulary_);
    }
    ledger::add_to_book(&book_, chart_, transactions_, splits_);
    // A book is a file rather than a session, so an entry is written as it is
    // made. The next blank line keeps the date just used, because entering a
    // morning of receipts means typing the same date over and over otherwise.
    const types::Date keep = p.blank.date;
    p.blank = reg::blank_line_on(keep);
    save();
    rebuildWorkspace();
    refresh();
    status_->setText(QString("Recorded %1  --  %2 transactions")
                         .arg(QString::fromStdString(done.transaction.payee.value()))
                         .arg(transactions_.size()));
}

// The other half of a transfer just entered by hand. The evidence is the
// import's evidence and nothing more: equal and opposite to the cent, in the
// account the destination column just named, within the two-day window, and
// not already paired with something. The payee is not consulted, because
// narrowing by payee loses every payment nobody recorded a payee for -- which
// is most of them. Closest in days is offered first.
void MainWindow::offerTheOtherHalf(const reg::Committed& done, const std::string& own) {
    const std::string named = chart_.path_for_ref(done.transaction.destination);
    const std::vector<transfers::Half> halves = transfers::halves_in(
        named, own, done.transaction.date, done.transaction.amount,
        transactions_, chart_);
    // Nothing equal and opposite in that account is silence. A dialog that says
    // there is nothing to ask is worse than no dialog.
    if (halves.empty()) return;

    QStringList offered;
    for (const transfers::Half& half : halves)
        offered << QString("%1   %2   %3   (%4 day%5 apart)")
                       .arg(QString::fromStdString(half.account))
                       .arg(QString::fromStdString(half.date.iso()))
                       .arg(QString::fromStdString(
                           half.payee.empty() ? std::string("no payee") : half.payee))
                       .arg(half.days_apart)
                       .arg(half.days_apart == 1 ? "" : "s");
    const QString none = "None of them";
    offered << none;

    bool said = false;
    const QString chose = QInputDialog::getItem(
        this, "The other half of this transfer",
        QString("%1 already has a line that could be the other side of this %2.\n"
                "Is one of them the same payment?")
            .arg(QString::fromStdString(named))
            .arg(money(done.transaction.amount)),
        offered, 0, false, &said);
    // Answering none creates nothing and unpicks nothing: the line just entered
    // already names both accounts, so the other register has its own row either
    // way. What none rules out is the merge, not the transfer.
    if (!said || chose == none) return;
    const int at = offered.indexOf(chose);
    if (at < 0 || static_cast<std::size_t>(at) >= halves.size()) return;
    const transfers::Half& half = halves[static_cast<std::size_t>(at)];

    // A merge and not a link. What the line in the other register knew and this
    // one does not -- that the bank had reported it, and the identifier a
    // download gave it -- comes across, and then it is gone. The row this
    // commit made for that same account stands in its place, so it is the one
    // that carries the cleared status forward: the statement that cleared the
    // line being replaced is that account's statement, not this one's.
    types::ClearedStatus theirs = types::ClearedStatus::Uncleared;
    for (const ledger::Transaction& t : transactions_)
        if (t.id.value() == half.transaction_id) theirs = t.cleared;
    const std::string other_side =
        done.is_transfer ? done.other_side.id.value() : std::string();
    if (!transfers::merge(done.transaction.id.value(), half.transaction_id,
                          &transactions_))
        return;
    if (!other_side.empty())
        for (ledger::Transaction& t : transactions_)
            if (t.id.value() == other_side &&
                static_cast<int>(theirs) > static_cast<int>(t.cleared))
                t.cleared = theirs;

    ledger::add_to_book(&book_, chart_, transactions_, splits_);
    save();
    rebuildWorkspace();
    status_->setText(QString("Paired with the line of %1 in %2")
                         .arg(QString::fromStdString(half.date.iso()))
                         .arg(QString::fromStdString(half.account)));
}

// --- the work, off the thread that paints -----------------------------------
//
// A reader of a real file is one call that takes seconds, and while it runs on
// this thread the window cannot repaint and a click cannot arrive. So it runs
// on a thread of its own and this one waits in an event loop, polling what the
// worker has written and painting the strip from it. That is what makes Stop a
// button rather than a decoration.
//
// Two rules of the waiting are worth stating. The worker touches nothing this
// window paints: it is handed its own copies, and the ticker it reports through
// writes numbers into a progress::Running and reads one flag back. And while it
// runs, the menu bar and the whole of the window below it are disabled -- the
// one thing offered is Stop, because anything else offered would be a way to
// change the book half way through reading a file into it.
bool MainWindow::runWithProgress(
    const std::string& what, const std::string& unit,
    const std::function<void(const progress::Ticker&)>& work) {
    progress::Running running;
    std::atomic<bool> done{false};
    std::exception_ptr failed;

    progressStopping_ = &running;
    // No total yet: the reader finds out how much there is when it opens the
    // file, and until it says so the strip is words and a bar that only says
    // something is going on. A count with no total is not an estimate.
    beginProgress(what, 0, unit);
    menuBar()->setEnabled(false);
    if (QWidget* middle = centralWidget()) middle->setEnabled(false);

    std::thread worker([&] {
        // A throw here would otherwise end the program rather than the import,
        // so it is carried back and raised on the thread that asked.
        try {
            work(running.ticker());
        } catch (...) {
            failed = std::current_exception();
        }
        done.store(true);
    });

    QEventLoop waiting;
    QTimer polls;
    polls.setInterval(50);
    connect(&polls, &QTimer::timeout, &waiting, [&] {
        const progress::Report said = running.report();
        if (!said.what.empty()) progress_.what = said.what;
        progress_.total = said.total;
        advanceProgress(said.done);
        if (done.load()) waiting.quit();
    });
    polls.start();
    waiting.exec();
    worker.join();

    progressStopping_ = nullptr;
    endProgress();
    menuBar()->setEnabled(true);
    if (QWidget* middle = centralWidget()) middle->setEnabled(true);
    if (failed) std::rethrow_exception(failed);
    return !running.stopping();
}

// --- saying that something long is going on ---------------------------------
//
// The report is asked for at the start and shown only if the work is still
// going a second later, because a strip that appears and goes in the same blink
// is worse than none at all: the person sees a flicker and looks for what it
// said. progress::shows is where that second lives.
void MainWindow::beginProgress(const std::string& what, long long total,
                               const std::string& unit) {
    progress_ = progress::Report();
    progress_.what = what;
    progress_.total = total;
    progress_.unit = unit;
    progressShowing_ = false;
    if (progressStop_) {
        progressStop_->setText("Stop");
        progressStop_->setEnabled(true);
        progressStop_->hide();
    }
    progressSince_.start();
}

// Returns whether to carry on. That is false once Stop has been pressed, and
// true for work on this thread, where there is nothing to press: the click
// could not arrive until the work it would stop had finished.
bool MainWindow::advanceProgress(long long done) {
    progress_ = progress::advanced(progress_, done);
    const double running = static_cast<double>(progressSince_.elapsed()) / 1000.0;
    if (!progress::shows(progress_, running))
        return !progressStopping_ || !progressStopping_->stopping();
    if (!progressShowing_) {
        progressShowing_ = true;
        progressText_->show();
        progressBar_->show();
        if (progressStopping_) progressStop_->show();
    }
    progressText_->setText(QString::fromStdString(progress::reads(progress_)));
    if (progress_.total > 0) {
        progressBar_->setRange(0, static_cast<int>(progress_.total));
        progressBar_->setValue(static_cast<int>(progress_.done));
    } else {
        // A count with no total is not an estimate, so the bar says only that
        // something is going on.
        progressBar_->setRange(0, 0);
    }
    // Repaints only, for work on this thread: a click arriving in the middle of
    // a half-filled table would be answered against a register that does not
    // yet exist. Work on a thread of its own is already inside an event loop
    // that is taking clicks, which is where Stop comes from, so nothing is
    // pumped here.
    if (!progressStopping_) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        return true;
    }
    return !progressStopping_->stopping();
}

// Finishing takes the report away. The strip is not a log, and what the work
// did is said in the work's own notes.
void MainWindow::endProgress() {
    progress_ = progress::stopped(progress_);
    if (!progressShowing_) return;
    progressShowing_ = false;
    progressText_->hide();
    progressBar_->hide();
    progressStop_->hide();
}
