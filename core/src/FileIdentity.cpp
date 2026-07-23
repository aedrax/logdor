#include "logdor/FileIdentity.h"

#include "logdor/FileSource.h"

#include <QCryptographicHash>

namespace logdor {

FileIdentity computeFileIdentity(const FileSource& source)
{
    constexpr quint32 kPrefixCap = 64 * 1024;

    FileIdentity identity;
    identity.size = source.size();
    identity.prefixLength = quint32(qMin<quint64>(source.size(), kPrefixCap));
    const QByteArray prefix = source.read(0, identity.prefixLength);
    identity.prefixSha256 =
        QCryptographicHash::hash(prefix, QCryptographicHash::Sha256).toHex();
    return identity;
}

IdentityMatch matchIdentity(const FileIdentity& saved, const FileSource& current)
{
    if (!saved.isValid() || current.size() < saved.prefixLength)
        return IdentityMatch::Mismatch;

    const QByteArray prefix = current.read(0, saved.prefixLength);
    const QByteArray hash =
        QCryptographicHash::hash(prefix, QCryptographicHash::Sha256).toHex();
    if (hash != saved.prefixSha256)
        return IdentityMatch::Mismatch;
    if (current.size() == saved.size)
        return IdentityMatch::Identical;
    if (current.size() > saved.size)
        return IdentityMatch::Grown;
    return IdentityMatch::Mismatch; // shrank: head matches but content was cut
}

} // namespace logdor
