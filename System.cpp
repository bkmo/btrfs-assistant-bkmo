#include "System.h"

#include <QProcess>

System::System(QObject *parent) : QObject{parent} {}

bool System::enableService(QString serviceName, bool enable) {
    int exitCode;

    if (enable) {
        exitCode = System::runCmd("systemctl", {"enable", "--now", serviceName}, false).exitCode;
    } else {
        exitCode = System::runCmd("systemctl", {"disable", "--now", serviceName}, false).exitCode;
    }

    return exitCode == 0;
}

QStringList System::findEnabledUnits() {

    const QString bashOutput = System::runCmd("systemctl list-unit-files --state=enabled -q --no-pager", false).output;
    const QStringList outputList = bashOutput.split('\n');
    QStringList serviceList;
    for (const QString &line : outputList) {
        serviceList.append(line.split(QRegExp("[\t ]+")).at(0));
    }

    return serviceList;
}

const Result System::runCmd(const QString &cmd, bool includeStderr, int timeout) {
    return runCmd("/bin/bash", QStringList() << "-c" << cmd, includeStderr, timeout);
}

const Result System::runCmd(const QString &cmd, const QStringList &args, bool includeStderr, int timeout) {
    QProcess proc;

    if (includeStderr)
        proc.setProcessChannelMode(QProcess::MergedChannels);

    proc.start(cmd, args);

    proc.waitForFinished(1000 * 60);
    return {proc.exitCode(), proc.readAllStandardOutput().trimmed()};
}

const QString System::toHumanReadable(double number) {
    int i = 0;
    const QVector<QString> units = {"B", "kiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"};
    while (number > 1024) {
        number /= 1024;
        i++;
    }
    return QString::number(number, 'f', 2) + " " + units[i];
}
