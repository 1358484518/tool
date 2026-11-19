#include "TMX_TOOL.h"
#include "AppLanguage.h"
#include "network/NetCommon.h"

#include <QApplication>
#include <QSettings>

int main(int argc, char *argv[])  // 启用高 DPI、登记跨线程类型，然后打开主窗口
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("TMX"));
    QCoreApplication::setApplicationName(QStringLiteral("TMX_TOOL"));
    // APP_VERSION 来自 ymodem.pro 的 VERSION。不能用 QStringLiteral，它会把宏名粘成宽字符串。
    QCoreApplication::setApplicationVersion(QLatin1String(APP_VERSION));
    // 设置写在 exe 旁边的 TMX/TMX_TOOL.ini。换目录就用那一目录的文件，不读注册表。
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QCoreApplication::applicationDirPath());
    AppLanguage::applySaved();
    qRegisterMetaType<NetProtocol>("NetProtocol");
    qRegisterMetaType<RemoteHost>("RemoteHost");

    TMX_TOOL w;
    w.show();
    return a.exec();
}
