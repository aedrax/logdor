#include "filtertoolbar.h"

#include <logdor/Query.h>

#include <QCheckBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace {

constexpr int kDebounceMs = 300;

} // namespace

FilterToolbar::FilterToolbar(QWidget* parent)
    : QWidget(parent)
    , m_input(new QLineEdit(this))
    , m_caseSensitiveButton(new QPushButton(tr("Aa"), this))
    , m_invertButton(new QPushButton(tr("!"), this))
    , m_queryModeButton(new QPushButton(tr("Q"), this))
    , m_regexModeButton(new QPushButton(tr(".*"), this))
    , m_beforeSpinBox(new QSpinBox(this))
    , m_afterSpinBox(new QSpinBox(this))
    , m_debounce(new QTimer(this))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(4);

    layout->addWidget(new QLabel(tr("Filter:"), this));
    m_input->setPlaceholderText(tr("Enter filter text..."));
    m_input->setMinimumWidth(140);
    layout->addWidget(m_input, /*stretch*/ 1);

    const auto setupToggleButton = [](QPushButton* button,
                                      const QString& tooltip) {
        button->setCheckable(true);
        button->setFlat(false);
        button->setFixedSize(32, 24);
        button->setToolTip(tooltip);
        button->setStyleSheet(
            "QPushButton {"
            "  border: 1px solid #777777;"
            "  padding: 2px;"
            "  border-radius: 3px;"
            "}"
            "QPushButton:checked {"
            "  background-color: #0e639c;"
            "  border: 1px solid #1177bb;"
            "  font-weight: bold;"
            "}"
            "QPushButton:hover:!checked {"
            "  background-color: #3e3e3e;"
            "  border: 1px solid #999999;"
            "}");
    };
    setupToggleButton(m_caseSensitiveButton,
                      tr("Toggle case sensitive filtering"));
    layout->addWidget(m_caseSensitiveButton);
    setupToggleButton(m_invertButton,
                      tr("Show lines that don't match the filter"));
    layout->addWidget(m_invertButton);
    setupToggleButton(m_regexModeButton,
                      tr("Treat filter as a regular expression"));
    layout->addWidget(m_regexModeButton);
    setupToggleButton(
        m_queryModeButton,
        tr("Field query mode: level:error tag:Wifi* pid>=100 \"free text\""));
    layout->addWidget(m_queryModeButton);

    // Time-range picker: a popup with From/To datetime edits that injects
    // @time>= / @time<= query terms - @time resolves to each viewer's own
    // timestamp column.
    auto* timeRangeButton = new QPushButton(tr("Time"), this);
    timeRangeButton->setFlat(false);
    timeRangeButton->setFixedSize(44, 24);
    timeRangeButton->setToolTip(
        tr("Filter by time range (adds @time terms to the query)"));
    layout->addWidget(timeRangeButton);

    auto* timeMenu = new QMenu(this);
    auto* timePanel = new QWidget(timeMenu);
    auto* timeGrid = new QGridLayout(timePanel);
    timeGrid->setContentsMargins(8, 8, 8, 8);
    auto* fromCheck = new QCheckBox(tr("From"), timePanel);
    auto* fromEdit = new QDateTimeEdit(timePanel);
    auto* toCheck = new QCheckBox(tr("To"), timePanel);
    auto* toEdit = new QDateTimeEdit(timePanel);
    for (QDateTimeEdit* edit : { fromEdit, toEdit }) {
        edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        edit->setCalendarPopup(true);
        edit->setEnabled(false);
    }
    connect(fromCheck, &QCheckBox::toggled, fromEdit, &QWidget::setEnabled);
    connect(toCheck, &QCheckBox::toggled, toEdit, &QWidget::setEnabled);
    auto* applyButton = new QPushButton(tr("Apply"), timePanel);
    auto* clearButton = new QPushButton(tr("Clear"), timePanel);
    timeGrid->addWidget(fromCheck, 0, 0);
    timeGrid->addWidget(fromEdit, 0, 1);
    timeGrid->addWidget(toCheck, 1, 0);
    timeGrid->addWidget(toEdit, 1, 1);
    timeGrid->addWidget(clearButton, 2, 0);
    timeGrid->addWidget(applyButton, 2, 1);
    auto* timeAction = new QWidgetAction(timeMenu);
    timeAction->setDefaultWidget(timePanel);
    timeMenu->addAction(timeAction);

    connect(timeRangeButton, &QPushButton::clicked, this,
            [timeMenu, timeRangeButton, fromCheck, toCheck, fromEdit,
             toEdit]() {
                if (!fromCheck->isChecked() && !toCheck->isChecked()) {
                    // Fresh defaults: today so far.
                    const QDateTime now = QDateTime::currentDateTime();
                    fromEdit->setDateTime(QDateTime(now.date(), QTime(0, 0)));
                    toEdit->setDateTime(now);
                }
                timeMenu->popup(timeRangeButton->mapToGlobal(
                    QPoint(0, timeRangeButton->height())));
            });
    connect(applyButton, &QPushButton::clicked, this,
            [this, timeMenu, fromCheck, toCheck, fromEdit, toEdit]() {
                const QString format = QStringLiteral("yyyy-MM-dd HH:mm:ss");
                QStringList terms;
                if (fromCheck->isChecked())
                    terms << QStringLiteral("@time>=")
                            + logdor::quoteQueryValue(
                                fromEdit->dateTime().toString(format));
                if (toCheck->isChecked())
                    terms << QStringLiteral("@time<=")
                            + logdor::quoteQueryValue(
                                toEdit->dateTime().toString(format));
                timeMenu->hide();
                applyTimeRange(terms);
            });
    connect(clearButton, &QPushButton::clicked, this,
            [this, timeMenu, fromCheck, toCheck]() {
                fromCheck->setChecked(false);
                toCheck->setChecked(false);
                timeMenu->hide();
                applyTimeRange({});
            });

    // Saved filter presets.
    m_savedButton = new QPushButton(tr("★"), this);
    m_savedButton->setFlat(false);
    m_savedButton->setFixedSize(32, 24);
    m_savedButton->setToolTip(tr("Saved filters"));
    layout->addWidget(m_savedButton);
    connect(m_savedButton, &QPushButton::clicked,
            this, &FilterToolbar::showSavedQueriesMenu);

    // Query mode and regex mode are mutually exclusive filter languages.
    connect(m_queryModeButton, &QPushButton::toggled, this, [this](bool on) {
        if (on)
            m_regexModeButton->setChecked(false);
    });
    connect(m_regexModeButton, &QPushButton::toggled, this, [this](bool on) {
        if (on)
            m_queryModeButton->setChecked(false);
    });

    layout->addSpacing(8);
    layout->addWidget(new QLabel(tr("Lines Before:"), this));
    m_beforeSpinBox->setValue(0);
    m_beforeSpinBox->setToolTip(
        tr("Number of context lines to show before matches"));
    layout->addWidget(m_beforeSpinBox);
    layout->addWidget(new QLabel(tr("Lines After:"), this));
    m_afterSpinBox->setValue(0);
    m_afterSpinBox->setToolTip(
        tr("Number of context lines to show after matches"));
    layout->addWidget(m_afterSpinBox);

    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_input, &QLineEdit::textChanged, m_debounce,
            qOverload<>(&QTimer::start));
    connect(m_debounce, &QTimer::timeout,
            this, &FilterToolbar::emitFilterChanged);
    connect(m_beforeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &FilterToolbar::emitFilterChanged);
    connect(m_afterSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &FilterToolbar::emitFilterChanged);
    connect(m_caseSensitiveButton, &QPushButton::toggled,
            this, &FilterToolbar::emitFilterChanged);
    connect(m_invertButton, &QPushButton::toggled,
            this, &FilterToolbar::emitFilterChanged);
    connect(m_regexModeButton, &QPushButton::toggled,
            this, &FilterToolbar::emitFilterChanged);
    connect(m_queryModeButton, &QPushButton::toggled,
            this, &FilterToolbar::emitFilterChanged);
}

