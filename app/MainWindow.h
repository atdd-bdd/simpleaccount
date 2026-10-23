#pragma once
#include <QElapsedTimer>
#include <QFont>
#include <QMainWindow>
#include <QMenu>
#include <QPoint>
#include <QTimer>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "chart.h"
#include "ledger.h"
#include "transaction.h"
#include "payee_rules.h"
#include "progress.h"
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
class QDialog;
class QLineEdit;
class QFormLayout;
class QProgressBar;
class QPushButton;

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
    // confirmed in the dialog in front of it. Nothing reaches the book until
    // the review is accepted.
    void importTransactionsFrom(const QString& path);

    // What the dialog in front of an import was answered with: the account,
    // and the balance the bank says the account stands at. The second is the
    // file's own figure where it stated one, and is typed where it did not --
    // a CSV never states a balance and is the reason the field is there. An
    // empty account means cancelled, and an empty balance means no answer,
    // which is not the same thing as zero.
    struct ImportAnswer {
        std::string account;
        std::string online_balance;
        std::string online_balance_at;
    };
    ImportAnswer confirmImportAccount(const ui::Workspace::ImportConfirmation& proposed);
    void askAboutTransfers();
    // The program's own file dialog, which an import uses so that the newest
    // file is the first one offered.
    QString chooseImportFile(const QString& title, const QString& filter);
    // A QIF read into the review panel. Reached from the one import item rather
    // than from a menu item of its own.
    void importQifForReviewFrom(const QString& path);
    // The question a new book opens with: which accounts it starts from, and a
    // history file to take them out of if there is one.
    void askTheNewBookQuestion(const QString& name);

private slots:
    void newBook();
    void openBook();
    void newAccount();
    void importTransactions();
    void editRules();
    void assignPayeesAccordingToRules();
    // The converter tables: one window of two tabs, because the two are read
    // together -- a name is converted and then the converted name is
    // categorised -- and kept apart, because the columns differ. See
    // decisions.md.
    void editPayeeTables();
    void convertPayeesInTheBook();
    void assignCategoriesInTheBook();
    // The book's own settings, which are about the book rather than about any
    // register in it: the check window, whether a CSV import arrives cleared,
    // the date before which nothing shows, and the fiscal year start.
    void editBookSettings();
    // The tag vocabulary, with how many transactions carry each word.
    void showTagListing();
    void sortRegisterBy(int tabPosition, int column);
    void registerRangeChanged(int tabPosition);
    // The two listings, which show the chart itself rather than the work done
    // in it. See the listings section of UserInterface.spectable.
    void showAccountListing();
    void showCategoryListing();
    void showReport();
    void toggleHidden();
    void accountClicked();
    void tabClosed(int index);
    void tabChanged(int index);
    void acceptImport();
    void cancelImport();
    // Find. The dialog is modeless and there is one of it, so showFind brings
    // the same window back rather than making a second, and performFind
    // replaces what is in the results tab. See the Find section of
    // UserInterface.spectable.
    void showFind();
    void performFind();
    void findAmountMatchChanged();
    void chooseFont();

