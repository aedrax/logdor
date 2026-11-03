// logdor-tui: a terminal log viewer on the QtCore-only core.
// Ctrl+L or / goes to the filter. Type a filter and push Enter. Esc goes
// back to the table. F4 selects the format. Shift+F10, the Menu key or a
// right click opens the cell actions: copy, and add the cell value to the
// filter. Ctrl+C copies the selected cells. Tab moves the focus. Ctrl+Q
// quits.

#include "LogModel.h"
#include "Session.h"
#include "formatcatalog.h"

#include <logdor/ColumnScan.h>
#include <logdor/FormatRegistry.h>
#include <logdor/LineIndexer.h>
#include <logdor/Query.h>
#include <logdor/Version.h>

#include <QtTUI/QtTUI>

#include <QFileInfo>
#include <QFutureWatcher>
#include <QStringListModel>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <utility>

using namespace QtTUI;

namespace {

// Gives each key event to a function first. The function returns true when
// it uses the key.
struct KeyFilter : QObject {
    std::function<bool(TuiKeyEvent*)> handle;
    bool eventFilter(QObject*, QEvent* ev) override
    {
        return ev->type() == TuiEventTypes::keyEventType()
            && handle(static_cast<TuiKeyEvent*>(ev));
    }
};

enum FilterMode { TextMode, RegexMode, QueryMode };
const QStringList kModeNames = {QStringLiteral("Text"), QStringLiteral("Regex"),
                                QStringLiteral("Query")};

// A field for a context line count. QTuiSpinBox has a 3-row frame; this
// field uses 1 row.
struct CountEdit : QTuiLineEdit {
    explicit CountEdit(QTuiWidget* parent) : QTuiLineEdit(parent)
    {
        auto* validator = new QTuiIntValidator(this);
        validator->setRange(0, 999);
        setValidator(validator);
        setText(QStringLiteral("0"));
    }
    QSize minimumSize() const override { return QSize(4, 1); }
};

// A dialog that is large enough for the list of formats.
struct PickerDialog : QTuiDialog {
    using QTuiDialog::QTuiDialog;
    QSize minimumSize() const override { return QSize(48, 22); }
};

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::fprintf(stderr, "Usage: logdor-tui <file>\n");
        return 2;
    }
    // QTuiApplication puts the terminal in raw mode when it starts. Open the
    // file before that, so errors print normally.
    std::shared_ptr<logdor::FileSource> source;
    {
        QCoreApplication probe(argc, argv);
        QString error;
        source = logdor::FileSource::open(QCoreApplication::arguments().at(1), &error);
        if (!source) {
            std::fprintf(stderr, "logdor-tui: %s\n", qPrintable(error));
            return 1;
        }
    }

    QTuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("logdor"));

    QTuiMainWindow window;
    app.setMainWindow(&window);
    // A window with a menu bar puts its other widgets in the central widget.
    auto* central = new QTuiWidget;
    window.setCentralWidget(central);
    auto* menuBar = new QTuiMenuBar;
    window.setMenuBar(menuBar);

    // Row 1: the filter text. Row 2: the filter options, as in the GUI
    // filter bar, and the format. All the controls use 1 row.
    auto* filter = new QTuiLineEdit(central);
    filter->setPlaceholderText(QStringLiteral("Filter (Ctrl+L or /, Enter to apply)"));
    auto* optionRow = new QTuiWidget(central);
    auto* optionLayout = createHBoxLayout(optionRow);
    optionLayout->setSpacing(1);
    auto* modeButton = new QTuiToolButton(QString(), optionRow);
    auto* modeMenu = new QTuiMenu(QString(), &window);
    modeButton->setMenu(modeMenu);
    auto* caseSensitive = new QTuiCheckBox(QStringLiteral("Case"), optionRow);
    auto* invert = new QTuiCheckBox(QStringLiteral("Invert"), optionRow);
    auto* before = new CountEdit(optionRow);
    auto* after = new CountEdit(optionRow);
    auto* formatButton = new QTuiToolButton(QString(), optionRow);
    optionLayout->addWidget(modeButton, 0);
    optionLayout->addWidget(caseSensitive, 0);
    optionLayout->addWidget(invert, 0);
    optionLayout->addWidget(new QTuiLabel(QStringLiteral("Before"), optionRow), 0);
    optionLayout->addWidget(before, 0);
    optionLayout->addWidget(new QTuiLabel(QStringLiteral(" After"), optionRow), 0);
    optionLayout->addWidget(after, 0);
    optionLayout->addStretch(1);
    optionLayout->addWidget(formatButton, 0);
    optionRow->setLayout(optionLayout);

    auto* table = new QTuiTableView(central);
    auto* progress = new QTuiProgressBar(central);
    progress->setRange(0, 1000); // the core reports progress in permille
    auto* status = new QTuiLabel(QString(), central);

    auto* layout = createVBoxLayout(central);
    layout->addWidget(filter, 0);
    layout->addWidget(optionRow, 0);
    layout->addWidget(table, 1);
    layout->addWidget(progress, 0);
    layout->addWidget(status, 0);
    central->setLayout(layout);

    const QString name = QFileInfo(source->filePath()).fileName();
    std::shared_ptr<const logdor::LineIndex> index;
    QList<std::shared_ptr<const logdor::FormatParser>> formats; // the order of the picker
    std::shared_ptr<const logdor::FormatParser> detected;
    std::shared_ptr<const logdor::FormatParser> parser;
    LogModel* model = nullptr;
    logdor::ColumnCache columns; // the parsed columns that queries use
    std::shared_ptr<const logdor::CompiledQuery> query;
    int filterMode = TextMode;
    bool dialogOpen = false; // a dialog gets all the keys
    QFutureWatcher<logdor::IndexingResult> indexWatcher;
    QFutureWatcher<logdor::ColumnScanResult> extractWatcher;
    QFutureWatcher<logdor::FilterScanResult> filterWatcher;
    const QList<QFutureWatcherBase*> watchers{&indexWatcher, &extractWatcher, &filterWatcher};
    for (QFutureWatcherBase* w : watchers)
        QObject::connect(w, &QFutureWatcherBase::progressValueChanged, progress, &QTuiProgressBar::setValue);

    auto showStatus = [&](const QString& state) {
        QString text = name;
        if (parser)
            text += QStringLiteral(" | ") + parser->displayName();
        if (model) {
            text += QStringLiteral(" | %1 of %2 lines")
                        .arg(model->rowCount())
                        .arg(model->lineCount());
        }
        if (!state.isEmpty())
            text += QStringLiteral(" | ") + state;
        status->setText(text + QStringLiteral(" | Shift+F10 actions | Ctrl+C copy | Ctrl+Q quit"));
    };
    auto showProgress = [&](bool busy) {
        progress->setValue(0);
        progress->setVisible(busy);
    };

    auto startScan = [&] {
        logdor::LineFilter f;
        if (query) {
            f.fieldQuery = query;
            f.columns = columns.snapshot(query->referencedColumns(), query->needsSeverity());
        } else if (filterMode != QueryMode) {
            f.query = filter->text();
            f.regexMode = filterMode == RegexMode;
        }
        f.caseSensitive = caseSensitive->isChecked();
        f.invert = invert->isChecked();
        f.contextBefore = before->text().toInt();
        f.contextAfter = after->text().toInt();
        filterWatcher.cancel();
        showProgress(true);
        showStatus(QStringLiteral("Filtering"));
        filterWatcher.setFuture(model->scan(std::move(f)));
    };

    // Each new filter cancels the work that runs. A query first reads the
    // columns it uses, then scans.
    auto applyFilter = [&] {
        if (!model)
            return;
        extractWatcher.cancel();
        query.reset();
        if (filterMode == QueryMode && !filter->text().isEmpty()) {
            logdor::QueryError error;
            query = logdor::CompiledQuery::compile(
                filter->text(), parser->schema(),
                caseSensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive,
                {}, &error);
            if (!query) {
                // Keep the rows that show.
                showStatus(QStringLiteral("Query error: ") + error.message);
                return;
            }
            const QList<int> missing = columns.missing(query->referencedColumns());
            const bool wantSeverity = query->needsSeverity() && !columns.hasSeverity();
            if (!missing.isEmpty() || wantSeverity) {
                filterWatcher.cancel();
                showProgress(true);
                showStatus(QStringLiteral("Reading columns"));
                extractWatcher.setFuture(
                    logdor::extractColumns(source, index, parser, missing, wantSeverity));
                return;
            }
        }
        startScan();
    };

    // Show the file in a format. A new format has new columns, so it gets a
    // new model.
    auto applyFormat = [&](std::shared_ptr<const logdor::FormatParser> p) {
        parser = std::move(p);
        formatButton->setText(QStringLiteral("Format: %1%2 (F4)")
                                  .arg(parser->displayName(),
                                       parser == detected ? QStringLiteral(", auto") : QString()));
        optionLayout->invalidate();
        columns.clear();
        LogModel* old = model;
        model = new LogModel(source, index, parser, &window);
        table->setModel(model);
        delete old;
        // Fit the short columns to their contents and give the free width
        // to the message column.
        QTuiHeaderView* header = table->horizontalHeader();
        const auto schema = parser->schema();
        for (int c = 0; c < schema.size(); ++c) {
            header->setSectionResizeMode(c, schema[c].hint == logdor::FieldHint::Message
                                                ? QTuiHeaderView::Stretch
                                                : QTuiHeaderView::ResizeToContents);
        }
        applyFilter();
    };

    QObject::connect(&indexWatcher, &QFutureWatcherBase::finished, [&] {
        if (indexWatcher.isCanceled())
            return;
        index = indexWatcher.result().index;
        formats = loadAllParsers() + logdor::fileDerivedParsers(*source, *index);
        const auto scores = logdor::detectFormat(*source, *index, formats);
        detected = logdor::parserById(
            scores.isEmpty() ? QStringLiteral("plaintext") : scores.front().parserId, formats);
        // The picker shows the formats by name, as in the GUI.
        std::stable_sort(formats.begin(), formats.end(), [](const auto& a, const auto& b) {
            return QString::compare(a->displayName(), b->displayName(), Qt::CaseInsensitive) < 0;
        });
        applyFormat(detected);
    });

    // The format picker: "Auto-detect" and then all the formats. Enter or a
    // double click selects, Esc cancels.
    auto pickFormat = [&] {
        if (!index || dialogOpen)
            return;
        QStringList names{QStringLiteral("Auto-detect (%1)").arg(detected->displayName())};
        for (const auto& p : formats)
            names.append(p->displayName());
        QStringListModel list(names);
        PickerDialog dialog(&window);
        dialog.setWindowTitle(QStringLiteral("Format"));
        auto* view = new QTuiListView(&dialog);
        view->setModel(&list);
        const int current = parser == detected ? 0 : int(formats.indexOf(parser)) + 1;
        view->setCurrentIndex(list.index(qMax(0, current)));
        QObject::connect(view, &QTuiAbstractItemView::activated, &dialog, &QTuiDialog::accept);
        QObject::connect(view, &QTuiAbstractItemView::doubleClicked, &dialog, &QTuiDialog::accept);
        auto* dialogLayout = createVBoxLayout();
        dialogLayout->addWidget(view, 1);
        dialog.setLayout(dialogLayout);
        view->setFocus();
        dialogOpen = true;
        const bool accepted = dialog.exec() == QTuiDialog::Accepted;
        dialogOpen = false;
        const int row = view->currentIndex().row();
        if (accepted && row >= 0)
            applyFormat(row == 0 ? detected : formats.value(row - 1, detected));
        table->setFocus();
    };
    QObject::connect(formatButton, &QTuiAbstractButton::clicked, pickFormat);

    // Help > About. F10 or Alt+H opens the menu.
    QObject::connect(menuBar->addMenu(QStringLiteral("&Help"))->addAction(QStringLiteral("About")),
                     &QTuiAction::triggered, [&] {
                         dialogOpen = true;
                         // No icon: information() puts "ℹ" before the text.
                         QTuiMessageBox box(QTuiMessageBox::NoIcon, QStringLiteral("About Logdor"),
                                            logdor::aboutText(), QTuiMessageBox::Ok, &window);
                         box.exec();
                         dialogOpen = false;
                     });

    // The mode menu.
    auto setMode = [&](int m) {
        filterMode = m;
        modeButton->setText(QStringLiteral("Mode: ") + kModeNames.at(m));
        optionLayout->invalidate();
    };
    for (int m = 0; m < kModeNames.size(); ++m) {
        QObject::connect(modeMenu->addAction(kModeNames.at(m)), &QTuiAction::triggered, [&, m] {
            setMode(m);
            applyFilter();
        });
    }
    QObject::connect(modeButton, &QTuiAbstractButton::clicked, [&] {
        modeMenu->popup(modeButton->geometry().bottomLeft() + QPoint(0, 1));
    });
    formatButton->setText(QStringLiteral("Format: ... (F4)"));

    // Restore the last filter of this file, from the GUI or the TUI. This
    // runs before the option signals connect, so it starts no scan.
    const QString sessionsPath = session::defaultPath();
    const QJsonObject saved = session::loadFilter(sessionsPath, *source);
    filter->setText(saved.value(QStringLiteral("query")).toString());
    setMode(saved.value(QStringLiteral("queryMode")).toBool()   ? QueryMode
            : saved.value(QStringLiteral("regexMode")).toBool() ? RegexMode
                                                                : TextMode);
    caseSensitive->setChecked(saved.value(QStringLiteral("caseSensitive")).toBool());
    invert->setChecked(saved.value(QStringLiteral("invert")).toBool());
    before->setText(QString::number(saved.value(QStringLiteral("before")).toInt()));
    after->setText(QString::number(saved.value(QStringLiteral("after")).toInt()));

    QObject::connect(&extractWatcher, &QFutureWatcherBase::finished, [&] {
        if (extractWatcher.isCanceled())
            return;
        columns.insert(extractWatcher.result());
        startScan();
    });

    QObject::connect(&filterWatcher, &QFutureWatcherBase::finished, [&] {
        if (filterWatcher.isCanceled())
            return;
        model->setRows(filterWatcher.result().rows);
        showProgress(false);
        // "tag:keyv2" in Text mode looks for that literal text. If nothing
        // matches and the text is a query on the fields of this format,
        // tell the user about Query mode.
        QString hint;
        if (filterMode != QueryMode && model->rowCount() == 0 && !filter->text().isEmpty()) {
            const auto asQuery = logdor::CompiledQuery::compile(
                filter->text(), parser->schema(), Qt::CaseInsensitive);
            if (asQuery && !asQuery->referencedColumns().isEmpty())
                hint = QStringLiteral("No match as text. Set Mode: Query for a field query");
        }
        showStatus(hint);
    });

    // Add a query term to the filter and switch to Query mode, as the GUI
    // filter bar does. A text filter becomes a quoted free-text term, so it
    // still applies. A regex has no query form, so the term replaces it.
    auto appendTerm = [&](const QString& term) {
        QString text = filter->text().trimmed();
        if (filterMode == QueryMode)
            text = text.isEmpty() ? term : text + u' ' + term; // implicit AND
        else if (filterMode == TextMode && !text.isEmpty())
            text = logdor::quoteQueryValue(text, /*forceQuote=*/true) + u' ' + term;
        else
            text = term;
        filter->setText(text);
        setMode(QueryMode);
        applyFilter();
    };

    // Copy the selected cells, or the current cell if no cell is selected.
    // Cells of a row are separated by a tab and rows by a newline, as in a
    // spreadsheet. QtTUI sends the text to the terminal clipboard (OSC 52).
    // A large selection does not copy: a list of its cells is slow, and
    // most terminals have an OSC 52 limit below 1 MB.
    constexpr qint64 kMaxCopyCells = 100000;
    auto copyCells = [&] {
        if (!model)
            return;
        qint64 selected = 0;
        for (const QItemSelectionRange& range : table->selectionModel()->selection())
            selected += qint64(range.width()) * range.height();
        if (selected > kMaxCopyCells) {
            showStatus(QStringLiteral("Too many cells to copy (%1, limit %2)")
                           .arg(selected).arg(kMaxCopyCells));
            return;
        }
        QModelIndexList cells = table->selectionModel()->selectedIndexes();
        if (cells.isEmpty() && table->currentIndex().isValid())
            cells = {table->currentIndex()};
        if (cells.isEmpty())
            return;
        std::sort(cells.begin(), cells.end()); // by row, then by column
        QString text;
        for (int i = 0; i < cells.size(); ++i) {
            if (i > 0)
                text += cells[i].row() != cells[i - 1].row() ? u'\n' : u'\t';
            text += cells[i].data().toString();
        }
        QTuiApplication::clipboard()->setText(text);
        showStatus(cells.size() == 1 ? QStringLiteral("Copied 1 cell")
                                     : QStringLiteral("Copied %1 cells").arg(cells.size()));
    };

    // The cell actions menu: a right click, Shift+F10 or the Menu key. Copy,
    // and the same filter actions as the GUI.
    bool menuFromKeyboard = false;
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    // Shift+arrows and Shift+click select a range of cells to copy.
    table->setSelectionMode(QTuiAbstractItemView::ExtendedSelection);
    QObject::connect(table, &QTuiWidget::customContextMenuRequested, [&](const QPoint& pos) {
        if (!model)
            return;
        // A key asks for the menu of the current cell. A click asks for the
        // menu of the cell under the mouse.
        QModelIndex cell = table->currentIndex();
        const QPoint at = table->geometry().topLeft() + pos;
        if (!std::exchange(menuFromKeyboard, false)) {
            const int rows = table->geometry().height() - 1; // without the header
            for (int r = table->scrollOffset(); r < qMin(model->rowCount(), table->scrollOffset() + rows); ++r) {
                for (int c = 0; c < model->columnCount(); ++c) {
                    if (table->visualRect(model->index(r, c)).contains(at))
                        cell = model->index(r, c);
                }
            }
            // A click in the selection keeps the selection, so that Copy
            // copies all of it.
            if (!table->selectionModel()->isSelected(cell))
                table->setCurrentIndex(cell);
        }
        if (!cell.isValid())
            return;
        const logdor::FieldSchema field = parser->schema().value(cell.column());
        const QString value = cell.data().toString();
        const QString include = logdor::buildQueryTerm(field, value, false);
        const QString exclude = logdor::buildQueryTerm(field, value, true);

        auto* menu = new QTuiMenu(QString(), &window);
        QObject::connect(menu, &QTuiMenu::aboutToHide, menu, &QObject::deleteLater);
        QObject::connect(menu->addAction(QStringLiteral("Copy"), QStringLiteral("Ctrl+C")),
                         &QTuiAction::triggered, copyCells);
        const auto label = [](QString term) {
            term = term.simplified();
            return term.size() > 48 ? term.left(48) + QStringLiteral("...") : term;
        };
        const auto addAction = [&](const QString& text, const QString& term) {
            QObject::connect(menu->addAction(text), &QTuiAction::triggered,
                             [&appendTerm, term] { appendTerm(term); });
        };
        // No filter actions if the value has no query term, for example text
        // in a number field.
        if (!include.isEmpty()) {
            menu->addSeparator();
            addAction(QStringLiteral("Filter: ") + label(include), include);
            addAction(QStringLiteral("Exclude: ") + label(exclude), exclude);
            // Time cells: a range from the time of this row.
            if (field.type == logdor::FieldType::DateTime) {
                const QString after = logdor::buildQueryTerm(field, value, logdor::QueryCmp::Ge);
                if (!after.isEmpty())
                    addAction(QStringLiteral("Filter: at or after this time"), after);
                const QString before = logdor::buildQueryTerm(field, value, logdor::QueryCmp::Le);
                if (!before.isEmpty())
                    addAction(QStringLiteral("Filter: at or before this time"), before);
            }
        }
        const QRect cellRect = table->visualRect(cell);
        menu->popup(cellRect.isValid() ? cellRect.bottomLeft() + QPoint(0, 1) : at);
    });

    // The text applies on Enter. The options apply when they change.
    QObject::connect(filter, &QTuiLineEdit::returnPressed, [&] {
        applyFilter();
        table->setFocus();
    });
    QObject::connect(caseSensitive, &QTuiAbstractButton::toggled, applyFilter);
    QObject::connect(invert, &QTuiAbstractButton::toggled, applyFilter);
    QObject::connect(before, &QTuiLineEdit::textChanged, applyFilter);
    QObject::connect(after, &QTuiLineEdit::textChanged, applyFilter);

    // Save the filter, then stop the worker threads before main() returns.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
        session::saveFilter(sessionsPath, *source,
                            {{QStringLiteral("query"), filter->text()},
                             {QStringLiteral("queryMode"), filterMode == QueryMode},
                             {QStringLiteral("regexMode"), filterMode == RegexMode},
                             {QStringLiteral("caseSensitive"), caseSensitive->isChecked()},
                             {QStringLiteral("invert"), invert->isChecked()},
                             {QStringLiteral("before"), before->text().toInt()},
                             {QStringLiteral("after"), after->text().toInt()}});
        for (QFutureWatcherBase* w : watchers)
            w->cancel();
        for (QFutureWatcherBase* w : watchers)
            w->waitForFinished();
    });

    auto* keys = new KeyFilter;
    keys->setParent(&app);
    keys->handle = [&](TuiKeyEvent* ke) {
        // A dialog gets all its keys.
        if (dialogOpen)
            return false;
        const bool ctrl = ke->modifiers() & Qt::ControlModifier;
        // Let the context menu handler know that a key, not a click, asks
        // for the menu. The key still goes to the window.
        if (ke->key() == Qt::Key_Menu
            || (ke->key() == Qt::Key_F10 && (ke->modifiers() & Qt::ShiftModifier)))
            menuFromKeyboard = true;
        if (ctrl && ke->key() == Qt::Key_Q) {
            QCoreApplication::quit();
            return true;
        }
        // Ctrl+L as in the GUI, and / as in less, when no text field has
        // the focus.
        QTuiWidget* focus = QTuiWidget::focusWidget();
        const bool typing = focus && focus->acceptsTextInput();
        if ((ctrl && ke->key() == Qt::Key_L) || (!typing && ke->key() == Qt::Key_Slash)) {
            filter->setFocus();
            filter->selectAll();
            return true;
        }
        // Ctrl+C copies cells only in the table. The text fields keep their
        // own Ctrl+C.
        if (ctrl && ke->key() == Qt::Key_C && focus == table) {
            copyCells();
            return true;
        }
        if (ke->key() == Qt::Key_F4) {
            pickFormat();
            return true;
        }
        if (focus == filter && ke->key() == Qt::Key_Escape) {
            table->setFocus();
            return true;
        }
        return false;
    };
    app.installEventFilter(keys);

    showProgress(true);
    showStatus(QStringLiteral("Indexing"));
    indexWatcher.setFuture(logdor::buildLineIndex(source));

    table->setFocus();
    window.show();
    app.triggerRender();
    return app.exec();
}
