#ifndef HIGHLIGHTRULES_H
#define HIGHLIGHTRULES_H

#include "plugininterface.h"

#include <QRegularExpression>

/// QSettings persistence for the app-wide highlight rules ("highlightRules").
Q_DECL_EXPORT QList<HighlightRule> loadHighlightRules();
Q_DECL_EXPORT void storeHighlightRules(const QList<HighlightRule>& rules);

/// A pleasant default color for the next new rule (cycles a small palette).
Q_DECL_EXPORT QColor nextHighlightColor(int existingRuleCount);

/**
 * Compiled evaluator shared by LogTableModel and the timeline: first
 * enabled matching rule wins. At most 32 enabled rules are honored -
 * evaluation is per rendered row. Rebuild via setRules on changes;
 * matching itself is const and cheap.
 */
class Q_DECL_EXPORT HighlightMatcher {
public:
    void setRules(const QList<HighlightRule>& rules);
    bool isEmpty() const { return m_rules.isEmpty(); }

    /// Background for @p text, or an invalid color when no rule matches.
    QColor match(const QString& text) const;

private:
    struct Compiled {
        HighlightRule rule;
        QRegularExpression regex; // valid only for regex rules
    };
    QList<Compiled> m_rules;
};

#endif // HIGHLIGHTRULES_H
