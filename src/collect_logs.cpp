/*
 * miryu-toolkit-collect-logs
 *
 * Collects system information and logs into a tar.zstd (or tar.gz) archive
 * placed on the invoking user's Desktop.
 *
 * This is a polkit helper invoked via pkexec. It accepts an optional
 * target username as the first argument so the archive can be chowned
 * to the calling user.
 *
 * Adapted from the original CollectLogs shell script, with:
 *   - pacman logs  -> dnf5 logs
 *   - /etc/os-release -> /usr/lib/os-release
 *   - calamares cache -> anaconda installer logs
 */

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

static void logMsg(const QString &msg)
{
    fprintf(stderr, "[collect-logs] %s\n", msg.toLocal8Bit().constData());
    fflush(stderr);
}

static bool runCommand(const QString &outfile, QStringList args, const QString &workdir = QString())
{
    QProcess proc;
    if (!workdir.isEmpty()) {
        proc.setWorkingDirectory(workdir);
    }
    proc.setStandardOutputFile(outfile);
    proc.setStandardErrorFile(outfile, QIODevice::Append);
    proc.start(args.takeFirst(), args);
    if (!proc.waitForFinished(60000)) {
        proc.kill();
        proc.waitForFinished(5000);
        logMsg(QStringLiteral("TIMEOUT: %1").arg(proc.program()));
        return false;
    }
    if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
        logMsg(QStringLiteral("OK: %1 -> %2").arg(proc.program(), QFileInfo(outfile).fileName()));
        return true;
    }
    logMsg(QStringLiteral("FAILED: %1 (exit %2)").arg(proc.program()).arg(proc.exitCode()));
    return false;
}

static bool tryCmd(const QString &tmpDir, const QString &outName, const QStringList &cmd)
{
    QString outfile = tmpDir + QDir::separator() + outName;
    QStringList args = cmd;
    return runCommand(outfile, args);
}

static bool copyFile(const QString &src, const QString &dst)
{
    QFile f(src);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    QByteArray data = f.readAll();
    f.close();

    QFile out(dst);
    if (!out.open(QIODevice::WriteOnly)) {
        return false;
    }
    out.write(data);
    out.close();
    return true;
}

static bool copyDir(const QString &src, const QString &dst)
{
    QDir srcDir(src);
    if (!srcDir.exists()) {
        return false;
    }
    QDir().mkpath(dst);

    const QFileInfoList entries = srcDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (entry.isDir()) {
            copyDir(entry.absoluteFilePath(), dst + QDir::separator() + entry.fileName());
        } else {
            copyFile(entry.absoluteFilePath(), dst + QDir::separator() + entry.fileName());
        }
    }
    return true;
}

static QString getHomeForUser(const QString &user)
{
    QProcess g;
    g.start(QStringLiteral("getent"), {QStringLiteral("passwd"), user});
    g.waitForFinished(5000);
    QString out = QString::fromLocal8Bit(g.readAllStandardOutput()).trimmed();
    auto parts = out.split(QLatin1Char(':'));
    if (parts.size() >= 6) {
        return parts[5];
    }
    return QStringLiteral("/home/") + user;
}

static QString getDesktop(const QString &targetUser)
{
    if (!targetUser.isEmpty()) {
        return getHomeForUser(targetUser) + QStringLiteral("/Desktop");
    }

    QProcess xdg;
    xdg.start(QStringLiteral("xdg-user-dir"), {QStringLiteral("DESKTOP")});
    xdg.waitForFinished(5000);
    QString d = QString::fromLocal8Bit(xdg.readAllStandardOutput()).trimmed();
    if (!d.isEmpty()) {
        return d;
    }

    QString home = QProcessEnvironment::systemEnvironment().value(QStringLiteral("HOME"));
    return home + QStringLiteral("/Desktop");
}

