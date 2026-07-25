#include "logdor/Query.h"

#include "TextMatch_p.h"

#include <QDateTime>
#include <QRegularExpression>

#include <algorithm>

namespace logdor {

//=== ColumnData ==============================================================

ColumnData::Builder::Builder(FieldType t, TimestampCodec c)
    : type(t)
    , codec(std::move(c))
{
    if (type == FieldType::Integer) {
        // no blob
    } else {
        offsets.push_back(0);
    }
}

void ColumnData::Builder::appendString(QByteArrayView utf8)
{
    blob.append(utf8.data(), utf8.size());
    offsets.push_back(quint64(blob.size()));
}

void ColumnData::Builder::appendInt(const QString& text)
{
    bool ok = false;
    const qint64 value = text.toLongLong(&ok);
    ints.push_back(ok ? value : 0);
    intValid.push_back(ok);
}

void ColumnData::Builder::append(const QString& fieldText, FieldType t)
{
    if (t == FieldType::Integer) {
        appendInt(fieldText);
        return;
    }
    appendString(fieldText.toUtf8());
    if (t == FieldType::DateTime) {
        qint64 ms = 0;
        const bool ok = codec.parse(fieldText, &ms);
        ints.push_back(ok ? ms : 0);
        intValid.push_back(ok);
    }
}

qint64 ColumnData::Builder::count() const
{
    return type == FieldType::Integer ? qint64(ints.size())
                                      : qint64(offsets.size()) - 1;
}

ColumnData ColumnData::Builder::build() &&
{
    ColumnData data;
    data.m_type = type;
    data.m_count = count();
    data.m_validIntCount
        = std::count(intValid.begin(), intValid.end(), true);
    data.m_monotonicTime = type == FieldType::DateTime && codec.isMonotonic();
    data.m_blob = std::move(blob);
    data.m_offsets = std::move(offsets);
    data.m_ints = std::move(ints);
    data.m_intValid = std::move(intValid);
    data.m_blob.squeeze();
    data.m_offsets.shrink_to_fit();
    data.m_ints.shrink_to_fit();
    data.m_intValid.shrink_to_fit();
    return data;
}

ColumnData ColumnData::appended(const ColumnData& head, qint64 headRows,
                                const ColumnData& tail)
{
    Q_ASSERT(head.m_type == tail.m_type);
    Q_ASSERT(headRows >= 0 && headRows <= head.m_count);

    ColumnData out;
    out.m_type = head.m_type;
    out.m_count = headRows + tail.m_count;
    out.m_monotonicTime = head.m_monotonicTime;

    if (head.m_type != FieldType::Integer) { // text lane
        const quint64 headBlobEnd = head.m_offsets[size_t(headRows)];
        out.m_blob.reserve(qsizetype(headBlobEnd) + tail.m_blob.size());
        out.m_blob.append(head.m_blob.constData(), qsizetype(headBlobEnd));
        out.m_blob.append(tail.m_blob);
        out.m_offsets.reserve(size_t(out.m_count) + 1);
        out.m_offsets.insert(out.m_offsets.end(), head.m_offsets.begin(),
                             head.m_offsets.begin() + headRows + 1);
        for (size_t k = 1; k < tail.m_offsets.size(); ++k)
            out.m_offsets.push_back(tail.m_offsets[k] + headBlobEnd);
    }
    if (head.m_type != FieldType::String) { // integer lane
        out.m_ints.reserve(size_t(out.m_count));
        out.m_ints.insert(out.m_ints.end(), head.m_ints.begin(),
                          head.m_ints.begin() + headRows);
        out.m_ints.insert(out.m_ints.end(), tail.m_ints.begin(),
                          tail.m_ints.end());
        out.m_intValid.reserve(size_t(out.m_count));
        out.m_intValid.insert(out.m_intValid.end(), head.m_intValid.begin(),
                              head.m_intValid.begin() + headRows);
        out.m_intValid.insert(out.m_intValid.end(), tail.m_intValid.begin(),
                              tail.m_intValid.end());
        out.m_validIntCount
            = std::count(out.m_intValid.begin(), out.m_intValid.end(), true);
    }
    return out;
}

size_t ColumnData::memoryUsage() const
{
    return size_t(m_blob.capacity()) + m_offsets.capacity() * sizeof(quint64)
        + m_ints.capacity() * sizeof(qint64) + m_intValid.capacity() / 8;
}

//=== AST =====================================================================

namespace {

enum class CmpOp : quint8 { Contains, Equals, NotEquals, Lt, Le, Gt, Ge };

// ASCII case-folded byte comparisons for string terms.
bool bytesEqualFolded(QByteArrayView a, QByteArrayView b, bool folded)
{
    if (a.size() != b.size())
        return false;
    if (!folded)
        return std::memcmp(a.data(), b.data(), size_t(a.size())) == 0;
    for (qsizetype i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x = char(x + 32);
        if (y >= 'A' && y <= 'Z')
            y = char(y + 32);
        if (x != y)
            return false;
    }
    return true;
}

} // namespace

struct CompiledQuery::Node {
    enum class Kind : quint8 {
        And, Or, Not,
        AlwaysTrue,
        FreeText,     // matcher over the raw line
        StringCmp,    // column bytes vs needle / wildcard regex
        IntCmp,       // column int vs literal
        SeverityCmp,  // severity byte vs literal, enum order
        DateTimeCmp,  // column epoch ms vs the literal's [lower, upper) range
        TimeOfDayCmp, // epoch ms -> zone-local ms-of-day vs [lower, upper)
    };

