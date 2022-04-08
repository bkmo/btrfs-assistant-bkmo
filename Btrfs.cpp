#include "Btrfs.h"
#include "Settings.h"
#include "System.h"

#include <QDebug>
#include <QDir>
#include <QTemporaryDir>
#include <QtConcurrent/QtConcurrent>

Btrfs::Btrfs(QObject *parent) : QObject{parent} { loadVolumes(); }

const QString Btrfs::balanceStatus(const QString &mountpoint) const {
    return System::runCmd("btrfs", {"balance", "status", mountpoint}, false).output;
}

const BtrfsMeta Btrfs::btrfsVolume(const QString &uuid) const {
    // If the uuid isn't found return a default constructed btrfsMeta
    if (!m_volumes.contains(uuid)) {
        return BtrfsMeta();
    }

    return m_volumes[uuid];
}

const QStringList Btrfs::children(const int subvolId, const QString &uuid) const {
    QStringList children;
    if (m_volumes.contains(uuid) && m_volumes[uuid].subvolumes.contains(subvolId)) {
        const QList<int> keys = m_volumes[uuid].subvolumes.keys();
        for (const int &key : keys) {
            if (m_volumes[uuid].subvolumes[key].parentId == subvolId) {
                children.append(m_volumes[uuid].subvolumes[key].subvolName);
            }
        }
    }

    return children;
}

const bool Btrfs::deleteSubvol(const QString &uuid, const int subvolid) {
    Subvolume subvol;
    if (m_volumes.contains(uuid)) {
        subvol = m_volumes[uuid].subvolumes.value(subvolid);
        if (subvol.parentId != 0) {
            QString mountpoint = mountRoot(uuid);

            // Everything checks out, lets delete the subvol
            const QString subvolPath = QDir::cleanPath(mountpoint + QDir::separator() + subvol.subvolName);
            Result result = System::runCmd("btrfs", {"subvolume", "delete", subvolPath}, true);
            if (result.exitCode == 0) {
                return true;
            }
        }
    }

    // If we get to here, it failed
    return false;
}

bool Btrfs::isSnapper(const QString &subvolume) {
    static QRegularExpression re("\\/[0-9]*\\/snapshot$");
    return re.match(subvolume).hasMatch();
}

bool Btrfs::isMounted(const QString &uuid, const int subvolid) {
    const QStringList outputList =
        System::runCmd("findmnt -nO subvolid=" + QString::number(subvolid) + " -o uuid", false).output.trimmed().split("\n");
    return uuid == outputList.at(0).trimmed();
}

bool Btrfs::isQuotaEnabled(const QString &mountpoint) {
    return !System::runCmd("btrfs", {QStringLiteral("qgroup"), QStringLiteral("show"), mountpoint}, false).output.isEmpty();
}

const QStringList Btrfs::listFilesystems() {
    const QStringList outputList = System::runCmd("btrfs filesystem show -m", false).output.split('\n');
    QStringList uuids;
    for (const QString &line : outputList) {
        if (line.contains("uuid:")) {
            uuids.append(line.split("uuid:").at(1).trimmed());
        }
    }
    return uuids;
}

const QStringList Btrfs::listMountpoints() {
    QStringList mountpoints;

    // loop through mountpoints and only include those with btrfs fileystem
    const QStringList output = System::runCmd("findmnt --real -lno fstype,target", false).output.trimmed().split('\n');
    for (const QString &line : output) {
        if (line.startsWith("btrfs")) {
            QString mountpoint = line.simplified().split(' ').at(1).trimmed();
            QStringList crap = line.split(' ');
            qDebug() << crap;
            if (!mountpoint.isEmpty()) {
                mountpoints.append(mountpoint);
            }
        }
    }

    mountpoints.sort();

    return mountpoints;
}

const QMap<int, Subvolume> Btrfs::listSubvolumes(const QString &uuid) const {
    // If the uuid isn't found return a default constructed QMap
    if (!m_volumes.contains(uuid)) {
        return QMap<int, Subvolume>();
    }

    return m_volumes[uuid].subvolumes;
}

