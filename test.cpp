/*
 * Copyright (C) 2019 ~ 2022 Uniontech Software Technology Co.,Ltd.
 *
 * Author:     libao <libao@uniontech.com>
 *
 * Maintainer: libao <libao@uniontech.com>
 */
#include "policy_common.h"
#include "consts.h"
#include "filecontent.h"
#include "desktop_shortcut.h"
#include "scms_interface.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QDebug>
#include <QUrl>
#include <QList>
#include <QPair>
using namespace std;
using namespace udcp;
using namespace udcp::base;
const char *DesktopShortcut_Key = "desktop_shortcuts_list";
DesktopShortCutPlugin::DesktopShortCutPlugin(QObject *parent)
    : QObject(parent)
{
}
int DesktopShortCutPlugin::pluginType()
{
    return PolicyPlugin;
}
QByteArray DesktopShortCutPlugin::pluginIds()
{
    QJsonArray arr = QJsonArray() << DesktopShortcut_Key;
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}
void DesktopShortCutPlugin::init()
{
}
void DesktopShortCutPlugin::runPolicy(QByteArray &result, const QByteArray &config)
{
    const QMap<QString, QString> &configMap = PolicyCommon::policyJsonToMap(config);
    if (!configMap.contains(DesktopShortcut_Key)) {
        reportFeedback(result, PolicySuccess);
        return;
    }
    QString host, port;
    FileContent::getHostAndPort(host, port);
    const QJsonArray &array = QJsonDocument::fromJson(configMap.value(DesktopShortcut_Key).toUtf8()).array();
    QVector<QPair<QString, QString>> extraInfo;
    extraInfo.reserve(array.size());
    QStringList urls;
    QList<QString> ls;
    for (const auto &ele : array) {
        const QJsonObject &object = ele.toObject();
        // 快捷方式名称
        const QString &name = object.value("name").toString();
        // 快捷方式链接
        const QString &url = object.value("url").toString();
        // 图标下载路径
        const QString &downloadUrl = kUrlHead.arg(host).arg(port) + object.value("iconPath").toString();
        extraInfo << QPair<QString, QString>(name, url);
        urls << downloadUrl;
    }
    ScmsInterface inter(ScmsServiceName, ScmsServicePath, QDBusConnection::systemBus());
    inter.setTimeout(3600 * 1000);
    QDBusPendingReply<QStringList> reply = inter.Download(urls, QDir::homePath() + "/.cache/udcp/desktop-shortcut");
    QStringList sm = reply.value();
    if (reply.isError() || sm.isEmpty()) {
        reportFeedback(result, PolicyFailed);
        return;
    }
    auto ret = generateDesktopFiles(sm, extraInfo);
    reportFeedback(result, ret ? PolicySuccess : PolicyFailed);
}
int DesktopShortCutPlugin::domainBackup()
{
    return 0;
}
int DesktopShortCutPlugin::domainRestore()
{
    return 0;
}
void DesktopShortCutPlugin::reportFeedback(QByteArray &result, PolicyErrorCode code) const
{
    QJsonObject feedback;
    feedback.insert(DesktopShortcut_Key, code);
    result = QJsonDocument(feedback).toJson(QJsonDocument::Compact);
}
static inline QPair<bool, QString> exec(const QString &program, const QStringList &arguments)
{
    QProcess process;
    process.start(program, arguments);
    if (process.waitForFinished()) {
        return {true, process.readAllStandardOutput()};
    }
    qWarning() << "failed to execute command:" << program << "with arguments:" << arguments << ", reason:" << process.errorString();
    return {false, {}};
}
static inline QString createDesktopFilename(const QString &name, const QString &url)
{
    auto hash = QCryptographicHash::hash(QString(name + url).toUtf8(), QCryptographicHash::Md5);
    return QString(hash.toHex()) + ".desktop";
}
bool DesktopShortCutPlugin::generateDesktopFile(const QString &iconFile, const QString &name, const QString &urlStr) const
{
    if (name.isEmpty()) {
        qWarning() << "desktop shortcut name is empty";
        return false;
    }
    QUrl url(urlStr);
    if (!url.isValid()) {
        qWarning() << "invalid desktop shortcut url:" << urlStr;
        return false;
    }
    // url 指向非 HTTP HTTPS 时不处理
    // 因为文管不支持 Type=Link，使用 xdg-open 打开，所以必须有这个校验
    if (url.scheme() != "http" and url.scheme() != "https") {
        qWarning() << "invalid url scheme:" << urlStr;
        return false;
    }
    // 图标文件类型判断
    QMimeDatabase db;
    if (!db.mimeTypeForFile(iconFile).name().startsWith("image/")) {
        qWarning() << "incorrect icon file format:" << iconFile;
        return false;
    }
    auto resp = exec("xdg-user-dir", {"DESKTOP"});
    if (!resp.first) {
        qWarning() << "failed to get user desktop dir";
        return false;
    }
    auto desktopDir = resp.second.trimmed();
    // 于bug170289，文件唯一性由名称+链接确认
    auto desktopFileName = createDesktopFilename(name, urlStr);
    auto desktopFilePath = FileContent::join(desktopDir, desktopFileName);
    QString content;
    content += "[Desktop Entry]\n";
    content += QString("Name=%1\n").arg(name);
    content += QString("Icon=%1\n").arg(iconFile);
    content += QString("Exec=xdg-open %1\n").arg(urlStr);
    content += "Categories=Network\n";
    content += "StartupNotify=false\n";
    content += "Terminal=false\n";
    content += "Type=Application\n";
    content += "X-Deepin-CreatedBy=udcp\n";
    auto ok = FileContent::writeTo(desktopFilePath, content.toUtf8());
    if (!ok) {
        qWarning() << "failed to create the desktop file:" << desktopFilePath;
        return false;
    }
    return true;
}
bool DesktopShortCutPlugin::generateDesktopFiles(const QStringList &ls, const QVector<QPair<QString, QString>> &extra)
{
    Q_ASSERT(ls.size() == extra.size());
    for (auto i = 0; i < ls.size(); ++i) {
        const auto &iconFile = ls[i];
        const auto &name = extra[i].first;
        const auto &url = extra[i].second;
        if (!generateDesktopFile(iconFile, name, url))
            return false;
    }
    return true;
}