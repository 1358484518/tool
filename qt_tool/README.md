# TMX 调试助手（Qt5）

基于 Qt5 的串口 / 网络调试工具，集成 YModem 文件发送。

**思路、流程、类和线程怎么管**：见 [`ymodem/README.md`](ymodem/README.md)。

## 目录

```
ymodem/
├── main.cpp / TMX_TOOL.* / tmx_tool.ui   主窗口
├── serial/                               串口助手、YModem
├── network/                              网络助手（TCP/UDP）
├── common/                               串口与网络共用工具
├── image/  miscfile/                     资源
└── ymodem.pro
```
