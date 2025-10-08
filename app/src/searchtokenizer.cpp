#include "searchtokenizer.h"
#include <QDebug>
#include <QUrl>
#include <algorithm>
#include <cmath>

const QStringList SearchTokenizer::s_defaultStopWords = {
    "a", "an", "and", "are", "as", "at", "be", "by", "for", "from",
    "has", "he", "in", "is", "it", "its", "of", "on", "that", "the",
    "to", "was", "will", "with", "or", "not", "but", "this", "these",
    "those", "they", "them", "their", "there", "where", "when", "what",
    "how", "why", "who", "which", "can", "could", "should", "would",
    "do", "did", "does", "don", "won", "doesn", "wouldn", "couldn",
    "shouldn", "mustn", "needn", "daren", "hadn", "hasn", "haven",
    "isn", "wasn", "weren", "ain"
};

SearchTokenizer::SearchTokenizer()
    : m_minTokenLength(2)
    , m_maxTokenLength(50)
    , m_fuzzyThreshold(0.7f)
    , m_tokenizeNumbers(true)
    , m_tokenizePaths(true)
    , m_tokenizeUrls(true)
    , m_tokenizeEmails(true)
{
    // Initialize stop words with defaults
    m_stopWords.clear();
    for (const QString& word : s_defaultStopWords) {
        m_stopWords.insert(word);
    }
    
    // Setup regex patterns
    m_wordBoundaryRegex = QRegularExpression("\\b\\w+\\b");
    m_urlRegex = QRegularExpression("https?://[^\\s]+");
    m_emailRegex = QRegularExpression("\\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Z|a-z]{2,}\\b");
    m_pathRegex = QRegularExpression("(?:[/\\\\]\\w+)+");
    m_numberRegex = QRegularExpression("\\b\\d+(?:\\.\\d+)?\\b");
    m_dateTimeRegex = QRegularExpression("\\d{4}-\\d{2}-\\d{2}|\\d{2}:\\d{2}:\\d{2}|\\d{2}/\\d{2}/\\d{4}");
    
    // Setup field type scoring weights
    m_fieldTypeWeights["message"] = 1.0f;
    m_fieldTypeWeights["content"] = 1.0f;
    m_fieldTypeWeights["text"] = 1.0f;
    m_fieldTypeWeights["title"] = 1.5f;
    m_fieldTypeWeights["name"] = 1.3f;
    m_fieldTypeWeights["tag"] = 1.2f;
    m_fieldTypeWeights["level"] = 1.1f;
    m_fieldTypeWeights["status"] = 1.1f;
    m_fieldTypeWeights["method"] = 1.1f;
    m_fieldTypeWeights["id"] = 0.8f;
    m_fieldTypeWeights["timestamp"] = 0.5f;
    m_fieldTypeWeights["time"] = 0.5f;
    m_fieldTypeWeights["date"] = 0.5f;
}

QStringList SearchTokenizer::tokenizeText(const QString& text, const QString& fieldName)
{
    if (text.isEmpty()) {
        return QStringList();
    }

    QStringList tokens;
    
    // Try specialized tokenization first
    if (!fieldName.isEmpty()) {
        QStringList specialized = specializedTokenize(text, fieldName);
        if (!specialized.isEmpty()) {
            tokens.append(specialized);
        }
    }
    
    // Always do basic tokenization as well
    tokens.append(basicTokenize(text));
    
    // Remove duplicates and invalid tokens
    QStringList result;
    QSet<QString> seen;
    
    for (const QString& token : tokens) {
        QString normalized = normalizeToken(token);
        if (isValidToken(normalized) && !seen.contains(normalized)) {
            seen.insert(normalized);
            result.append(normalized);
        }
    }
    
    return result;
}

QList<SearchToken> SearchTokenizer::tokenizeFields(int entryId, const QList<ParsedField>& fields)
{
    QList<SearchToken> searchTokens;
    
    for (const ParsedField& field : fields) {
        QString textContent = field.value.toString();
        if (textContent.isEmpty()) {
            continue;
        }
        
        QStringList tokens = tokenizeText(textContent, field.name);
        
        for (int i = 0; i < tokens.size(); ++i) {
            SearchToken searchToken;
            searchToken.token = tokens[i];
            searchToken.entryId = entryId;
            searchToken.fieldName = field.name;
            searchToken.position = i;
            searchTokens.append(searchToken);
        }
    }
    
    return searchTokens;
}

