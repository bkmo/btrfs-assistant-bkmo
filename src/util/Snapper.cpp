#include "util/Snapper.h"
#include "util/Settings.h"
#include "util/System.h"

#include <unistd.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QXmlStreamReader>

constexpr const char *DEFAULT_SNAP_PATH = "/.snapshots";
constexpr const char *DEFAULT_SNAP_SUBVOL = ".snapshots";

Snapper::Snapper(Btrfs *btrfs, QString snapperCommand, QObject *parent) : QObject{parent}, m_btrfs(btrfs), m_snapperCommand(snapperCommand)
{
    // Load the subvolume map from settings if present
    QMap<QString, QString> *settingsSubvolMap = Settings::instance().subvolMap();
    const auto keys = settingsSubvolMap->keys();
    for (auto &key : keys) {
        MapSubvol ms;
        ms.targetName = settingsSubvolMap->value(key).split(",").at(0);
        ms.uuid = settingsSubvolMap->value(key).split(",").at(1);
        m_subvolMap.insert(key, ms);
    }

    load();
}

Snapper::Config Snapper::config(const QString &name) { return m_configs.value(name); }

void Snapper::createSubvolMap()
{
    for (const QVector<SnapperSubvolume> &subvol : qAsConst(m_subvols)) {
        const QString snapshotSubvol = findSnapshotSubvolume(subvol.at(0).subvol);
        const QString uuid = subvol.at(0).uuid;
        if (m_subvolMap.value(snapshotSubvol).uuid != uuid) {
            const uint64_t snapSubvolId = m_btrfs->subvolId(uuid, snapshotSubvol);
            const uint64_t targetId = m_btrfs->subvolParent(uuid, snapSubvolId);
            SubvolResult sr = m_btrfs->subvolumeName(uuid, targetId);

            // Get the subvolid of the target and do some additional error checking
            if (targetId == 0 || !sr.success) {
                continue;
            }

            m_subvolMap.insert(snapshotSubvol, {uuid, sr.name});
        }
    }
}

QString Snapper::findSnapshotSubvolume(const QString &subvol)
{
    static QRegularExpression re("\\/[0-9]*\\/snapshot$");
    QStringList subvolSplit = subvol.split(re);

    // If count > 1 than the split worked, otherwise there was no match
    if (subvolSplit.count() > 1) {
        return subvolSplit.at(0);
    } else {
        return QString();
    }
}

QString Snapper::findTargetPath(const QString &snapshotPath, const QString &filePath, const QString &uuid)
{
    // Make sure it is Snapper snapshot
    if (!Btrfs::isSnapper(snapshotPath)) {
        return QString();
    }

    QString snapshotSubvol = findSnapshotSubvolume(snapshotPath);

    // Ensure the root of the partition is mounted and get the mountpoint
    QString mountpoint = m_btrfs->mountRoot(uuid);

    QDir mp(mountpoint);

    const QString relSnapshotSubvol = mp.relativeFilePath(snapshotSubvol);

    QString targetSubvol;
    const SubvolResult sr = findTargetSubvol(relSnapshotSubvol, uuid);
    if (sr.success) {
        targetSubvol = sr.name;
    } else {
        return QString();
    }

    QDir snapshotDir(snapshotPath);

    QString relpath = snapshotDir.relativeFilePath(filePath);

    if (snapshotSubvol.isEmpty() || mountpoint.isEmpty() || relpath.isEmpty()) {
        return QString();
    }

    return QDir::cleanPath(mountpoint + QDir::separator() + targetSubvol + QDir::separator() + relpath);
}

SubvolResult Snapper::findTargetSubvol(const QString &snapshotSubvol, const QString &uuid) const
{
    if (m_subvolMap.value(snapshotSubvol).uuid == uuid) {
        return {m_subvolMap.value(snapshotSubvol).targetName, true};
    } else {
        return {QString(), false};
    }
}

