#pragma once
#include <QFont>
#include <QMainWindow>
#include <memory>
#include <vector>
#include "chart.h"
#include "ledger.h"
#include "posting.h"
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

private slots:
    void newBook();
    void openBook();
    void newAccount();
    void importHistory();
    void importQifForReview();
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
    void refreshPane(int paneOneBased);
    PaneWidgets makePane(int paneOneBased);
    std::string selectedAccount() const;

    // The book this window is of, held open. Empty until one is made or opened:
    // the window starts with no book rather than an untitled one, because a
    // book is named before it exists.
    store::Book store_;
    chart::Chart chart_;
    ledger::Ledger book_;
    std::vector<ledger::Transaction> transactions_;
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