private:
    // One open tab: a register, or a report with its own controls. Only the
    // fields for the kind it actually is are filled in, so an empty pointer
    // in the other half is never read.
    struct TabWidgets {
        bool isReport = false;
        QWidget* widget = nullptr;   // what is added to the tab bar
        // Whether what this tab shows is older than the book. A tab nobody is
        // looking at is not brought up to date, because filling thirty
        // thousand Qt cells nobody can see is the whole of why switching
        // registers took a noticeable time. It is filled when it is shown.
        // See decisions.md.
        bool stale = false;

        // A register. The controls above the table are a register's view of
        // itself: the date range it shows, and -- when that range is Since a
        // date -- the date it starts at. The column it is sorted by and which
        // way round are the same kind of thing, and both are per tab, because
        // two registers open at once are two views of two accounts.
        QTableWidget* reg = nullptr;
        reg::BlankLine blank;
        QComboBox* range = nullptr;
        QDateEdit* since = nullptr;
        QLabel* note = nullptr;
        // The three figures across the top: what the register adds up to, what
        // of it has cleared, and what the last download said -- with the
        // difference between the last two, which is the number a person is
        // actually reconciling. See the three-balances section of
        // TransactionRegister.spectable.
        QLabel* balances = nullptr;
        std::string sortColumn = "Date";
        std::string sortDirection = "Ascending";

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
    bool saveColumnWidths();
    void refresh();
    void refreshAccounts();
    QMenu* recent_ = nullptr;
    // Makes the tab bar match workspace_->open_tabs(): adds a widget for a
    // tab that just opened, removes one for a tab that closed, and brings
    // the rest's titles and active state into step.
    void syncTabs();
    // The index is the book's, not the pane's: a refresh builds one and every
    // open register is read from it, rather than each of them building the
    // same thing again. See reg::BookIndex.
    void refreshPane(int tabPosition, const reg::BookIndex& book);
    // Makes the bottom panel's tab bar match workspace_->bottom_panel_tabs().
    // The pages are the window's own two widgets rather than new ones, so a
    // tab that closes and opens again is the same widget both times.
    void syncBottomTabs();
    void refreshReviewPanel();
    void refreshFindPanel();
    // The Find dialog, built the first time it is asked for.
    void buildFindDialog();
    // The menu on a selection of register lines, and what its items do. The
    // selection itself lives in the workspace, which has no Qt in it.
    void registerMenu(int tabPosition, const QPoint& at);
    // The menu on a line of the import review, which offers to show what the
    // import decided the line already was. See the show-the-duplicate section
    // of UserInterface.spectable.
    void reviewMenu(const QPoint& at);
    void showDuplicate();
    // The menu on a find result, which offers the transaction and, when the
    // result is a split, the lines the row cannot show. See the find-results
    // menu section of UserInterface.spectable.
    void findResultMenu(const QPoint& at);
    void showFindResultSplit(int line);
    // The menu on a line of the tree on the left. A group heading offers only
    // a new account, a heading being nothing in the book.
    void treeMenu(const QPoint& at);
    // The editor of one node, opened by clicking its line in a listing or by
    // Edit on the tree -- the same window either way.
    void editAccountSettings(const QString& path);
    void editCategorySettings(const QString& path);
    // Adds one from the category listing, which is where a person is looking
    // when they find the category they wanted is not there. True when one was
    // added, so the listing behind it can fill itself again.
    bool addCategoryFromListing(QWidget* parent);
    void hideAccount(const QString& path);
    void deleteAccount(const QString& path);
    // Puts the widths somebody dragged onto every register. The width belongs
    // to the column rather than to the register; see the width rule in
    // UserInterface.spectable.
    void applyColumnWidths();
    void recategorise(int tabPosition);
    void deleteSelection(int tabPosition);
    // Splitting a transaction already in the register. See the splitting
    // section of UserInterface.spectable.
    void splitSelection(int tabPosition);
    void addPayeeRuleFrom(int tabPosition);
    // Runs the rules already written over the lines selected, which is the
    // same pass the import makes. See applying the payee rules to a selection
    // in UserInterface.spectable.
    void applyPayeeRulesToSelection(int tabPosition);
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
    std::vector<ledger::TransactionSplit> splits_;
    // The book's payee rules, held with it because they are part of it: an
    // import asks them for the payee and the category of every row.
    std::vector<payees::Rule> rules_;
    // The two converter tables and the tag vocabulary, held with the book the
    // same way. Each is an ordered list: the first row that matches acts, so
    // the order is the user's and is kept.
    std::vector<payees::Conversion> conversions_;
    std::vector<payees::Categorisation> categorisations_;
    std::vector<std::string> tagVocabulary_;
    // The book's hide-before date, as the book stores it: an ISO date, the
    // word none, or nothing at all. Every register reads it.
    std::string hideBefore_;
    std::unique_ptr<ui::Workspace> workspace_;
    bool showHidden_ = false;
    QFont appFont_;
    // Set while the widgets are being filled in, so that the signals that
    // firing causes are not read back as the user changing something.
    bool refreshing_ = false;

    // Dragging a divider reports every pixel. Applying and writing each one
    // laid every other register out again and wrote the book to disk a hundred
    // times for one drag, so the pixels are recorded and this says when the
    // drag has stopped. Made on the first drag and kept.
    QTimer* resizeSettle_ = nullptr;

    QTreeWidget* accounts_ = nullptr;
    QSplitter* outer_ = nullptr;
    QSplitter* rightSplit_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    std::vector<TabWidgets> tabWidgets_;

    // The width each register column was dragged to, by the column's name in
    // the specification rather than its heading -- Payment and Deposit are
    // headed differently per account type and the width is not. Only the
    // columns somebody resized are in here.
    std::map<std::string, int> columnWidths_;

    // The panel below the register, one tab per thing waiting to be judged.
    // Hidden until it has a tab in it. See the bottom-panel section of
    // UserInterface.spectable.
    QTabWidget* bottomTabs_ = nullptr;

    // The import review, which is one of that panel's tabs. One of these, not
    // one per register tab: an import is read against the register above it.
    QWidget* reviewPanel_ = nullptr;
    QLabel* reviewHeading_ = nullptr;
    QTableWidget* reviewTable_ = nullptr;

    // The results of a find, the panel's other tab. The dialog that asks for
    // them is modeless and kept, so that a search can be adjusted and run
    // again without typing it out a second time; its fields are the window's,
    // not the workspace's, the same way a report's controls are.
    QWidget* findPanel_ = nullptr;
    QLabel* findHeading_ = nullptr;
    QTableWidget* findTable_ = nullptr;
    QDialog* findDialog_ = nullptr;
    QLineEdit* findText_ = nullptr;
    QLineEdit* findDateFrom_ = nullptr;
    QLineEdit* findDateTo_ = nullptr;
    // The amount is one field until Range is chosen, and the dropdown is what
    // says which is meant -- two fields always on the screen is what this had,
    // and nothing on it said what leaving the second one blank would do.
    QFormLayout* findForm_ = nullptr;
    QComboBox* findAmountMatch_ = nullptr;
    QLabel* findAmountLabel_ = nullptr;
    QLabel* findAmountToLabel_ = nullptr;
    QLineEdit* findAmount_ = nullptr;
    QLineEdit* findAmountTo_ = nullptr;
    QLineEdit* findAccount_ = nullptr;
    QComboBox* findScope_ = nullptr;

    // --- saying that something long is going on -----------------------------
    //
    // The words and the two numbers are progress::Report's, which has no Qt in
    // it and is stated in UserInterface.spectable; these three are the painting
    // of it. In the status bar rather than in a window of its own, because a
    // window over the work hides what the work is about and because nothing is
    // being asked. advanceProgress returns whether to carry on, which is false
    // only once there is a way to stop -- see questions.md.
    void beginProgress(const std::string& what, long long total,
                       const std::string& unit = std::string());
    bool advanceProgress(long long done);
    void endProgress();

    progress::Report progress_;
    QElapsedTimer progressSince_;
    QLabel* progressText_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    bool progressShowing_ = false;

    // The other half of a transfer entered by hand, asked about from the
    // register. Separate from recordBlankLine because it is a question and a
    // merge rather than part of committing the line.
    void offerTheOtherHalf(const reg::Committed& done, const std::string& own);

    QLabel* status_ = nullptr;
};
