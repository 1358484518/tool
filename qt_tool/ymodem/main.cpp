#include "TMX_TOOL.h"
#include "AppLanguage.h"
#include "network/NetCommon.h"

#include <QApplication>

int main(int argc, char *argv[])  // 启用高 DPI、登记跨线程类型，然后打开主窗口
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QCoreApplication::setOrganizationName(QStringLiteral("TMX"));
    QCoreApplication::setApplicationName(QStringLiteral("TMX_TOOL"));
    // APP_VERSION 来自 ymodem.pro 的 VERSION。不能用 QStringLiteral，它会把宏名粘成宽字符串。
    QCoreApplication::setApplicationVersion(QLatin1String(APP_VERSION));

    QApplication a(argc, argv);
    AppLanguage::applySaved();
    qRegisterMetaType<NetProtocol>("NetProtocol");
    qRegisterMetaType<RemoteHost>("RemoteHost");

    TMX_TOOL w;
    w.show();
    return a.exec();
}