QStringList SearchTokenizer::basicTokenize(const QString& text)
{
    QStringList tokens;
    
    // Extract all word boundaries
    auto iterator = m_wordBoundaryRegex.globalMatch(text);
    while (iterator.hasNext()) {
        auto match = iterator.next();
        tokens.append(match.captured());
    }
    
    return tokens;
}

QStringList SearchTokenizer::specializedTokenize(const QString& text, const QString& fieldName)
{
    QStringList tokens;
    QString lowerFieldName = fieldName.toLower();
    
    // URL tokenization
    if (m_tokenizeUrls && (lowerFieldName.contains("url") || lowerFieldName.contains("link"))) {
        tokens.append(tokenizeUrl(text));
    }
    
    // Path tokenization
    if (m_tokenizePaths && (lowerFieldName.contains("path") || lowerFieldName.contains("file"))) {
        tokens.append(tokenizePath(text));
    }
    
    // Email tokenization
    if (m_tokenizeEmails && (lowerFieldName.contains("email") || lowerFieldName.contains("mail"))) {
        tokens.append(tokenizeEmail(text));
    }
    
    // Number tokenization
    if (m_tokenizeNumbers && (lowerFieldName.contains("number") || lowerFieldName.contains("count") || 
                              lowerFieldName.contains("size") || lowerFieldName.contains("bytes"))) {
        tokens.append(tokenizeNumber(text));
    }
    
    // DateTime tokenization
    if (lowerFieldName.contains("time") || lowerFieldName.contains("date")) {
        tokens.append(tokenizeDateTime(text));
    }
    
    return tokens;
}

QStringList SearchTokenizer::tokenizeUrl(const QString& url)
{
    QStringList tokens;
    
    QUrl qurl(url);
    if (qurl.isValid()) {
        // Host components
        QString host = qurl.host();
        if (!host.isEmpty()) {
            tokens.append(host.split('.', Qt::SkipEmptyParts));
        }
        
        // Path components
        QString path = qurl.path();
        if (!path.isEmpty()) {
            tokens.append(path.split('/', Qt::SkipEmptyParts));
        }
        
        // Query parameters
        auto query = qurl.query();
        if (!query.isEmpty()) {
            tokens.append(query.split(QRegularExpression("[&=]"), Qt::SkipEmptyParts));
        }
    }
    
    return tokens;
}

QStringList SearchTokenizer::tokenizePath(const QString& path)
{
    QStringList tokens;
    
    // Split on path separators
    QStringList pathParts = path.split(QRegularExpression("[/\\\\]"), Qt::SkipEmptyParts);
    
    for (const QString& part : pathParts) {
        // Further split on dots for file extensions
        QStringList subParts = part.split('.', Qt::SkipEmptyParts);
        tokens.append(subParts);
        
        // Also add the whole part
        tokens.append(part);
    }
    
    return tokens;
}

QStringList SearchTokenizer::tokenizeEmail(const QString& email)
{
    QStringList tokens;
    
    auto match = m_emailRegex.match(email);
    if (match.hasMatch()) {
        QString captured = match.captured();
        QStringList parts = captured.split('@');
        
        if (parts.size() == 2) {
            // Username part
            tokens.append(parts[0]);
            
            // Domain parts
            QStringList domainParts = parts[1].split('.');
            tokens.append(domainParts);
            
            // Full domain
            tokens.append(parts[1]);
        }
    }
    
    return tokens;
}

QStringList SearchTokenizer::tokenizeNumber(const QString& number)
{
    QStringList tokens;
    
    auto iterator = m_numberRegex.globalMatch(number);
    while (iterator.hasNext()) {
        auto match = iterator.next();
        QString num = match.captured();
        tokens.append(num);
        
        // For large numbers, also tokenize significant digits
        if (num.length() > 3) {
            // Add rounded versions (e.g., 1234 -> 1000, 12000)
            bool ok;
            double value = num.toDouble(&ok);
            if (ok) {
                int magnitude = static_cast<int>(std::log10(value));
                if (magnitude >= 3) {
                    double rounded = std::round(value / std::pow(10, magnitude - 1)) * std::pow(10, magnitude - 1);
                    tokens.append(QString::number(static_cast<int>(rounded)));
                }
            }
        }
    }
    
    return tokens;
}

