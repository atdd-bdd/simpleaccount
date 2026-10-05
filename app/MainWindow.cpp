#include "MainWindow.h"

#include <QAction>
#include <QApplication>
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
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <fstream>
#include <sstream>

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
    file->addSeparator();
    // A QIF is a one-time migration out of Quicken, not a way to open a book.
    file->addAction("Import &history from Quicken...", this,
                    &MainWindow::importHistory);
    file->addAction("Import QIF for &review...", this,
                    &MainWindow::importQifForReview);
    file->addSeparator();
    file->addAction("E&xit", this, &QWidget::close);

    QMenu* accounts = menuBar()->addMenu("&Accounts");
    accounts->addAction("&New account...", this, &MainWindow::newAccount);

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
    workspace_ = std::make_unique<ui::Workspace>(chart_, book_, transactions_);
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
        store::Book::open(store::path_for(name.toStdString()), &opened);
    if (no.refused) {
        QMessageBox::warning(this, "SimpleAccount", QString::fromStdString(no.reason));
        return;
    }
    chart::Chart accounts;
    ledger::Ledger ledger;
    std::vector<ledger::Transaction> transactions;
    no = opened.read(&accounts, &ledger, &transactions);
    if (no.refused) {
        QMessageBox::warning(this, "SimpleAccount", QString::fromStdString(no.reason));
        return;
    }
    store_ = std::move(opened);
    chart_ = accounts;
    book_ = ledger;
    transactions_ = transactions;
    bookChanged(name);
}

// A book is a file rather than a session, so anything that changes it writes it.
// Closing the window is not a way to lose a morning's entry.
bool MainWindow::save() {
    if (!store_.is_open()) return false;
    const store::Failure no = store_.write(chart_, transactions_);
    if (no.refused) {
        QMessageBox::warning(this, "SimpleAccount", QString::fromStdString(no.reason));
        return false;
    }
    return true;
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

    auto* path = new QLineEdit(&dialog);
    path->setPlaceholderText("Assets:Checking");
    // Pre-filled from whatever is selected, because a new account is usually a
    // sibling or a child of the one being looked at.
    const std::string chosen = selectedAccount();
    if (!chosen.empty())
        path->setText(QString::fromStdString(chosen) + ":");

    auto* type = new QComboBox(&dialog);
    for (const char* name : {"Bank", "Cash", "CreditCard", "Asset", "Liability",
                             "Investment", "Income", "Expense", "Equity"})
        type->addItem(name);

    auto* opening = new QLineEdit(&dialog);
    opening->setPlaceholderText("0.00");
    auto* asAt = new QDateEdit(QDate::currentDate(), &dialog);
    asAt->setCalendarPopup(true);
    asAt->setDisplayFormat("yyyy-MM-dd");

    auto* form = new QFormLayout(&dialog);
    form->addRow("Path", path);
    form->addRow("Type", type);
    form->addRow("Opening balance", opening);
    form->addRow("As at", asAt);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    path->setFocus();

    if (dialog.exec() != QDialog::Accepted) return;

    const std::string wanted = path->text().trimmed().toStdString();
    if (wanted.empty()) return;
    const types::AccountType kind =
        types::account_type_from_string(type->currentText().toStdString());

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
    refresh();
}

void MainWindow::cancelImport() {
    workspace_->cancel_import();
    refresh();
}

MainWindow::PaneWidgets MainWindow::makePane(int paneOneBased) {
    PaneWidgets p;
    p.stack = new QStackedWidget;

    p.reg = new QTableWidget;
    p.reg->setColumnCount(8);
    p.reg->setHorizontalHeaderLabels(
        {"Date", "Num", "Payee", "Category", "Memo", "Payment", "Deposit", "Balance"});
    p.reg->verticalHeader()->setVisible(false);
    p.reg->setSelectionBehavior(QAbstractItemView::SelectRows);
    p.reg->setEditTriggers(QAbstractItemView::NoEditTriggers);
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
            item->setText(0, QString::fromStdString(row.group));
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
        reg::headings_for(a ? a->type : types::AccountType::Bank);
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
    // Opening on the most recent transactions: twenty years is tens of thousands
    // of lines, and starting at the top would mean scrolling through 2005.
    if (!lines.empty()) p.reg->scrollToBottom();
    refreshing_ = was;
}
