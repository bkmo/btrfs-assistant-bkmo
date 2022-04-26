#include "util/Btrfs.h"
#include "util/Settings.h"
#include "util/System.h"
#include <sys/mount.h>

#include <QDebug>
#include <QDir>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <btrfsutil.h>

namespace {

QString uuidToString(const uint8_t uuid[16])
{
    QString ret;
    bool allZeros = true;
    for (int i = 0; i < 16; ++i) {
        ret.append(QString::number(uuid[i], 16));
        if ((i + 1) % 2 == 0 && (i > 1 && i < 10)) {
            ret.append('-');
        }
        allZeros &= (uuid[i] == 0);
    }
    return allZeros ? "" : ret;
}

} // namespace

Btrfs::Btrfs(QObject *parent) : QObject{parent} { loadVolumes(); }

Btrfs::~Btrfs() { unmountFilesystems(); }

QString Btrfs::balanceStatus(const QString &mountpoint) const
{
    return System::runCmd("btrfs", {"balance", "status", mountpoint}, false).output;
}

BtrfsFilesystem Btrfs::filesystem(const QString &uuid) const
{
    // If the uuid isn't found return a default constructed btrfsMeta
    if (!m_filesystems.contains(uuid)) {
        return BtrfsFilesystem();
    }

    return m_filesystems[uuid];
}

QStringList Btrfs::children(const uint64_t subvolId, const QString &uuid)
{
    const QString mountpoint = mountRoot(uuid);
    btrfs_util_subvolume_iterator *iter;

    btrfs_util_error returnCode = btrfs_util_create_subvolume_iterator(mountpoint.toLocal8Bit(), BTRFS_ROOT_ID, 0, &iter);
    if (returnCode != BTRFS_UTIL_OK) {
        return QStringList();
    }

    QStringList children;

    while (returnCode != BTRFS_UTIL_ERROR_STOP_ITERATION) {
        char *path = nullptr;
        struct btrfs_util_subvolume_info subvolInfo;
        returnCode = btrfs_util_subvolume_iterator_next_info(iter, &path, &subvolInfo);
        if (returnCode == BTRFS_UTIL_OK && subvolInfo.parent_id == subvolId) {
            children.append(QString::fromLocal8Bit(path));
            free(path);
        }
    }

    btrfs_util_destroy_subvolume_iterator(iter);
    return children;
}

bool Btrfs::createSnapshot(const QString &source, const QString &dest)
{
    return btrfs_util_create_snapshot(source.toLocal8Bit(), dest.toLocal8Bit(), 0, nullptr, nullptr) == BTRFS_UTIL_OK;
}

bool Btrfs::deleteSubvol(const QString &uuid, const uint64_t subvolid)
{
    Subvolume subvol;
    if (m_filesystems.contains(uuid)) {
        subvol = m_filesystems[uuid].subvolumes.value(subvolid);
        if (subvol.parentId != 0) {
            QString mountpoint = mountRoot(uuid);

            // Everything checks out, lets delete the subvol
            const QString subvolPath = QDir::cleanPath(mountpoint + QDir::separator() + subvol.subvolName);
            btrfs_util_error returnCode = btrfs_util_delete_subvolume(subvolPath.toLocal8Bit(), 0);
            if (returnCode == BTRFS_UTIL_OK) {
                return true;
            }
        }
    }

    // If we get to here, it failed
    return false;
}

bool Btrfs::isSnapper(const QString &subvolume)
{
    static QRegularExpression re("\\/[0-9]*\\/snapshot$");
    return re.match(subvolume).hasMatch();
}

bool Btrfs::isMounted(const QString &uuid, const uint64_t subvolid)
{
    const QStringList outputList =
        System::runCmd("findmnt -nO subvolid=" + QString::number(subvolid) + " -o uuid", false).output.trimmed().split("\n");
    return uuid == outputList.at(0).trimmed();
}

bool Btrfs::isQuotaEnabled(const QString &mountpoint)
{
    return !System::runCmd("btrfs", {QStringLiteral("qgroup"), QStringLiteral("show"), mountpoint}, false).output.isEmpty();
}

