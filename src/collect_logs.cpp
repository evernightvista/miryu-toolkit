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
#include <QLocale>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <KLocalizedString>

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
        logMsg(i18n("Command timed out: %1", proc.program()));
        return false;
    }
    if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
        logMsg(i18n("Completed: %1 -> %2", proc.program(), QFileInfo(outfile).fileName()));
        return true;
    }
    logMsg(i18n("Failed: %1 (exit status %2)", proc.program(), proc.exitCode()));
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
    KLocalizedString::setApplicationDomain("miryu-toolkit");
    const QStringList arguments = app.arguments();
    const int languageIndex = arguments.indexOf(QStringLiteral("--lang"));
    if (languageIndex > 0 && languageIndex + 1 < arguments.size()) {
        const QLocale locale(arguments.at(languageIndex + 1));
        if (locale.language() != QLocale::C)
            KLocalizedString::setLanguages({locale.name()});
    }

    QString targetUser;
    if (arguments.size() > 1 && arguments.at(1) != QLatin1String("--lang"))
        targetUser = arguments.at(1);

    QTemporaryDir tempDir(QStringLiteral("/tmp/collect-logs-XXXXXX"));
    if (!tempDir.isValid()) {
        logMsg(i18n("Failed to create a temporary directory."));
        return 1;
    }
    QString tmp = tempDir.path();

    QString desktop = getDesktop(targetUser);
    QDir().mkpath(desktop);

    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString outName = QStringLiteral("miryu-logs-%1.tar.zstd").arg(timestamp);

    logMsg(i18n("Creating temporary workspace: %1", tmp));
    logMsg(i18n("Target user: %1", targetUser.isEmpty() ? i18n("none") : targetUser));

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
            logMsg(i18n("Copied %1", QStringLiteral("/var/log/dnf5.log")));
        } else {
            logMsg(i18n("Failed to copy %1", QStringLiteral("/var/log/dnf5.log")));
        }
    }
    if (QFile::exists(QStringLiteral("/var/log/dnf.log"))) {
        if (copyFile(QStringLiteral("/var/log/dnf.log"), tmp + QStringLiteral("/dnf.log"))) {
            logMsg(i18n("Copied %1", QStringLiteral("/var/log/dnf.log")));
        }
    }

    // --- Anaconda installer logs (only in LiveCD environment) ---
    if (isLiveCD()) {
        logMsg(i18n("Live CD environment detected; collecting Anaconda logs…"));
        if (QDir(QStringLiteral("/var/log/anaconda")).exists()) {
            if (copyDir(QStringLiteral("/var/log/anaconda"), tmp + QStringLiteral("/anaconda"))) {
                logMsg(i18n("Copied %1", QStringLiteral("/var/log/anaconda")));
            } else {
                logMsg(i18n("Failed to copy %1", QStringLiteral("/var/log/anaconda")));
            }
        }
        // Also collect /root/anaconda-ks.cfg if present
        if (QFile::exists(QStringLiteral("/root/anaconda-ks.cfg"))) {
            if (copyFile(QStringLiteral("/root/anaconda-ks.cfg"), tmp + QStringLiteral("/anaconda-ks.cfg"))) {
                logMsg(i18n("Copied %1", QStringLiteral("/root/anaconda-ks.cfg")));
            }
        }
    } else {
        logMsg(i18n("Not a Live CD environment; skipping Anaconda logs."));
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
            logMsg(i18n("zstd was not found; created gzip archive: %1", archivePath));
            archived = true;
        } else {
            logMsg(i18n("Could not create the gzip archive."));
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
            logMsg(i18n("Archive created: %1", archivePath));
            archived = true;
        } else {
            logMsg(i18n("Could not create the zstd archive (tar exit status %1, zstd exit status %2).",
                        tar.exitCode(), zstd.exitCode()));
        }
    }

    // --- Chown to target user if specified ---
    if (archived && !targetUser.isEmpty()) {
        QProcess chown;
        chown.start(QStringLiteral("chown"),
                    {targetUser + QStringLiteral(":") + targetUser, archivePath});
        chown.waitForFinished(10000);
        if (chown.exitCode() != 0) {
            logMsg(i18n("Could not change archive ownership: %1", archivePath));
        }
    }

    // Write archive path to stdout so the GUI can read it
    if (archived) {
        printf("%s\n", archivePath.toLocal8Bit().constData());
        fflush(stdout);
    }

    logMsg(i18n("Log collection finished."));
    return archived ? 0 : 1;
}