    Kind kind;
    std::vector<std::unique_ptr<Node>> children;

    // FreeText
    std::optional<detail::Matcher> matcher;

    // StringCmp / IntCmp / SeverityCmp / DateTimeCmp / TimeOfDayCmp
    int column = -1;
    CmpOp op = CmpOp::Contains;
    QByteArray needleUtf8;       // pre-lowercased when folded
    bool folded = false;
    bool needleAscii = true;
    QString needleUtf16;         // non-ASCII path
    std::optional<QRegularExpression> wildcard; // anchored, for '*'/'?'
    qint64 intLiteral = 0;
    quint8 severityLiteral = 0;
    qint64 lowerMs = 0, upperMs = 0; // DateTimeCmp epoch / TimeOfDayCmp tod
    std::shared_ptr<const std::vector<std::pair<qint64, qint32>>>
        todOffsets; // TimeOfDayCmp: zone steps for epoch -> local time of day

    bool eval(qint64 line, QByteArrayView raw, const ColumnSnapshot& cols) const
    {
        switch (kind) {
        case Kind::And:
            return std::all_of(children.begin(), children.end(),
                               [&](const auto& c) { return c->eval(line, raw, cols); });
        case Kind::Or:
            return std::any_of(children.begin(), children.end(),
                               [&](const auto& c) { return c->eval(line, raw, cols); });
        case Kind::Not:
            return !children.front()->eval(line, raw, cols);
        case Kind::AlwaysTrue:
            return true;
        case Kind::FreeText:
            return matcher->textMatches(raw);
        case Kind::StringCmp: {
            const QByteArrayView value = cols.columns[column]->stringAt(line);
            if (wildcard)
                return wildcard->match(QString::fromUtf8(value)).hasMatch();
            switch (op) {
            case CmpOp::Contains:
                if (needleAscii)
                    return detail::containsAscii(value, needleUtf8, folded);
                return QString::fromUtf8(value).contains(
                    needleUtf16, folded ? Qt::CaseInsensitive : Qt::CaseSensitive);
            case CmpOp::Equals:
                if (needleAscii)
                    return bytesEqualFolded(value, needleUtf8, folded);
                return QString::fromUtf8(value).compare(
                           needleUtf16,
                           folded ? Qt::CaseInsensitive : Qt::CaseSensitive) == 0;
            case CmpOp::NotEquals:
                if (needleAscii)
                    return !bytesEqualFolded(value, needleUtf8, folded);
                return QString::fromUtf8(value).compare(
                           needleUtf16,
                           folded ? Qt::CaseInsensitive : Qt::CaseSensitive) != 0;
            default: {
                // Ordering: lexicographic on raw bytes (documented).
                const int c = value.compare(QByteArrayView(needleUtf8));
                switch (op) {
                case CmpOp::Lt: return c < 0;
                case CmpOp::Le: return c <= 0;
                case CmpOp::Gt: return c > 0;
                case CmpOp::Ge: return c >= 0;
                default: return false;
                }
            }
            }
            return false;
        }
        case Kind::IntCmp: {
            qint64 value = 0;
            if (!cols.columns[column]->intAt(line, &value))
                return false; // unparseable rows never match
            switch (op) {
            case CmpOp::Contains:
            case CmpOp::Equals: return value == intLiteral;
            case CmpOp::NotEquals: return value != intLiteral;
            case CmpOp::Lt: return value < intLiteral;
            case CmpOp::Le: return value <= intLiteral;
            case CmpOp::Gt: return value > intLiteral;
            case CmpOp::Ge: return value >= intLiteral;
            }
            return false;
        }
        case Kind::SeverityCmp: {
            const quint8 value = (*cols.severity)[size_t(line)];
            switch (op) {
            case CmpOp::Contains:
            case CmpOp::Equals: return value == severityLiteral;
            case CmpOp::NotEquals: return value != severityLiteral;
            case CmpOp::Lt: return value < severityLiteral;
            case CmpOp::Le: return value <= severityLiteral;
            case CmpOp::Gt: return value > severityLiteral;
            case CmpOp::Ge: return value >= severityLiteral;
            }
            return false;
        }
        case Kind::DateTimeCmp: {
            qint64 value = 0;
            if (!cols.columns[column]->intAt(line, &value))
                return false; // unparseable timestamps never match
            // The literal is the half-open range [lowerMs, upperMs) at its
            // own granularity; ordering ops compare against its bounds.
            switch (op) {
            case CmpOp::Contains:
            case CmpOp::Equals: return value >= lowerMs && value < upperMs;
            case CmpOp::NotEquals: return value < lowerMs || value >= upperMs;
            case CmpOp::Lt: return value < lowerMs;
            case CmpOp::Le: return value < upperMs;
            case CmpOp::Gt: return value >= upperMs;
            case CmpOp::Ge: return value >= lowerMs;
            }
            return false;
        }
        case Kind::TimeOfDayCmp: {
            qint64 value = 0;
            if (!cols.columns[column]->intAt(line, &value))
                return false;
            const qint32 offset
                = todOffsets ? zoneOffsetAt(*todOffsets, value) : 0;
            qint64 tod = (value + offset) % 86'400'000;
            if (tod < 0)
                tod += 86'400'000;
            switch (op) {
            case CmpOp::Contains:
            case CmpOp::Equals: return tod >= lowerMs && tod < upperMs;
            case CmpOp::NotEquals: return tod < lowerMs || tod >= upperMs;
            case CmpOp::Lt: return tod < lowerMs;
            case CmpOp::Le: return tod < upperMs;
            case CmpOp::Gt: return tod >= upperMs;
            case CmpOp::Ge: return tod >= lowerMs;
            }
            return false;
        }
        }
        return false;
    }
};

//=== Tokenizer ===============================================================

namespace {

struct Token {
    enum class Kind : quint8 {
        End, LParen, RParen, KwAnd, KwOr, KwNot,
        Word,   // bareword (may become field/op/value or free text)
        Quoted, // "..." with the quotes stripped
    };
    Kind kind = Kind::End;
    QString text;
    qsizetype position = 0;
    qsizetype length = 0;
};

class Tokenizer {
public:
    explicit Tokenizer(const QString& input) : m_input(input) {}