void Btrfs::loadQgroups(const QString &uuid) {
    if (!isUuidLoaded(uuid)) {
        return;
    }

    const QString mountpoint = mountRoot(uuid);
    if (mountpoint.isEmpty()) {
        return;
    }

    bool shouldDisableQgroup = false;
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
    if (!m_subvolSize.contains(uuid)) {
        m_subvolSize.insert(uuid, QMap<int, QVector<long>>());
    }

    for (const QString &line : qAsConst(outputList)) {
        const QStringList qgroupList = line.split(" ", Qt::SkipEmptyParts);
        int subvolId;
        if (qgroupList.at(0).contains("/")) {
            subvolId = qgroupList.at(0).split("/").at(1).toInt();
        } else {
            continue;
        }
        long size = qgroupList.at(1).toLong();
        long exclusive = qgroupList.at(2).toLong();

        QVector<long> sizes;
        sizes.append(size);
        sizes.append(exclusive);
        m_subvolSize[uuid][subvolId] = sizes;
    }
}

void Btrfs::loadSubvols(const QString &uuid) {
    if (isUuidLoaded(uuid)) {
        m_volumes[uuid].subvolumes.clear();

        QString mountpoint = mountRoot(uuid);

        QStringList output = System::runCmd("btrfs", {"subvolume", "list", mountpoint}, false).output.split('\n');
        QMap<int, Subvolume> subvols;
        for (const QString &line : qAsConst(output)) {
            if (!line.isEmpty()) {
                Subvolume subvol;
                int subvolId = line.split(' ').at(1).toInt();
                subvol.subvolName = line.split(' ').at(8).trimmed();
                subvol.parentId = line.split(' ').at(6).toInt();
                subvol.subvolId = subvolId;
                subvol.uuid = uuid;
                subvols[subvolId] = subvol;
            }
        }
        m_volumes[uuid].subvolumes = subvols;
        m_subvolSize.remove(uuid); //  clear existing size information in case it is disabled since app launch
        loadQgroups(uuid);
        m_subvolModel.loadModel(m_volumes, m_subvolSize);
    }
}

void Btrfs::loadVolumes() {
    m_volumes.clear();

    QStringList uuidList = listFilesystems();

    // Loop through btrfs devices and retrieve filesystem usage
    for (const QString &uuid : qAsConst(uuidList)) {
        QString mountpoint = mountRoot(uuid);
        if (!mountpoint.isEmpty()) {
            BtrfsMeta btrfs;
            btrfs.populated = true;
            btrfs.mountPoint = mountpoint;
            QStringList usageLines = System::runCmd("LANG=C ; btrfs fi usage -b \"" + mountpoint + "\"", false).output.split('\n');
            for (const QString &line : qAsConst(usageLines)) {
                QString type = line.split(':').at(0).trimmed();
                if (type == "Device size") {
                    btrfs.totalSize = line.split(':').at(1).trimmed().toLong();
                } else if (type == "Device allocated") {
                    btrfs.allocatedSize = line.split(':').at(1).trimmed().toLong();
                } else if (type == "Used") {
                    btrfs.usedSize = line.split(':').at(1).trimmed().toLong();
                } else if (type == "Free (estimated)") {
                    btrfs.freeSize = line.split(':').at(1).split(QRegExp("\\s+"), Qt::SkipEmptyParts).at(0).trimmed().toLong();
                } else if (type.startsWith("Data,")) {
                    btrfs.dataSize = line.split(':').at(2).split(',').at(0).trimmed().toLong();
                    btrfs.dataUsed = line.split(':').at(3).split(' ').at(0).trimmed().toLong();
                } else if (type.startsWith("Metadata,")) {
                    btrfs.metaSize = line.split(':').at(2).split(',').at(0).trimmed().toLong();
                    btrfs.metaUsed = line.split(':').at(3).split(' ').at(0).trimmed().toLong();
                } else if (type.startsWith("System,")) {
                    btrfs.sysSize = line.split(':').at(2).split(',').at(0).trimmed().toLong();
                    btrfs.sysUsed = line.split(':').at(3).split(' ').at(0).trimmed().toLong();
                }
            }
            m_volumes[uuid] = btrfs;
            loadSubvols(uuid);
        }
    }
}

