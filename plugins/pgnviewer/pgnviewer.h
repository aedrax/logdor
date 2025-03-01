#ifndef PGNVIEWER_H
#define PGNVIEWER_H

#include "../../app/src/plugininterface.h"
#include "pgntablemodel.h"
#include <QTableView>
#include <QSplitter>
#include <QToolBar>
#include <QPushButton>
#include <QVBoxLayout>
#include "chessboardwidget.h"
#include "chessgame.h"
#include <QString>
#include <QtPlugin>
#include <QItemSelection>

class PgnViewer : public PluginInterface {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
public:
    explicit PgnViewer(QObject* parent = nullptr);
    ~PgnViewer();

    // PluginInterface implementation
    QString name() const override { return tr("PGN Viewer"); }
    QString version() const override { return "0.1.0"; }
    QString description() const override { return tr("A viewer for chess games in PGN format."); }
    QWidget* widget() override { return m_container; }
    bool setLogs(const QList<LogEntry>& content) override;
    void setFilter(const FilterOptions& options) override;
    QList<FieldInfo> availableFields() const override;
    QSet<int> filteredLines() const override;
    void synchronizeFilteredLines(const QSet<int>& lines) override;

public slots:
    void onPluginEvent(PluginEvent event, const QVariant& data) override;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
    void updateBoard(int row);
    void nextMove();
    void previousMove();
    void firstMove();
    void lastMove();

private:
    QWidget* m_container;
    QVBoxLayout* m_layout;
    QToolBar* m_toolbar;
    QSplitter* m_splitter;
    QTableView* m_tableView;
    PgnTableModel* m_model;
    ChessboardWidget* m_board;
    ChessGame m_game;
    bool m_showingBlackMove;  // true if showing black's move in current row
    
    QPushButton* m_firstButton;
    QPushButton* m_prevButton;
    QPushButton* m_nextButton;
    QPushButton* m_lastButton;
    
    // Helper methods
    void parsePgnGame(const QString& content);
    QString calculateFenForMove(int row, bool includeBlackMove);
};

#endif // PGNVIEWER_H
