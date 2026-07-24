#include "customformatviewer.h"

#include <logdor/DeclarativeParser.h>
#include <logdor/FormatRegistry.h>

#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

const QHash<QString, QString>& standardSeverityMap()
{
    static const QHash<QString, QString> map = {
        { "trace", "verbose" }, { "verbose", "verbose" },
        { "debug", "debug" },   { "info", "info" },
        { "notice", "info" },   { "warn", "warning" },
        { "warning", "warning" }, { "error", "error" },
        { "err", "error" },     { "critical", "fatal" },
        { "fatal", "fatal" },   { "panic", "fatal" },
    };
    return map;
}

} // namespace

CustomFormatViewer::CustomFormatViewer(QObject* parent)
    : PluginInterface(parent)
    , m_container(new QWidget())
    , m_viewer(new LogViewerWidget())
    , m_patternEdit(new QLineEdit())
    , m_status(new QLabel())
    , m_debounce(new QTimer(this))
{
    auto* layout = new QVBoxLayout(m_container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bar = new QWidget();
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(4, 2, 4, 2);
    barLayout->addWidget(new QLabel(tr("Pattern:")));
    m_patternEdit->setPlaceholderText(
        tr("Regex with named captures, e.g. ^(?<time>\\S+) \\[(?<level>\\w+)\\] (?<message>.*)$"));
    barLayout->addWidget(m_patternEdit, 1);
    auto* saveButton = new QPushButton(tr("Save as Format..."));
    saveButton->setToolTip(
        tr("Store this pattern as a named format: shareable, auto-detected, "
           "and available in the Plain Text Viewer's format list"));
    barLayout->addWidget(saveButton);
    layout->addWidget(bar);
    layout->addWidget(m_status);
    m_status->setContentsMargins(6, 0, 6, 2);
    layout->addWidget(m_viewer, 1);

    m_debounce->setSingleShot(true);
    m_debounce->setInterval(300);
    connect(m_patternEdit, &QLineEdit::textChanged, m_debounce,
            qOverload<>(&QTimer::start));
    connect(m_debounce, &QTimer::timeout, this,
            &CustomFormatViewer::rebuildParser);
    connect(saveButton, &QPushButton::clicked, this,
            &CustomFormatViewer::saveAsFormat);
    connect(m_viewer, &LogViewerWidget::linesSelected, this,
            [this](const QList<int>& lines) {
                emit pluginEvent(PluginEvent::LinesSelected,
                                 QVariant::fromValue(lines));
            });

    QSettings settings("Logdor", "Logdor");
    m_patternEdit->setText(
        settings.value("CustomFormatViewer/pattern").toString());
    rebuildParser();
}

CustomFormatViewer::~CustomFormatViewer()
{
    QSettings settings("Logdor", "Logdor");
    settings.setValue("CustomFormatViewer/pattern", m_patternEdit->text());
    delete m_container;
}

std::optional<logdor::FormatSpec>
CustomFormatViewer::buildSpec(const QString& id, const QString& displayName) const
{
    const QString pattern = m_patternEdit->text();
    const QRegularExpression regex(pattern);
    if (pattern.isEmpty() || !regex.isValid())
        return std::nullopt;

    QStringList captures = regex.namedCaptureGroups();
    captures.removeAll(QString()); // unnamed groups come back empty
    if (captures.isEmpty())
        return std::nullopt;

    // Pick the message (stretch/fallback) column: 'message'/'msg', else last.
    int messageIndex = captures.size() - 1;
    QString severityCapture;
    for (int i = 0; i < captures.size(); ++i) {
        const QString lower = captures[i].toLower();
        if (lower == u"message" || lower == u"msg")
            messageIndex = i;
        if (lower == u"level" || lower == u"severity")
            severityCapture = captures[i];
    }

    logdor::FormatSpec spec;
    spec.id = id;
    spec.displayName = displayName;
    spec.pattern = pattern;
    for (int i = 0; i < captures.size(); ++i) {
        logdor::FormatSpecField field;
        // Title-case the capture name for the column header.
        field.name = captures[i];
        if (!field.name.isEmpty())
            field.name[0] = field.name[0].toUpper();
        field.capture = captures[i];
        field.hint = i == messageIndex ? logdor::FieldHint::Message
            : captures[i] == severityCapture ? logdor::FieldHint::SeverityName
                                             : logdor::FieldHint::None;
        spec.fields.append(field);
    }
    if (!severityCapture.isEmpty()) {
        spec.severityCapture = severityCapture;
        const auto& names = standardSeverityMap();
        for (auto it = names.begin(); it != names.end(); ++it) {
            if (it.value() == u"verbose") spec.severityMap[it.key()] = logdor::Severity::Verbose;
            else if (it.value() == u"debug") spec.severityMap[it.key()] = logdor::Severity::Debug;
            else if (it.value() == u"info") spec.severityMap[it.key()] = logdor::Severity::Info;
            else if (it.value() == u"warning") spec.severityMap[it.key()] = logdor::Severity::Warning;
            else if (it.value() == u"error") spec.severityMap[it.key()] = logdor::Severity::Error;
            else if (it.value() == u"fatal") spec.severityMap[it.key()] = logdor::Severity::Fatal;
        }
    }
    spec.specificity = 0.8;
    return spec;
}

void CustomFormatViewer::rebuildParser()
{
    const QString pattern = m_patternEdit->text();
    const QRegularExpression regex(pattern);

    QString tint;
    if (pattern.isEmpty()) {
        m_status->setText(tr("Enter a regex with named captures to shape the table."));
    } else if (!regex.isValid()) {
        m_status->setText(tr("Invalid regex at %1: %2")
                              .arg(regex.patternErrorOffset())
                              .arg(regex.errorString()));
        tint = QStringLiteral("QLineEdit { background-color: #FFB6C1; color: black; }");
    } else if (auto spec = buildSpec(QStringLiteral("custom"),
                                     QStringLiteral("Custom"))) {
        m_status->setText(tr("%1 columns").arg(spec->fields.size()));
        tint = QStringLiteral("QLineEdit { background-color: #90EE90; color: black; }");
        m_viewer->setParser(
            std::make_shared<const logdor::DeclarativeParser>(std::move(*spec)));
        m_viewer->applyFilter(m_lastFilter);
        m_patternEdit->setStyleSheet(tint);
        return;
    } else {
        m_status->setText(tr("Add named capture groups: (?<name>...)"));
    }

    m_patternEdit->setStyleSheet(tint);
    m_viewer->setParser(logdor::parserById(u"plaintext"));
    m_viewer->applyFilter(m_lastFilter);
}

void CustomFormatViewer::saveAsFormat()
{
    bool ok = false;
    const QString displayName = QInputDialog::getText(
        m_container, tr("Save as Format"), tr("Format name:"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || displayName.trimmed().isEmpty())
        return;

    QString id = displayName.toLower().simplified();
    id.replace(u' ', u'-');
    id.remove(QRegularExpression(QStringLiteral("[^a-z0-9-]")));
    if (id.isEmpty())
        id = QStringLiteral("custom-format");

    const auto spec = buildSpec(id, displayName.trimmed());
    if (!spec) {
        QMessageBox::warning(m_container, tr("Save as Format"),
                             tr("The pattern must be a valid regex with at "
                                "least one named capture group."));
        return;
    }

    // Serialize per the documented schema (FormatSpec.h).
    QJsonArray fields;
    for (const auto& field : spec->fields) {
        QJsonObject entry;
        entry.insert(u"name", field.name);
        entry.insert(u"capture", field.capture);
        entry.insert(u"type", QStringLiteral("string"));
        if (field.hint == logdor::FieldHint::Message)
            entry.insert(u"hint", QStringLiteral("message"));
        else if (field.hint == logdor::FieldHint::SeverityName)
            entry.insert(u"hint", QStringLiteral("severityname"));
        fields.append(entry);
    }
    QJsonObject root;
    root.insert(u"id", spec->id);
    root.insert(u"displayName", spec->displayName);
    root.insert(u"pattern", spec->pattern);
    root.insert(u"fields", fields);
    if (!spec->severityCapture.isEmpty()) {
        QJsonObject map;
        for (auto it = standardSeverityMap().begin();
             it != standardSeverityMap().end(); ++it)
            map.insert(it.key(), it.value());
        QJsonObject severity;
        severity.insert(u"capture", spec->severityCapture);
        severity.insert(u"map", map);
        root.insert(u"severity", severity);
    }
    root.insert(u"specificity", 0.8);
    const QByteArray json =
        QJsonDocument(root).toJson(QJsonDocument::Indented);

    // Round-trip validation before writing.
    logdor::FormatSpecError error;
    if (!logdor::parseFormatSpec(json, QStringLiteral("(unsaved)"), &error)) {
        QMessageBox::warning(m_container, tr("Save as Format"), error.message);
        return;
    }

    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/formats");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/%1.json").arg(spec->id);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || (file.write(json), !file.commit())) {
        QMessageBox::warning(m_container, tr("Save as Format"),
                             tr("Could not write %1").arg(path));
        return;
    }
    m_status->setText(
        tr("Saved to %1 - appears in the Plain Text Viewer's format list on "
           "next launch, and can be shared as a file.").arg(path));
}

void CustomFormatViewer::setCoreSource(std::shared_ptr<logdor::FileSource> source,
                                       std::shared_ptr<const logdor::LineIndex> index)
{
    m_viewer->setCoreSource(std::move(source), std::move(index));
}

void CustomFormatViewer::setFilter(const FilterOptions& options)
{
    m_lastFilter = options;
    m_viewer->applyFilter(options);
}

void CustomFormatViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    if (event == PluginEvent::LinesSelected) {
        m_viewer->selectSourceLines(data.value<QList<int>>());
    } else if (event == PluginEvent::LinesConstrained) {
        const QList<int> lines = data.value<QList<int>>();
        auto sorted = std::make_shared<std::vector<qint32>>(lines.begin(),
                                                            lines.end());
        m_viewer->setLineConstraint(std::move(sorted));
    }
}
