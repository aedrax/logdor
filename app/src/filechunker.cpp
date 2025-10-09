#include "filechunker.h"
#include <QFileInfo>
#include <QDebug>
#include <QMutexLocker>
#include <algorithm>

// Constants
const qint64 FileChunker::DEFAULT_CHUNK_SIZE = 1024 * 1024; // 1MB
const int FileChunker::DEFAULT_MAX_LOOKAHEAD = 8192; // 8KB
const int FileChunker::DEFAULT_SAMPLE_SIZE = 8192; // 8KB
const int FileChunker::MIN_CHUNK_SIZE = 64 * 1024; // 64KB
const int FileChunker::MAX_CHUNK_SIZE = 100 * 1024 * 1024; // 100MB

FileChunker::FileChunker(QObject* parent)
    : QObject(parent)
    , m_chunkSize(DEFAULT_CHUNK_SIZE)
    , m_maxLookaheadBytes(DEFAULT_MAX_LOOKAHEAD)
    , m_sampleSize(DEFAULT_SAMPLE_SIZE)
{
}

FileChunker::~FileChunker() = default;

void FileChunker::setChunkSize(qint64 bytes)
{
    if (bytes >= MIN_CHUNK_SIZE && bytes <= MAX_CHUNK_SIZE) {
        m_chunkSize = bytes;
    }
}

void FileChunker::setMaxLookaheadBytes(int bytes)
{
    if (bytes > 0 && bytes <= 1024 * 1024) { // Max 1MB lookahead
        m_maxLookaheadBytes = bytes;
    }
}

void FileChunker::setLineEndingDetectionSampleSize(int bytes)
{
    if (bytes > 0 && bytes <= 1024 * 1024) { // Max 1MB sample
        m_sampleSize = bytes;
    }
}

FileChunkingResult FileChunker::chunkFile(const QString& filePath)
{
    FileChunkingResult result;
    result.processingTime.start();
    
    updateStatus("Starting file chunking...");
    
    // Validate file
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        setError(QString("File does not exist or is not readable: %1").arg(filePath));
        result.errorMessage = getLastError();
        return result;
    }
    
    result.totalFileSize = fileInfo.size();
    
    if (result.totalFileSize == 0) {
        result.success = true; // Empty file is valid
        return result;
    }
    
    updateStatus("Detecting line endings...");
    
    // Detect line ending style
    result.detectedLineEnding = detectLineEnding(filePath, m_sampleSize);
    if (result.detectedLineEnding.isEmpty()) {
        result.detectedLineEnding = "\n"; // Default to Unix line endings
    }
    
    updateProgress(10);
    
    // Open file for chunking
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(QString("Failed to open file for reading: %1").arg(file.errorString()));
        result.errorMessage = getLastError();
        return result;
    }
    
    updateStatus("Creating chunks...");
    
    // Create chunks
    qint64 currentOffset = 0;
    int currentLineNumber = 1;
    int chunkIndex = 0;
    
    while (currentOffset < result.totalFileSize) {
        qint64 targetEndOffset = qMin(currentOffset + m_chunkSize, result.totalFileSize);
        
        ChunkMetadata chunk = createChunk(file, chunkIndex, currentOffset, 
                                        targetEndOffset, currentLineNumber, 
                                        result.detectedLineEnding);
        
        if (!chunk.isValid()) {
            setError(QString("Failed to create chunk %1").arg(chunkIndex));
            result.errorMessage = getLastError();
            return result;
        }
        
        chunk.isLastChunk = (chunk.fileOffset + chunk.chunkSize >= result.totalFileSize);
        result.chunks.append(chunk);
        
        // Update for next chunk
        currentOffset = chunk.fileOffset + chunk.chunkSize;
        currentLineNumber = chunk.endLineNumber + 1;
        chunkIndex++;
        
        // Update progress
        int progress = 10 + (currentOffset * 80) / result.totalFileSize;
        updateProgress(progress);
        
        emit chunkCreated(chunk);
    }
    
    updateStatus("Finalizing chunking...");
    
    // Calculate total line count
    result.totalLineCount = 0;
    for (const ChunkMetadata& chunk : result.chunks) {
        result.totalLineCount += chunk.actualLineCount;
    }
    
    updateProgress(95);
    
    // Validate chunking if requested
    if (!validateChunking(filePath, result)) {
        result.errorMessage = getLastError();
        return result;
    }
    
    updateProgress(100);
    updateStatus("File chunking completed successfully");
    
    result.success = true;
    return result;
}

QByteArray FileChunker::detectLineEnding(const QString& filePath, int sampleSize)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    
    QByteArray sample = file.read(sampleSize);
    
    // Create temporary instance to call the method
    FileChunker chunker;
    return chunker.detectLineEndingFromSample(sample);
}

QByteArray FileChunker::detectLineEndingFromSample(const QByteArray& sample)
{
    int crlfCount = 0;
    int lfCount = 0;
    int crCount = 0;
    
    for (int i = 0; i < sample.size(); ++i) {
        if (sample[i] == '\r') {
            if (i + 1 < sample.size() && sample[i + 1] == '\n') {
                crlfCount++;
                i++; // Skip the \n
            } else {
                crCount++;
            }
        } else if (sample[i] == '\n') {
            lfCount++;
        }
    }
    
    // Determine the most common line ending
    if (crlfCount > lfCount && crlfCount > crCount) {
        return "\r\n"; // Windows
    } else if (crCount > lfCount && crCount > crlfCount) {
        return "\r"; // Classic Mac
    } else {
        return "\n"; // Unix/Linux/Modern Mac
    }
}