QStringList SearchTokenizer::tokenizeDateTime(const QString& dateTime)
{
    QStringList tokens;
    
    auto iterator = m_dateTimeRegex.globalMatch(dateTime);
    while (iterator.hasNext()) {
        auto match = iterator.next();
        QString dt = match.captured();
        
        // Split on common date/time separators
        QStringList parts = dt.split(QRegularExpression("[-:/\\s]"), Qt::SkipEmptyParts);
        tokens.append(parts);
        
        // Add the full date/time as well
        tokens.append(dt);
    }
    
    return tokens;
}

QString SearchTokenizer::normalizeToken(const QString& token)
{
    QString normalized = token.toLower().trimmed();
    
    // Remove common punctuation from ends
    while (!normalized.isEmpty() && 
           (normalized.startsWith('.') || normalized.startsWith(',') || 
            normalized.startsWith(';') || normalized.startsWith(':') ||
            normalized.endsWith('.') || normalized.endsWith(',') || 
            normalized.endsWith(';') || normalized.endsWith(':'))) {
        
        if (normalized.startsWith('.') || normalized.startsWith(',') || 
            normalized.startsWith(';') || normalized.startsWith(':')) {
            normalized = normalized.mid(1);
        }
        if (normalized.endsWith('.') || normalized.endsWith(',') || 
            normalized.endsWith(';') || normalized.endsWith(':')) {
            normalized = normalized.left(normalized.length() - 1);
        }
    }
    
    return normalized;
}

bool SearchTokenizer::isValidToken(const QString& token)
{
    if (token.isEmpty()) {
        return false;
    }
    
    if (token.length() < m_minTokenLength || token.length() > m_maxTokenLength) {
        return false;
    }
    
    if (m_stopWords.contains(token.toLower())) {
        return false;
    }
    
    // Must contain at least one alphanumeric character
    if (!token.contains(QRegularExpression("[a-zA-Z0-9]"))) {
        return false;
    }
    
    return true;
}

QStringList SearchTokenizer::generateFuzzyVariants(const QString& token)
{
    QStringList variants;
    variants.append(token); // Always include the original
    
    // Phonetic variants
    variants.append(generatePhoneticVariants(token));
    
    // Edit distance variants (for short tokens)
    if (token.length() <= 8) {
        variants.append(generateEditDistanceVariants(token));
    }
    
    // Substring variants
    variants.append(generateSubstringVariants(token));
    
    // Remove duplicates
    variants.removeDuplicates();
    return variants;
}

QStringList SearchTokenizer::generatePhoneticVariants(const QString& token)
{
    QStringList variants;
    
    QString sdx = soundex(token);
    if (!sdx.isEmpty() && sdx != token) {
        variants.append(sdx);
    }
    
    QString mph = metaphone(token);
    if (!mph.isEmpty() && mph != token && mph != sdx) {
        variants.append(mph);
    }
    
    return variants;
}

QStringList SearchTokenizer::generateEditDistanceVariants(const QString& token)
{
    // This is a simplified version - in production you'd want more sophisticated generation
    QStringList variants;
    
    // Single character deletions
    for (int i = 0; i < token.length(); ++i) {
        QString variant = token;
        variant.remove(i, 1);
        if (isValidToken(variant)) {
            variants.append(variant);
        }
    }
    
    return variants;
}

QStringList SearchTokenizer::generateSubstringVariants(const QString& token)
{
    QStringList variants;
    
    // For longer tokens, generate meaningful substrings
    if (token.length() >= 6) {
        // Prefixes
        for (int len = m_minTokenLength; len < token.length(); ++len) {
            QString prefix = token.left(len);
            if (isValidToken(prefix)) {
                variants.append(prefix);
            }
        }
        
        // Suffixes
        for (int len = m_minTokenLength; len < token.length(); ++len) {
            QString suffix = token.right(len);
            if (isValidToken(suffix)) {
                variants.append(suffix);
            }
        }
    }
    
    return variants;
}

float SearchTokenizer::calculateSimilarity(const QString& token1, const QString& token2)
{
    if (token1 == token2) {
        return 1.0f;
    }
    
    if (token1.isEmpty() || token2.isEmpty()) {
        return 0.0f;
    }
    
    // Use edit distance for similarity
    int editDist = levenshteinDistance(token1, token2);
    int maxLen = std::max(token1.length(), token2.length());
    
    return 1.0f - (static_cast<float>(editDist) / maxLen);
}

float SearchTokenizer::calculateRelevance(const SearchResult& result, const SearchQuery& query)
{
    float relevance = result.relevanceScore;
    
    // Boost based on matched fields
    for (const QString& field : result.matchedFields) {
        relevance *= calculateFieldRelevance(field, query.query);
    }
    
    return relevance;
}

