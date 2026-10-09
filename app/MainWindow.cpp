#include "MainWindow.h"

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
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QListWidget>
#include <QModelIndex>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <functional>
#include <memory>
#include <fstream>
#include <sstream>

#include "import_review.h"
#include "register_entry.h"
#include "report.h"
#include "report_ranges.h"
#include "payee_rules.h"
#include "qif_import.h"
#include "register_lines.h"
#include "register_view.h"

namespace {

QString money(const Money& m) { return QString::fromStdString(m.in_register()); }

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
    RuleForm(QWidget* parent, const QString& title, const payees::Rule& start)
        : QDialog(parent) {
        setWindowTitle(title);
        auto* form = new QFormLayout(this);

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
        form->addRow("Category", category_);

        enabled_ = new QCheckBox("Apply this rule");
        enabled_->setChecked(start.enabled);
        form->addRow(QString(), enabled_);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                             QDialogButtonBox::Cancel);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
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
// The richer search is specified in Accounts -- an alias standing for a
// category, and one unused for years dropping out of the list. This is the
// plain form of it until that is built.
class CategoryPick : public QDialog {
public:
    CategoryPick(QWidget* parent, const chart::Chart& accounts,
                 const std::string& start)
        : QDialog(parent) {
        setWindowTitle("Choose a category");
        resize(460, 420);
        auto* layout = new QVBoxLayout(this);

        typed_ = new QLineEdit(QString::fromStdString(start));
        typed_->setPlaceholderText("Type part of a category");
        typed_->selectAll();
        layout->addWidget(typed_);

        list_ = new QListWidget;
        layout->addWidget(list_, 1);

        for (const chart::Account& a : accounts.picker())
            every_ << QString::fromStdString(a.path.value());
        every_.sort();

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                             QDialogButtonBox::Cancel);
        ok_ = buttons->button(QDialogButtonBox::Ok);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        const auto filter = [this]() {
            const QString part = typed_->text().trimmed();
            list_->clear();
            for (const QString& one : every_)
                if (part.isEmpty() || one.contains(part, Qt::CaseInsensitive))
                    list_->addItem(one);
            // Whatever was typed, if it names a category exactly, is the
            // choice; otherwise the chosen line of the list is.
            for (int i = 0; i < list_->count(); ++i)
                if (list_->item(i)->text().compare(part, Qt::CaseInsensitive) == 0) {
                    list_->setCurrentRow(i);
                    return;
                }
            if (list_->count() > 0) list_->setCurrentRow(0);
        };
        connect(typed_, &QLineEdit::textChanged, this, filter);
        connect(list_, &QListWidget::currentRowChanged, this, [this](int) {
            ok_->setEnabled(list_->currentItem() != nullptr);
        });
        // Double-clicking a line is choosing it.
        connect(list_, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
        filter();
        ok_->setEnabled(list_->currentItem() != nullptr);
        typed_->setFocus();
    }

    // Empty when nothing was chosen, which the caller treats as a refusal.
    std::string chosen() const {
        return list_->currentItem() == nullptr
                   ? std::string()
                   : list_->currentItem()->text().toStdString();
    }

private:
    QLineEdit* typed_ = nullptr;
    QListWidget* list_ = nullptr;
    QStringList every_;
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

    panes_ = new QSplitter(Qt::Vertical);
    outer_ = new QSplitter(Qt::Horizontal);
    outer_->addWidget(accounts_);
    outer_->addWidget(panes_);
    outer_->setStretchFactor(0, 0);
    outer_->setStretchFactor(1, 1);
    setCentralWidget(outer_);

