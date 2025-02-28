#include "regexviewer.h"
#include <QHeaderView>
#include <QLabel>
#include <QSettings>

RegexViewer::RegexViewer(QObject* parent)
    : PluginInterface(parent)
    , m_widget(new QWidget)
    , m_layout(new QVBoxLayout(m_widget))
    , m_patternEdit(new QLineEdit)
    , m_settingsButton(new QPushButton(tr("Configure Columns")))
    , m_tableView(new QTableView)
    , m_model(new RegexTableModel(this))
{
    // Setup pattern input
    QHBoxLayout* patternLayout = new QHBoxLayout;
    patternLayout->addWidget(new QLabel(tr("Regex Pattern:")));
    patternLayout->addWidget(m_patternEdit);
    patternLayout->addWidget(m_settingsButton);
    m_layout->addLayout(patternLayout);

    // Setup table view
    m_tableView->setModel(m_model);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_tableView->verticalHeader()->hide();
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_layout->addWidget(m_tableView);
    m_tableView->setAlternatingRowColors(true);

    // Load settings
    QSettings settings;
    settings.beginGroup("RegexViewer");
    m_patternEdit->setText(settings.value("pattern").toString());
    
    // Load field info
    int size = settings.beginReadArray("fields");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        RegexFieldInfo field;
        field.name = settings.value("name").toString();
        field.type = static_cast<DataType>(settings.value("type").toInt());
        field.groupNumber = i;  // Group number is the index
        m_fields.append(field);
    }
    settings.endArray();
    settings.endGroup();

    // If no fields are configured, add default ones
    if (m_fields.isEmpty()) {
        m_fields.append(RegexFieldInfo(tr("Full Match"), DataType::String, 0));
    }

    m_model->setFieldInfo(m_fields);
    m_model->setPattern(m_patternEdit->text());

    // Connect signals
    connect(m_patternEdit, &QLineEdit::textChanged, this, &RegexViewer::onPatternChanged);
    connect(m_settingsButton, &QPushButton::clicked, this, &RegexViewer::onSettingsClicked);
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &RegexViewer::onSelectionChanged);
}

RegexViewer::~RegexViewer()
{
    // Save settings
    QSettings settings;
    settings.beginGroup("RegexViewer");
    settings.setValue("pattern", m_patternEdit->text());
    
    settings.beginWriteArray("fields");
    for (int i = 0; i < m_fields.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("name", m_fields[i].name);
        settings.setValue("type", static_cast<int>(m_fields[i].type));
    }
    settings.endArray();
    settings.endGroup();

    delete m_widget;
}

bool RegexViewer::setLogs(const QList<LogEntry>& content)
{
    m_model->setLogEntries(content);
    return true;
}

void RegexViewer::setFilter(const FilterOptions& options)
{
    m_model->setFilter(options);
}

QList<FieldInfo> RegexViewer::availableFields() const
{
    // Convert RegexFieldInfo to FieldInfo for the interface
    QList<FieldInfo> fields;
    for (const RegexFieldInfo& field : m_fields) {
        FieldInfo baseField;
        baseField.name = field.name;
        baseField.type = field.type;
        baseField.possibleValues = field.possibleValues;
        fields.append(baseField);
    }
    return fields;
}

QSet<int> RegexViewer::filteredLines() const
{
    return m_model->getFilteredLines();
}

void RegexViewer::synchronizeFilteredLines(const QSet<int>& lines)
{
    // No need to implement as we handle filtering through the model
}

void RegexViewer::onPluginEvent(PluginEvent event, const QVariant& data)
{
    switch (event) {
        case PluginEvent::LinesSelected: {
            // Handle line selection from other plugins
            QSet<int> selectedLines = data.value<QSet<int>>();
            m_tableView->clearSelection();
            for (int i = 0; i < m_model->rowCount(); ++i) {
                int sourceRow = m_model->mapToSourceRow(i);
                if (selectedLines.contains(sourceRow)) {
                    m_tableView->selectRow(i);
                }
            }
            break;
        }
        default:
            break;
    }
}

void RegexViewer::onPatternChanged(const QString& pattern)
{
    m_model->setPattern(pattern);
}

void RegexViewer::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
    QList<int> selectedLines;
    for (const QModelIndex& index : m_tableView->selectionModel()->selectedRows()) {
        selectedLines.append(m_model->mapToSourceRow(index.row()));
    }
    emit pluginEvent(PluginEvent::LinesSelected, QVariant::fromValue(selectedLines));
}

void RegexViewer::onSettingsClicked()
{
    RegexSettingsDialog dialog(m_fields, m_widget);
    if (dialog.exec() == QDialog::Accepted) {
        m_fields = dialog.getFields();
        m_model->setFieldInfo(m_fields);
    }
}
