#ifndef REGEXSETTINGSDIALOG_H
#define REGEXSETTINGSDIALOG_H

#include "regextablemodel.h"
#include "../../app/src/plugininterface.h"
#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QColorDialog>

class RegexSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit RegexSettingsDialog(const QList<RegexFieldInfo>& fields, QWidget* parent = nullptr);
    QList<RegexFieldInfo> getFields() const;

private slots:
    void onAddColumn();
    void onRemoveColumn();
    void onTypeChanged(int row, int column);
    void onCellDoubleClicked(int row, int column);
    void onValueColorChanged(int row, const QString& value, const QColor& color);

private:
    void setupUi();
    void populateFields(const QList<RegexFieldInfo>& fields);
    RegexFieldInfo getFieldFromRow(int row) const;
    void updateValueColors(int row);
    QMap<QString, QColor> parseValueColors(const QString& text) const;
    QString formatValueColors(const QMap<QString, QColor>& valueColors) const;

    QTableWidget* m_columnsTable;
    QPushButton* m_addButton;
    QPushButton* m_removeButton;
    QPushButton* m_okButton;
    QPushButton* m_cancelButton;
};

#endif // REGEXSETTINGSDIALOG_H