FilterOptions FilterToolbar::options() const
{
    return FilterOptions(m_input->text(), m_beforeSpinBox->value(),
                         m_afterSpinBox->value(),
                         m_caseSensitiveButton->isChecked()
                             ? Qt::CaseSensitive
                             : Qt::CaseInsensitive,
                         m_invertButton->isChecked(),
                         m_queryModeButton->isChecked(),
                         m_regexModeButton->isChecked());
}

void FilterToolbar::setOptionsSilently(const FilterOptions& options)
{
    m_debounce->stop();
    const QSignalBlocker blockInput(m_input);
    const QSignalBlocker blockCase(m_caseSensitiveButton);
    const QSignalBlocker blockInvert(m_invertButton);
    const QSignalBlocker blockQuery(m_queryModeButton);
    const QSignalBlocker blockRegex(m_regexModeButton);
    const QSignalBlocker blockBefore(m_beforeSpinBox);
    const QSignalBlocker blockAfter(m_afterSpinBox);
    m_input->setText(options.query);
    m_caseSensitiveButton->setChecked(options.caseSensitivity
                                      == Qt::CaseSensitive);
    m_invertButton->setChecked(options.invertFilter);
    m_queryModeButton->setChecked(options.inQueryMode);
    m_regexModeButton->setChecked(options.inRegexMode);
    m_beforeSpinBox->setValue(options.contextLinesBefore);
    m_afterSpinBox->setValue(options.contextLinesAfter);
}

