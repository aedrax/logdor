#ifndef FILTERTOOLBAR_H
#define FILTERTOOLBAR_H

#include "plugininterface.h"

#include <QStringList>
#include <QWidget>

class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;

/**
 * The shared filter bar: input + mode toggles + time-range picker + context
 * spinboxes, debounced into filterChanged(FilterOptions). Extracted from
 * MainWindow so saved queries, highlight rules, and session persistence
 * have one owner for filter state.
 *
 * Text edits debounce; toggles and programmatic applications fire in one
 * shot. setOptionsSilently() updates the widgets without emitting - session
 * restore applies the options itself.
 */
class Q_DECL_EXPORT FilterToolbar : public QWidget {
    Q_OBJECT
public:
    explicit FilterToolbar(QWidget* parent = nullptr);

    /// The current widget state as options.
    FilterOptions options() const;

    /// Update every control without emitting filterChanged.
    void setOptionsSilently(const FilterOptions& options);

    /**
     * Replace (never stack) the @time terms previously injected by the
     * range picker or a histogram brush with @p terms, then emit. An empty
     * list just strips them.
     */
    void applyTimeRange(const QStringList& terms);

    /// Add a query-language term (implicit AND), switching to query mode -
    /// a plain-text filter survives as a quoted free-text term. Emits once.
    void appendTerm(const QString& term);

    /// The @time terms currently injected; session capture/restore.
    QStringList timeRangeTerms() const { return m_timeRangeTerms; }
    void setTimeRangeTermsSilently(const QStringList& terms)
    {
        m_timeRangeTerms = terms;
    }

    void focusInput();

    /// Saved filter presets (QSettings "savedQueries").
    struct SavedQuery {
        QString name;
        FilterOptions options;
    };
    static QList<SavedQuery> loadSavedQueries();
    static void storeSavedQueries(const QList<SavedQuery>& queries);

signals:
    void filterChanged(const FilterOptions& options);

private:
    void emitFilterChanged(); // tints the input, then emits
    void showSavedQueriesMenu();
    void saveCurrentFilter();
    void manageSavedQueries();

    QPushButton* m_savedButton = nullptr;

    QLineEdit* m_input = nullptr;
    QPushButton* m_caseSensitiveButton = nullptr;
    QPushButton* m_invertButton = nullptr;
    QPushButton* m_queryModeButton = nullptr;
    QPushButton* m_regexModeButton = nullptr;
    QSpinBox* m_beforeSpinBox = nullptr;
    QSpinBox* m_afterSpinBox = nullptr;
    QTimer* m_debounce = nullptr;
    QStringList m_timeRangeTerms;
};

#endif // FILTERTOOLBAR_H
