#pragma once

// Internal shared text-matching primitives for FilterScan and Query.
// Not installed; include from core/src only.

#include <QByteArrayView>
#include <QRegularExpression>
#include <QString>

#include <algorithm>
#include <cstring>

namespace logdor::detail {

inline bool isAsciiOnly(const QString& s)
{
    return std::all_of(s.begin(), s.end(),
                       [](QChar c) { return c.unicode() < 0x80; });
}

// Substring search with a memchr first-byte skip. @p needle must be
// pre-lowercased when @p folded; ASCII-only case folding (matches
// QString::contains for ASCII needles under Qt's simple folding).
inline bool containsAscii(QByteArrayView hay, QByteArrayView needle, bool folded)
{
    const qsizetype n = needle.size();
    if (n == 0)
        return true;
    if (hay.size() < n)
        return false;

    const char first = needle[0];
    const char firstUp = folded && first >= 'a' && first <= 'z'
        ? char(first - 32) : first;
    const char* p = hay.data();
    const char* const end = hay.data() + hay.size() - n + 1;

    while (p < end) {
        const char* c1 = static_cast<const char*>(std::memchr(p, first, size_t(end - p)));
        const char* c = c1;
        if (firstUp != first) {
            const char* c2 = static_cast<const char*>(
                std::memchr(p, firstUp, size_t(end - p)));
            if (!c || (c2 && c2 < c))
                c = c2;
        }
        if (!c)
            return false;
        bool ok = true;
        for (qsizetype i = 1; i < n; ++i) {
            char h = c[i];
            if (folded && h >= 'A' && h <= 'Z')
                h = char(h + 32);
            if (h != needle[i]) {
                ok = false;
                break;
            }
        }
        if (ok)
            return true;
        p = c + 1;
    }
    return false;
}

// Compiled once; match paths are thread-safe (QRegularExpression::match is
// documented thread-safe on a shared const instance).
struct Matcher {
    enum class Mode { Empty, AsciiExact, AsciiFolded, Utf16, Regex };
    Mode mode = Mode::Empty;
    QByteArray asciiNeedle; // pre-lowercased in AsciiFolded mode
    QString utf16Needle;
    Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    QRegularExpression regex;

    Matcher(const QString& query, bool caseSensitive, bool regexMode)
    {
        cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        if (query.isEmpty())
            return;
        if (regexMode) {
            mode = Mode::Regex;
            regex = QRegularExpression(query,
                caseSensitive ? QRegularExpression::NoPatternOption
                              : QRegularExpression::CaseInsensitiveOption);
            // Invalid pattern: never matches (legacy behavior).
        } else if (isAsciiOnly(query)) {
            mode = caseSensitive ? Mode::AsciiExact : Mode::AsciiFolded;
            asciiNeedle = query.toUtf8();
            if (mode == Mode::AsciiFolded)
                asciiNeedle = asciiNeedle.toLower();
        } else {
            mode = Mode::Utf16;
            utf16Needle = query;
        }
    }

    bool textMatches(QByteArrayView raw) const
    {
        switch (mode) {
        case Mode::Empty:
            return true;
        case Mode::AsciiExact:
            return containsAscii(raw, asciiNeedle, false);
        case Mode::AsciiFolded:
            return containsAscii(raw, asciiNeedle, true);
        case Mode::Utf16:
            return QString::fromUtf8(raw).contains(utf16Needle, cs);
        case Mode::Regex:
            return regex.isValid()
                && regex.match(QString::fromUtf8(raw)).hasMatch();
        }
        return false;
    }
};

} // namespace logdor::detail