void FilterToolbar::applyTimeRange(const QStringList& terms)
{
    // Replace, never stack: strip the terms this picker injected last time,
    // leaving anything the user typed alone.
    QString text = m_input->text();
    for (const QString& old : std::as_const(m_timeRangeTerms)) {
        const qsizetype at = text.indexOf(old);
        if (at < 0)
            continue; // the user edited it away; nothing to strip
        qsizetype begin = at, end = at + old.size();
        if (end < text.size() && text[end] == u' ')
            ++end; // absorb one separator space
        else if (begin > 0 && text[begin - 1] == u' ')
            --begin;
        text.remove(begin, end - begin);
    }
    text = text.trimmed();
    m_timeRangeTerms = terms;

    m_debounce->stop();
    {
        const QSignalBlocker blockInput(m_input);
        m_input->setText(text);
    }
    if (terms.isEmpty())
        emitFilterChanged();
    else
        appendTerm(terms.join(u' '));
}

void FilterToolbar::appendTerm(const QString& term)
{
    QString text = m_input->text().trimmed();
    if (m_queryModeButton->isChecked()) {
        text = text.isEmpty() ? term : text + u' ' + term; // implicit AND
    } else if (!m_regexModeButton->isChecked() && !text.isEmpty()) {
        // A plain-text filter is equivalent to a quoted free-text term
        // (raw-line substring match), so it survives the mode switch.
        text = logdor::quoteQueryValue(text, /*forceQuote=*/true) + u' '
            + term;
    } else {
        text = term; // empty filter, or regex (no query-language equivalent)
    }

    // No-debounce pattern: set the widgets silently, then apply in one shot.
    m_debounce->stop();
    {
        const QSignalBlocker blockInput(m_input);
        const QSignalBlocker blockQuery(m_queryModeButton);
        const QSignalBlocker blockRegex(m_regexModeButton);
        m_input->setText(text);
        m_regexModeButton->setChecked(false);
        m_queryModeButton->setChecked(true);
    }
    emitFilterChanged();
}