float SearchTokenizer::calculateFieldRelevance(const QString& fieldName, const QString& query)
{
    QString lowerFieldName = fieldName.toLower();
    
    float weight = m_fieldTypeWeights.value(lowerFieldName, 1.0f);
    
    // Additional boost for exact field name matches
    if (lowerFieldName.contains(query.toLower())) {
        weight *= 1.2f;
    }
    
    return weight;
}

QString SearchTokenizer::soundex(const QString& word)
{
    if (word.isEmpty()) {
        return QString();
    }
    
    QString upper = word.toUpper();
    QString result = upper.left(1);
    
    // Soundex mapping
    QString prev;
    for (int i = 1; i < upper.length() && result.length() < 4; ++i) {
        QString current;
        QChar c = upper[i];
        
        switch (c.toLatin1()) {
        case 'B': case 'F': case 'P': case 'V':
            current = "1";
            break;
        case 'C': case 'G': case 'J': case 'K': case 'Q': case 'S': case 'X': case 'Z':
            current = "2";
            break;
        case 'D': case 'T':
            current = "3";
            break;
        case 'L':
            current = "4";
            break;
        case 'M': case 'N':
            current = "5";
            break;
        case 'R':
            current = "6";
            break;
        default:
            current = "";
            break;
        }
        
        if (!current.isEmpty() && current != prev) {
            result += current;
        }
        prev = current;
    }
    
    // Pad with zeros
    return result.leftJustified(4, '0');
}

QString SearchTokenizer::metaphone(const QString& word)
{
    // Simplified metaphone implementation
    if (word.isEmpty()) {
        return QString();
    }
    
    QString upper = word.toUpper();
    QString result;
    
    // Basic metaphone rules (simplified)
    for (int i = 0; i < upper.length(); ++i) {
        QChar c = upper[i];
        switch (c.toLatin1()) {
        case 'A': case 'E': case 'I': case 'O': case 'U':
            if (i == 0) result += c;
            break;
        case 'B':
            if (i == 0 || upper[i-1] != 'M') result += c;
            break;
        case 'C':
            if (i < upper.length() - 1 && (upper[i+1] == 'H')) {
                result += "X";
                i++; // Skip the H
            } else {
                result += "K";
            }
            break;
        case 'D':
            if (i < upper.length() - 1 && upper[i+1] == 'G') {
                result += "J";
            } else {
                result += "T";
            }
            break;
        case 'G':
            if (i < upper.length() - 1 && upper[i+1] == 'H') {
                // Skip GH
                i++;
            } else {
                result += "K";
            }
            break;
        case 'H':
            // Skip H unless at beginning
            if (i == 0) result += c;
            break;
        case 'P':
            if (i < upper.length() - 1 && upper[i+1] == 'H') {
                result += "F";
                i++; // Skip the H
            } else {
                result += c;
            }
            break;
        case 'Q':
            result += "K";
            break;
        case 'S':
            if (i < upper.length() - 1 && upper[i+1] == 'H') {
                result += "X";
                i++; // Skip the H
            } else {
                result += c;
            }
            break;
        case 'T':
            if (i < upper.length() - 1 && upper[i+1] == 'H') {
                result += "0";
                i++; // Skip the H
            } else {
                result += c;
            }
            break;
        case 'V':
            result += "F";
            break;
        case 'W': case 'Y':
            // Skip unless at beginning
            if (i == 0) result += c;
            break;
        case 'X':
            result += "KS";
            break;
        case 'Z':
            result += "S";
            break;
        default:
            result += c;
            break;
        }
    }
    
    return result;
}

int SearchTokenizer::levenshteinDistance(const QString& str1, const QString& str2)
{
    int len1 = str1.length();
    int len2 = str2.length();
    
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;
    
    QVector<QVector<int>> matrix(len1 + 1, QVector<int>(len2 + 1));
    
    // Initialize first row and column
    for (int i = 0; i <= len1; i++) matrix[i][0] = i;
    for (int j = 0; j <= len2; j++) matrix[0][j] = j;
    
    // Fill the matrix
    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (str1[i-1] == str2[j-1]) ? 0 : 1;
            matrix[i][j] = std::min({
                matrix[i-1][j] + 1,     // deletion
                matrix[i][j-1] + 1,     // insertion
                matrix[i-1][j-1] + cost // substitution
            });
        }
    }
    
    return matrix[len1][len2];
}
