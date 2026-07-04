#include "TMX_TOOL.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    TMX_TOOL w;
    w.show();
    return a.exec();
}
