#ifndef SEARCHTOKENIZER_H
#define SEARCHTOKENIZER_H

#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QHash>
#include <QVariant>
#include <QSet>
#include "plugininterface.h"
#include "databasemanager.h"

struct ParsedField {
    QString name;
    QVariant value;
    DataType type;
};

struct SearchToken {
    QString token;
    int entryId;
    QString fieldName;
    int position;
};

enum class ParseStatus {
    NotParsed,
    Parsing,
    Complete,
    Failed
};

class SearchTokenizer
{
public:
    SearchTokenizer();
    ~SearchTokenizer() = default;

    // Core tokenization functions
    QStringList tokenizeText(const QString& text, const QString& fieldName = QString());
    QList<SearchToken> tokenizeFields(int entryId, const QList<ParsedField>& fields);
    
    // Fuzzy matching support
    QStringList generateFuzzyVariants(const QString& token);
    float calculateSimilarity(const QString& token1, const QString& token2);
    
    // Relevance scoring
    float calculateRelevance(const SearchResult& result, const SearchQuery& query);
    float calculateFieldRelevance(const QString& fieldName, const QString& query);
    
    // Configuration
    void setMinTokenLength(int length) { m_minTokenLength = length; }
    void setMaxTokenLength(int length) { m_maxTokenLength = length; }
    void setStopWords(const QStringList& stopWords) { 
        m_stopWords.clear();
        for (const QString& word : stopWords) {
            m_stopWords.insert(word);
        }
    }
    void setFuzzyThreshold(float threshold) { m_fuzzyThreshold = threshold; }
    
    // Advanced tokenization options
    void setTokenizeNumbers(bool tokenize) { m_tokenizeNumbers = tokenize; }
    void setTokenizePaths(bool tokenize) { m_tokenizePaths = tokenize; }
    void setTokenizeUrls(bool tokenize) { m_tokenizeUrls = tokenize; }
    void setTokenizeEmails(bool tokenize) { m_tokenizeEmails = tokenize; }
    
    // Phonetic algorithms for fuzzy matching
    QString soundex(const QString& word);
    QString metaphone(const QString& word);
    int levenshteinDistance(const QString& str1, const QString& str2);

private:
    // Core tokenization helpers
    QStringList basicTokenize(const QString& text);
    QStringList specializedTokenize(const QString& text, const QString& fieldName);
    QString normalizeToken(const QString& token);
    bool isValidToken(const QString& token);
    
    // Fuzzy matching helpers
    QStringList generatePhoneticVariants(const QString& token);
    QStringList generateEditDistanceVariants(const QString& token);
    QStringList generateSubstringVariants(const QString& token);
    
    // Field-specific tokenization
    QStringList tokenizeUrl(const QString& url);
    QStringList tokenizePath(const QString& path);
    QStringList tokenizeEmail(const QString& email);
    QStringList tokenizeNumber(const QString& number);
    QStringList tokenizeDateTime(const QString& dateTime);
    
    // Relevance calculation helpers
    float calculateTokenFrequencyScore(const QString& token, const QStringList& tokens);
    float calculatePositionScore(int position, int totalTokens);
    float calculateFieldTypeBoost(const QString& fieldName);
    
    // Configuration
    int m_minTokenLength;
    int m_maxTokenLength;
    QSet<QString> m_stopWords;
    float m_fuzzyThreshold;
    bool m_tokenizeNumbers;
    bool m_tokenizePaths;
    bool m_tokenizeUrls;
    bool m_tokenizeEmails;
    
    // Cached regex patterns
    QRegularExpression m_wordBoundaryRegex;
    QRegularExpression m_urlRegex;
    QRegularExpression m_emailRegex;
    QRegularExpression m_pathRegex;
    QRegularExpression m_numberRegex;
    QRegularExpression m_dateTimeRegex;
    
    // Field type scoring weights
    QHash<QString, float> m_fieldTypeWeights;
    
    // Common stop words
    static const QStringList s_defaultStopWords;
};

#endif // SEARCHTOKENIZER_H