void Snapper::load()
{
    // Load the list of valid configs
    m_configs.clear();
    m_snapshots.clear();
    const SnapperResult result = runSnapper("list-configs --columns config");

    if (result.exitCode != 0 || result.outputList.isEmpty()) {
        return;
    }

    for (const QString &line : qAsConst(result.outputList)) {
        // for each config, add to the map and add it's snapshots to the vector
        SnapperResult listResult;
        QString name = line.trimmed();

        loadConfig(name);

        // The root needs special handling because we may be booted off a snapshot
        if (name == "root") {
            listResult = runSnapper("list --columns number,date,description,type");

            if (listResult.exitCode != 0) {
                continue;
            }

            if (listResult.outputList.isEmpty()) {
                // This means that either there are no snapshots or the root is mounted on non-btrfs filesystem like an overlayfs
                // Let's check the latter case first
                if (!m_btrfs->subvolumeName(DEFAULT_SNAP_SUBVOL).success) {
                    // This probably means there are just no snapshots or we are using a nested subvol in another place
                    continue;
                }

                // Now we need to find out where the snapshots are actually stored
                const uint64_t parentId = m_btrfs->subvolParent(DEFAULT_SNAP_SUBVOL);

                // It shouldn't be possible for the parent to not exist but we check anyway
                if (parentId == 0) {
                    continue;
                }

                const QString uuid = System::runCmd("findmnt", {"-no", "uuid", DEFAULT_SNAP_PATH}, false).output;

                // Make sure the root of the partition is mounted
                QString mountpoint = m_btrfs->mountRoot(uuid);
                if (mountpoint.isEmpty()) {
                    continue;
                }

                const QString parentName = m_btrfs->subvolumeName(uuid, parentId).name;

                listResult = runSnapper("--no-dbus -r " + QDir::cleanPath(mountpoint + QDir::separator() + parentName) +
                                        " list --columns number,date,description,type");
                if (listResult.exitCode != 0 || listResult.outputList.isEmpty()) {
                    // If this is still empty, give up
                    continue;
                }
            }
        } else {
            listResult = runSnapper("list --columns number,date,description,type", name);
            if (listResult.exitCode != 0 || listResult.outputList.isEmpty()) {
                continue;
            }
        }

        for (const QString &snap : qAsConst(listResult.outputList)) {
            const QStringList &cols = snap.split(',');
            const uint number = cols.at(0).trimmed().toUInt();

            // Snapshot 0 is not a real snapshot
            if (number == 0) {
                continue;
            }

            m_snapshots[name].append(
                {number, QDateTime::fromString(cols.at(1).trimmed(), Qt::ISODate), cols.at(2).trimmed(), cols.at(3).trimmed()});
        }
    }
    loadSubvols();
}

