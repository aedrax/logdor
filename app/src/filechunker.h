#ifndef FILECHUNKER_H
#define FILECHUNKER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QFile>
#include <QMutex>
#include <QElapsedTimer>

struct ChunkMetadata {
    int chunkIndex;
    qint64 fileOffset;
    qint64 chunkSize;
    int startLineNumber;
    int endLineNumber;
    int actualLineCount;
    QByteArray lineEndingType; // "\n", "\r\n", or "\r"
    bool isLastChunk;
    
    ChunkMetadata(int index = -1)
        : chunkIndex(index)
        , fileOffset(0)
        , chunkSize(0)
        , startLineNumber(1)
        , endLineNumber(1)
        , actualLineCount(0)
        , isLastChunk(false)
    {}
    
    bool isValid() const { 
        return chunkIndex >= 0 && chunkSize > 0 && startLineNumber > 0; 
    }
};

struct FileChunkingResult {
    QList<ChunkMetadata> chunks;
    qint64 totalFileSize;
    int totalLineCount;
    QByteArray detectedLineEnding;
    QString errorMessage;
    bool success;
    QElapsedTimer processingTime;
    
    FileChunkingResult()
        : totalFileSize(0)
        , totalLineCount(0)
        , success(false)
    {}
};

class FileChunker : public QObject
{
    Q_OBJECT

public:
    explicit FileChunker(QObject* parent = nullptr);
    ~FileChunker();

    // Configuration
    void setChunkSize(qint64 bytes);
    void setMaxLookaheadBytes(int bytes);
    void setLineEndingDetectionSampleSize(int bytes);
    
    qint64 chunkSize() const { return m_chunkSize; }
    int maxLookaheadBytes() const { return m_maxLookaheadBytes; }
    int lineEndingDetectionSampleSize() const { return m_sampleSize; }
    
    // Main chunking operation
    FileChunkingResult chunkFile(const QString& filePath);
    
    // Utility methods
    static QByteArray detectLineEnding(const QString& filePath, int sampleSize = 8192);
    static int countLinesInData(const QByteArray& data, const QByteArray& lineEnding);
    static qint64 findNextLineEnding(QFile& file, qint64 startPos, const QByteArray& lineEnding, int maxLookahead);
    
    // Chunk data reading
    QByteArray readChunkData(const QString& filePath, const ChunkMetadata& metadata);
    QList<QByteArray> readChunkLines(const QString& filePath, const ChunkMetadata& metadata);
    
    // Validation
    bool validateChunking(const QString& filePath, const FileChunkingResult& result);
    
signals:
    void chunkingProgress(int percentage);
    void chunkingStatusChanged(const QString& status);
    void chunkCreated(const ChunkMetadata& metadata);

private:
    // Core chunking logic
    ChunkMetadata createChunk(QFile& file, int chunkIndex, qint64 startOffset, 
                             qint64 targetEndOffset, int startLineNumber, 
                             const QByteArray& lineEnding);
    
    qint64 adjustToLineBoundary(QFile& file, qint64 position, const QByteArray& lineEnding);
    int countLinesInRange(QFile& file, qint64 startOffset, qint64 endOffset, const QByteArray& lineEnding);
    
    // Line ending detection
    QByteArray detectLineEndingFromSample(const QByteArray& sample);
    bool isValidLineEnding(const QByteArray& lineEnding);
    
    // Error handling
    void setError(const QString& error);
    QString getLastError() const;
    
    // Progress tracking
    void updateProgress(int percentage);
    void updateStatus(const QString& status);
    
    // Configuration
    qint64 m_chunkSize;
    int m_maxLookaheadBytes;
    int m_sampleSize;
    
    // State
    QString m_lastError;
    mutable QMutex m_mutex;
    
    // Constants
    static const qint64 DEFAULT_CHUNK_SIZE;
    static const int DEFAULT_MAX_LOOKAHEAD;
    static const int DEFAULT_SAMPLE_SIZE;
    static const int MIN_CHUNK_SIZE;
    static const int MAX_CHUNK_SIZE;
};

// Inline utility functions for performance
inline bool isLineEndingChar(char c) {
    return c == '\n' || c == '\r';
}

inline int getLineEndingLength(const QByteArray& lineEnding) {
    if (lineEnding == "\r\n") return 2;
    if (lineEnding == "\n" || lineEnding == "\r") return 1;
    return 1; // Default to 1 for unknown line endings
}

#endif // FILECHUNKER_H