int FileChunker::countLinesInData(const QByteArray& data, const QByteArray& lineEnding)
{
    if (data.isEmpty()) {
        return 0;
    }
    
    int count = 0;
    int pos = 0;
    
    while ((pos = data.indexOf(lineEnding, pos)) != -1) {
        count++;
        pos += lineEnding.length();
    }
    
    // If the data doesn't end with a line ending, count the last line
    if (!data.endsWith(lineEnding)) {
        count++;
    }
    
    return count;
}

qint64 FileChunker::findNextLineEnding(QFile& file, qint64 startPos, const QByteArray& lineEnding, int maxLookahead)
{
    if (!file.seek(startPos)) {
        return -1;
    }
    
    QByteArray buffer = file.read(maxLookahead);
    if (buffer.isEmpty()) {
        return -1;
    }
    
    int pos = buffer.indexOf(lineEnding);
    if (pos == -1) {
        return -1; // No line ending found within lookahead
    }
    
    return startPos + pos + lineEnding.length();
}

QByteArray FileChunker::readChunkData(const QString& filePath, const ChunkMetadata& metadata)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    
    if (!file.seek(metadata.fileOffset)) {
        return QByteArray();
    }
    
    return file.read(metadata.chunkSize);
}

QList<QByteArray> FileChunker::readChunkLines(const QString& filePath, const ChunkMetadata& metadata)
{
    QByteArray chunkData = readChunkData(filePath, metadata);
    if (chunkData.isEmpty()) {
        return QList<QByteArray>();
    }
    
    QList<QByteArray> lines;
    QByteArray lineEnding = metadata.lineEndingType.isEmpty() ? "\n" : metadata.lineEndingType;
    
    int start = 0;
    int pos = 0;
    
    while ((pos = chunkData.indexOf(lineEnding, start)) != -1) {
        QByteArray line = chunkData.mid(start, pos - start);
        lines.append(line);
        start = pos + lineEnding.length();
    }
    
    // Handle last line if it doesn't end with line ending
    if (start < chunkData.size()) {
        QByteArray lastLine = chunkData.mid(start);
        if (!lastLine.isEmpty()) {
            lines.append(lastLine);
        }
    }
    
    return lines;
}

bool FileChunker::validateChunking(const QString& filePath, const FileChunkingResult& result)
{
    // Basic validation: check that chunks cover the entire file
    qint64 totalChunkSize = 0;
    int expectedLineNumber = 1;
    
    for (const ChunkMetadata& chunk : result.chunks) {
        if (chunk.startLineNumber != expectedLineNumber) {
            setError(QString("Chunk %1 has incorrect start line number: expected %2, got %3")
                    .arg(chunk.chunkIndex)
                    .arg(expectedLineNumber)
                    .arg(chunk.startLineNumber));
            return false;
        }
        
        totalChunkSize += chunk.chunkSize;
        expectedLineNumber = chunk.endLineNumber + 1;
    }
    
    if (totalChunkSize != result.totalFileSize) {
        setError(QString("Total chunk size (%1) does not match file size (%2)")
                .arg(totalChunkSize)
                .arg(result.totalFileSize));
        return false;
    }
    
    return true;
}

ChunkMetadata FileChunker::createChunk(QFile& file, int chunkIndex, qint64 startOffset, 
                                      qint64 targetEndOffset, int startLineNumber, 
                                      const QByteArray& lineEnding)
{
    ChunkMetadata chunk(chunkIndex);
    chunk.fileOffset = startOffset;
    chunk.startLineNumber = startLineNumber;
    chunk.lineEndingType = lineEnding;
    
    // Adjust end offset to line boundary
    qint64 actualEndOffset = adjustToLineBoundary(file, targetEndOffset, lineEnding);
    if (actualEndOffset == -1) {
        // If we can't find a line boundary, use the target end offset
        actualEndOffset = targetEndOffset;
    }
    
    chunk.chunkSize = actualEndOffset - startOffset;
    
    // Count lines in this chunk
    chunk.actualLineCount = countLinesInRange(file, startOffset, actualEndOffset, lineEnding);
    chunk.endLineNumber = startLineNumber + chunk.actualLineCount - 1;
    
    return chunk;
}

qint64 FileChunker::adjustToLineBoundary(QFile& file, qint64 position, const QByteArray& lineEnding)
{
    // If we're at the end of file, no adjustment needed
    if (position >= file.size()) {
        return file.size();
    }
    
    // Find the next line ending after the target position
    qint64 lineEndingPos = findNextLineEnding(file, position, lineEnding, m_maxLookaheadBytes);
    
    if (lineEndingPos != -1) {
        return lineEndingPos;
    }
    
    // If no line ending found, return the file end
    return file.size();
}

int FileChunker::countLinesInRange(QFile& file, qint64 startOffset, qint64 endOffset, const QByteArray& lineEnding)
{
    if (startOffset >= endOffset) {
        return 0;
    }
    
    if (!file.seek(startOffset)) {
        return 0;
    }
    
    qint64 rangeSize = endOffset - startOffset;
    QByteArray data = file.read(rangeSize);
    
    return countLinesInData(data, lineEnding);
}

bool FileChunker::isValidLineEnding(const QByteArray& lineEnding)
{
    return lineEnding == "\n" || lineEnding == "\r\n" || lineEnding == "\r";
}

void FileChunker::setError(const QString& error)
{
    QMutexLocker locker(&m_mutex);
    m_lastError = error;
    qWarning() << "FileChunker error:" << error;
}

QString FileChunker::getLastError() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastError;
}

void FileChunker::updateProgress(int percentage)
{
    emit chunkingProgress(qBound(0, percentage, 100));
}

void FileChunker::updateStatus(const QString& status)
{
    emit chunkingStatusChanged(status);
}

