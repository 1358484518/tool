## 工具说明

| 目录 | 说明 |
|------|------|
| `py_tool/` | Python 编写的工具 |
| `qt_tool/` | Qt 编写的工具 |


这是一个工具集合仓库。

## DA 认证

DA 是芯片封板之后的调试认证。发布程序烧进去之后，再做一次 DA 认证，产品状态变成 CLOSED，普通 SWD 调试口就关了。以后要重新烧程序，必须先拿私钥和证书链做回归，把芯片打开回到 Open。

测试程序、调试程序只烧录，不跑 DA。发布程序的烧录脚本把 TrustZone 打开，启动地址写成 `0xC0100`，产品状态留在 Open。烧完发布程序后，再在下载页点「DA 认证」。

### 文件放哪

脚本在 `qt_tool/da/`。Qt 程序不让人填脚本路径。exe 旁边有 `ACSF` 时，就用这一层里的 `da`。旁边没有 `ACSF` 时，才用编译时记下的源码目录。

打包给别的电脑时，按下面摆。`da` 必须和 `ACSF` 同级。

```text
ymodem.exe
ACSF\                         各产品的烧录脚本和镜像
da\
  da_close.bat                认证并进入 CLOSED
  da_regression.bat           回归到 Open
  password.bin                密码认证时用的口令文件
  DA_ConfigWithPassword.obk   停在 PROVISIONING 时的密码 OBK
windows-tfm-tools\
  windows-tfm-tools-h573\windows-tfm-tools\gen_da_obk.py
```

`da\log` 是每次运行自己写的日志，不用打包。

| 文件 | 作用 |
|------|------|
| `da_close.bat` | 用公钥生成 OBK，写入芯片，产品状态设为 CLOSED |
| `da_regression.bat` | 用叶子或中间私钥和证书链回归到 Open |
| `password.bin` | 芯片已经是密码认证时，回归带上的口令 |
| `DA_ConfigWithPassword.obk` | 芯片停在 PROVISIONING、证书回归失败时，改走密码认证用的 OBK |

本机要有 STM32CubeProgrammer，还要有 `py -3` 或 `python`。生成 OBK 会调用 `gen_da_obk.py`。脚本按这个顺序找编程器：`D:\ST\STM32CubeProgrammer\bin`、Program Files、Program Files (x86)、最后才是 PATH。下载页上填了编程器路径时，只是把它的目录加到 PATH 前面，前面三个目录里已经有编程器时，仍用那三个。

### 下载页怎么用

G0、RAT、RD0047、SECC 各是一个抽屉，可以同时展开。每个抽屉三行：测试、调试、发布。每行选 Boot（只能是 `.hex`）、安全程序、应用程序（`.bin` 或打包的 `.fwmp`），再点下载。

RAT、RD0047 和 G0 用各自目录里的 `flash_stm32h573.bat`、`flash_stm32h573_release.bat`。SECC 用 `flash_stm32h5f4.bat`、`flash_stm32h5f4_release.bat`。

DA 栏固定在抽屉下面，一直显示，不跟着抽屉收起。

- 「DA 认证」跑 `da_close.bat`，只用公钥。
- 「回归」跑 `da_regression.bat`，用私钥和证书链。
- 叶子、中间二选一。切换时，两个路径框会填上 STM32H573I-DK 例程里的默认钥匙和证书，之后可以改。

页面上的公钥、私钥、证书链会记在本机。换一台电脑，这些路径要重新选，不会跟着 exe 走。

### 做 DA 认证之前

芯片必须已经打开 TrustZone。发现结果里，`cryptosystems` 这一行是 `Ecdsa-P256 SHA256`，才是证书认证。如果这一行是 `ST Password`，说明 TrustZone 关着，关闭脚本直接退出，不写选项字节，也不写 OBK。

发布程序烧录会把 TrustZone 设成 `0xB4`。回归成功之后 TrustZone 会关掉，所以下一次认证之前，要再烧一次发布程序。

不要用 Cube 例程里的 `provisioning.bat` 前两步。那两步会整片擦除，并把 `SECBOOTADD` 写成 `0xC0000`。发布烧录脚本已经写好 TrustZone、启动地址和 `BOOT_UBE`。DA 只做后面的封板。

### DA 认证做了什么

命令行：`da_close.bat 公钥.pem`

