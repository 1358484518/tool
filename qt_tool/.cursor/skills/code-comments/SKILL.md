---
name: code-comments
description: >-
  Require Chinese comments on every class, function, and class member when
  adding or editing code, plus comments on important logic inside functions.
  Use when writing, modifying, or refactoring C++, Qt, headers, or any source
  in this project, or when the user mentions 注释, comments, 类, 函数, or 成员变量.
---

# 代码注释

后面修改代码时，每个类、每个函数、每个类变量都要有注释。重要的函数代码也要有注释。

注释用中文，和现有头文件一样：说明用途或约束，不重复名字。

## 必须写

- **类、结构体、枚举**：声明上方用 `/** ... */`，写它是什么、和谁协作、有什么约束。
- **函数**：含构造、析构、信号、槽、静态函数。声明行尾用 `//`，一句话说它做什么。定义和声明不重复写两遍；只改 `.cpp` 时，把注释写在定义上。
- **类变量**：每个成员都要有行尾 `//`，包括控件指针和看起来直白的标志。写它保存什么，或什么时候为真。

改到一条还没有注释的声明，就补上。行为变了，同步改原来的注释。没改到的旧声明保持不动。

## 函数体内

只给重要代码写注释，说明为什么，不逐行翻译：

- 线程归属、对象生命周期、不能 `moveToThread` 或不能在错误线程里 `delete`
- 协议时序、超时、重试、为什么换新套接字或先 `abort`
- 不直观的分支、边界和失败后的去向

一眼能看懂的赋值、布局和信号连接不写。

## 示例

```cpp
/**
 * 网络页 UI。套接字都在 m_netWorker 所在线程。
 * 打开/连接由本页管理；收 onIoData，发走 IoSource::sendIoData。
 */
class NetAssistWidget : public QWidget
{
public:
    explicit NetAssistWidget(QWidget *parent = nullptr);  // 建 UI、工作线程、NetworkWorker
    void onOpenToggled(bool checked);                     // 打开或关闭本地口 / 监听

private:
    NetworkWorker *m_netWorker = nullptr;  // 无 parent，一直活在 workerThread
    bool m_manualTcpDisconnect = false;    // 用户点断开，不要立刻当成失败重连
};

void NetAssistWidget::onOpenToggled(bool checked)
{
    if (!checked) {
        m_manualTcpDisconnect = true;
        // 后端一直待在工作线程：就地关掉套接字，不要 move 回 UI。
        emit sigCloseNetwork();
    }
}
```
