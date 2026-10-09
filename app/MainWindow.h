#pragma once
#include <QFont>
#include <QMainWindow>
#include <QMenu>
#include <QPoint>
#include <memory>
#include <vector>
#include "chart.h"
#include "ledger.h"
#include "posting.h"
#include "payee_rules.h"
#include "register_entry.h"
#include "store_sqlite.h"
#include "ui_model.h"

class QTreeWidget;
class QTableWidget;
class QSplitter;
class QStackedWidget;
class QLabel;

// The window. Three regions: the account list on the left, and on the right one
// or two panes, each showing either a register or an import waiting to be
// reviewed.
//
// Everything about what is shown lives in ui::Workspace, which has no Qt in it
// and is tested against UserInterface.spectable. This class only paints it and
// turns clicks back into calls on it.
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

    // Reads a downloaded QFX or CSV into the review pane, for the account that
    // is selected. Nothing reaches the book until the review is accepted.
    void importTransactionsFrom(const QString& path);

private slots:
    void newBook();
    void openBook();
    void newAccount();
    void importHistory();
    void importQifForReview();
    void importTransactions();
    void editRules();
    void showReport();
    void toggleSplit();
    void toggleHidden();
    void accountClicked();
    void acceptImport();
    void cancelImport();
    void chooseFont();

private:
    // One pane: a register table, or the import review, whichever is showing.
    struct PaneWidgets {
        QStackedWidget* stack = nullptr;
        QTableWidget* reg = nullptr;
        QWidget* review = nullptr;
        QTableWidget* reviewTable = nullptr;
        QLabel* heading = nullptr;
        // What has been typed on the blank line at the end of this register.
        // Per pane, because the same account may be open in both and a line
        // half typed in one is not a line in the other.
        reg::BlankLine blank;
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
    void refreshPane(int paneOneBased);
    // The books opened lately, by name. Kept in QSettings rather than in a
    // book, because which books a person has been in is about this machine and
    // not about any one of them.
    // The menu on a selection of register lines, and what its items do. The
    // selection itself lives in the workspace, which has no Qt in it.
    void registerMenu(int paneOneBased, const QPoint& at);
    void recategorise(int paneOneBased);
    void deleteSelection(int paneOneBased);
    void addPayeeRuleFrom(int paneOneBased);
    void rebuildRecent();
    void rememberRecent(const QString& name);
    // Reads what was typed on the blank line of that pane back out of the
    // table, and records it if an amount was entered. An amount is what makes
    // it a transaction: a payee typed and thought better of leaves nothing.
    void readBlankLine(int paneOneBased);
    void recordBlankLine(int paneOneBased);
    PaneWidgets makePane(int paneOneBased);
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
    QSplitter* panes_ = nullptr;
    std::vector<PaneWidgets> paneWidgets_;
    QLabel* status_ = nullptr;
};
