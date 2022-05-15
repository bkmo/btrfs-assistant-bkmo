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
    QString backupName();

private:
    Ui::SnapshotSubvolumeDialog *m_ui = nullptr;
    QString m_backupName = QString();

private slots:
    void on_pushButton_yes_clicked();
    void on_pushButton_no_clicked();

};

#endif // SNAPSHOTSUBVOLUMEDIALOG_H
