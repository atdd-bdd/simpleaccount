#pragma once
#include <QFont>
#include <QMainWindow>
#include <QMenu>
#include <QPoint>
#include <functional>
#include <memory>
#include <vector>
#include "chart.h"
#include "ledger.h"
#include "posting.h"
#include "payee_rules.h"
#include "register_entry.h"
#include "report.h"
#include "store_sqlite.h"
#include "ui_model.h"

class QTreeWidget;
class QTableWidget;
class QSplitter;
class QTabWidget;
class QLabel;
class QComboBox;
class QDateEdit;
class QCheckBox;

// The window. Three regions: the account list on the left, a tab bar of
// registers and reports on the right, and -- only while an import is waiting
// -- a second panel beside the active tab. See the tabs section of
// UserInterface.spectable.
//
// Everything about which tabs are open and which is active lives in
// ui::Workspace, which has no Qt in it and is tested against that spec. This
// class only paints that and turns clicks back into calls on it. A report's
// own controls -- its period, which columns it shows -- are not modeled in
// the workspace at all: they belong to the window, the same way a dialog's
// fields always have.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

    // Opens the book of that name, as File > Open book does. Empty opens
    // nothing and is not an error.
    void openNamed(const QString& name);

    // Reads a QIF export into the book that is open, as a one-time migration
    // from Quicken. Not how a book is opened; see File > Open book.
    void importHistoryFrom(const QString& path);

    // Reads a downloaded QFX or CSV into the review panel, for the account
    // the active tab is open on. Nothing reaches the book until the review
    // is accepted.
    void importTransactionsFrom(const QString& path);

private slots:
    void newBook();
    void openBook();
    void newAccount();
    void importHistory();
    void importQifForReview();
    void importTransactions();
    void editRules();
    void assignPayeesAccordingToRules();
    void showReport();
    void toggleHidden();
    void accountClicked();
    void tabClosed(int index);
    void tabChanged(int index);
    void acceptImport();
    void cancelImport();
    void chooseFont();

private:
    // One open tab: a register, or a report with its own controls. Only the
    // fields for the kind it actually is are filled in, so an empty pointer
    // in the other half is never read.
    struct TabWidgets {
        bool isReport = false;
        QWidget* widget = nullptr;   // what is added to the tab bar

        // A register.
        QTableWidget* reg = nullptr;
        reg::BlankLine blank;

        // A report. run() redraws reportTable and totals from the controls
        // above it; shown is what the table is currently showing, kept so
        // the context menu can turn a clicked row back into a transaction.
        QComboBox* period = nullptr;
        QDateEdit* from = nullptr;
        QDateEdit* to = nullptr;
        QComboBox* detail = nullptr;
        QCheckBox* quicken = nullptr;
        QCheckBox* zero = nullptr;
        QTableWidget* reportTable = nullptr;
        QLabel* totals = nullptr;
        std::shared_ptr<std::vector<reports::DetailLine>> shown;
        std::function<void()> run;
    };

    void buildMenus();
    // Sets the font for every window and brings the widgets that carry their
    // own -- the bold headings, the row heights -- back into step with it.
    void applyFont(const QFont& font);
    void rebuildWorkspace();
    // Everything that has to happen after the book is replaced wholesale, so
    // that New and Open cannot drift apart.
    void bookChanged(const QString& what);
    // Writes the book that is open. Called after anything that changes it,
    // because a book is a file rather than a session: closing the window must
    // not be a way to lose a morning's entry.
    bool save();
    void refresh();
    void refreshAccounts();
    QMenu* recent_ = nullptr;
    // Makes the tab bar match workspace_->open_tabs(): adds a widget for a
    // tab that just opened, removes one for a tab that closed, and brings
    // the rest's titles and active state into step.
    void syncTabs();
    void refreshPane(int tabPosition);
    void refreshReviewPanel();
    // The menu on a selection of register lines, and what its items do. The
    // selection itself lives in the workspace, which has no Qt in it.
    void registerMenu(int tabPosition, const QPoint& at);
    void recategorise(int tabPosition);
    void deleteSelection(int tabPosition);
    void addPayeeRuleFrom(int tabPosition);
    void rebuildRecent();
    void rememberRecent(const QString& name);
    // Reads what was typed on the blank line of that tab back out of the
    // table, and records it if an amount was entered. An amount is what makes
    // it a transaction: a payee typed and thought better of leaves nothing.
    void readBlankLine(int tabPosition);
    void recordBlankLine(int tabPosition);
    TabWidgets makeRegisterTab(int tabPosition);
    TabWidgets makeReportTab(int tabPosition, const std::string& title);
    std::string selectedAccount() const;

    // The book this window is of, held open. Empty until one is made or opened:
    // the window starts with no book rather than an untitled one, because a
    // book is named before it exists.
    store::Book store_;
    chart::Chart chart_;
    ledger::Ledger book_;
    std::vector<ledger::Transaction> transactions_;
    // The book's payee rules, held with it because they are part of it: an
    // import asks them for the payee and the category of every row.
    std::vector<payees::Rule> rules_;
    std::unique_ptr<ui::Workspace> workspace_;
    bool showHidden_ = false;
    QFont appFont_;
    // Set while the widgets are being filled in, so that the signals that
    // firing causes are not read back as the user changing something.
    bool refreshing_ = false;

    QTreeWidget* accounts_ = nullptr;
    QSplitter* outer_ = nullptr;
    QSplitter* rightSplit_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    std::vector<TabWidgets> tabWidgets_;

    // The review panel, beside the active tab while an import is waiting. One
    // of these, not one per tab: see the second-pane section of
    // UserInterface.spectable.
    QWidget* reviewPanel_ = nullptr;
    QLabel* reviewHeading_ = nullptr;
    QTableWidget* reviewTable_ = nullptr;

    QLabel* status_ = nullptr;
};
