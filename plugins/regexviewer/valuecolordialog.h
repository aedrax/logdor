#ifndef VALUECOLORDIALOG_H
#define VALUECOLORDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QMap>
#include <QColor>
#include <QString>
#include <QVariant>
#include "regextablemodel.h"

class ValueColorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ValueColorDialog(DataType type, const QMap<QString, QColor>& valueColors, QWidget* parent = nullptr);
    QMap<QString, QColor> getValueColors() const;

private slots:
    void onAddRow();
    void onRemoveRow();
    void onPickColor();
    void validateCell(int row, int column);

private:
    void setupUi();
    void populateTable();
    bool validateValue(const QString& value) const;

    QTableWidget* m_table;
    QPushButton* m_addButton;
    QPushButton* m_removeButton;
    QPushButton* m_okButton;
    QPushButton* m_cancelButton;
    DataType m_type;
    QMap<QString, QColor> m_valueColors;
};

#endif // VALUECOLORDIALOG_H
