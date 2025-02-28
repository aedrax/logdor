#include "regexsettingsdialog.h"
#include <QHeaderView>
#include <QComboBox>
#include <QHBoxLayout>

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
}

void RegexSettingsDialog::setupUi()
{
    setWindowTitle(tr("Configure Columns"));
    resize(400, 300);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Setup table
    m_columnsTable->setColumnCount(3);
    m_columnsTable->setHorizontalHeaderLabels({tr("Column Name"), tr("Type"), tr("Group")});
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
            // Handle type change if needed
        }
    }
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
        fields.append(field);
    }
    
    return fields;
}
