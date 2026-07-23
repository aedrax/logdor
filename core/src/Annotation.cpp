#include "logdor/Annotation.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

#include <algorithm>

namespace logdor {

namespace {

constexpr int kSchemaVersion = 1;

bool orderedBefore(const Annotation& a, const Annotation& b)
{
    if (a.startLine != b.startLine)
        return a.startLine < b.startLine;
    return a.id < b.id;
}

} // namespace

bool Annotation::operator==(const Annotation& other) const
{
    return id == other.id && startLine == other.startLine
        && endLine == other.endLine && note == other.note
        && color == other.color && tag == other.tag && author == other.author
        && createdAt == other.createdAt && modifiedAt == other.modifiedAt
        && anchorHash == other.anchorHash && snippet == other.snippet;
}

//=== AnnotationSet ===========================================================

bool AnnotationSet::upsert(Annotation annotation)
{
    if (annotation.id.isNull())
        return false;
    remove(annotation.id); // may or may not exist; keeps dirty set either way
    const auto it = std::lower_bound(m_annotations.begin(), m_annotations.end(),
                                     annotation, orderedBefore);
    m_annotations.insert(it, std::move(annotation));
    m_dirty = true;
    return true;
}

bool AnnotationSet::remove(const QUuid& id)
{
    for (qsizetype i = 0; i < m_annotations.size(); ++i) {
        if (m_annotations[i].id == id) {
            m_annotations.removeAt(i);
            m_dirty = true;
            return true;
        }
    }
    return false;
}

const Annotation* AnnotationSet::find(const QUuid& id) const
{
    for (const Annotation& annotation : m_annotations) {
        if (annotation.id == id)
            return &annotation;
    }
    return nullptr;
}

QList<Annotation> AnnotationSet::annotationsAtLine(qint64 line) const
{
    QList<Annotation> out;
    for (const Annotation& annotation : m_annotations) {
        if (annotation.startLine > line)
            break; // sorted by startLine; nothing further can cover the line
        if (annotation.coversLine(line))
            out.append(annotation);
    }
    return out;
}

bool AnnotationSet::hasAnnotationAtLine(qint64 line) const
{
    for (const Annotation& annotation : m_annotations) {
        if (annotation.startLine > line)
            return false;
        if (annotation.coversLine(line))
            return true;
    }
    return false;
}

//=== Serialization ===========================================================

std::optional<AnnotationFile> loadAnnotations(const QByteArray& json,
                                              AnnotationFileError* error)
{
    const auto fail = [&](const QString& message) -> std::optional<AnnotationFile> {
        if (error)
            *error = AnnotationFileError { message };
        return std::nullopt;
    };

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull())
        return fail(QStringLiteral("invalid JSON at offset %1: %2")
                        .arg(parseError.offset)
                        .arg(parseError.errorString()));
    if (!doc.isObject())
        return fail(QStringLiteral("top-level value must be an object"));
    const QJsonObject root = doc.object();

    const int version = root.value(u"version").toInt(-1);
    if (version != kSchemaVersion)
        return fail(QStringLiteral(
            "unsupported version %1 — this file was created by a newer Logdor")
                        .arg(version));

    AnnotationFile file;
    const QJsonObject fileObject = root.value(u"file").toObject();
    file.identity.size = quint64(fileObject.value(u"size").toDouble());
    file.identity.prefixSha256 =
        fileObject.value(u"prefixSha256").toString().toUtf8();
    file.identity.prefixLength = quint32(fileObject.value(u"prefixLength").toDouble());