    // Characters ending a bareword. ':' and comparison chars stay inside the
    // word; the term splitter handles them.
    static bool isWordEnd(QChar c)
    {
        return c.isSpace() || c == u'(' || c == u')' || c == u'"';
    }

    Token next()
    {
        while (m_pos < m_input.size() && m_input[m_pos].isSpace())
            ++m_pos;
        Token token;
        token.position = m_pos;
        if (m_pos >= m_input.size())
            return token;

        const QChar c = m_input[m_pos];
        if (c == u'(') {
            ++m_pos;
            token.kind = Token::Kind::LParen;
            token.length = 1;
            return token;
        }
        if (c == u')') {
            ++m_pos;
            token.kind = Token::Kind::RParen;
            token.length = 1;
            return token;
        }
        if (c == u'"') {
            ++m_pos;
            QString value;
            bool closed = false;
            while (m_pos < m_input.size()) {
                const QChar q = m_input[m_pos];
                if (q == u'\\' && m_pos + 1 < m_input.size()
                    && (m_input[m_pos + 1] == u'"' || m_input[m_pos + 1] == u'\\')) {
                    value += m_input[m_pos + 1];
                    m_pos += 2;
                    continue;
                }
                if (q == u'"') {
                    ++m_pos;
                    closed = true;
                    break;
                }
                value += q;
                ++m_pos;
            }
            token.kind = Token::Kind::Quoted;
            token.text = value;
            token.length = m_pos - token.position;
            m_unterminated = !closed;
            return token;
        }

        QString word;
        while (m_pos < m_input.size() && !isWordEnd(m_input[m_pos])) {
            word += m_input[m_pos];
            ++m_pos;
        }
        token.length = m_pos - token.position;
        token.text = word;
        if (word.compare(u"and", Qt::CaseInsensitive) == 0)
            token.kind = Token::Kind::KwAnd;
        else if (word.compare(u"or", Qt::CaseInsensitive) == 0)
            token.kind = Token::Kind::KwOr;
        else if (word.compare(u"not", Qt::CaseInsensitive) == 0)
            token.kind = Token::Kind::KwNot;
        else
            token.kind = Token::Kind::Word;
        return token;
    }