const QString Btrfs::mountRoot(const QString &uuid) {
    // Check to see if it is already mounted
    QStringList findmntOutput = System::runCmd("findmnt -nO subvolid=5 -o uuid,target", false).output.split('\n');
    QString mountpoint;
    for (const QString &line : qAsConst(findmntOutput)) {
        if (!line.isEmpty() && line.split(' ').at(0).trimmed() == uuid)
            mountpoint = line.section(' ', 1).trimmed();
    }

    // If it isn't mounted we need to mount it
    if (mountpoint.isEmpty()) {
        // Get a temp mountpoint
        QTemporaryDir tempDir;
        tempDir.setAutoRemove(false);
        if (!tempDir.isValid()) {
            qWarning() << "Failed to create temporary directory" << Qt::endl;
            return QString();
        }

        mountpoint = tempDir.path();

        // Create the mountpoint and mount the volume if successful
        QDir tempMount;
        if (tempMount.mkpath(mountpoint)) {
            System::runCmd("mount", {"-t", "btrfs", "-o", "subvolid=5", "UUID=" + uuid, mountpoint}, false);
        } else {
            return QString();
        }
    }

    return mountpoint;
}

bool Btrfs::renameSubvolume(const QString &source, const QString &target) {
    QDir dir;
    // If there is an empty dir at target, remove it
    if (dir.exists(target)) {
        dir.rmdir(target);
    }
    return dir.rename(source, target);
}

const QString Btrfs::scrubStatus(const QString &mountpoint) const {
    return System::runCmd("btrfs", {"scrub", "status", mountpoint}, false).output;
}

void Btrfs::setQgroupEnabled(const QString &mountpoint, bool enable) {
    if (enable) {
        System::runCmd(QStringLiteral("btrfs"), {QStringLiteral("quota"), QStringLiteral("enable"), mountpoint}, false);
    } else {
        System::runCmd(QStringLiteral("btrfs"), {QStringLiteral("quota"), QStringLiteral("disable"), mountpoint}, false);
    }
}

const int Btrfs::subvolId(const QString &uuid, const QString &subvolName) const {
    if (!m_volumes.contains(uuid) || !m_volumes[uuid].populated) {
        return 0;
    }

    const QMap<int, Subvolume> subvols = m_volumes[uuid].subvolumes;
    int subvolId = 0;
    QList<int> subvolIds = subvols.keys();
    for (const int &subvol : subvolIds) {
        if (subvols[subvol].subvolName.trimmed() == subvolName.trimmed()) {
            subvolId = subvol;
        }
    }
    return subvolId;
}

const QString Btrfs::subvolName(const QString &uuid, const int subvolId) const {
    if (m_volumes.contains(uuid) && m_volumes[uuid].subvolumes.contains(subvolId)) {
        return m_volumes[uuid].subvolumes[subvolId].subvolName;
    } else {
        return QString();
    }
}

const int Btrfs::subvolParent(const QString &uuid, const int subvolId) const {
    if (m_volumes.contains(uuid) && m_volumes[uuid].subvolumes.contains(subvolId)) {
        return m_volumes[uuid].subvolumes[subvolId].parentId;
    } else {
        return 0;
    }
}

bool Btrfs::isUuidLoaded(const QString &uuid) {
    // First make sure the data we are trying to access exists
    if (!m_volumes.contains(uuid) || !m_volumes[uuid].populated) {
        loadVolumes();
    }

    // If it still doesn't exist, we need to bail
    if (!m_volumes.contains(uuid) || !m_volumes[uuid].populated) {
        qWarning() << tr("UUID " + uuid.toUtf8() + " not found!");
        return false;
    }

    return true;
}

void Btrfs::startBalanceRoot(const QString &uuid) {
    if (isUuidLoaded(uuid)) {
        QString mountpoint = mountRoot(uuid);

        // Run full balance command against UUID top level subvolume.
        System::runCmd("btrfs", {"balance", "start", mountpoint, "--full-balance", "--bg"}, false);
    }
}

void Btrfs::startScrubRoot(const QString &uuid) {
    if (isUuidLoaded(uuid)) {
        QString mountpoint = mountRoot(uuid);

        System::runCmd("btrfs", {"scrub", "start", mountpoint}, false);
    }
}

void Btrfs::stopBalanceRoot(const QString &uuid) {
    if (isUuidLoaded(uuid)) {
        QString mountpoint = mountRoot(uuid);

        System::runCmd("btrfs", {"balance", "cancel", mountpoint}, false);
    }
}

void Btrfs::stopScrubRoot(const QString &uuid) {
    if (isUuidLoaded(uuid)) {
        QString mountpoint = mountRoot(uuid);

        System::runCmd("btrfs", {"scrub", "cancel", mountpoint}, false);
    }
}