QStringList Btrfs::listFilesystems()
{
    const QStringList outputList = System::runCmd("btrfs filesystem show -m", false).output.split('\n');
    QStringList uuids;
    for (const QString &line : outputList) {
        if (line.contains("uuid:")) {
            uuids.append(line.split("uuid:").at(1).trimmed());
        }
    }
    return uuids;
}

QStringList Btrfs::listMountpoints()
{
    QStringList mountpoints;

    // loop through mountpoints and only include those with btrfs fileystem
    const QStringList output = System::runCmd("findmnt --real -lno fstype,target", false).output.trimmed().split('\n');
    for (const QString &line : output) {
        if (line.startsWith("btrfs")) {
            QString mountpoint = line.section(' ', 1);
            if (!mountpoint.isEmpty()) {
                mountpoints.append(mountpoint);
            }
        }
    }

    mountpoints.sort();

    return mountpoints;
}

SubvolumeMap Btrfs::listSubvolumes(const QString &uuid) const { return m_filesystems.value(uuid).subvolumes; }

void Btrfs::loadQgroups(const QString &uuid)
{
    if (!isUuidLoaded(uuid)) {
        return;
    }

    const QString mountpoint = mountRoot(uuid);
    if (mountpoint.isEmpty()) {
        return;
    }

    if (!isQuotaEnabled(mountpoint)) {
        // If qgroups aren't enabled we need to abort
        return;
    }

    QStringList outputList = System::runCmd("btrfs", {"qgroup", "show", "--raw", "--sync", mountpoint}, false).output.split("\n");

    // The header takes the first two lines, make sure it is more than two lines and then consume them
    if (outputList.count() <= 2) {
        return;
    }
    outputList.takeFirst();
    outputList.takeFirst();

    // Load the data

    for (const QString &line : qAsConst(outputList)) {
        const QStringList qgroupList = line.split(" ", Qt::SkipEmptyParts);
        uint64_t subvolId;
        if (!qgroupList.at(0).contains("/")) {
            continue;
        }

        subvolId = qgroupList.at(0).split("/").at(1).toUInt();

        m_filesystems[uuid].subvolumes[subvolId].size = qgroupList.at(1).toULong();
        m_filesystems[uuid].subvolumes[subvolId].exclusive = qgroupList.at(2).toULong();
    }
}

void Btrfs::loadSubvols(const QString &uuid)
{
    if (isUuidLoaded(uuid)) {
        m_filesystems[uuid].subvolumes.clear();

        const QString mountpoint = mountRoot(uuid);
        btrfs_util_subvolume_iterator *iter;

        btrfs_util_error returnCode = btrfs_util_create_subvolume_iterator(mountpoint.toLocal8Bit(), BTRFS_ROOT_ID, 0, &iter);
        if (returnCode != BTRFS_UTIL_OK) {
            return;
        }

        SubvolumeMap subvols;

        while (returnCode != BTRFS_UTIL_ERROR_STOP_ITERATION) {
            char *path = nullptr;
            struct btrfs_util_subvolume_info subvolInfo;
            returnCode = btrfs_util_subvolume_iterator_next_info(iter, &path, &subvolInfo);
            if (returnCode == BTRFS_UTIL_OK) {
                subvols[subvolInfo.id].subvolName = QString::fromLocal8Bit(path);
                subvols[subvolInfo.id].parentId = subvolInfo.parent_id;
                subvols[subvolInfo.id].id = subvolInfo.id;
                subvols[subvolInfo.id].uuid = uuidToString(subvolInfo.uuid);
                subvols[subvolInfo.id].parentUuid = uuidToString(subvolInfo.parent_uuid);
                subvols[subvolInfo.id].receivedUuid = uuidToString(subvolInfo.received_uuid);
                subvols[subvolInfo.id].generation = subvolInfo.generation;
                subvols[subvolInfo.id].flags = subvolInfo.flags;
                subvols[subvolInfo.id].createdAt = QDateTime::fromSecsSinceEpoch(subvolInfo.otime.tv_sec);
                subvols[subvolInfo.id].filesystemUuid = uuid;
                free(path);
            }
        }
        btrfs_util_destroy_subvolume_iterator(iter);

        m_filesystems[uuid].subvolumes = subvols;
        loadQgroups(uuid);
    }
}

