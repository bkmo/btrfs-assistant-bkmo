#include "Btrfs.h"
#include "System.h"

#include <QDebug>
#include <QDir>
#include <QUuid>

Btrfs::Btrfs(QObject *parent) : QObject{parent} { reloadVolumes(); }

/**
 * @brief Return Btrfs data for a particular UUID.
 * @param uuid
 * @return BtrfsMeta
 */
const BtrfsMeta Btrfs::btrfsVolume(const QString &uuid) const {
    // If the uuid isn't found return a default constructed btrfsMeta
    if (!m_volumes.contains(uuid)) {
        return BtrfsMeta();
    }

    return m_volumes[uuid];
}

/**
 * @brief Returns list of children Btrfs subvolumes for a given subvolume.
 * @param subvolId
 * @param uuid
 * @return QStringList<BtrfsMeta>
 */
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

/**
 * @brief Delete a subvolume from a btrfs device.
 * @param uuid
 * @param subvolid
 * @return bool
 */
const bool Btrfs::deleteSubvol(const QString &uuid, const int subvolid) {
    Subvolume subvol;
    if (m_volumes.contains(uuid)) {
        subvol = m_volumes[uuid].subvolumes.value(subvolid);
        if (subvol.parentId != 0) {
            QString mountpoint = mountRoot(uuid);

            // Everything checks out, lets delete the subvol
            if (mountpoint.right(1) != "/")
                mountpoint += "/";
            Result result = System::runCmd("btrfs subvolume delete " + mountpoint + subvol.subvolName, true);
            if (result.exitCode == 0) {
                return true;
            }
        }
    }

    // If we get to here, it failed
    return false;
}

/**
 * @brief Checks whether a given subvolume is a snapper snapshot.
 * @param subvolume
 * @return bool
 */
bool Btrfs::isSnapper(const QString &subvolume) {
    static QRegularExpression re("\\/[0-9]*\\/snapshot$");
    return re.match(subvolume).hasMatch();
}

/**
 * @brief Checks if a given subvolume is mounted.
 * @param uuid
 * @param subvolid
 * @return bool
 */
bool Btrfs::isMounted(const QString &uuid, const int subvolid) {
    const QStringList outputList =
        System::runCmd("findmnt -nO subvolid=" + QString::number(subvolid) + " -o uuid", false).output.trimmed().split("\n");
    return uuid == outputList.at(0).trimmed();
}

/**
 * @brief Returns the root subvolume for currently mounted device.
 * @return
 */
const QString Btrfs::findRootSubvol() {
    const Result findmntResult = System::runCmd("LANG=C findmnt -no uuid,options /", false);
    if (findmntResult.exitCode != 0 || findmntResult.output.isEmpty())
        return QString();

    const QString uuid = findmntResult.output.split(' ').at(0).trimmed();
    const QString options = findmntResult.output.right(findmntResult.output.length() - uuid.length()).trimmed();
    if (options.isEmpty() || uuid.isEmpty())
        return QString();

    QString subvol;
    const QStringList optionsList = options.split(',');
    for (const QString &option : optionsList) {
        if (option.startsWith("subvol="))
            subvol = option.split("subvol=").at(1);
    }

    // Make sure subvolume doesn't have a leading slash
    if (subvol.startsWith("/"))
        subvol = subvol.right(subvol.length() - 1);

    // At this point subvol will either contain nothing or the name of the subvol
    return subvol;
}

/**
 * @brief Return list of found btrfs devices
 * @return
 */
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

/**
 * @brief Return list of btrfs mountpoints
 * @return
 */
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

/**
 * @brief Return subvolume map for a given btrfs device.
 * @param uuid
 * @return
 */
const QMap<int, Subvolume> Btrfs::listSubvolumes(const QString &uuid) const {
    // If the uuid isn't found return a default constructed QMap
    if (!m_volumes.contains(uuid)) {
        return QMap<int, Subvolume>();
    }

    return m_volumes[uuid].subvolumes;
}

/**
 * @brief Mount the root subvolume, if not already mounted.
 * @param uuid
 * @return
 */
const QString Btrfs::mountRoot(const QString &uuid) {
    // Check to see if it is already mounted
    QStringList findmntOutput = System::runCmd("findmnt -nO subvolid=5 -o uuid,target", false).output.split('\n');
    QString mountpoint;
    for (const QString &line : qAsConst(findmntOutput)) {
        if (!line.isEmpty() && line.split(' ').at(0).trimmed() == uuid)
            mountpoint = line.split(' ').at(1).trimmed();
    }

    // If it isn't mounted we need to mount it
    if (mountpoint.isEmpty()) {
        // Format a temp mountpoint using a GUID
        mountpoint = QDir::cleanPath(QDir::tempPath() + QDir::separator() + QUuid::createUuid().toString());

        // Create the mountpoint and mount the volume if successful
        QDir tempMount;
        if (tempMount.mkpath(mountpoint))
            System::runCmd("mount -t btrfs -o subvolid=5 UUID=" + uuid + " " + mountpoint, false);
        else
            return QString();
    }

    return mountpoint;
}

/**
 * @brief Reloads subvolumes for a given btrfs device.
 * @param uuid
 */
