#ifndef REGEXVIEWER_H
#define REGEXVIEWER_H

#include "../../app/src/plugininterface.h"
#include "regextablemodel.h"
#include "regexsettingsdialog.h"
#include <QTableView>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QPushButton>
#include <QString>
#include <QtPlugin>

class RegexViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit RegexViewer(QObject* parent = nullptr);
    ~RegexViewer();

    // PluginInterface implementation
    QString name() const override { return tr("Regex Viewer"); }
    QString version() const override { return "0.1.0"; }
    QString description() const override { return tr("A viewer that parses logs using regex patterns and displays matches in columns."); }
    QWidget* widget() override { return m_widget; }
    bool setLogs(const QList<LogEntry>& content) override;
    void setFilter(const FilterOptions& options) override;
    QList<FieldInfo> availableFields() const override;
    QSet<int> filteredLines() const override;
    void synchronizeFilteredLines(const QSet<int>& lines) override;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

private slots:
    void onPatternChanged(const QString& pattern);
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
    void onSettingsClicked();

private:
    QWidget* m_widget;
    QVBoxLayout* m_layout;
    QLineEdit* m_patternEdit;
    QPushButton* m_settingsButton;
    QTableView* m_tableView;
    RegexTableModel* m_model;
    QList<RegexFieldInfo> m_fields;
};

#endif // REGEXVIEWER_H