    status_ = new QLabel;
    statusBar()->addWidget(status_);

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
    for (PaneWidgets& p : paneWidgets_) {
        p.reg->setFont(appFont_);
        p.reg->verticalHeader()->setDefaultSectionSize(row);
        p.reviewTable->setFont(appFont_);
        p.reviewTable->verticalHeader()->setDefaultSectionSize(row);
        p.heading->setFont(bold);
    }
    accounts_->setFont(appFont_);
    if (status_) status_->setFont(appFont_);
    menuBar()->setFont(appFont_);
    // The account list is rebuilt rather than restyled, because its headings
    // carry a bold of their own that is derived from the font in force.
    if (workspace_) refreshAccounts();
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
    // A QIF is a one-time migration out of Quicken, not a way to open a book.
    file->addAction("Import &history from Quicken...", this,
                    &MainWindow::importHistory);
    file->addAction("Import QIF for &review...", this,
                    &MainWindow::importQifForReview);
    file->addSeparator();
    // The ordinary way transactions arrive: a download for one account, looked
    // over in the pane beside its register before it changes anything.
    file->addAction("&Import transactions...", this,
                    &MainWindow::importTransactions);
    file->addSeparator();
    file->addAction("E&xit", this, &QWidget::close);

    QMenu* accounts = menuBar()->addMenu("&Accounts");
    accounts->addAction("&New account...", this, &MainWindow::newAccount);
    accounts->addSeparator();
    accounts->addAction("Payee &rules...", this, &MainWindow::editRules);
    accounts->addSeparator();
    accounts->addAction("&Assign payees according to rules", this,
                        &MainWindow::assignPayeesAccordingToRules);

    QMenu* reports = menuBar()->addMenu("&Reports");
    reports->addAction("&Spending by category...", this, &MainWindow::showReport);

    QMenu* view = menuBar()->addMenu("&View");
    auto* split = view->addAction("&Split view");
    split->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(split, &QAction::triggered, this, &MainWindow::toggleSplit);
    view->addSeparator();
    auto* font = view->addAction("&Font...");
    connect(font, &QAction::triggered, this, &MainWindow::chooseFont);
    view->addSeparator();
    auto* hidden = view->addAction("Show &hidden accounts");
    hidden->setCheckable(true);
    connect(hidden, &QAction::triggered, this, &MainWindow::toggleHidden);
}

void MainWindow::rebuildWorkspace() {
    // Which account each pane was showing, so that it still is afterwards.
    // Rebuilding is how the workspace is handed the book again after it changes;
    // it is not a request to forget what was being read. Without this, recording
    // a transaction emptied the register it was recorded in.
    std::vector<std::string> showing;
    bool wasSplit = false;
    int active = 1;
    if (workspace_) {
        for (const ui::Pane& pane : workspace_->panes())
            showing.push_back(pane.account);
        wasSplit = workspace_->split_open();
        active = workspace_->active_pane();
    }

    workspace_ = std::make_unique<ui::Workspace>(chart_, book_, transactions_);

    if (showing.empty()) return;
    workspace_->select(showing.front());
    if (wasSplit) {
        // Splitting makes the new pane the active one, which is where the
        // second account goes.
        workspace_->split();
        if (showing.size() > 1) workspace_->select(showing[1]);
    }
    workspace_->make_active(active);
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
    rememberRecent(name);
    bookChanged(name);
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
    no = opened.read(&accounts, &ledger, &transactions, &rules);
    if (no.refused) {
        QMessageBox::warning(this, "SimpleAccount", QString::fromStdString(no.reason));
        return;
    }
    store_ = std::move(opened);
    chart_ = accounts;
    book_ = ledger;
    transactions_ = transactions;
    rules_ = rules;
    rememberRecent(name);
    bookChanged(name);
}

// A book is a file rather than a session, so anything that changes it writes it.
// Closing the window is not a way to lose a morning's entry.
bool MainWindow::save() {
    if (!store_.is_open()) return false;
    const store::Failure no = store_.write(chart_, transactions_, rules_);
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

// The path carries the tree, so a new account says where it belongs rather than
// being filed afterwards: Assets:Checking, not Checking. Every level above it
// that is missing is filled in as a placeholder; see the chart rules in Accounts.
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
    auto* asAt = new QDateEdit(QDate::currentDate(), &dialog);
    asAt->setCalendarPopup(true);
    asAt->setDisplayFormat("yyyy-MM-dd");

    auto* form = new QFormLayout(&dialog);
    form->addRow("Name", name);
    form->addRow("Type", type);
    form->addRow("Under", parent);
    form->addRow("Opening balance", opening);
    form->addRow("As at", asAt);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    name->setFocus();

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
            for (const ledger::DatedPosting& p : postings) {
                ledger::Posting posting;
                posting.account = p.account;
                posting.amount = p.amount;
                t.postings.push_back(posting);
                book_.add({p.date, p.account, p.amount, t.ref.value()});
            }
            transactions_.push_back(t);
        }
    }

    if (!save()) return;
    bookChanged(QString::fromStdString(wanted) + " added");
}