void Btrfs::loadVolumes()
{
    QStringList uuidList = listFilesystems();

    // Loop through btrfs devices and retrieve filesystem usage
    for (const QString &uuid : qAsConst(uuidList)) {
        QString mountpoint = mountRoot(uuid);
        if (!mountpoint.isEmpty()) {
            BtrfsFilesystem btrfs;
            btrfs.isPopulated = true;
            btrfs.mountPoint = mountpoint;
            QStringList usageLines = System::runCmd("LANG=C ; btrfs fi usage -b \"" + mountpoint + "\"", false).output.split('\n');
            for (const QString &line : qAsConst(usageLines)) {
                const QStringList &cols = line.split(':');
                QString type = cols.at(0).trimmed();
                if (type == "Device size") {
                    btrfs.totalSize = cols.at(1).trimmed().toULong();
                } else if (type == "Device allocated") {
                    btrfs.allocatedSize = cols.at(1).trimmed().toULong();
                } else if (type == "Used") {
                    btrfs.usedSize = cols.at(1).trimmed().toULong();
                } else if (type == "Free (estimated)") {
                    btrfs.freeSize = cols.at(1).split(QRegExp("\\s+"), Qt::SkipEmptyParts).at(0).trimmed().toULong();
                } else if (type.startsWith("Data,")) {
                    btrfs.dataSize = cols.at(2).split(',').at(0).trimmed().toULong();
                    btrfs.dataUsed = cols.at(3).split(' ').at(0).trimmed().toULong();
                } else if (type.startsWith("Metadata,")) {
                    btrfs.metaSize = cols.at(2).split(',').at(0).trimmed().toULong();
                    btrfs.metaUsed = cols.at(3).split(' ').at(0).trimmed().toULong();
                } else if (type.startsWith("System,")) {
                    btrfs.sysSize = cols.at(2).split(',').at(0).trimmed().toULong();
                    btrfs.sysUsed = cols.at(3).split(' ').at(0).trimmed().toULong();
                }
            }
            m_filesystems[uuid] = btrfs;
            loadSubvols(uuid);
        }
    }
}

QString Btrfs::mountRoot(const QString &uuid)
{
    // Check to see if it is already mounted
    QStringList findmntOutput =
        System::runCmd("findmnt", {"-nO", "subvolid=" + QString::number(BTRFS_ROOT_ID), "-o", "uuid,target"}, false).output.split('\n');
    QString mountpoint;
    for (const QString &line : qAsConst(findmntOutput)) {
        if (!line.isEmpty() && line.split(' ').at(0).trimmed() == uuid)
            mountpoint = line.section(' ', 1).trimmed();
    }

    // If it isn't mounted we need to mount it
    if (mountpoint.isEmpty()) {
        mountpoint = QDir::cleanPath(System::mountPathRoot() + QDir::separator() + uuid);

        // Add this mountpoint to a list so it can be unmounted later
        m_tempMountpoints.append(mountpoint);

        // Create the mountpoint and mount the volume if successful
        QDir tempMount;
        const QString device = QDir::cleanPath(QStringLiteral("/dev/disk/by-uuid/") + uuid);
        const QString options = "subvolid=" + QString::number(BTRFS_ROOT_ID);
        if (!(tempMount.mkpath(mountpoint) &&
              mount(device.toLocal8Bit(), mountpoint.toLocal8Bit(), "btrfs", 0, options.toLocal8Bit()) == 0)) {
            return QString();
        }
    }

    return mountpoint;
}

bool Btrfs::renameSubvolume(const QString &source, const QString &target)
{
    QDir dir;
    // If there is an empty dir at target, remove it
    if (dir.exists(target)) {
        dir.rmdir(target);
    }
    return dir.rename(source, target);
}

QString Btrfs::scrubStatus(const QString &mountpoint) const
{
    return System::runCmd("btrfs", {"scrub", "status", mountpoint}, false).output;
}

void Btrfs::setQgroupEnabled(const QString &mountpoint, bool enable)
{
    if (enable) {
        System::runCmd(QStringLiteral("btrfs"), {QStringLiteral("quota"), QStringLiteral("enable"), mountpoint}, false);
    } else {
        System::runCmd(QStringLiteral("btrfs"), {QStringLiteral("quota"), QStringLiteral("disable"), mountpoint}, false);
    }
}

