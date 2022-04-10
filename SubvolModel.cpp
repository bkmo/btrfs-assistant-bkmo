#include "SubvolModel.h"
#include "Btrfs.h"
#include "System.h"

QVariant SubvolumeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    if (orientation == Qt::Vertical) {
        return section;
    }

    switch (section) {
    case Column::ParentId:
        return tr("Parent ID");
    case Column::Id:
        return tr("Subvol ID");
    case Column::Name:
        return tr("Subvolume");
    case Column::Size:
        return tr("Size");
    case Column::Uuid:
        return tr("UUID");
    case Column::Exclusive:
        return tr("Exclusive");
    }

    return QString();
}

int SubvolumeModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;

    return m_data.count();
}

int SubvolumeModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid())
        return 0;

    return ColumnCount;
}

QVariant SubvolumeModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) {
        return QVariant();
    }

    // Ensure the data is in range
    if (index.column() >= ColumnCount || index.row() >= m_data.count()) {
        return QVariant();
    }

    if (role == Qt::TextAlignmentRole) {
        switch (static_cast<Column>(index.column())) {
        case Column::Id:
        case Column::ParentId:
        case Column::Name:
        case Column::Uuid:
            return {};
        case Column::Size:
        case Column::Exclusive:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
            break;
        case Column::ColumnCount:
            return {};
        }
    }

    if (role != Qt::DisplayRole && role != Role::Sort && role != Qt::TextAlignmentRole) {
        return QVariant();
    }

    const Subvolume &subvol = m_data[index.row()];
    switch (index.column()) {
    case Column::ParentId:
        return subvol.parentId;
    case Column::Id:
        return subvol.subvolId;
    case Column::Name:
        return subvol.subvolName;
    case Column::Size:
        if (role == Qt::DisplayRole) {
            return System::toHumanReadable(subvol.size);
        } else {
            return QVariant::fromValue<qulonglong>(subvol.size);
        }
    case Column::Exclusive:
        if (role == Qt::DisplayRole) {
            return System::toHumanReadable(subvol.exclusive);
        } else {
            return QVariant::fromValue<qulonglong>(subvol.exclusive);
        }
    case Column::Uuid:
        return subvol.uuid;
    }

    return QVariant();
}

void SubvolumeModel::loadModel(const QMap<int, Subvolume> &subvolData, const QMap<int, QVector<long>> &subvolSize) {
    // Ensure that multiple threads don't try to update the model at the same time
    QMutexLocker lock(&m_updateMutex);

    beginResetModel();
    m_data.clear();
    const QList<int> keys = subvolData.keys();

    for (const int key : keys) {
        Subvolume subvol = subvolData[key];
        if (subvolSize.contains(key) && subvolSize[key].count() == 2) {
            subvol.size = subvolSize[key][0];
            subvol.exclusive = subvolSize[key][1];
        }
        m_data.append(subvol);
    }

    endResetModel();
}

SubvolumeFilterModel::SubvolumeFilterModel(QObject *parent) : QSortFilterProxyModel(parent) {
    setSortRole(static_cast<int>(SubvolumeModel::Role::Sort));
}

bool SubvolumeFilterModel::includeSnapshots() const { return m_includeSnapshots; }

bool SubvolumeFilterModel::includeDocker() const { return m_includeDocker; }

void SubvolumeFilterModel::setIncludeSnapshots(bool includeSnapshots) {
    if (m_includeSnapshots != includeSnapshots) {
        m_includeSnapshots = includeSnapshots;
        invalidateFilter();
    }
}

void SubvolumeFilterModel::setIncludeDocker(bool includeDocker) {
    if (m_includeDocker != includeDocker) {
        m_includeDocker = includeDocker;
        invalidateFilter();
    }
}

bool SubvolumeFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    QModelIndex nameIdx = sourceModel()->index(sourceRow, static_cast<int>(SubvolumeModel::Column::Name), sourceParent);
    const QString &name = sourceModel()->data(nameIdx).toString();

    if (!m_includeSnapshots && (Btrfs::isSnapper(name) || Btrfs::isTimeshift(name))) {
        return false;
    }
    if (!m_includeDocker && Btrfs::isDocker(name)) {
        return false;
    }

    return true;
}