    const QJsonArray annotations = root.value(u"annotations").toArray();
    for (int i = 0; i < annotations.size(); ++i) {
        const auto skip = [&](const QString& why) {
            file.warnings.append(
                QStringLiteral("annotations[%1] skipped: %2").arg(i).arg(why));
        };
        if (!annotations[i].isObject()) {
            skip(QStringLiteral("not an object"));
            continue;
        }
        const QJsonObject entry = annotations[i].toObject();

        Annotation annotation;
        annotation.id = QUuid::fromString(entry.value(u"id").toString());
        if (annotation.id.isNull()) {
            skip(QStringLiteral("missing or invalid id"));
            continue;
        }
        annotation.startLine = qint64(entry.value(u"startLine").toDouble(-1));
        annotation.endLine = qint64(entry.value(u"endLine").toDouble(-1));
        if (annotation.startLine < 0 || annotation.endLine < annotation.startLine) {
            skip(QStringLiteral("invalid line range"));
            continue;
        }
        if (!entry.contains(u"note")) {
            skip(QStringLiteral("missing note"));
            continue;
        }
        annotation.note = entry.value(u"note").toString();
        annotation.color = entry.value(u"color").toString();
        annotation.tag = entry.value(u"tag").toString();
        annotation.author = entry.value(u"author").toString();
        annotation.createdAt = QDateTime::fromString(
            entry.value(u"createdAt").toString(), Qt::ISODateWithMs);
        annotation.modifiedAt = QDateTime::fromString(
            entry.value(u"modifiedAt").toString(), Qt::ISODateWithMs);
        annotation.createdAt.setTimeZone(QTimeZone::utc());
        annotation.modifiedAt.setTimeZone(QTimeZone::utc());

        const QJsonObject anchor = entry.value(u"anchor").toObject();
        annotation.anchorHash = anchor.value(u"lineSha256").toString().toUtf8();
        if (annotation.anchorHash.isEmpty()) {
            skip(QStringLiteral("missing anchor"));
            continue;
        }
        annotation.snippet = anchor.value(u"snippet").toString();

        file.set.upsert(std::move(annotation));
    }
    file.set.clearDirty(); // freshly loaded state is clean
    return file;
}

QByteArray saveAnnotations(const AnnotationSet& set, const FileIdentity& identity,
                           const QString& fileName)
{
    QJsonObject fileObject;
    if (!fileName.isEmpty())
        fileObject.insert(u"name", fileName);
    fileObject.insert(u"size", double(identity.size));
    fileObject.insert(u"prefixSha256", QString::fromUtf8(identity.prefixSha256));
    fileObject.insert(u"prefixLength", double(identity.prefixLength));

    QJsonArray annotations;
    for (const Annotation& annotation : set.annotations()) {
        QJsonObject anchor;
        anchor.insert(u"lineSha256", QString::fromUtf8(annotation.anchorHash));
        anchor.insert(u"snippet", annotation.snippet);

        QJsonObject entry;
        entry.insert(u"id", annotation.id.toString());
        entry.insert(u"startLine", double(annotation.startLine));
        entry.insert(u"endLine", double(annotation.endLine));
        entry.insert(u"note", annotation.note);
        if (!annotation.color.isEmpty())
            entry.insert(u"color", annotation.color);
        if (!annotation.tag.isEmpty())
            entry.insert(u"tag", annotation.tag);
        if (!annotation.author.isEmpty())
            entry.insert(u"author", annotation.author);
        entry.insert(u"createdAt",
                     annotation.createdAt.toUTC().toString(Qt::ISODateWithMs));
        entry.insert(u"modifiedAt",
                     annotation.modifiedAt.toUTC().toString(Qt::ISODateWithMs));
        entry.insert(u"anchor", anchor);
        annotations.append(entry);
    }

    QJsonObject root;
    root.insert(u"version", kSchemaVersion);
    root.insert(u"generator", QStringLiteral("logdor/0.1"));
    root.insert(u"file", fileObject);
    root.insert(u"annotations", annotations);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

AnnotationSet mergeAnnotations(const AnnotationSet& a, const AnnotationSet& b)
{
    AnnotationSet merged;
    for (const Annotation& annotation : a.annotations())
        merged.upsert(annotation);
    for (const Annotation& annotation : b.annotations()) {
        const Annotation* existing = merged.find(annotation.id);
        if (!existing) {
            merged.upsert(annotation);
            continue;
        }
        // Last-write-wins; deterministic tie-break on the id string.
        if (annotation.modifiedAt > existing->modifiedAt
            || (annotation.modifiedAt == existing->modifiedAt
                && annotation.id.toString() > existing->id.toString()))
            merged.upsert(annotation);
    }
    merged.markDirty();
    return merged;
}

} // namespace logdor