1. 发现当前产品状态和认证方式。LOCKED、已经 CLOSED、状态读不出来，都失败退出。不是证书认证，也失败退出。
2. 当前是 Open（`0xED`）时，先写成 PROVISIONING（`0x17`）。已经是 PROVISIONING（`0x17`）或 PROVISIONED（`0x2E`）就跳过这一步。
3. 用公钥生成 OBK。权限固定 `0x00005077`，加密标志为 1。OBK 写到临时文件，再烧进芯片。
4. 一条命令同时写 `SECBOOTADD=0xC0100`、`BOOT_UBE=0xB4`、`PRODUCT_STATE=0x72`。进入 PROVISIONING 会把启动地址清成 0，所以启动地址必须和 CLOSED 写在一起。
5. 变成 CLOSED 后 SWD 会掉线，编程器常打印 `failed to reconnect after reset`。这个报错不代表失败。脚本再做一次发现，看到 `ST_LIFECYCLE_CLOSED` 才算成功。

不写 LOCKED（`0x5C`）。LOCKED 回不来。

成功之后，发现结果里完整性是 `0xeaeaeaea`，表示 OBK 已经写入。权限是全回归、退回 TrustZone，以及各级调试。

### 回归做了什么

命令行：`da_regression.bat 私钥.pem 证书链.b64`

1. 先关掉可能还开着的调试口，再发现。已经是 Open 就不用回归。LOCKED 和状态不明都失败退出。
2. 认证方式是证书时，用私钥和证书链做全回归，权限请求 `per=a`。
3. 认证方式是密码时，不再走证书命令。证书命令在密码芯片上会停住等口令文件，界面会一直转。这时改用 `password.bin`。
4. 芯片停在 PROVISIONING，并且证书回归失败时，先把 TrustZone 写成密码模式，写入 `DA_ConfigWithPassword.obk`，再带私钥、证书链和 `password.bin` 回归。
5. 成功的标志是发现结果变成 `ST_LIFECYCLE_OPEN`。用户区 Flash 会被擦掉。启动地址不会恢复，程序也不会自动烧回去。完整性变成 `0xf5f5f5f5`，表示 OBK 没了。`cryptosystems` 变回 `ST Password`。

叶子私钥配 `cert_leaf_chain.b64`，中间私钥配 `cert_intermediate_chain.b64`。私钥必须和证书链最后一张对得上。

回归之后要重新烧程序。要再封板时，先烧发布程序，等 TrustZone 重新打开，再点 DA 认证。

### 私钥上的口令

口令就是这把私钥文件的密码。生成或导出私钥时，工具可以问一句密码，把私钥打乱后再写入 pem。文件开头会变成：

```text
-----BEGIN ENCRYPTED PRIVATE KEY-----
```

没有这句密码，文件里看不到真正的私钥。密码通常单独告诉持有人，不和 pem 放在同一个包里。

回归脚本只把文件路径交给编程器，没有填密码的地方。编程器解不开这种文件，日志里是：

```text
import_private_key : error : sdm : Error loading key file
```

芯片状态不会因此改变。要回归，先在本机把密码解开，得到开头是 `BEGIN PRIVATE KEY` 或 `BEGIN EC PRIVATE KEY` 的明文 pem。

在命令行执行下面这句。执行后窗口会停住，出现 `Enter pass phrase:`。这时输入口令再按回车。输入时屏幕上不显示字符。口令不要写进命令里。

```bat
openssl pkey -in 加密的私钥.pem -out leaf.key.pem
```

回归时选 `leaf.key.pem`。这个文件没有密码，拿到的人就能做回归，不要放进 git，也不要发给别人。原来的加密 pem 可以留着。

### 编程器会删掉私钥文件

STM32CubeProgrammer 读完 `key=` 指向的那个 pem 后，会把那个文件删掉。读失败时也会删。

所以回归脚本先把私钥复制到 `%TEMP%\da_regression_key.pem`，只把这份临时文件交给编程器。临时文件会被删掉，你选中的原文件还在。证书链 `.b64` 不会被删。

日志里如果看到临时路径，是正常的：

```text
Key file path submitted : C:\Users\...\AppData\Local\Temp\da_regression_key.pem
```

### 日志和界面提示

每次运行在 `da\log\` 写一个新日志，关闭脚本是 `da_close_时间.log`，回归是 `da_regression_时间.log`。下载页只跟随这次新出现的日志，最多保留 2000 行。进度条里的乱码是编程器进度字符按 GBK 显示错了，不表示失败。

界面底部会写这次是成功还是失败，以及用了多少时间。日志里出现 `[FAIL]` 或 `Failed:` 就记为失败。CLOSED 之后那次重连失败不算失败。
