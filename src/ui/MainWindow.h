#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSet>

class QCheckBox;

class Btrfs;
class BtrfsMaintenance;
class Snapper;
class SubvolumeFilterModel;
class SubvolumeModel;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

/**
 * @brief The MainWindow class handles the custom application logic that ties all the various btrfs and snapper systems together.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    /**
     * @brief Constructor that initializes application with various services and ensures application running as root.
     * @param btrfsMaintenance
     * @param btrfs
     * @param snapper
     * @param parent
     */
    MainWindow(Btrfs *btrfs, BtrfsMaintenance *btrfsMaintenance, Snapper *snapper, QWidget *parent = nullptr);
    ~MainWindow();

    /**
     * @brief A simple wrapper to QMessageBox for creating consistent error messages
     * @param errorText - A QString containing the text to display to the user
     */
    void displayError(const QString &errorText);

  private:
    /**
     * @brief Btrfs maintenance frequency values
     */
    Ui::MainWindow *m_ui = nullptr;
    Btrfs *m_btrfs = nullptr;
    BtrfsMaintenance *m_btrfsMaint = nullptr;
    Snapper *m_snapper = nullptr;
    QSet<QCheckBox *> m_changedCheckBoxes;
    QHash<QString, QCheckBox *> m_configCheckBoxes;
    bool m_hasSnapper = false;
    bool m_hasBtrfsmaintenance = false;
    SubvolumeFilterModel *m_subvolumeModel = nullptr;
    SubvolumeModel *m_sourceModel = nullptr;

    /**
     * @brief Timer used to periodically update UI on balance progress
     */
    QTimer *m_balanceTimer;

    /**
     * @brief Timer used to periodically update UI on scrub progress
     */
    QTimer *m_scrubTimer;

    /**
     * @brief Checks if snapper is installed and load snapper UI elements.
     */
    void loadSnapperUI();

    /**
     * @brief Sets up Btrfs Maintenance tab
     */
    void populateBmTab();

    /**
     * @brief Populate the Btrfs tab with the selected device's information.
     * @param uuid
     */
    void populateBtrfsUi(const QString &uuid);

    /**
     * @brief Populates the grid on the Snapper New subtab
     */
    void populateSnapperGrid();

    /**
     * @brief Populates the grid on the Snapper Restore subtab
     */
    void populateSnapperRestoreGrid();

    /**
     * @brief Populates a selected config on the Snapper Settings tab
     */
    void populateSnapperConfigSettings();

    /**
     * @brief Populates the Btrfs Subvolumes tab with all devices subvolumes.
     */
    void refreshSubvolListUi();

    /**
     * @brief Refresh the Btrfs tab UI.
     */
    void refreshBtrfsUi();

    /**
     * @brief Update the UI on Snapper Settings based on systems enabled units.
     */
    void refreshSnapperServices();

    /**
     * @brief Restore a selected snapshot to the selected subvolume.
     * @param uuid
     * @param subvolume
     */
    void restoreSnapshot(const QString &uuid, const QString &subvolume);

    /**
     * @brief Initial application setup.
     * @return
     */
    void setup();

    /**
     * @brief Enables or disables the timeline spinboxes to match the timeline checkbox
     * @param enable
     */
    void snapperTimelineEnable(bool enable);

    /**
     * @brief Update system service states depending on checkbox states in UI.
     * @param checkboxList
     */
    void updateServices(QList<QCheckBox *>);

    /**
     * @brief Method used to fetch and update the btrfs balance status
     */
    void btrfsBalanceStatusUpdateUI();

    /**
     * @brief Method used to fetch and update the btrfs balance status
     */
    void btrfsScrubStatusUpdateUI();

    /**
     * @brief Method used to fetch and update the btrfs balance status
     */
    void btrfsDefragStatusUpdateUI();

    /**
     * @brief Changes the text on the Enable Quota button to Disable Quota when appropriate
     */
    void setEnableQuotaButtonStatus();

  private slots:

    /**
     * @brief Toggle balance list selection depending on checkbox state.
     * @param checked
     */
    void on_checkBox_bmBalance_clicked(bool checked);

    /**
     * @brief Toggle defrag list selection depending on checkbox state.
     * @param checked
     */
    void on_checkBox_bmDefrag_clicked(bool checked);

    /**
     * @brief Toggle scrub list selection depending on checkbox state.
     * @param checked
     */
    void on_checkBox_bmScrub_clicked(bool checked);

    /**
     * @brief Toggle timeline snapshot inputs based on enable timeline snapshots checkbox state.
     * @param checked
     */
    void on_checkBox_snapperEnableTimeline_clicked(bool checked);

    /**
     * @brief Btrfs balance button handler
     */
    void on_pushButton_btrfsBalance_clicked();

    /**
     * @brief When a change is detected on the dropdown of btrfs devices, repopulate the UI based on the new selection
     */
    void on_comboBox_btrfsDevice_activated(int index);

    /**
     * @brief Btrfs scrub button handler
     */
    void on_pushButton_btrfsScrub_clicked();
    void on_pushButton_enableQuota_clicked();

    /**
     * @brief Repopulate the grid when a different config is selected
     */
    void on_comboBox_snapperConfigs_activated(int index);

    /**
     * @brief When a new config is selected repopulate the UI
     */
    void on_comboBox_snapperConfigSettings_activated(int index);
    void on_comboBox_snapperSubvols_activated(int index);
    /**
     * @brief Apply btrfs maintenance system settings based on the UI state.
     */
    void on_pushButton_bmApply_clicked();

    /**
     * @brief Delete a subvolume after checking for a variety of errors
     */
    void on_pushButton_subvolDelete_clicked();

    /**
     * @brief Refreshes Btrfs data button handler
     */
    void on_pushButton_btrfsRefreshData_clicked();

    /**
     * @brief Refreshes subvolume data button handler
     */
    void on_pushButton_subvolRefresh_clicked();

    /**
     * @brief Restore snapshot button handler
     */
    void on_pushButton_snapperRestore_clicked();

    /**
     * @brief Snapper browse snapshot button handler
     */
    void on_pushButton_snapperBrowse_clicked();

    /**
     * @brief Snapper new snapshot button handler
     */
    void on_pushButton_snapperCreate_clicked();

    /**
     * @brief Snapper delete snapshot button handler
     */
    void on_pushButton_snapperDelete_clicked();

    /**
     * @brief Snapper new config button handler
     * Use snapper to delete a config when the delete config button is pressed
     */
    void on_pushButton_snapperDeleteConfig_clicked();

    /**
     * @brief Snapper new config button handler
     * Switches the snapper config between edit config and new config mode
     */
    void on_pushButton_snapperNewConfig_clicked();

    /**
     * @brief Snapper save config button handler
     * Uses snapper to create a new config or save an existing config when the save button is pressed
     */
    void on_pushButton_snapperSaveConfig_clicked();

    /**
     * @brief Snapper Settings apply systemd changes button handler
     */
    void on_pushButton_snapperUnitsApply_clicked();

    void on_toolButton_snapperNewRefresh_clicked();
    void on_toolButton_snapperRestoreRefresh_clicked();
};
#endif // MAINWINDOW_H