    bool unterminatedQuote() const { return m_unterminated; }

private:
    const QString& m_input;
    qsizetype m_pos = 0;
    bool m_unterminated = false;
};

QString normalizeFieldName(QStringView name)
{
    QString out;
    out.reserve(name.size());
    for (QChar c : name) {
        if (!c.isSpace())
            out += c.toLower();
    }
    return out;
}

std::optional<Severity> severityFromName(const QString& name)
{
    const QString n = name.toLower();
    if (n == u"none" || n == u"unknown") return Severity::None;
    if (n == u"verbose") return Severity::Verbose;
    if (n == u"debug") return Severity::Debug;
    if (n == u"info") return Severity::Info;
    if (n == u"warning" || n == u"warn") return Severity::Warning;
    if (n == u"error") return Severity::Error;
    if (n == u"fatal") return Severity::Fatal;
    return std::nullopt;
}

} // namespace

//=== Parser ==================================================================

struct QueryParser {
    using Node = CompiledQuery::Node;
    using Kind = CompiledQuery::Node::Kind;

    Tokenizer tokenizer;
    Token current;
    const QList<FieldSchema>& schema;
    Qt::CaseSensitivity cs;
    QueryOptions options;
    const TimeParseContext& timeContext;
    QueryError error;
    QList<int> referencedColumns;
    bool needsSeverity = false;
    std::shared_ptr<const std::vector<std::pair<qint64, qint32>>> m_todOffsets;

    QueryParser(const QString& text, const QList<FieldSchema>& schema,
                Qt::CaseSensitivity cs, QueryOptions options,
                const TimeParseContext& timeContext)
        : tokenizer(text)
        , schema(schema)
        , cs(cs)
        , options(options)
        , timeContext(timeContext)
    {
        current = tokenizer.next();
    }

    // Zone steps shared by every time-of-day term in this query, built once.
    std::shared_ptr<const std::vector<std::pair<qint64, qint32>>> todOffsets()
    {
        if (!m_todOffsets) {
            const QDate today = QDate::currentDate();
            const int refYear = timeContext.referenceYear > 0
                ? timeContext.referenceYear : today.year();
            const qint64 to = QDateTime(QDate(std::max(refYear, today.year()) + 2,
                                              1, 1),
                                        QTime(0, 0), QTimeZone::utc())
                                  .toMSecsSinceEpoch();
            m_todOffsets = std::make_shared<
                const std::vector<std::pair<qint64, qint32>>>(
                zoneOffsetTable(timeContext.assumedZone, 0, to));
        }
        return m_todOffsets;
    }