uint64_t Btrfs::subvolId(const QString &uuid, const QString &subvolName)
{
    const QString mountpoint = mountRoot(uuid);
    if (mountpoint.isEmpty()) {
        return 0;
    }

    const QString subvolPath = QDir::cleanPath(mountpoint + QDir::separator() + subvolName);
    uint64_t id;
    btrfs_util_error returnCode = btrfs_util_subvolume_id(subvolPath.toLocal8Bit(), &id);
    if (returnCode == BTRFS_UTIL_OK) {
        return id;
    } else {
        return 0;
    }
}

QString Btrfs::subvolumeName(const QString &uuid, const uint64_t subvolId) const
{
    if (m_filesystems.contains(uuid) && m_filesystems[uuid].subvolumes.contains(subvolId)) {
        return m_filesystems[uuid].subvolumes[subvolId].subvolName;
    } else {
        return QString();
    }
}

QString Btrfs::subvolumeName(const QString &path) const
{
    QString ret;
    char *subvolName = nullptr;
    btrfs_util_error returnCode = btrfs_util_subvolume_path(path.toLocal8Bit(), 0, &subvolName);
    if (returnCode == BTRFS_UTIL_OK) {
        ret = QString::fromLocal8Bit(subvolName);
        free(subvolName);
    }
    return ret;
}

uint64_t Btrfs::subvolParent(const QString &uuid, const uint64_t subvolId) const
{
    if (m_filesystems.contains(uuid) && m_filesystems[uuid].subvolumes.contains(subvolId)) {
        return m_filesystems[uuid].subvolumes[subvolId].parentId;
    } else {
        return 0;
    }
}

uint64_t Btrfs::subvolParent(const QString &path) const
{
    struct btrfs_util_subvolume_info subvolInfo;
    btrfs_util_error returnCode = btrfs_util_subvolume_info(path.toLocal8Bit(), 0, &subvolInfo);
    if (returnCode != BTRFS_UTIL_OK) {
        return 0;
    }

    return subvolInfo.parent_id;
}

bool Btrfs::isUuidLoaded(const QString &uuid)
{
    // First make sure the data we are trying to access exists
    if (!m_filesystems.contains(uuid) || !m_filesystems[uuid].isPopulated) {
        loadVolumes();
    }

    // If it still doesn't exist, we need to bail
    if (!m_filesystems.contains(uuid) || !m_filesystems[uuid].isPopulated) {
        qWarning() << tr("UUID " + uuid.toLocal8Bit() + " not found!");
        return false;
    }

    return true;
}

void Btrfs::startBalanceRoot(const QString &uuid)
{
    if (isUuidLoaded(uuid)) {
        QString mountpoint = mountRoot(uuid);

        // Run full balance command against UUID top level subvolume.
        System::runCmd("btrfs", {"balance", "start", mountpoint, "--full-balance", "--bg"}, false);
    }
}

void Btrfs::startScrubRoot(const QString &uuid)
{
    if (isUuidLoaded(uuid)) {
        QString mountpoint = mountRoot(uuid);

        System::runCmd("btrfs", {"scrub", "start", mountpoint}, false);
    }
}

void Btrfs::stopBalanceRoot(const QString &uuid)
{
    if (isUuidLoaded(uuid)) {
        QString mountpoint = mountRoot(uuid);

        System::runCmd("btrfs", {"balance", "cancel", mountpoint}, false);
    }
}

void Btrfs::stopScrubRoot(const QString &uuid)
{
    if (isUuidLoaded(uuid)) {
        QString mountpoint = mountRoot(uuid);

        System::runCmd("btrfs", {"scrub", "cancel", mountpoint}, false);
    }
}

void Btrfs::unmountFilesystems()
{
    for (const QString &mountpoint : qAsConst(m_tempMountpoints)) {
        umount2(mountpoint.toLocal8Bit(), MNT_DETACH);
    }
}

bool Subvolume::isReadOnly() const { return flags & 0x1u; }

bool Subvolume::isSnapshot() const { return !parentUuid.isEmpty(); }

bool Subvolume::isReceived() const { return !receivedUuid.isEmpty(); }
