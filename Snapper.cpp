#include "Snapper.h"

// Read a snapper snapshot meta file and return the data
static SnapperSnapshots getSnapperMeta(const QString &filename) {
    SnapperSnapshots snap;
    snap.number = 0;
    QFile metaFile(filename);
    if (!metaFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return snap;

    while (!metaFile.atEnd()) {
        QString line = metaFile.readLine();
        if (line.trimmed().startsWith("<num>"))
            snap.number = line.trimmed().split("<num>").at(1).split("</num>").at(0).trimmed().toInt();
        else if (line.trimmed().startsWith("<date>"))
            snap.time = line.trimmed().split("<date>").at(1).split("</date>").at(0).trimmed();
        else if (line.trimmed().startsWith("<description>"))
            snap.desc = line.trimmed().split("<description>").at(1).split("</description>").at(0).trimmed();
        else if (line.trimmed().startsWith("<type>"))
            snap.type = line.trimmed().split("<type>").at(1).split("</type>").at(0).trimmed();
    }

    return snap;
}

Snapper::Snapper(Btrfs *btrfs, QString snapperCommand, const QMap<QString, QString> &subvolMap, QObject *parent) : QObject{parent} {
    m_btrfs = btrfs;
    m_snapperCommand = snapperCommand;
    m_subvolMap = subvolMap;
    load();
}

const QMap<QString, QString> Snapper::config(const QString &name) {
    if (m_configs.contains(name)) {
        return m_configs[name];
    } else {
        return QMap<QString, QString>();
    }
}

void Snapper::createSubvolMap() {
    for (const QVector<SnapperSubvolume> &subvol : qAsConst(m_subvols)) {
        const QString snapshotSubvol = findSnapshotSubvolume(subvol.at(0).subvol);
        const QString uuid = subvol.at(0).uuid;
        if (!m_subvolMap.value(snapshotSubvol, "").endsWith(uuid)) {
            const int snapSubvolId = m_btrfs->subvolId(uuid, snapshotSubvol);
            int targetId = m_btrfs->subvolTopParent(uuid, snapSubvolId);
            QString targetSubvol = m_btrfs->subvolName(uuid, targetId);

            // Get the subvolid of the target and do some additional error checking
            if (targetId == 0 || targetSubvol.isEmpty()) {
                continue;
            }

            // Handle a special case where the snapshot is of the root of the Btrfs partition
            if (targetSubvol == ".snapshots") {
                targetSubvol = "";
            }

            m_subvolMap.insert(snapshotSubvol, targetSubvol + "," + uuid);
        }
    }
}

const QString Snapper::findSnapshotSubvolume(const QString &subvol) {
    static QRegularExpression re("\\/[0-9]*\\/snapshot$");
    QStringList subvolSplit = subvol.split(re);

    // If count > 1 than the split worked, otherwise there was no match
    if (subvolSplit.count() > 1) {
        return subvolSplit.at(0);
    } else {
        return QString();
    }
}

const QString Snapper::findTargetSubvol(const QString &snapshotSubvol, const QString &uuid) const {
    if (m_subvolMap.value(snapshotSubvol, "").endsWith(uuid)) {
        return m_subvolMap[snapshotSubvol].split(",").at(0);
    } else {
        return QString();
    }
}

void Snapper::load() {

    // Load the list of valid configs
    m_configs.clear();
    m_snapshots.clear();
    const QStringList outputList = runSnapper("list-configs --columns config");

    for (const QString &line : qAsConst(outputList)) {
        // for each config, add to the map and add it's snapshots to the vector
        QStringList list;
        QString name = line.trimmed();

        loadConfig(name);

        // The root needs special handling because we may be booted off a snapshot
        if (name == "root") {
            list = runSnapper("list --columns number,date,description,type");
            if (list.isEmpty()) {
                // This means that either there are no snapshots or the root is mounted on non-btrfs filesystem like an overlayfs
                // Let's check the latter case first
                QString findmntOutput = System::runCmd("findmnt -no uuid,options /.snapshots", false).output;
                if (findmntOutput.isEmpty()) {
                    // This probably means there are just no snapshots
                    continue;
                }

                // We found something mounted at /.snapshots, now we need to figure out what it is.

                QString uuid = findmntOutput.split(' ').at(0).trimmed();
                QString options = findmntOutput.right(findmntOutput.length() - uuid.length()).trimmed();
                if (options.isEmpty() || uuid.isEmpty()) {
                    continue;
                }

                QString subvol;
                const QStringList optionsList = options.split(',');
                for (const QString &option : optionsList) {
                    if (option.startsWith("subvol=")) {
                        subvol = option.split("subvol=").at(1);
                    }
                }

                if (subvol.isEmpty() || !subvol.contains(".snapshots")) {
                    continue;
                }

                // Make sure subvolume doesn't have a leading slash
                if (subvol.startsWith("/")) {
                    subvol = subvol.right(subvol.length() - 1);
                }

                // Now we need to find out where the snapshots are actually stored
                QString prefix = subvol.split(".snapshots").at(0);

                // It shouldn't be possible for the prefix to empty when booted off a snapshot but we check anyway
                if (prefix.isEmpty()) {
                    continue;
                }

                // Make sure the root of the partition is mounted
                QString mountpoint = Btrfs::mountRoot(uuid);

                // Make sure we have a trailing /
                if (mountpoint.right(1) != "/") {
                    mountpoint += "/";
                }

                list = runSnapper("--no-dbus -r " + QDir::cleanPath(mountpoint + prefix) + " list --columns number,date,description,type");
                if (list.isEmpty()) {
                    // If this is still empty, give up
                    continue;
                }
            }
        } else {
            list = runSnapper("list --columns number,date,description,type", name);
            if (list.isEmpty()) {
                continue;
            }
        }

        for (const QString &snap : qAsConst(list)) {
            m_snapshots[name].append({snap.split(',').at(0).trimmed().toInt(), snap.split(',').at(1).trimmed(),
                                      snap.split(',').at(2).trimmed(), snap.split(',').at(3).trimmed()});
        }
    }
    loadSubvols();
}

void Snapper::loadConfig(const QString &name) {
    // If the config is already loaded, remove the old data
    if (m_configs.contains(name)) {
        m_configs.remove(name);
    }

    // Call Snapper to get the config data
    const QStringList outputList = runSnapper("get-config", name);

    // Iterate over the data adding the name/value pairs to the map
    QMap<QString, QString> config;
    for (const QString &line : outputList) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        QString key = line.split(',').at(0).trimmed();
        QString value = line.split(',').at(1).trimmed();
        config.insert(key, value);
    }

    // Add the map to m_configs
    if (!config.isEmpty()) {
        m_configs[name] = config;
    }
}

