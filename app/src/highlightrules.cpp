#include "highlightrules.h"

#include <QSettings>

namespace {

constexpr int kMaxEnabledRules = 32;

} // namespace

QList<HighlightRule> loadHighlightRules()
{
    QSettings settings("Logdor", "Logdor");
    QList<HighlightRule> rules;
    const int count = settings.beginReadArray(QStringLiteral("highlightRules"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        HighlightRule rule;
        rule.name = settings.value("name").toString();
        rule.pattern = settings.value("pattern").toString();
        rule.regex = settings.value("regex").toBool();
        rule.caseSensitive = settings.value("caseSensitive").toBool();
        rule.enabled = settings.value("enabled", true).toBool();
        rule.color = QColor(settings.value("color").toString());
        if (!rule.pattern.isEmpty())
            rules.append(rule);
    }
    settings.endArray();
    return rules;
}

void storeHighlightRules(const QList<HighlightRule>& rules)
{
    QSettings settings("Logdor", "Logdor");
    settings.remove(QStringLiteral("highlightRules"));
    settings.beginWriteArray(QStringLiteral("highlightRules"), rules.size());
    for (int i = 0; i < rules.size(); ++i) {
        settings.setArrayIndex(i);
        const HighlightRule& rule = rules[i];
        settings.setValue("name", rule.name);
        settings.setValue("pattern", rule.pattern);
        settings.setValue("regex", rule.regex);
        settings.setValue("caseSensitive", rule.caseSensitive);
        settings.setValue("enabled", rule.enabled);
        settings.setValue("color", rule.color.name(QColor::HexRgb));
    }
    settings.endArray();
}

QColor nextHighlightColor(int existingRuleCount)
{
    // Soft pastels that keep black text readable.
    static const QColor kPalette[] = {
        QColor(255, 245, 157), // yellow
        QColor(178, 235, 242), // cyan
        QColor(248, 187, 208), // pink
        QColor(200, 230, 201), // green
        QColor(255, 204, 188), // orange
        QColor(209, 196, 233), // violet
    };
    return kPalette[existingRuleCount % (sizeof(kPalette) / sizeof(QColor))];
}

void HighlightMatcher::setRules(const QList<HighlightRule>& rules)
{
    m_rules.clear();
    for (const HighlightRule& rule : rules) {
        if (!rule.enabled || rule.pattern.isEmpty() || !rule.color.isValid())
            continue;
        if (m_rules.size() >= kMaxEnabledRules)
            break;
        Compiled compiled { rule, QRegularExpression() };
        if (rule.regex) {
            compiled.regex = QRegularExpression(
                rule.pattern, rule.caseSensitive
                    ? QRegularExpression::NoPatternOption
                    : QRegularExpression::CaseInsensitiveOption);
            if (!compiled.regex.isValid())
                continue;
        }
        m_rules.append(std::move(compiled));
    }
}

QColor HighlightMatcher::match(const QString& text) const
{
    for (const Compiled& compiled : m_rules) {
        const bool hit = compiled.rule.regex
            ? compiled.regex.match(text).hasMatch()
            : text.contains(compiled.rule.pattern,
                            compiled.rule.caseSensitive ? Qt::CaseSensitive
                                                        : Qt::CaseInsensitive);
        if (hit)
            return compiled.rule.color;
    }
    return {};
}