static bool isLiveCD()
{
    // Check /proc/cmdline for live boot indicators
    QFile f(QStringLiteral("/proc/cmdline"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString cmdline = QString::fromLocal8Bit(f.readAll()).toLower();
        f.close();
        if (cmdline.contains(QStringLiteral("live")) ||
            cmdline.contains(QStringLiteral("rd.live")) ||
            cmdline.contains(QStringLiteral("inst.stage2"))) {
            return true;
        }
    }
    // Check for LiveCD-specific mount point
    if (QDir(QStringLiteral("/run/initramfs/live")).exists()) {
        return true;
    }
    return false;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QString targetUser;
    if (argc > 1) {
        targetUser = QString::fromLocal8Bit(argv[1]);
    }

    QTemporaryDir tempDir(QStringLiteral("/tmp/collect-logs-XXXXXX"));
    if (!tempDir.isValid()) {
        logMsg(QStringLiteral("Failed to create temporary directory."));
        return 1;
    }
    QString tmp = tempDir.path();

    QString desktop = getDesktop(targetUser);
    QDir().mkpath(desktop);

    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString outName = QStringLiteral("miryu-logs-%1.tar.zstd").arg(timestamp);

    logMsg(QStringLiteral("Creating temporary workspace: %1").arg(tmp));
    logMsg(QStringLiteral("TARGET_USER: %1").arg(targetUser.isEmpty() ? QStringLiteral("<none>") : targetUser));

    // --- System information ---
    tryCmd(tmp, QStringLiteral("uname.txt"), {QStringLiteral("uname"), QStringLiteral("-a")});
    tryCmd(tmp, QStringLiteral("os-release.txt"), {QStringLiteral("cat"), QStringLiteral("/usr/lib/os-release")});
    tryCmd(tmp, QStringLiteral("lsb-release.txt"), {QStringLiteral("lsb_release"), QStringLiteral("-a")});

    // --- Hardware information ---
    tryCmd(tmp, QStringLiteral("inxi.txt"), {QStringLiteral("inxi"), QStringLiteral("-F")});
    tryCmd(tmp, QStringLiteral("lshw.txt"), {QStringLiteral("lshw"), QStringLiteral("-short")});
    tryCmd(tmp, QStringLiteral("lsblk.txt"), {QStringLiteral("lsblk"), QStringLiteral("-f")});
    tryCmd(tmp, QStringLiteral("df.txt"), {QStringLiteral("df"), QStringLiteral("-h")});
    tryCmd(tmp, QStringLiteral("free.txt"), {QStringLiteral("free"), QStringLiteral("-h")});

    // --- Kernel and modules ---
    tryCmd(tmp, QStringLiteral("dmesg.txt"), {QStringLiteral("dmesg")});
    tryCmd(tmp, QStringLiteral("lsmod.txt"), {QStringLiteral("lsmod")});
    tryCmd(tmp, QStringLiteral("modules.txt"), {QStringLiteral("cat"), QStringLiteral("/proc/modules")});
    tryCmd(tmp, QStringLiteral("kernel-config.txt"), {QStringLiteral("zcat"), QStringLiteral("/proc/config.gz")});
    tryCmd(tmp, QStringLiteral("mount.txt"), {QStringLiteral("mount")});

    // --- Systemd ---
    tryCmd(tmp, QStringLiteral("systemctl-units.txt"),
           {QStringLiteral("systemctl"), QStringLiteral("list-units"), QStringLiteral("--no-pager")});

    // --- Package information (dnf5 instead of pacman) ---
    tryCmd(tmp, QStringLiteral("dnf5-history.txt"),
           {QStringLiteral("dnf5"), QStringLiteral("history"), QStringLiteral("list"), QStringLiteral("--reverse")});
    tryCmd(tmp, QStringLiteral("dnf5-history-userinstalled.txt"),
           {QStringLiteral("dnf5"), QStringLiteral("repoquery"), QStringLiteral("--userinstalled")});
    tryCmd(tmp, QStringLiteral("rpm-qa.txt"), {QStringLiteral("rpm"), QStringLiteral("-qa"), QStringLiteral("--last")});

    // --- Journal ---
    tryCmd(tmp, QStringLiteral("journalctl.txt"),
           {QStringLiteral("journalctl"), QStringLiteral("-b"), QStringLiteral("--no-pager")});

    // --- Segfault and crash logs ---
    tryCmd(tmp, QStringLiteral("segfault-journal.txt"),
           {QStringLiteral("sh"), QStringLiteral("-c"),
            QStringLiteral("journalctl -b --no-pager 2>/dev/null | grep -i segfault || true")});
    tryCmd(tmp, QStringLiteral("segfault-dmesg.txt"),
           {QStringLiteral("sh"), QStringLiteral("-c"),
            QStringLiteral("dmesg 2>/dev/null | grep -i segfault || true")});
    tryCmd(tmp, QStringLiteral("coredump-list.txt"),
           {QStringLiteral("sh"), QStringLiteral("-c"),
            QStringLiteral("coredumpctl list 2>/dev/null || true")});

    // --- dnf5 log (instead of pacman.log) ---
    // dnf5 writes to /var/log/dnf5.log; older dnf uses /var/log/dnf.log
    if (QFile::exists(QStringLiteral("/var/log/dnf5.log"))) {
        if (copyFile(QStringLiteral("/var/log/dnf5.log"), tmp + QStringLiteral("/dnf5.log"))) {
            logMsg(QStringLiteral("Copied /var/log/dnf5.log"));
        } else {
            logMsg(QStringLiteral("Failed to copy /var/log/dnf5.log"));
        }
    }
    if (QFile::exists(QStringLiteral("/var/log/dnf.log"))) {
        if (copyFile(QStringLiteral("/var/log/dnf.log"), tmp + QStringLiteral("/dnf.log"))) {
            logMsg(QStringLiteral("Copied /var/log/dnf.log"));
        }
    }

    // --- Anaconda installer logs (only in LiveCD environment) ---
    if (isLiveCD()) {
        logMsg(QStringLiteral("LiveCD environment detected, collecting Anaconda logs..."));
        if (QDir(QStringLiteral("/var/log/anaconda")).exists()) {
            if (copyDir(QStringLiteral("/var/log/anaconda"), tmp + QStringLiteral("/anaconda"))) {
                logMsg(QStringLiteral("Copied /var/log/anaconda"));
            } else {
                logMsg(QStringLiteral("Failed to copy /var/log/anaconda"));
            }
        }
        // Also collect /root/anaconda-ks.cfg if present
        if (QFile::exists(QStringLiteral("/root/anaconda-ks.cfg"))) {
            if (copyFile(QStringLiteral("/root/anaconda-ks.cfg"), tmp + QStringLiteral("/anaconda-ks.cfg"))) {
                logMsg(QStringLiteral("Copied /root/anaconda-ks.cfg"));
            }
        }
    } else {
        logMsg(QStringLiteral("Not a LiveCD environment, skipping Anaconda logs."));
    }

    // --- Manifest ---
    {
        QFile manifest(tmp + QStringLiteral("/MANIFEST.txt"));
        if (manifest.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream s(&manifest);
            s << "Collected on: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n";
            s << "Host: " << QProcessEnvironment::systemEnvironment().value(QStringLiteral("HOSTNAME")) << "\n";
            s << "User: " << QString::fromLocal8Bit(getlogin()) << "\n";
            s << "Target user: " << (targetUser.isEmpty() ? QStringLiteral("<none>") : targetUser) << "\n";
            s << "Included files:\n";

            QDir dir(tmp);
            const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*"),
                                                          QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                                                          QDir::Name);
            for (const QFileInfo &fi : files) {
                if (fi.isDir()) {
                    QDir subDir(fi.absoluteFilePath());
                    const QFileInfoList subFiles = subDir.entryInfoList(QStringList() << QStringLiteral("*"),
                                                                        QDir::Files,
                                                                        QDir::Name);
                    for (const QFileInfo &sf : subFiles) {
                        s << fi.fileName() << "/" << sf.fileName() << "\n";
                    }
                } else {
                    s << fi.fileName() << "\n";
                }
            }
            manifest.close();
        }
    }

    // --- Create archive ---
    QString archivePath = desktop + QDir::separator() + outName;
    bool archived = false;

    // Try zstd first
    if (QStandardPaths::findExecutable(QStringLiteral("zstd")).isEmpty()) {
        // zstd not found, fallback to gzip
        outName = QStringLiteral("miryu-logs-%1.tar.gz").arg(timestamp);
        archivePath = desktop + QDir::separator() + outName;

        QProcess tar;
        tar.setWorkingDirectory(tmp);
        tar.start(QStringLiteral("tar"), {QStringLiteral("-czf"), archivePath, QStringLiteral(".")});
        if (tar.waitForFinished(120000) && tar.exitCode() == 0) {
            logMsg(QStringLiteral("zstd not found; created gzip archive: %1").arg(archivePath));
            archived = true;
        } else {
            logMsg(QStringLiteral("gzip archive failed"));
        }
    } else {
        // Pipe tar output through zstd: tar stdout → zstd stdin → file
        QProcess tar, zstd;
        tar.setWorkingDirectory(tmp);

        tar.setStandardOutputProcess(&zstd);
        zstd.setStandardOutputFile(archivePath);

        tar.start(QStringLiteral("tar"), {QStringLiteral("-cf"), QStringLiteral("-"), QStringLiteral(".")});
        zstd.start(QStringLiteral("zstd"), {QStringLiteral("-T0"), QStringLiteral("-q")});

        tar.waitForFinished(120000);
        zstd.waitForFinished(120000);

        if (zstd.exitCode() == 0 && tar.exitCode() == 0) {
            logMsg(QStringLiteral("Archive created: %1").arg(archivePath));
            archived = true;
        } else {
            logMsg(QStringLiteral("zstd archive failed (tar exit %1, zstd exit %2)")
                       .arg(tar.exitCode()).arg(zstd.exitCode()));
        }
    }

    // --- Chown to target user if specified ---
    if (archived && !targetUser.isEmpty()) {
        QProcess chown;
        chown.start(QStringLiteral("chown"),
                    {targetUser + QStringLiteral(":") + targetUser, archivePath});
        chown.waitForFinished(10000);
        if (chown.exitCode() != 0) {
            logMsg(QStringLiteral("chown failed for %1").arg(archivePath));
        }
    }

    // Write archive path to stdout so the GUI can read it
    if (archived) {
        printf("%s\n", archivePath.toLocal8Bit().constData());
        fflush(stdout);
    }

    logMsg(QStringLiteral("Done."));
    return archived ? 0 : 1;
}
