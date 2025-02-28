#include "valuecolordialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QColorDialog>
#include <QMessageBox>
#include <QDateTime>

ValueColorDialog::ValueColorDialog(DataType type, const QMap<QString, QColor>& valueColors, QWidget* parent)
    : QDialog(parent)
    , m_table(new QTableWidget)
    , m_addButton(new QPushButton(tr("Add Value")))
    , m_removeButton(new QPushButton(tr("Remove Value")))
    , m_okButton(new QPushButton(tr("OK")))
    , m_cancelButton(new QPushButton(tr("Cancel")))
    , m_type(type)
    , m_valueColors(valueColors)
{
    setupUi();
    populateTable();

    connect(m_addButton, &QPushButton::clicked, this, &ValueColorDialog::onAddRow);
    connect(m_removeButton, &QPushButton::clicked, this, &ValueColorDialog::onRemoveRow);
    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_table, &QTableWidget::cellChanged, this, &ValueColorDialog::validateCell);
}

void ValueColorDialog::setupUi()
{
    setWindowTitle(tr("Configure Values and Colors"));
    resize(400, 300);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Setup table
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({tr("Value"), tr("Color")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    mainLayout->addWidget(m_table);

    // Button layout
    QHBoxLayout* buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_removeButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_okButton);
    buttonLayout->addWidget(m_cancelButton);
    mainLayout->addLayout(buttonLayout);
}

void ValueColorDialog::populateTable()
{
    m_table->setRowCount(m_valueColors.size());
    int row = 0;
    
    for (auto it = m_valueColors.begin(); it != m_valueColors.end(); ++it, ++row) {
        // Value
        QTableWidgetItem* valueItem = new QTableWidgetItem(it.key());
        m_table->setItem(row, 0, valueItem);
        
        // Color button
        QPushButton* colorButton = new QPushButton;
        colorButton->setFixedSize(24, 24);
        QString styleSheet = QString("background-color: %1;").arg(it.value().name());
        colorButton->setStyleSheet(styleSheet);
        colorButton->setProperty("row", row);
        connect(colorButton, &QPushButton::clicked, this, &ValueColorDialog::onPickColor);
        m_table->setCellWidget(row, 1, colorButton);
    }
}

void ValueColorDialog::onAddRow()
{
    int row = m_table->rowCount();
    m_table->insertRow(row);
    
    // Value
    QTableWidgetItem* valueItem = new QTableWidgetItem();
    m_table->setItem(row, 0, valueItem);
    
    // Color button
    QPushButton* colorButton = new QPushButton;
    colorButton->setFixedSize(24, 24);
    colorButton->setStyleSheet("background-color: #808080;"); // Default gray
    colorButton->setProperty("row", row);
    connect(colorButton, &QPushButton::clicked, this, &ValueColorDialog::onPickColor);
    m_table->setCellWidget(row, 1, colorButton);
}

void ValueColorDialog::onRemoveRow()
{
    QList<QTableWidgetItem*> selectedItems = m_table->selectedItems();
    if (!selectedItems.isEmpty()) {
        m_table->removeRow(selectedItems.first()->row());
    }
}

void ValueColorDialog::onPickColor()
{
    QPushButton* button = qobject_cast<QPushButton*>(sender());
    if (!button) return;
    
    int row = button->property("row").toInt();
    QString currentColor = button->styleSheet();
    currentColor.remove("background-color: ").remove(";");
    
    QColor color = QColorDialog::getColor(QColor(currentColor), this);
    if (color.isValid()) {
        button->setStyleSheet(QString("background-color: %1;").arg(color.name()));
    }
}

void ValueColorDialog::validateCell(int row, int column)
{
    if (column != 0) return; // Only validate value column
    
    QTableWidgetItem* item = m_table->item(row, column);
    if (!item) return;
    
    QString value = item->text().trimmed();
    if (!validateValue(value)) {
        item->setText("");
        QMessageBox::warning(this, tr("Invalid Value"),
            tr("Please enter a valid value for the selected type:\n"
               "Integer: whole numbers\n"
               "DateTime: YYYY-MM-DDThh:mm:ss\n"
               "String: any text"));
    }
}

bool ValueColorDialog::validateValue(const QString& value) const
{
    if (value.isEmpty()) return true;

    switch (m_type) {
        case DataType::Integer: {
            bool ok;
            value.toInt(&ok);
            return ok;
        }
        case DataType::DateTime: {
            QDateTime dt = QDateTime::fromString(value, Qt::ISODate);
            return dt.isValid();
        }
        default: // String
            return true;
    }
}

QMap<QString, QColor> ValueColorDialog::getValueColors() const
{
    QMap<QString, QColor> result;
    
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem* valueItem = m_table->item(row, 0);
        QPushButton* colorButton = qobject_cast<QPushButton*>(m_table->cellWidget(row, 1));
        
        if (!valueItem || !colorButton) continue;
        
        QString value = valueItem->text().trimmed();
        if (value.isEmpty()) continue;
        
        QString styleSheet = colorButton->styleSheet();
        QString colorStr = styleSheet.mid(styleSheet.indexOf("#"), 7);
        result[value] = QColor(colorStr);
    }
    
    return result;
}
