#include "regexsettingsdialog.h"
#include <QHeaderView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDateTime>

RegexSettingsDialog::RegexSettingsDialog(const QList<RegexFieldInfo>& fields, QWidget* parent)
    : QDialog(parent)
    , m_columnsTable(new QTableWidget)
    , m_addButton(new QPushButton(tr("Add Column")))
    , m_removeButton(new QPushButton(tr("Remove Column")))
    , m_okButton(new QPushButton(tr("OK")))
    , m_cancelButton(new QPushButton(tr("Cancel")))
{
    setupUi();
    populateFields(fields);

    connect(m_addButton, &QPushButton::clicked, this, &RegexSettingsDialog::onAddColumn);
    connect(m_removeButton, &QPushButton::clicked, this, &RegexSettingsDialog::onRemoveColumn);
    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_columnsTable, &QTableWidget::cellChanged, this, &RegexSettingsDialog::onTypeChanged);
    connect(m_columnsTable, &QTableWidget::cellDoubleClicked, this, &RegexSettingsDialog::onCellDoubleClicked);
}

void RegexSettingsDialog::setupUi()
{
    setWindowTitle(tr("Configure Columns"));
    resize(500, 300);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Setup table
    m_columnsTable->setColumnCount(4);
    m_columnsTable->setHorizontalHeaderLabels({tr("Column Name"), tr("Type"), tr("Group"), tr("Possible Values")});
    
    // Set tooltip for possible values column
    m_columnsTable->horizontalHeaderItem(3)->setToolTip(
        tr("Enter possible values separated by semicolons.\n"
           "Example for String: value1[#ff0000]; value2[#00ff00]; value3[#0000ff]\n"
           "Example for Integer: 1[#ff0000]; 2[#00ff00]; 3[#0000ff]\n"
           "Example for DateTime: 2024-02-27T12:00:00[#ff0000]; 2024-02-27T13:00:00[#00ff00]\n"
           "Double-click a cell to set colors for values")
    );
    m_columnsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_columnsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mainLayout->addWidget(m_columnsTable);

    // Button layout
    QHBoxLayout* buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_removeButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);
    mainLayout->addLayout(buttonLayout);
}

void RegexSettingsDialog::populateFields(const QList<RegexFieldInfo>& fields)
{
    m_columnsTable->setRowCount(fields.size());
    
    for (int i = 0; i < fields.size(); ++i) {
        const RegexFieldInfo& field = fields[i];
        
        // Column name
        QTableWidgetItem* nameItem = new QTableWidgetItem(field.name);
        m_columnsTable->setItem(i, 0, nameItem);
        
        // Type combobox
        QComboBox* typeCombo = new QComboBox;
        typeCombo->addItems({tr("String"), tr("Integer"), tr("DateTime")});
        typeCombo->setCurrentIndex(static_cast<int>(field.type));
        m_columnsTable->setCellWidget(i, 1, typeCombo);
        
        // Group number (read-only)
        QTableWidgetItem* groupItem = new QTableWidgetItem(QString::number(i));
        groupItem->setFlags(groupItem->flags() & ~Qt::ItemIsEditable);
        m_columnsTable->setItem(i, 2, groupItem);

        // Possible values with colors
        QMap<QString, QColor> valueColors;
        for (const ValueWithColor& vc : field.coloredValues) {
            valueColors[vc.value.toString()] = vc.color;
        }
        QTableWidgetItem* valuesItem = new QTableWidgetItem(formatValueColors(valueColors));
        m_columnsTable->setItem(i, 3, valuesItem);
    }
}

void RegexSettingsDialog::onAddColumn()
{
    int row = m_columnsTable->rowCount();
    m_columnsTable->insertRow(row);
    
    // Column name
    QTableWidgetItem* nameItem = new QTableWidgetItem(tr("Column %1").arg(row + 1));
    m_columnsTable->setItem(row, 0, nameItem);
    
    // Type combobox
    QComboBox* typeCombo = new QComboBox;
    typeCombo->addItems({tr("String"), tr("Integer"), tr("DateTime")});
    m_columnsTable->setCellWidget(row, 1, typeCombo);
    
    // Group number (read-only)
    QTableWidgetItem* groupItem = new QTableWidgetItem(QString::number(row));
    groupItem->setFlags(groupItem->flags() & ~Qt::ItemIsEditable);
    m_columnsTable->setItem(row, 2, groupItem);

        // Empty possible values with colors
        QTableWidgetItem* valuesItem = new QTableWidgetItem("");
        valuesItem->setToolTip(tr("Double-click to set colors for values"));
    m_columnsTable->setItem(row, 3, valuesItem);
}

void RegexSettingsDialog::onRemoveColumn()
{
    QList<QTableWidgetItem*> selectedItems = m_columnsTable->selectedItems();
    if (!selectedItems.isEmpty()) {
        m_columnsTable->removeRow(selectedItems.first()->row());
        
        // Update group numbers
        for (int i = 0; i < m_columnsTable->rowCount(); ++i) {
            QTableWidgetItem* groupItem = m_columnsTable->item(i, 2);
            groupItem->setText(QString::number(i));
        }
    }
}