void MainWindow::importHistory() {
    if (!store_.is_open()) {
        QMessageBox::information(
            this, "SimpleAccount",
            "Open a book first, or make one with File > New book.\n\n"
            "A QIF export is history brought in once; it is not a book.");
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, "Import history from Quicken", {}, "Quicken export (*.qif *.QIF)");
    if (path.isEmpty()) return;
    importHistoryFrom(path);
}

void MainWindow::importHistoryFrom(const QString& path) {
    std::ifstream in(path.toStdString(), std::ios::binary);
    if (!in) {
        QMessageBox::warning(this, "SimpleAccount", "That file could not be read.");
        return;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    qif::Importer importer;
    const qif::Imported imported = importer.run(buffer.str());
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
void MainWindow::importQifForReview() {
    const QString path = QFileDialog::getOpenFileName(this, "Import a QIF file for review", {},
                                                      "Quicken export (*.qif *.QIF)");
    if (path.isEmpty()) return;
    std::ifstream in(path.toStdString(), std::ios::binary);
    if (!in) return;
    std::ostringstream buffer;
    buffer << in.rdbuf();

    qif::Importer importer;
    importer.start_from(chart_);
    importer.import_into(selectedAccount());
    const qif::Imported imported = importer.run(buffer.str());
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
        if (t.postings.size() != 2) continue;
        ui::ReviewRow row;
        row.line = ++line;
        row.date = t.date;
        row.payee = t.payee.value();
        row.account = t.postings[0].account.value();
        row.category = t.postings[1].account.value();
        row.amount = t.postings[0].amount;
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
    // A download is for one account, and which account is not in the file in any
    // form this program can trust: a QFX names the bank's own number for it and a
    // CSV usually names nothing. So the account is the one being read.
    const std::string account = selectedAccount();
    if (account.empty()) {
        QMessageBox::information(
            this, "SimpleAccount",
            "Select the account this download is for, then import into it.");
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QString("Import into %1").arg(QString::fromStdString(account)), {},
        "Downloaded transactions (*.qfx *.QFX *.ofx *.OFX *.csv *.CSV);;All files (*)");
    if (path.isEmpty()) return;
    importTransactionsFrom(path);
}

void MainWindow::importTransactionsFrom(const QString& path) {
    const std::string account = selectedAccount();
    if (account.empty()) return;
    std::ifstream in(path.toStdString(), std::ios::binary);
    if (!in) {
        QMessageBox::warning(this, "SimpleAccount",
                             QString("%1 could not be read").arg(path));
        return;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    // Everything about reading the file is production code with no Qt in it, so
    // what happens here is the same as what the command line does.
    chart::Chart accounts = chart_;
    const review::Prepared prepared = review::from_file(
        buffer.str(), account, transactions_, &accounts, rules_);
    if (prepared.refused) {
        QMessageBox::warning(this, "SimpleAccount",
                             QString::fromStdString(prepared.refusal));
        return;
    }
    if (prepared.rows.empty()) {
        QMessageBox::information(this, "SimpleAccount",
                                 "There is nothing in that file to review.");
        return;
    }

    // Categories the import had to invent are needed for the register to resolve
    // them once the rows are accepted.
    chart_ = accounts;
    // The pane keeps the account it was showing, so the review opens beside the
    // register it is about to change. That is the whole reason it is shown here
    // rather than in a dialog of its own.
    rebuildWorkspace();
    workspace_->review_import(prepared.rows, prepared.into);
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

void MainWindow::showReport() {
    if (!store_.is_open()) {
        QMessageBox::information(this, "SimpleAccount",
                                 "Open a book before reporting on it.");
        return;
    }

    QDialog box(this);
    box.setWindowTitle("Spending by category");
    box.resize(820, 640);
    auto* layout = new QVBoxLayout(&box);

    // The periods Quicken offers, by the names it uses, because the figures are
    // read against its reports. All Dates runs from the first posting to today.
    auto* period = new QComboBox;
    period->addItems({"YearToDate", "ThisYear", "LastYear", "ThisQuarter",
                      "ThisMonth", "MonthToDate", "LastMonth", "Last12Months",
                      "All"});
    auto* from = new QDateEdit;
    auto* to = new QDateEdit;
    for (QDateEdit* edit : {from, to}) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat("yyyy-MM-dd");
    }
    // How much of the tree to show. The three the report offers, in the order
    // they go from summary to everything. See the detail section of
    // Reports.spectable.
    auto* detail = new QComboBox;
    detail->addItem("Categories - highest only",
                    QString::fromStdString(reports::to_string(reports::Detail::HighestOnly)));
    detail->addItem("Categories - all",
                    QString::fromStdString(reports::to_string(reports::Detail::AllCategories)));
    detail->addItem("Transactions",
                    QString::fromStdString(reports::to_string(reports::Detail::Transactions)));
    detail->setCurrentIndex(1);

    auto* quicken = new QCheckBox("Quicken signs");
    quicken->setToolTip("Expenses negative, as Quicken writes them, so a figure "
                        "can be read straight against one of its reports.");
    auto* zero = new QCheckBox("Show empty categories");

    auto* chooser = new QHBoxLayout;
    chooser->addWidget(new QLabel("Period"));
    chooser->addWidget(period);
    chooser->addWidget(new QLabel("from"));
    chooser->addWidget(from);
    chooser->addWidget(new QLabel("to"));
    chooser->addWidget(to);
    chooser->addSpacing(12);
    chooser->addWidget(new QLabel("Show"));
    chooser->addWidget(detail);
    chooser->addStretch(1);
    chooser->addWidget(quicken);
    chooser->addWidget(zero);
    layout->addLayout(chooser);

    auto* table = new QTableWidget;
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table, 1);

    auto* totals = new QLabel;
    QFont bold = appFont_;
    bold.setBold(true);
    totals->setFont(bold);
    layout->addWidget(totals);

    auto* close = new QPushButton("&Close");
    close->setDefault(true);
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(close);
    layout->addLayout(row);

    // The earliest posting in the book, which is where All Dates starts.
    types::Date earliest = asDate(QDate::currentDate());
    for (const ledger::DatedPosting& one : book_.postings())
        if (one.date < earliest) earliest = one.date;

    // One step of indent per level, which is what makes a child read as part of
    // the line above it rather than as another category of its own.
    const auto indented = [](int level, const std::string& text) {
        return QString::fromStdString(
            std::string(static_cast<std::size_t>(level) * 2, ' ') + text);
    };

    // The lines the table is showing, kept so that the menu can turn a selected
    // row back into a transaction. The report itself is still built here rather
    // than in the workspace, which is why the click-through scenarios wait.
    auto shown = std::make_shared<std::vector<reports::DetailLine>>();

    const auto run = [&, shown]() {
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
                reports::detail_report(chart_, book_, transactions_, spec);
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
    };

    const auto takePeriod = [&]() {
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

    QObject::connect(period, &QComboBox::currentTextChanged, &box, takePeriod);
    // Typing a date of your own is not the same as asking for a named period, so
    // it runs the report without moving the period back.
    QObject::connect(from, &QDateEdit::dateChanged, &box, run);
    QObject::connect(to, &QDateEdit::dateChanged, &box, run);
    // One transaction selected can be opened; several can only be given a
    // category together. A category line is neither, and offers nothing.
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(table, &QTableWidget::customContextMenuRequested, &box,
                     [&, shown](const QPoint& at) {
        std::vector<std::string> refs;
        for (const QModelIndex& index : table->selectionModel()->selectedRows()) {
            const std::size_t row = static_cast<std::size_t>(index.row());
            if (row >= shown->size()) continue;
            const reports::DetailLine& line = (*shown)[row];
            if (line.kind != reports::LineKind::Transaction) continue;
            refs.push_back(line.ref);
        }
        if (refs.empty()) return;

        QMenu menu(&box);
        if (refs.size() == 1) {
            // A transfer is in two registers and neither is more right than the
            // other -- a payment to a card is in the current account's register
            // and in the card's -- so both are offered and the reader chooses.
            const std::vector<std::string> where =
                workspace_->registers_offered(refs.front());
            const auto open = [&, refs](const std::string& account) {
                if (!workspace_->go_to_transaction_in(refs.front(), account)) return;
                // The register is behind the report, so the report closes to
                // show what was asked for.
                refresh();
                box.accept();
            };
            if (where.size() == 1) {
                QAction* go = menu.addAction("Go to the transaction...");
                const std::string only = where.front();
                QObject::connect(go, &QAction::triggered, &box,
                                 [open, only]() { open(only); });
            } else if (where.size() > 1) {
                QMenu* go = menu.addMenu("Go to the transaction in");
                for (const std::string& account : where) {
                    QAction* one =
                        go->addAction(QString::fromStdString(account));
                    QObject::connect(one, &QAction::triggered, &box,
                                     [open, account]() { open(account); });
                }
            }
        }
        QAction* change = menu.addAction("Recategorize...");
        QObject::connect(change, &QAction::triggered, &box, [&, refs]() {
            CategoryPick pick(&box, chart_, std::string());
            if (pick.exec() != QDialog::Accepted) return;
            const std::string into = pick.chosen();
            if (into.empty()) return;
            const ui::Recategorised done = workspace_->recategorise(refs, into);
            if (done.changed > 0) {
                transactions_ = workspace_->transactions();
                book_ = workspace_->book();
                save();
                rebuildWorkspace();
                refresh();
                // The figures have moved, which is the one case where
                // recategorising changes what is on the page.
                run();
            }
            if (done.refused > 0 && !done.reason.empty())
                QMessageBox::information(&box, "Recategorize",
                                         QString::fromStdString(done.reason));
        });
        menu.exec(table->viewport()->mapToGlobal(at));
    });

    QObject::connect(detail, &QComboBox::currentIndexChanged, &box, run);
    QObject::connect(quicken, &QCheckBox::toggled, &box, run);
    QObject::connect(zero, &QCheckBox::toggled, &box, run);
    QObject::connect(close, &QPushButton::clicked, &box, &QDialog::accept);

    takePeriod();
    box.exec();
}

void MainWindow::assignPayeesAccordingToRules() {
    if (!store_.is_open()) {
        QMessageBox::information(this, "SimpleAccount", "Open a book first.");
        return;
    }
    const payees::BookApplication done =
        payees::apply_to_book(rules_, chart_, &transactions_);
    book_ = ledger::Ledger();
    for (const ledger::Transaction& t : transactions_)
        for (const ledger::Posting& p : t.postings)
            book_.add({t.date, p.account, p.amount, t.ref.value()});
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
        RuleForm form(&box, "Add a rule", payees::Rule{});
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
        RuleForm form(&box, "Edit a rule", was);
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

// The lines selected in a register, counting from one, in the order shown.
void MainWindow::registerMenu(int paneOneBased, const QPoint& at) {
    PaneWidgets& p = paneWidgets_[static_cast<std::size_t>(paneOneBased - 1)];
    const ui::Pane& state =
        workspace_->panes()[static_cast<std::size_t>(paneOneBased - 1)];
    if (state.showing != ui::PaneContent::Register) return;

    // The blank line at the foot is not a transaction and cannot be acted on.
    const int lines = p.reg->rowCount() - 1;
    std::vector<int> chosen;
    for (const QModelIndex& index : p.reg->selectionModel()->selectedRows())
        if (index.row() < lines) chosen.push_back(index.row() + 1);
    std::sort(chosen.begin(), chosen.end());
    if (chosen.empty()) return;

    // The pane acted on is the one clicked in, and the selection is the
    // workspace's rather than the table's from here on.
    workspace_->make_active(paneOneBased);
    workspace_->select_lines(chosen);

    QMenu menu(this);
    for (const ui::MenuEntry& entry : workspace_->menu_items()) {
        QAction* action = menu.addAction(QString::fromStdString(entry.item));
        action->setEnabled(entry.enabled);
        if (entry.item.rfind("Recategorize", 0) == 0)
            connect(action, &QAction::triggered, this,
                    [this, paneOneBased]() { recategorise(paneOneBased); });
        else if (entry.item.rfind("Delete", 0) == 0)
            connect(action, &QAction::triggered, this,
                    [this, paneOneBased]() { deleteSelection(paneOneBased); });
        else
            connect(action, &QAction::triggered, this,
                    [this, paneOneBased]() { addPayeeRuleFrom(paneOneBased); });
    }
    menu.exec(p.reg->viewport()->mapToGlobal(at));
}

void MainWindow::recategorise(int paneOneBased) {
    const std::vector<int>& chosen = workspace_->selected_lines();
    if (chosen.empty()) return;
    // Opened on the category the first selected line already has, which is
    // what makes correcting one a matter of a few keystrokes.
    const std::vector<reg::Line> lines = workspace_->register_lines(paneOneBased);
    std::string start;
    if (chosen.front() >= 1 && chosen.front() <= static_cast<int>(lines.size())) {
        start = lines[static_cast<std::size_t>(chosen.front() - 1)].category;
        if (start == "--Split--") start.clear();
    }

    CategoryPick pick(this, chart_, start);
    if (pick.exec() != QDialog::Accepted) return;
    const std::string into = pick.chosen();
    if (into.empty()) return;

    const ui::Recategorised done = workspace_->recategorise_selection(into);
    if (done.changed > 0) {
        transactions_ = workspace_->transactions();
        book_ = workspace_->book();
        save();
        rebuildWorkspace();
        refresh();
    }
    // Said out loud, because a selection of twenty is too many to check by eye.
    QString said = QString("%1 changed").arg(done.changed);
    if (done.refused > 0) {
        said += QString(", %1 left alone").arg(done.refused);
        if (!done.reason.empty())
            said += "\n\n" + QString::fromStdString(done.reason);
    }
    if (done.refused > 0) QMessageBox::information(this, "Recategorize", said);
    else status_->setText(said + "  --  " + QString::fromStdString(into));
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
    RuleForm form(this, "Add payee rule", offered);
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

void MainWindow::toggleSplit() {
    if (workspace_->split_open()) workspace_->close_split();
    else workspace_->split();
    refresh();
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
    // A book is a file rather than a session: an import that is not written is
    // an import that is lost when the window closes.
    save();
    refresh();
}

void MainWindow::cancelImport() {
    workspace_->cancel_import();
    refresh();
}

MainWindow::PaneWidgets MainWindow::makePane(int paneOneBased) {
    PaneWidgets p;
    p.stack = new QStackedWidget;
    p.blank = reg::blank_line_on(asDate(QDate::currentDate()));

    p.reg = new QTableWidget;
    p.reg->setColumnCount(8);
    p.reg->setHorizontalHeaderLabels(
        {"Date", "Num", "Payee", "Category", "Memo", "Payment", "Deposit", "Balance"});
    p.reg->verticalHeader()->setVisible(false);
    p.reg->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Typing is allowed, and only the blank line's cells carry the editable
    // flag, so the rest of a register stays read-only: a stray keypress must
    // not change a transaction from 2009.
    p.reg->setEditTriggers(QAbstractItemView::DoubleClicked |
                           QAbstractItemView::EditKeyPressed |
                           QAbstractItemView::AnyKeyPressed);
    p.reg->installEventFilter(new EnterRecords(p.reg, [this, paneOneBased]() {
        recordBlankLine(paneOneBased);
    }));
    // Shift and control already extend and add to the selection; this is what
    // offers to do something with it. See the register-menu section of
    // UserInterface.spectable.
    p.reg->setSelectionMode(QAbstractItemView::ExtendedSelection);
    p.reg->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(p.reg, &QTableWidget::customContextMenuRequested, this,
            [this, paneOneBased](const QPoint& at) { registerMenu(paneOneBased, at); });
    p.reg->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    p.reg->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    p.stack->addWidget(p.reg);

    p.review = new QWidget;
    auto* layout = new QVBoxLayout(p.review);
    p.heading = new QLabel;
    layout->addWidget(p.heading);

    p.reviewTable = new QTableWidget;
    p.reviewTable->setColumnCount(7);
    p.reviewTable->setHorizontalHeaderLabels(
        {"", "Date", "Payee", "Account", "Category", "Amount", "Import says"});
    p.reviewTable->verticalHeader()->setVisible(false);
    p.reviewTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    p.reviewTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    p.reviewTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    layout->addWidget(p.reviewTable);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* accept = new QPushButton("Accept ticked rows");
    auto* cancel = new QPushButton("Cancel");
    connect(accept, &QPushButton::clicked, this, &MainWindow::acceptImport);
    connect(cancel, &QPushButton::clicked, this, &MainWindow::cancelImport);
    buttons->addWidget(cancel);
    buttons->addWidget(accept);
    layout->addLayout(buttons);
    p.stack->addWidget(p.review);

    // A pane made after the font was chosen has to be brought into step with it:
    // the app font reaches the tables, but not the bold heading or the row
    // heights, which are worked out from the font in force.
    const QFontMetrics metrics(appFont_);
    const int rowHeight = metrics.height() + 8;
    p.reg->verticalHeader()->setDefaultSectionSize(rowHeight);
    p.reviewTable->verticalHeader()->setDefaultSectionSize(rowHeight);
    QFont headingFont = appFont_;
    headingFont.setBold(true);
    p.heading->setFont(headingFont);

    // A tick is the only thing editable in the review, and changing it tells the
    // workspace rather than keeping a second copy of the answer here.
    connect(p.reviewTable, &QTableWidget::itemChanged, this,
            [this, paneOneBased](QTableWidgetItem* item) {
                if (item->column() != 0 || refreshing_) return;
                const int line = item->data(Qt::UserRole).toInt();
                if (item->checkState() == Qt::Checked) workspace_->tick(line);
                else workspace_->untick(line);
                refreshPane(paneOneBased);
            });
    return p;
}

void MainWindow::refresh() {
    const bool was = refreshing_;
    refreshing_ = true;
    // One widget per pane the workspace says there is.
    while (static_cast<int>(paneWidgets_.size()) < workspace_->pane_count()) {
        PaneWidgets p = makePane(static_cast<int>(paneWidgets_.size()) + 1);
        panes_->addWidget(p.stack);
        paneWidgets_.push_back(p);
    }
    for (std::size_t i = 0; i < paneWidgets_.size(); ++i)
        paneWidgets_[i].stack->setVisible(static_cast<int>(i) < workspace_->pane_count());

    refreshAccounts();
    for (int pane = 1; pane <= workspace_->pane_count(); ++pane) refreshPane(pane);
    refreshing_ = was;
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

void MainWindow::refreshPane(int paneOneBased) {
    const bool was = refreshing_;
    refreshing_ = true;
    PaneWidgets& p = paneWidgets_[static_cast<std::size_t>(paneOneBased - 1)];
    const ui::Pane& state = workspace_->panes()[static_cast<std::size_t>(paneOneBased - 1)];

    // The active pane is the one the next selection goes to, so it is worth
    // seeing which it is.
    const bool active = workspace_->active_pane() == paneOneBased;
    p.stack->setStyleSheet(active && workspace_->split_open()
                               ? "QStackedWidget { border: 2px solid #4a76c8; }"
                               : "QStackedWidget { border: 2px solid transparent; }");

    if (state.showing == ui::PaneContent::ImportReview) {
        p.stack->setCurrentWidget(p.review);
        const std::vector<ui::ReviewRow>& rows = workspace_->review();
        p.heading->setText(QString("Import into %1  --  %2 rows, %3 ticked")
                               .arg(QString::fromStdString(state.account))
                               .arg(rows.size())
                               .arg(workspace_->accepted_count()));
        p.reviewTable->setRowCount(static_cast<int>(rows.size()));
        for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
            const ui::ReviewRow& row = rows[static_cast<std::size_t>(r)];
            auto* tick = new QTableWidgetItem;
            tick->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            tick->setCheckState(row.accepted ? Qt::Checked : Qt::Unchecked);
            tick->setData(Qt::UserRole, row.line);
            p.reviewTable->setItem(r, 0, tick);
            p.reviewTable->setItem(r, 1, text(row.date.iso()));
            p.reviewTable->setItem(r, 2, text(row.payee, row.payee));
            p.reviewTable->setItem(r, 3, text(row.account, row.account));
            p.reviewTable->setItem(r, 4, text(row.category, row.category));
            setAmount(p.reviewTable, r, 5, row.amount);
            p.reviewTable->setItem(r, 6, text(row.disposition));
        }
        p.reviewTable->resizeColumnsToContents();
        p.reviewTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        refreshing_ = was;
        return;
    }

    p.stack->setCurrentWidget(p.reg);
    const std::vector<reg::Line> lines = workspace_->register_lines(paneOneBased);
    // The two amount columns are headed with the words the account uses: a
    // credit card is charged and paid, not paid and deposited into.
    const chart::Account* a = state.account.empty() ? nullptr
                                                    : chart_.find(state.account);
    const reg::ColumnHeadings headings =
        reg::headings_for(a ? a->type : types::AccountType::Checking);
    p.reg->setHorizontalHeaderLabels({"Date", "Num", "Payee", "Category", "Memo",
                                      QString::fromStdString(headings.out_column),
                                      QString::fromStdString(headings.in_column),
                                      "Balance"});
    p.reg->setRowCount(static_cast<int>(lines.size()));
    for (int r = 0; r < static_cast<int>(lines.size()); ++r) {
        const reg::Line& line = lines[static_cast<std::size_t>(r)];
        p.reg->setItem(r, 0, text(line.date.iso()));
        p.reg->setItem(r, 1, text(line.check_no));
        p.reg->setItem(r, 2, text(line.payee, line.payee));
        p.reg->setItem(r, 3, text(line.category, line.category_detail));
        p.reg->setItem(r, 4, text(line.memo, line.memo));
        setAmount(p.reg, r, 5, line.payment);
        setAmount(p.reg, r, 6, line.deposit);
        setAmount(p.reg, r, 7, line.balance);
    }
    // The blank line: one more row at the end, which is where a transaction is
    // entered. It is not in the book and nothing is written until an amount is
    // typed -- see the entry section of TransactionRegister.spectable.
    const bool canEnter = !state.account.empty() && a != nullptr &&
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
        p.reg->setItem(at, 5, typed(line.payment.cents() == 0
                                        ? std::string() : line.payment.in_register()));
        p.reg->setItem(at, 6, typed(line.deposit.cents() == 0
                                        ? std::string() : line.deposit.in_register()));
        auto* balance = new QTableWidgetItem;
        balance->setFlags(Qt::ItemIsEnabled);
        p.reg->setItem(at, 7, balance);
        for (int c = 0; c <= 7; ++c)
            p.reg->item(at, c)->setBackground(QColor(248, 248, 230));
    }

    // Opening on the most recent transactions: twenty years is tens of thousands
    // of lines, and starting at the top would mean scrolling through 2005.
    if (!lines.empty()) p.reg->scrollToBottom();
    refreshing_ = was;
}

// What is on the blank line now, read back out of the table. Called before
// anything is done with it, so that a cell still being typed in is included.
void MainWindow::readBlankLine(int paneOneBased) {
    PaneWidgets& p = paneWidgets_[static_cast<std::size_t>(paneOneBased - 1)];
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
    p.blank.payment = moneyTyped(cell(5));
    p.blank.deposit = moneyTyped(cell(6));
}

void MainWindow::recordBlankLine(int paneOneBased) {
    if (refreshing_) return;
    const ui::Pane& state =
        workspace_->panes()[static_cast<std::size_t>(paneOneBased - 1)];
    if (state.showing != ui::PaneContent::Register || state.account.empty()) return;
    readBlankLine(paneOneBased);
    PaneWidgets& p = paneWidgets_[static_cast<std::size_t>(paneOneBased - 1)];

    const reg::Committed done = reg::commit(p.blank, state.account,
                                            transactions_.size(), &chart_);
    if (!done.committed) {
        // A line with no amount is a line nobody meant, and is not an error.
        // Anything else is, and is worth saying.
        if (!done.reason.empty() && done.reason != "nothing was entered")
            QMessageBox::warning(this, "SimpleAccount",
                                 QString::fromStdString(done.reason));
        return;
    }
    transactions_.push_back(done.transaction);
    for (const ledger::Posting& posting : done.transaction.postings)
        book_.add({done.transaction.date, posting.account, posting.amount,
                   done.transaction.ref.value()});
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
