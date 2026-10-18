#ifndef APPLANGUAGE_H
#define APPLANGUAGE_H

#include <QString>

/**
 * 应用语言。英文是 tr() 的源文，中文由翻译器替换。
 * 在创建主窗口之前调用 applySaved()，设置页切换时再调用 setLanguage()。
 */
class AppLanguage
{
public:
    static void applySaved();                  // 按 QSettings 装上或卸下中文翻译
    static void setLanguage(const QString &code);  // code 为 en 或 zh_CN，并立即作用到已有控件
    static QString language();                 // 当前语言代码

private:
    AppLanguage() = delete;
};

#endif // APPLANGUAGE_H