void RegexSettingsDialog::onTypeChanged(int row, int column)
{
    if (column == 1) {
        QComboBox* typeCombo = qobject_cast<QComboBox*>(m_columnsTable->cellWidget(row, column));
        if (typeCombo) {
            DataType newType = static_cast<DataType>(typeCombo->currentIndex());
            QTableWidgetItem* valuesItem = m_columnsTable->item(row, 3);
            if (!valuesItem) return;

            QString valuesStr = valuesItem->text().trimmed();
            if (valuesStr.isEmpty()) return;

            QStringList valuesList = valuesStr.split(';', Qt::SkipEmptyParts);
            QStringList convertedValues;
            bool conversionOk = true;

            for (const QString& value : valuesList) {
                QString trimmedValue = value.trimmed();
                if (trimmedValue.isEmpty()) continue;

                switch (newType) {
                    case DataType::Integer: {
                        bool ok;
                        trimmedValue.toInt(&ok);
                        if (!ok) {
                            conversionOk = false;
                            break;
                        }
                        convertedValues << trimmedValue;
                        break;
                    }
                    case DataType::DateTime: {
                        QDateTime dt = QDateTime::fromString(trimmedValue, Qt::ISODate);
                        if (!dt.isValid()) {
                            conversionOk = false;
                            break;
                        }
                        convertedValues << dt.toString(Qt::ISODate);
                        break;
                    }
                    default:
                        convertedValues << trimmedValue;
                }

                if (!conversionOk) break;
            }

            if (!conversionOk) {
                // Reset possible values if conversion failed
                valuesItem->setText("");
                QMessageBox::warning(this, tr("Invalid Values"),
                    tr("The possible values could not be converted to the new type.\n"
                       "Please enter values in the correct format:\n"
                       "Integer: whole numbers\n"
                       "DateTime: YYYY-MM-DDThh:mm:ss"));
            } else if (!valuesList.isEmpty()) {
                // Update with converted values
                valuesItem->setText(convertedValues.join("; "));
            }
        }
    }
}

void RegexSettingsDialog::onCellDoubleClicked(int row, int column)
{
    if (column == 3) { // Possible values column
        updateValueColors(row);
    }
}

void RegexSettingsDialog::updateValueColors(int row)
{
    QTableWidgetItem* item = m_columnsTable->item(row, 3);
    if (!item) return;

    QMap<QString, QColor> valueColors = parseValueColors(item->text());
    
    for (auto it = valueColors.begin(); it != valueColors.end(); ++it) {
        QColor newColor = QColorDialog::getColor(it.value(), this, 
            tr("Choose color for value: %1").arg(it.key()));
        
        if (newColor.isValid()) {
            it.value() = newColor;
            onValueColorChanged(row, it.key(), newColor);
        }
    }

    item->setText(formatValueColors(valueColors));
}

QMap<QString, QColor> RegexSettingsDialog::parseValueColors(const QString& text) const
{
    QMap<QString, QColor> valueColors;
    if (text.isEmpty()) return valueColors;

    QStringList valuesList = text.split(';', Qt::SkipEmptyParts);
    QRegularExpression colorRegex(R"(^(.*?)\[(#[0-9a-fA-F]{6})\]$)");

    for (const QString& valueStr : valuesList) {
        QString trimmed = valueStr.trimmed();
        QRegularExpressionMatch match = colorRegex.match(trimmed);
        
        if (match.hasMatch()) {
            QString value = match.captured(1).trimmed();
            QString colorStr = match.captured(2);
            valueColors[value] = QColor(colorStr);
        } else {
            valueColors[trimmed] = QColor(); // No color specified
        }
    }

    return valueColors;
}

QString RegexSettingsDialog::formatValueColors(const QMap<QString, QColor>& valueColors) const
{
    QStringList formattedValues;
    for (auto it = valueColors.begin(); it != valueColors.end(); ++it) {
        if (it.value().isValid()) {
            formattedValues << QString("%1[%2]").arg(it.key(), it.value().name());
        } else {
            formattedValues << it.key();
        }
    }
    return formattedValues.join("; ");
}

void RegexSettingsDialog::onValueColorChanged(int row, const QString& value, const QColor& color)
{
    QTableWidgetItem* item = m_columnsTable->item(row, 3);
    if (!item) return;

    QMap<QString, QColor> valueColors = parseValueColors(item->text());
    valueColors[value] = color;
    item->setText(formatValueColors(valueColors));
}

QList<RegexFieldInfo> RegexSettingsDialog::getFields() const
{
    QList<RegexFieldInfo> fields;
    
    for (int i = 0; i < m_columnsTable->rowCount(); ++i) {
        RegexFieldInfo field;
        field.name = m_columnsTable->item(i, 0)->text();
        
        QComboBox* typeCombo = qobject_cast<QComboBox*>(m_columnsTable->cellWidget(i, 1));
        field.type = static_cast<DataType>(typeCombo->currentIndex());
        
        field.groupNumber = i;  // Group number is the row index

        // Parse possible values with colors
        QString valuesStr = m_columnsTable->item(i, 3)->text().trimmed();
        if (!valuesStr.isEmpty()) {
            QMap<QString, QColor> valueColors = parseValueColors(valuesStr);
            for (auto it = valueColors.begin(); it != valueColors.end(); ++it) {
                QString value = it.key();
                QColor color = it.value();

                // Convert value based on field type
                QVariant convertedValue;
                switch (field.type) {
                    case DataType::Integer:
                        convertedValue = value.toInt();
                        break;
                    case DataType::DateTime:
                        convertedValue = QDateTime::fromString(value, Qt::ISODate);
                        break;
                    default:
                        convertedValue = value;
                }
                
                field.addValueWithColor(convertedValue, color);
            }
        }
        
        fields.append(field);
    }
    
    return fields;
}