    void advance() { current = tokenizer.next(); }

    void fail(const Token& at, const QString& message)
    {
        if (!error.isError()) {
            error.position = at.position;
            error.length = qMax<qsizetype>(at.length, 1);
            error.message = message;
        }
    }

    std::unique_ptr<Node> makeNode(Kind kind)
    {
        auto node = std::make_unique<Node>();
        node->kind = kind;
        return node;
    }

    std::unique_ptr<Node> parseOr()
    {
        auto left = parseAnd();
        if (!left)
            return nullptr;
        while (current.kind == Token::Kind::KwOr) {
            advance();
            auto right = parseAnd();
            if (!right)
                return nullptr;
            auto node = makeNode(Kind::Or);
            node->children.push_back(std::move(left));
            node->children.push_back(std::move(right));
            left = std::move(node);
        }
        return left;
    }

    static bool startsUnary(const Token& t)
    {
        switch (t.kind) {
        case Token::Kind::KwNot:
        case Token::Kind::LParen:
        case Token::Kind::Word:
        case Token::Kind::Quoted:
            return true;
        default:
            return false;
        }
    }

    std::unique_ptr<Node> parseAnd()
    {
        auto left = parseUnary();
        if (!left)
            return nullptr;
        while (true) {
            if (current.kind == Token::Kind::KwAnd) {
                advance();
                if (!startsUnary(current)) {
                    fail(current, QStringLiteral("expected a term after AND"));
                    return nullptr;
                }
            } else if (!startsUnary(current)) {
                break; // implicit AND only continues on a term start
            }
            auto right = parseUnary();
            if (!right)
                return nullptr;
            auto node = makeNode(Kind::And);
            node->children.push_back(std::move(left));
            node->children.push_back(std::move(right));
            left = std::move(node);
        }
        return left;
    }

    std::unique_ptr<Node> parseUnary()
    {
        if (current.kind == Token::Kind::KwNot) {
            const Token notToken = current;
            advance();
            if (!startsUnary(current)) {
                fail(notToken, QStringLiteral("expected a term after NOT"));
                return nullptr;
            }
            auto operand = parseUnary();
            if (!operand)
                return nullptr;
            auto node = makeNode(Kind::Not);
            node->children.push_back(std::move(operand));
            return node;
        }
        if (current.kind == Token::Kind::LParen) {
            const Token open = current;
            advance();
            auto inner = parseOr();
            if (!inner)
                return nullptr;
            if (current.kind != Token::Kind::RParen) {
                fail(open, QStringLiteral("unbalanced '('"));
                return nullptr;
            }
            advance();
            return inner;
        }
        if (current.kind == Token::Kind::Quoted) {
            auto node = freeTextNode(current.text);
            advance();
            return node;
        }
        if (current.kind == Token::Kind::Word)
            return parseWordTerm();

        fail(current, current.kind == Token::Kind::RParen
                          ? QStringLiteral("unexpected ')'")
                          : QStringLiteral("expected a term"));
        return nullptr;
    }

    std::unique_ptr<Node> freeTextNode(const QString& text)
    {
        auto node = makeNode(Kind::FreeText);
        node->matcher.emplace(text, cs == Qt::CaseSensitive, false);
        return node;
    }

