#include "TMX_TOOL.h"
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

    QApplication a(argc, argv);
    qRegisterMetaType<NetProtocol>("NetProtocol");
    qRegisterMetaType<RemoteHost>("RemoteHost");

    TMX_TOOL w;
    w.show();
    return a.exec();
}