void Btrfs::reloadSubvols(const QString &uuid) {
    // First make sure the data we are trying to reload exists
    if (!m_volumes.contains(uuid) || !m_volumes[uuid].populated) {
        reloadVolumes();
    }

    // If it still doesn't exist, we need to bail
    if (!m_volumes.contains(uuid) || !m_volumes[uuid].populated) {
        qWarning() << tr("UUID " + uuid.toUtf8() + " not found!");
        return;
    }

    m_volumes[uuid].subvolumes.clear();

    QString mountpoint = mountRoot(uuid);

    QStringList output = System::runCmd("btrfs subvolume list " + mountpoint, false).output.split('\n');
    QMap<int, Subvolume> subvols;
    for (const QString &line : qAsConst(output)) {
        if (!line.isEmpty()) {
            Subvolume subvol;
            int subvolId = line.split(' ').at(1).toInt();
            subvol.subvolName = line.split(' ').at(8).trimmed();
            subvol.parentId = line.split(' ').at(6).toInt();
            subvols[subvolId] = subvol;
        }
    }

    m_volumes[uuid].subvolumes = subvols;
}

/**
 * @brief Reloads data and subvolumes for all detected btrfs devices.
 */
void Btrfs::reloadVolumes() {
    m_volumes.clear();

    QStringList uuidList = listFilesystems();

    for (const QString &uuid : qAsConst(uuidList)) {
        QString mountpoint = mountRoot(uuid);
        if (!mountpoint.isEmpty()) {
            BtrfsMeta btrfs;
            btrfs.populated = true;
            btrfs.mountPoint = mountpoint;
            QStringList usageLines = System::runCmd("LANG=C ; btrfs fi usage -b " + mountpoint, false).output.split('\n');
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
            reloadSubvols(uuid);
        }
    }
}

/**
 * @brief Renames a btrfs subvolume.
 * @param source
 * @param target
 * @return
 */
bool Btrfs::renameSubvolume(const QString &source, const QString &target) {
    QDir dir;
    // If there is an empty dir at target, remove it
    if (dir.exists(target)) {
        dir.rmdir(target);
    }
    return dir.rename(source, target);
}

/**
 * @brief Restores snapshot to a given btrfs device subvolume.
 * @param uuid
 * @param sourceId
 * @param targetId
 * @return
 */
const RestoreResult Btrfs::restoreSubvol(const QString &uuid, const int sourceId, const int targetId) const {
    RestoreResult restoreResult;

    // Get the subvol names associated with the IDs
    const QString sourceName = subvolName(uuid, sourceId);
    const QString targetName = subvolName(uuid, targetId);

    // Ensure the root of the partition is mounted and get the mountpoint
    QString mountpoint = Btrfs::mountRoot(uuid);

    // Make sure we have a trailing /
    QDir::cleanPath(mountpoint += "/");

    // We are out of excuses, time to do the restore....carefully
    QString targetBackup = "restore_backup_" + targetName + "_" + QTime::currentTime().toString("HHmmsszzz");
    restoreResult.backupSubvolName = targetBackup;

    QDir dirWorker;

    // Find the children before we start
    const QStringList children = this->children(targetId, uuid);

    // Rename the target
    if (!Btrfs::renameSubvolume(QDir::cleanPath(mountpoint + targetName), QDir::cleanPath(mountpoint + targetBackup))) {
        restoreResult.failureMessage = tr("Failed to make a backup of target subvolume");
        return restoreResult;
    }

    // We moved the snapshot so we need to change the location
    const QString newSubvolume = targetBackup + sourceName.right(sourceName.length() - targetName.length());

    // Place a snapshot of the source where the target was
    System::runCmd("btrfs subvolume snapshot " + mountpoint + newSubvolume + " " + mountpoint + targetName, false);

    // Make sure it worked
    if (!dirWorker.exists(mountpoint + targetName)) {
        // That failed, try to put the old one back
        Btrfs::renameSubvolume(QDir::cleanPath(mountpoint + targetBackup), QDir::cleanPath(mountpoint + targetName));
        restoreResult.failureMessage = tr("Failed to restore subvolume!") + "\n\n" +
                                       tr("Snapshot restore failed.  Please verify the status of your system before rebooting");
        return restoreResult;
    }

    // The restore was successful, now we need to move any child subvolumes into the target
    QString childSubvolPath;
    for (const QString &childSubvol : children) {
        childSubvolPath = childSubvol.right(childSubvol.length() - (targetName.length() + 1));

        // rename snapshot
        QString sourcePath = QDir::cleanPath(mountpoint + targetBackup + QDir::separator() + childSubvolPath);
        QString destinationPath = QDir::cleanPath(mountpoint + childSubvol);
        if (!Btrfs::renameSubvolume(sourcePath, destinationPath)) {
            // If this fails, not much can be done except let the user know
            restoreResult.failureMessage = tr("The restore was successful but the migration of the nested subvolumes failed") + "\n\n" +
                                           tr("Please migrate the those subvolumes manually");
            return restoreResult;
        }
    }

    // If we get to here, it worked!
    restoreResult.success = true;
    return restoreResult;
}

/**
 * @brief Return the subvolume id for a given subolume name on a device.
 * @param uuid
 * @param subvolName
 * @return
 */
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

/**
 * @brief Return the subvolume name for a given subvolume id on a device.
 * @param uuid
 * @param subvolId
 * @return
 */
const QString Btrfs::subvolName(const QString &uuid, const int subvolId) const {
    if (m_volumes.contains(uuid) && m_volumes[uuid].subvolumes.contains(subvolId)) {
        return m_volumes[uuid].subvolumes[subvolId].subvolName;
    } else {
        return QString();
    }
}

/**
 * @brief Return the top parent subvolume for a given subvolume on a device.
 * @param uuid
 * @param subvolId
 * @return
 */
const int Btrfs::subvolTopParent(const QString &uuid, const int subvolId) const {
    int parentId = subvolId;
    if (m_volumes.contains(uuid) && m_volumes[uuid].subvolumes.contains(subvolId)) {
        while (m_volumes[uuid].subvolumes[parentId].parentId != 5) {
            parentId = m_volumes[uuid].subvolumes[parentId].parentId;
        }
    } else {
        return 0;
    }

    return parentId;
}
