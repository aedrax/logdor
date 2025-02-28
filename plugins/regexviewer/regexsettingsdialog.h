#ifndef REGEXSETTINGSDIALOG_H
#define REGEXSETTINGSDIALOG_H

#include "regextablemodel.h"
#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QVBoxLayout>

class RegexSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit RegexSettingsDialog(const QList<RegexFieldInfo>& fields, QWidget* parent = nullptr);
    QList<RegexFieldInfo> getFields() const;

private slots:
    void onAddColumn();
    void onRemoveColumn();
    void onTypeChanged(int row, int column);

private:
    void setupUi();
    void populateFields(const QList<RegexFieldInfo>& fields);
    RegexFieldInfo getFieldFromRow(int row) const;

    QTableWidget* m_columnsTable;
    QPushButton* m_addButton;
    QPushButton* m_removeButton;
    QPushButton* m_okButton;
    QPushButton* m_cancelButton;
};

#endif // REGEXSETTINGSDIALOG_H
