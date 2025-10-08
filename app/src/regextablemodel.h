#ifndef REGEXTABLEMODEL_H
#define REGEXTABLEMODEL_H

#include <QAbstractTableModel>
#include <QVariant>
#include <QStringList>
#include <QSqlQuery>
#include <QTimer>
#include "databasemanager.h"
#include "plugininterface.h"

struct ColumnDefinition {
    QString name;
    QString displayName;
    DataType type;
    bool sortable = true;
    bool filterable = true;
    int defaultWidth = -1; // -1 means auto-size
    Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter;
};

struct SortOrder {
    int column;
    Qt::SortOrder order;
};

class RegexTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit RegexTableModel(DatabaseManager* database, QObject* parent = nullptr);
    ~RegexTableModel() override;

    // QAbstractTableModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool canFetchMore(const QModelIndex& parent = QModelIndex()) const override;
    void fetchMore(const QModelIndex& parent = QModelIndex()) override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    // Configuration
    void setColumnDefinitions(const QList<ColumnDefinition>& columns);
    void setTableName(const QString& tableName);
    void setBatchSize(int batchSize) { m_batchSize = batchSize; }
    void setLazyLoadingEnabled(bool enabled) { m_lazyLoadingEnabled = enabled; }

    // Data management
    void refresh();
    void clear();
    bool isEmpty() const { return m_totalRowCount == 0; }
    int totalRowCount() const { return m_totalRowCount; }

    // Filtering and searching
    void applyFilter(const FilterOptions& options);
    void performSearch(const SearchQuery& query);
    void clearFilter();
    void clearSearch();

    // Row mapping between model and database
    int mapToSourceRow(int modelRow) const;
    int mapFromSourceRow(int sourceRow) const;
    QList<int> getSourceRowsForModelRows(const QList<int>& modelRows) const;

    // Data access
    QVariant getFieldValue(int row, const QString& fieldName) const;
    QVariantMap getRowData(int row) const;
    QStringList getFieldNames() const;
    
    // Selection support
    QList<int> getSelectedSourceRows() const { return m_selectedSourceRows; }
    void setSelectedSourceRows(const QList<int>& rows);

signals:
    void dataRefreshed();
    void filterApplied();
    void searchCompleted(int resultCount);
    void errorOccurred(const QString& error);
    void loadingStarted();
    void loadingFinished();

private slots:
    void onDatabaseStatusChanged(DatabaseStatus status);
    void onDatabaseError(const QString& error);
    void performDelayedRefresh();

private:
    // Data loading
    bool loadData(int offset = 0, int limit = -1);
    bool loadSearchResults();
    QString buildQuery(int offset = 0, int limit = -1) const;
    QString buildFilterCondition() const;
    QString buildSortCondition() const;
    void cacheRowData(int startRow, const QSqlQuery& query);
    void clearCache();

    // Query building helpers
    QString escapeFieldName(const QString& fieldName) const;
    QString buildRegexCondition(const QString& field, const QString& pattern, Qt::CaseSensitivity cs) const;
    QString buildTextSearchCondition(const QString& field, const QString& text, Qt::CaseSensitivity cs) const;

    // Data formatting
    QVariant formatDisplayData(const QVariant& value, DataType type) const;
    QVariant formatUserRole(const QVariant& value, DataType type) const;
    Qt::Alignment getColumnAlignment(int column) const;
    
    // Background data operations
    void startBackgroundLoad();
    void stopBackgroundLoad();

    DatabaseManager* m_database;
    QString m_tableName;
    QList<ColumnDefinition> m_columns;
    
    // Data cache
    mutable QHash<int, QVariantList> m_rowCache;
    mutable int m_cacheStartRow;
    mutable int m_cacheSize;
    
    // Lazy loading
    bool m_lazyLoadingEnabled;
    int m_batchSize;
    int m_loadedRowCount;
    int m_totalRowCount;
    
    // Filtering and searching
    FilterOptions m_currentFilter;
    SearchQuery m_currentSearch;
    bool m_hasActiveFilter;
    bool m_hasActiveSearch;
    QList<SearchResult> m_searchResults;
    
    // Sorting
    QList<SortOrder> m_sortOrders;
    
    // Row mapping
    mutable QVector<int> m_modelToSourceMap;
    mutable QHash<int, int> m_sourceToModelMap;
    mutable bool m_mappingValid;
    
    // Selection tracking
    QList<int> m_selectedSourceRows;
    
    // Background operations
    QTimer* m_refreshTimer;
    bool m_refreshPending;
    
    static const int DEFAULT_BATCH_SIZE = 100;
    static const int MAX_CACHE_SIZE = 1000;
};

#endif // REGEXTABLEMODEL_H