    // A Word token: either FIELD op VALUE (op embedded in the word) or free text.
    std::unique_ptr<Node> parseWordTerm()
    {
        const Token word = current;
        const QString& text = word.text;

        struct OpSpec { const char16_t* text; CmpOp op; };
        static constexpr OpSpec kOps[] = {
            { u"!=", CmpOp::NotEquals }, { u"<=", CmpOp::Le },
            { u">=", CmpOp::Ge },        { u":", CmpOp::Contains },
            { u"=", CmpOp::Equals },     { u"<", CmpOp::Lt },
            { u">", CmpOp::Gt },
        };

        qsizetype opPos = -1;
        CmpOp op = CmpOp::Contains;
        qsizetype opLen = 0;
        for (qsizetype i = 0; i < text.size() && opPos < 0; ++i) {
            for (const OpSpec& spec : kOps) {
                const QString opText = QString(spec.text);
                if (QStringView(text).mid(i).startsWith(opText)) {
                    opPos = i;
                    op = spec.op;
                    opLen = opText.size();
                    break;
                }
            }
        }

        if (opPos < 0) {
            advance();
            return freeTextNode(text);
        }
        if (opPos == 0) {
            fail(word, QStringLiteral("expected a field name before the operator"));
            return nullptr;
        }

        const QString fieldName = text.left(opPos);
        QString value = text.mid(opPos + opLen);
        advance();
        if (value.isEmpty()) {
            // Value is the next token: `tag: "foo bar"` or `tag: value`.
            if (current.kind == Token::Kind::Quoted
                || current.kind == Token::Kind::Word) {
                value = current.text;
                advance();
            } else {
                fail(word, QStringLiteral("missing value for field '%1'")
                               .arg(fieldName));
                return nullptr;
            }
        }
        return fieldTerm(word, fieldName, op, value);
    }

    std::unique_ptr<Node> fieldTerm(const Token& at, const QString& fieldName,
                                    CmpOp op, const QString& value)
    {
        const QString normalized = normalizeFieldName(fieldName);
        int column = -1;
        for (int i = 0; i < schema.size(); ++i) {
            if (normalizeFieldName(schema[i].name) == normalized) {
                column = i;
                break;
            }
        }
        // Reserved pseudo-field: the schema's first timestamp column, so
        // shell affordances (the time-range picker) can emit one term that
        // fits every viewer regardless of what its column is called.
        if (column < 0 && normalized == u"@time") {
            for (int i = 0; i < schema.size() && column < 0; ++i) {
                if (schema[i].hint == FieldHint::Timestamp)
                    column = i;
            }
            for (int i = 0; i < schema.size() && column < 0; ++i) {
                if (schema[i].type == FieldType::DateTime)
                    column = i;
            }
        }
        if (column < 0) {
            if (options.testFlag(QueryOption::AllowUnknownFields))
                return makeNode(Kind::AlwaysTrue);
            fail(at, QStringLiteral("unknown field '%1'").arg(fieldName));
            return nullptr;
        }

        const FieldSchema& field = schema[column];

        // Severity-named fields with a recognized level compare enum order.
        if (field.hint == FieldHint::SeverityName) {
            if (const auto severity = severityFromName(value)) {
                auto node = makeNode(Kind::SeverityCmp);
                node->op = op;
                node->severityLiteral = quint8(*severity);
                needsSeverity = true;
                return node;
            }
            // Unrecognized level name: fall through to string semantics.
        }

        if (field.type == FieldType::Integer) {
            bool ok = false;
            const qint64 literal = value.toLongLong(&ok);
            if (!ok) {
                fail(at, QStringLiteral("field '%1' is numeric; '%2' is not a number")
                             .arg(field.name, value));
                return nullptr;
            }
            auto node = makeNode(Kind::IntCmp);
            node->op = op;
            node->column = column;
            node->intLiteral = literal;
            if (!referencedColumns.contains(column))
                referencedColumns.append(column);
            return node;
        }

        if (field.type == FieldType::DateTime) {
            if (auto node = dateTimeTerm(at, field, column, op, value)) {
                if (!referencedColumns.contains(column))
                    referencedColumns.append(column);
                return node;
            }
            if (error.isError())
                return nullptr;
            // Value is no recognizable time literal: ':'/'='/'!=' keep the
            // string semantics below (contains/wildcard/exact on the text).
        }

        // String / unparseable DateTime.
        auto node = makeNode(Kind::StringCmp);
        node->op = op;
        node->column = column;
        node->folded = cs == Qt::CaseInsensitive;
        if (op == CmpOp::Contains
            && (value.contains(u'*') || value.contains(u'?'))) {
            node->wildcard.emplace(QRegularExpression::wildcardToRegularExpression(
                                       value, QRegularExpression::DefaultWildcardConversion),
                                   cs == Qt::CaseSensitive
                                       ? QRegularExpression::NoPatternOption
                                       : QRegularExpression::CaseInsensitiveOption);
        } else {
            node->needleAscii = detail::isAsciiOnly(value);
            node->needleUtf8 = value.toUtf8();
            if (node->folded && node->needleAscii)
                node->needleUtf8 = node->needleUtf8.toLower();
            node->needleUtf16 = value;
        }
        if (!referencedColumns.contains(column))
            referencedColumns.append(column);
        return node;
    }