QList<FilterToolbar::SavedQuery> FilterToolbar::loadSavedQueries()
{
    QSettings settings("Logdor", "Logdor");
    QList<SavedQuery> queries;
    const int count = settings.beginReadArray(QStringLiteral("savedQueries"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        SavedQuery saved;
        saved.name = settings.value("name").toString();
        saved.options = FilterOptions(
            settings.value("query").toString(),
            settings.value("before").toInt(),
            settings.value("after").toInt(),
            settings.value("caseSensitive").toBool() ? Qt::CaseSensitive
                                                     : Qt::CaseInsensitive,
            settings.value("invert").toBool(),
            settings.value("queryMode").toBool(),
            settings.value("regexMode").toBool());
        if (!saved.name.isEmpty())
            queries.append(saved);
    }
    settings.endArray();
    return queries;
}

void FilterToolbar::storeSavedQueries(const QList<SavedQuery>& queries)
{
    QSettings settings("Logdor", "Logdor");
    settings.remove(QStringLiteral("savedQueries"));
    settings.beginWriteArray(QStringLiteral("savedQueries"), queries.size());
    for (int i = 0; i < queries.size(); ++i) {
        settings.setArrayIndex(i);
        const SavedQuery& saved = queries[i];
        settings.setValue("name", saved.name);
        settings.setValue("query", saved.options.query);
        settings.setValue("queryMode", saved.options.inQueryMode);
        settings.setValue("regexMode", saved.options.inRegexMode);
        settings.setValue("caseSensitive",
                          saved.options.caseSensitivity == Qt::CaseSensitive);
        settings.setValue("invert", saved.options.invertFilter);
        settings.setValue("before", saved.options.contextLinesBefore);
        settings.setValue("after", saved.options.contextLinesAfter);
    }
    settings.endArray();
}

void FilterToolbar::showSavedQueriesMenu()
{
    QMenu menu(this);
    const QList<SavedQuery> queries = loadSavedQueries();
    for (const SavedQuery& saved : queries) {
        QAction* action = menu.addAction(
            QStringLiteral("%1  —  %2").arg(saved.name, saved.options.query));
        connect(action, &QAction::triggered, this,
                [this, options = saved.options]() {
                    // The no-debounce pattern: silently, then one shot.
                    setOptionsSilently(options);
                    m_timeRangeTerms.clear(); // a preset replaces everything
                    emitFilterChanged();
                });
    }
    if (!queries.isEmpty())
        menu.addSeparator();
    QAction* save = menu.addAction(tr("Save Current Filter..."));
    save->setEnabled(!m_input->text().trimmed().isEmpty());
    connect(save, &QAction::triggered,
            this, &FilterToolbar::saveCurrentFilter);
    QAction* manage = menu.addAction(tr("Manage Saved Filters..."));
    manage->setEnabled(!queries.isEmpty());
    connect(manage, &QAction::triggered,
            this, &FilterToolbar::manageSavedQueries);
    menu.exec(m_savedButton->mapToGlobal(QPoint(0, m_savedButton->height())));
}

void FilterToolbar::saveCurrentFilter()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Save Filter"), tr("Name:"), QLineEdit::Normal,
        m_input->text().trimmed().left(32), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    QList<SavedQuery> queries = loadSavedQueries();
    queries.removeIf([&name](const SavedQuery& saved) {
        return saved.name == name.trimmed(); // same name overwrites
    });
    queries.append({ name.trimmed(), options() });
    storeSavedQueries(queries);
}

void FilterToolbar::manageSavedQueries()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Saved Filters"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    QList<SavedQuery> queries = loadSavedQueries();
    for (const SavedQuery& saved : std::as_const(queries))
        list->addItem(
            QStringLiteral("%1  —  %2").arg(saved.name, saved.options.query));
    layout->addWidget(list);
    auto* removeButton = new QPushButton(tr("Remove"), &dialog);
    connect(removeButton, &QPushButton::clicked, &dialog,
            [list, &queries]() {
                const int row = list->currentRow();
                if (row < 0)
                    return;
                delete list->takeItem(row);
                queries.removeAt(row);
                storeSavedQueries(queries);
            });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(removeButton);
    buttonRow->addStretch();
    buttonRow->addWidget(buttons);
    layout->addLayout(buttonRow);
    dialog.exec();
}

void FilterToolbar::focusInput()
{
    m_input->setFocus();
    m_input->selectAll();
}

void FilterToolbar::emitFilterChanged()
{
    // Tint the input by validity: regex mode validates the pattern, query
    // mode validates syntax only (schema-aware errors surface per viewer).
    if (m_regexModeButton->isChecked()) {
        const QRegularExpression regex(m_input->text());
        m_input->setStyleSheet(
            regex.isValid() || m_input->text().isEmpty()
                ? "QLineEdit { background-color: #90EE90; color: black; }"
                : "QLineEdit { background-color: #FFB6C1; color: black; }");
    } else if (m_queryModeButton->isChecked() && !m_input->text().isEmpty()) {
        logdor::QueryError error;
        const auto query = logdor::CompiledQuery::compile(
            m_input->text(), {},
            m_caseSensitiveButton->isChecked() ? Qt::CaseSensitive
                                               : Qt::CaseInsensitive,
            logdor::QueryOption::AllowUnknownFields, &error);
        if (query) {
            m_input->setStyleSheet(
                "QLineEdit { background-color: #90EE90; color: black; }");
            m_input->setToolTip(QString());
        } else {
            m_input->setStyleSheet(
                "QLineEdit { background-color: #FFB6C1; color: black; }");
            m_input->setToolTip(error.message);
        }
    } else {
        m_input->setStyleSheet("");
        m_input->setToolTip(QString());
    }

    emit filterChanged(options());
}
