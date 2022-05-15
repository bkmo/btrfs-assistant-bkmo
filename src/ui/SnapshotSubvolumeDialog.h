#ifndef SNAPSHOTSUBVOLUMEDIALOG_H
#define SNAPSHOTSUBVOLUMEDIALOG_H

#include <QDialog>

namespace Ui {
class SnapshotSubvolumeDialog;
}

class SnapshotSubvolumeDialog : public QDialog
{
    Q_OBJECT

public:
    SnapshotSubvolumeDialog(const QString &title, const QString &label, QWidget *parent = nullptr);
    ~SnapshotSubvolumeDialog();

private:
    Ui::SnapshotSubvolumeDialog *s_ui = nullptr;
    QString s_backupName = QString();
    bool s_isClicked = false, s_isConfirmed = false;

public slots:
    bool isComfirmed();
    QString getBackupInputText();
    void showDialog();
    void on_pushButton_yes_clicked();
    void on_pushButton_no_clicked();
};

#endif // SNAPSHOTSUBVOLUMEDIALOG_H