    // Temporal term for a DateTime column, or nullptr: with error set when
    // the literal is unusable, without to fall back to string semantics.
    std::unique_ptr<Node> dateTimeTerm(const Token& at, const FieldSchema& field,
                                       int column, CmpOp op,
                                       const QString& value)
    {
        const bool ordering = op == CmpOp::Lt || op == CmpOp::Le
            || op == CmpOp::Gt || op == CmpOp::Ge;
        const QString trimmed = value.trimmed();

        // Monotonic uptime columns count seconds since boot: the literal is
        // a plain number of seconds, never a calendar instant.
        const TimestampCodec codec
            = TimestampCodec::fromFormatString(field.timeFormat, timeContext);
        if (codec.isMonotonic()) {
            qint64 ms = 0;
            if (!codec.parse(trimmed, &ms)) {
                if (ordering
                    || parseTimeLiteral(trimmed, timeContext).kind
                        != TimeLiteral::Kind::Invalid) {
                    fail(at, QStringLiteral("field '%1' counts seconds since "
                                            "boot; use a number like 123.456")
                                 .arg(field.name));
                }
                return nullptr;
            }
            auto node = makeNode(Kind::DateTimeCmp);
            node->op = op;
            node->column = column;
            node->lowerMs = ms;
            node->upperMs = ms + fractionGranularityMs(trimmed);
            return node;
        }

        const TimeLiteral literal = parseTimeLiteral(trimmed, timeContext);
        switch (literal.kind) {
        case TimeLiteral::Kind::Absolute: {
            auto node = makeNode(Kind::DateTimeCmp);
            node->op = op;
            node->column = column;
            node->lowerMs = literal.lowerMs;
            node->upperMs = literal.upperMs;
            return node;
        }
        case TimeLiteral::Kind::TimeOfDay: {
            auto node = makeNode(Kind::TimeOfDayCmp);
            node->op = op;
            node->column = column;
            node->lowerMs = literal.todLowerMs;
            node->upperMs = literal.todUpperMs;
            node->todOffsets = todOffsets();
            return node;
        }
        case TimeLiteral::Kind::Invalid:
            break;
        }
        if (ordering) {
            fail(at, QStringLiteral(
                         "field '%1' is a timestamp; '%2' is not a date or time")
                         .arg(field.name, trimmed));
        }
        return nullptr;
    }