void Snapper::loadSubvols() {
    // Clear the existing info
    m_subvols.clear();

    // Get a list of the btrfs filesystems and loop over them
    const QStringList btrfsFilesystems = Btrfs::listFilesystems();
    for (const QString &uuid : btrfsFilesystems) {
        // We need to ensure the root is mounted and get the mountpoint
        QString mountpoint = Btrfs::mountRoot(uuid);
        if (mountpoint.isEmpty()) {
            continue;
        }
        QString output = System::runCmd("btrfs subvolume list " + mountpoint, false).output;

        if (output.isEmpty()) {
            continue;
        }

        // Ensure it has a trailing /
        if (mountpoint.right(1) != "/") {
            mountpoint += "/";
        }

        QStringList outputList = output.split('\n');
        for (const QString &line : outputList) {
            SnapperSubvolume subvol;
            if (line.isEmpty()) {
                continue;
            }

            subvol.uuid = uuid;
            subvol.subvolid = line.split(' ').at(1).trimmed().toInt();
            subvol.subvol = line.split(' ').at(8).trimmed();

            // Check if it is snapper snapshot
            if (!Btrfs::isSnapper(subvol.subvol)) {
                continue;
            }

            // It is a snapshot so now we parse it and read the snapper XML
            const QString end = "snapshot";
            QString filename = subvol.subvol.left(subvol.subvol.length() - end.length()) + "info.xml";

            filename = QDir::cleanPath(mountpoint + filename);

            SnapperSnapshots snap = getSnapperMeta(filename);

            if (snap.number == 0) {
                continue;
            }

            subvol.desc = snap.desc;
            subvol.time = snap.time;
            subvol.snapshotNum = snap.number;
            subvol.type = snap.type;

            const QString snapshotSubvol = findSnapshotSubvolume(subvol.subvol);
            if (snapshotSubvol.isEmpty()) {
                continue;
            }

            // Check the map for the target subvolume
            QString targetSubvol = findTargetSubvol(snapshotSubvol, uuid);

            // If it is empty, it may mean the the map isn't loaded yet for the nested subvolumes
            if (targetSubvol.isEmpty()) {
                if (snapshotSubvol.endsWith(".snapshots")) {
                    const int targetSubvolId = m_btrfs->subvolId(uuid, snapshotSubvol);
                    const int parentId = m_btrfs->subvolTopParent(uuid, targetSubvolId);
                    targetSubvol = m_btrfs->subvolName(uuid, parentId);
                } else {
                    continue;
                }
            }

            m_subvols[targetSubvol].append(subvol);
        }
    }
    createSubvolMap();
}

void Snapper::setConfig(const QString &name, const QMap<QString, QString> configMap) {
    const QStringList keys = configMap.keys();

    QString command;
    for (const QString &key : keys) {
        if (configMap[key].isEmpty()) {
            continue;
        }

        command += " " + key + "=" + configMap[key];
    }

    if (!command.isEmpty()) {
        runSnapper("set-config" + command, name);
    }

    loadConfig(name);
}

const QVector<SnapperSnapshots> Snapper::snapshots(const QString &config) {
    if (m_snapshots.contains(config)) {
        return m_snapshots[config];
    } else {
        return QVector<SnapperSnapshots>();
    }
}

const QVector<SnapperSubvolume> Snapper::subvols(const QString &config) {
    if (m_subvols.contains(config)) {
        return m_subvols[config];
    } else {
        return QVector<SnapperSubvolume>();
    }
}

/*
 *
 *  Private functions
 *
 */

const QStringList Snapper::runSnapper(const QString &command, const QString &name) const {
    QString output;

    if (name.isEmpty()) {
        output = System::runCmd(m_snapperCommand + " --machine-readable csv -q " + command, false).output;
    } else {
        output = System::runCmd(m_snapperCommand + " -c " + name + " --machine-readable csv -q " + command, false).output;
    }

    if (output.isEmpty()) {
        return QStringList();
    }

    QStringList outputList = output.split('\n');

    // Remove the header
    outputList.removeFirst();

    return outputList;
}