void Snapper::loadConfig(const QString &name)
{
    // If the config is already loaded, remove the old data
    if (m_configs.contains(name)) {
        m_configs.remove(name);
    }

    // Call Snapper to get the config data
    const SnapperResult result = runSnapper("get-config", name);

    if (result.exitCode != 0) {
        return;
    }

    // Iterate over the data adding the name/value pairs to the map
    Config config;
    for (const QString &line : result.outputList) {
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

void Snapper::loadSubvols()
{
    // Clear the existing info
    m_subvols.clear();

    // Get a list of the btrfs filesystems and loop over them
    const QStringList btrfsFilesystems = Btrfs::listFilesystems();
    for (const QString &uuid : btrfsFilesystems) {
        // We need to ensure the root is mounted and get the mountpoint
        QString mountpoint = m_btrfs->mountRoot(uuid);
        if (mountpoint.isEmpty()) {
            continue;
        }

        const SubvolumeMap &subvols = m_btrfs->listSubvolumes(uuid);

        for (const auto &subvol : subvols) {

            // Check if it is snapper snapshot
            if (!Btrfs::isSnapper(subvol.subvolName)) {
                continue;
            }

            SnapperSubvolume snapperSubvol;

            snapperSubvol.uuid = uuid;
            snapperSubvol.subvolid = subvol.id;
            snapperSubvol.subvol = subvol.subvolName;

            // It is a snapshot so now we parse it and read the snapper XML
            const QString end = "snapshot";
            QString filename = snapperSubvol.subvol.left(snapperSubvol.subvol.length() - end.length()) + "info.xml";

            filename = QDir::cleanPath(mountpoint + QDir::separator() + filename);

            SnapperSnapshot snap = readSnapperMeta(filename);

            if (snap.number == 0) {
                continue;
            }

            snapperSubvol.desc = snap.desc;
            snapperSubvol.time = snap.time;
            snapperSubvol.snapshotNum = snap.number;
            snapperSubvol.type = snap.type;

            const QString snapshotSubvol = findSnapshotSubvolume(snapperSubvol.subvol);
            if (snapshotSubvol.isEmpty()) {
                continue;
            }

            // Check the map for the target subvolume
            const SubvolResult sr = findTargetSubvol(snapshotSubvol, uuid);
            QString targetSubvol = sr.name;
            // If it failed, it may mean the the map isn't loaded yet for the nested subvolumes
            if (!sr.success) {
                if (snapshotSubvol.endsWith(DEFAULT_SNAP_PATH) || snapshotSubvol == DEFAULT_SNAP_SUBVOL) {
                    const uint64_t targetSubvolId = m_btrfs->subvolId(uuid, snapshotSubvol);
                    const uint64_t parentId = m_btrfs->subvolParent(uuid, targetSubvolId);
                    targetSubvol = m_btrfs->subvolumeName(uuid, parentId).name;
                } else {
                    continue;
                }
            }

            m_subvols[targetSubvol].append(snapperSubvol);
        }
    }
    createSubvolMap();
}

SnapperSnapshot Snapper::readSnapperMeta(const QString &filename)
{
    SnapperSnapshot snap;
    QFile metaFile(filename);

    if (metaFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QXmlStreamReader xml(&metaFile);

        // Read until we find snapshot
        while (!xml.atEnd() && xml.name() != "snapshot") {
            xml.readNextStartElement();
        }

        while (xml.readNextStartElement()) {
            if (xml.name() == "num") {
                snap.number = xml.readElementText().toUInt();
            } else if (xml.name() == "date") {
                snap.time = QDateTime::fromString(xml.readElementText(), Qt::ISODate);
                snap.time = snap.time.addSecs(snap.time.offsetFromUtc());
            } else if (xml.name() == "description") {
                snap.desc = xml.readElementText();
            } else if (xml.name() == "type") {
                snap.type = xml.readElementText();
            } else {
                xml.readElementText();
            }
        }
    }

    return snap;
}

bool Snapper::restoreFile(const QString &sourcePath, const QString &destPath) const
{

    // QFile won't overwrite an existing file so we need to remove it first
    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }
    if (!QFile::copy(sourcePath, destPath)) {
        return false;
    }

    // Now we need to restore the permissions or it will be owned by root
    QFileInfo qfi(sourcePath);
    if (chown(destPath.toUtf8(), qfi.ownerId(), qfi.groupId()) != 0) {
        qWarning() << tr("Failed to reset ownership of restored file") << Qt::endl;
    }
    QFile::setPermissions(destPath, QFile::permissions(sourcePath));

    return true;
}

SnapperResult Snapper::setConfig(const QString &name, const Config &configMap)
{
    SnapperResult result;

    const QStringList keys = configMap.keys();

    QString command;
    for (const QString &key : keys) {
        if (configMap[key].isEmpty()) {
            continue;
        }

        command += " " + key + "=" + configMap[key];
    }

    if (command.isEmpty()) {
        result.exitCode = -1;
        result.outputList = QStringList() << tr("Failed to set config");
    } else {
        result = runSnapper("set-config" + command, name);
    }

    loadConfig(name);

    return result;
}

QVector<SnapperSnapshot> Snapper::snapshots(const QString &config)
{
    if (m_snapshots.contains(config)) {
        return m_snapshots[config];
    } else {
        return QVector<SnapperSnapshot>();
    }
}

QVector<SnapperSubvolume> Snapper::subvols(const QString &config)
{
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

SnapperResult Snapper::runSnapper(const QString &command, const QString &name) const
{
    Result result;
    SnapperResult snapperResult;

    if (name.isEmpty()) {
        result = System::runCmd(m_snapperCommand + " --machine-readable csv -q " + command, true);
    } else {
        result = System::runCmd(m_snapperCommand + " -c " + name + " --machine-readable csv -q " + command, true);
    }

    snapperResult.exitCode = result.exitCode;

    if (result.exitCode != 0 || result.output.isEmpty()) {
        snapperResult.outputList = QStringList() << result.output;
    } else {
        QStringList outputList = result.output.split('\n');

        // Remove the header
        outputList.removeFirst();

        snapperResult.outputList = outputList;
    }

    return snapperResult;
}

bool Snapper::Config::isEmpty() const { return QMap<QString, QString>::isEmpty(); }

QString Snapper::Config::subvolume() const { return value("SUBVOLUME"); }

void Snapper::Config::setSubvolume(const QString &value) { insert("SUBVOLUME", value); }

bool Snapper::Config::isTimelineCreate() const { return boolValue("TIMELINE_CREATE"); }

void Snapper::Config::setTimelineCreate(bool value) { insertBool("TIMELINE_CREATE", value); }

int Snapper::Config::timelineLimitHourly() const { return intValue("TIMELINE_LIMIT_HOURLY"); }

void Snapper::Config::setTimelineLimitHourly(int value) { insertInt("TIMELINE_LIMIT_HOURLY", value); }

int Snapper::Config::timelineLimitDaily() const { return intValue("TIMELINE_LIMIT_DAILY"); }

void Snapper::Config::setTimelineLimitDaily(int value) { insertInt("TIMELINE_LIMIT_DAILY", value); }

int Snapper::Config::timelineLimitWeekly() const { return intValue("TIMELINE_LIMIT_WEEKLY"); }

void Snapper::Config::setTimelineLimitWeekly(int value) { insertInt("TIMELINE_LIMIT_WEEKLY", value); }

int Snapper::Config::timelineLimitMonthly() const { return intValue("TIMELINE_LIMIT_MONTHLY"); }

void Snapper::Config::setTimelineLimitMonthly(int value) { insertInt("TIMELINE_LIMIT_MONTHLY", value); }

int Snapper::Config::timelineLimitYearly() const { return intValue("TIMELINE_LIMIT_YEARLY"); }

void Snapper::Config::setTimelineLimitYearly(int value) { insertInt("TIMELINE_LIMIT_YEARLY", value); }

int Snapper::Config::numberLimit() const { return intValue("NUMBER_LIMIT"); }

void Snapper::Config::setNumberLimit(int value) { insertInt("NUMBER_LIMIT", value); }

void Snapper::Config::insertBool(const QString &key, bool value) { insert(key, value ? "yes" : "no"); }

bool Snapper::Config::boolValue(const QString &key, bool defaultValue) const { return value(key, defaultValue ? "yes" : "no") == "yes"; }

void Snapper::Config::insertInt(const QString &key, int value) { insert(key, QString::number(value)); }

int Snapper::Config::intValue(const QString &key, int defaultValue) const
{
    bool ok = false;
    int ret = value(key).toInt(&ok);
    if (!ok) {
        ret = defaultValue;
    }
    return ret;
}