    // Granularity of a plain seconds literal ("123" = that second,
    // "123.45" = 10 ms), mirroring time-of-day fraction handling.
    static qint64 fractionGranularityMs(const QString& value)
    {
        const qsizetype dot = value.indexOf(u'.');
        if (dot < 0)
            return 1000;
        const qsizetype digits = value.size() - dot - 1;
        return digits >= 3 ? 1 : digits == 2 ? 10 : 100;
    }
};

//=== CompiledQuery ===========================================================

CompiledQuery::CompiledQuery() = default;
CompiledQuery::~CompiledQuery() = default;

std::shared_ptr<const CompiledQuery> CompiledQuery::compile(
    const QString& text, const QList<FieldSchema>& schema,
    Qt::CaseSensitivity cs, QueryOptions options, QueryError* error,
    const TimeParseContext& timeContext)
{
    const auto setError = [&](qsizetype pos, qsizetype len, const QString& msg) {
        if (error)
            *error = QueryError { pos, len, msg };
    };

    if (text.trimmed().isEmpty()) {
        setError(0, qMax<qsizetype>(text.size(), 1),
                 QStringLiteral("empty query"));
        return nullptr;
    }

    QueryParser parser(text, schema, cs, options, timeContext);
    auto root = parser.parseOr();
    if (!root || parser.error.isError()) {
        if (parser.error.isError())
            setError(parser.error.position, parser.error.length,
                     parser.error.message);
        else
            setError(0, text.size(), QStringLiteral("invalid query"));
        return nullptr;
    }
    if (parser.current.kind != Token::Kind::End) {
        setError(parser.current.position, qMax<qsizetype>(parser.current.length, 1),
                 parser.current.kind == Token::Kind::RParen
                     ? QStringLiteral("unexpected ')'")
                     : QStringLiteral("unexpected trailing input"));
        return nullptr;
    }
    if (parser.tokenizer.unterminatedQuote()) {
        setError(text.size() - 1, 1, QStringLiteral("unterminated quote"));
        return nullptr;
    }

    auto query = std::shared_ptr<CompiledQuery>(new CompiledQuery);
    query->m_root = std::move(root);
    query->m_text = text;
    query->m_referencedColumns = std::move(parser.referencedColumns);
    query->m_needsSeverity = parser.needsSeverity;
    return query;
}

bool CompiledQuery::evaluate(qint64 line, QByteArrayView raw,
                             const ColumnSnapshot& columns) const
{
    return m_root->eval(line, raw, columns);
}

//=== Term building ===========================================================

QString quoteQueryValue(const QString& value, bool forceQuote)
{
    const bool bareSafe = !forceQuote && !value.isEmpty()
        && std::none_of(value.begin(), value.end(), Tokenizer::isWordEnd);
    if (bareSafe)
        return value;
    QString escaped = value;
    escaped.replace(u'\\', QStringLiteral("\\\\"));
    escaped.replace(u'"', QStringLiteral("\\\""));
    return u'"' + escaped + u'"';
}

QString buildQueryTerm(const FieldSchema& field, const QString& value,
                       bool exclude)
{
    return buildQueryTerm(field, value,
                          exclude ? QueryCmp::NotEquals : QueryCmp::Equals);
}

QString buildQueryTerm(const FieldSchema& field, const QString& value,
                       QueryCmp op, const TimeParseContext& timeContext)
{
    // The name must survive tokenization intact: spaces are stripped (they
    // are ignored during resolution anyway), and any operator or structural
    // character would split the term in the wrong place.
    QString name;
    name.reserve(field.name.size());
    for (QChar c : field.name) {
        if (c.isSpace())
            continue;
        static const QString kMeta = QStringLiteral(":=!<>()\"");
        if (kMeta.contains(c))
            return {};
        name += c;
    }
    if (name.isEmpty())
        return {};

    static constexpr const char16_t* kOpText[]
        = { u":", u"=", u"!=", u"<", u"<=", u">", u">=" };
    const QString opText(kOpText[int(op)]);

    if (field.type == FieldType::Integer) {
        const QString trimmed = value.trimmed();
        bool ok = false;
        trimmed.toLongLong(&ok);
        return ok ? name + opText + trimmed : QString();
    }
    if (field.type == FieldType::DateTime && op >= QueryCmp::Lt) {
        // Ordering terms must compile: gate on the value parsing as a time
        // literal, the same check the query compiler applies.
        const QString trimmed = value.trimmed();
        const TimestampCodec codec
            = TimestampCodec::fromFormatString(field.timeFormat, timeContext);
        qint64 ms = 0;
        const bool ok = codec.isMonotonic()
            ? codec.parse(trimmed, &ms)
            : parseTimeLiteral(trimmed, timeContext).kind
                != TimeLiteral::Kind::Invalid;
        return ok ? name + opText + quoteQueryValue(trimmed) : QString();
    }
    return name + opText + quoteQueryValue(value);
}

} // namespace logdor
