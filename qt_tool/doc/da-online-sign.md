# DA 在线签名回归

在线签名只替换回归的最后一步。Provisioning、烧 obk、写成 CLOSED 不动。芯片里已经有根公钥哈希和权限。本地仍用现在这条证书链，叶子私钥放在服务器上。

## 文件

脚本工作目录是 `ROT_Provisioning\DA`。H573 在：

`windows-tfm-tools/windows-tfm-tools-h573/windows-tfm-tools/ROT_Provisioning/DA`

H5F4 用同样的相对路径，在它自己的 `ROT_Provisioning\DA` 下。

本地保留：

- `Certificates/cert_leaf_chain.b64`：发给芯片的证书链。顺序是根证书、中间证书、叶子证书。
- 叶子证书里的公钥对应 `Keys/key_3_leaf.pem`。这把私钥不放在跑擦除的电脑上。

`Binary/password.bin` 只给 TrustZone 关闭那条路用，不交给签名服务器。

芯片里已经烧好的 obk 要满足两件事：

- 根公钥哈希等于 `cert_leaf_chain.b64` 里根证书的公钥哈希。
- 权限掩码允许这次申请的全回归 `per=a`。当前这份 obk 的权限是 `0x00004000`。掩码里没有全回归时，签名正确也不会回归。

## 现有命令做不到拆分

现在的回归最后一条是：

```bat
STM32_Programmer_CLI -c port=SWD per=a key=Keys/key_3_leaf.pem cert=Certificates/cert_leaf_chain.b64 pwd=Binary/password.bin debugauth=1
```

CubeProgrammer 2.22 的命令行和编程接口都只接受本地私钥文件。收随机数和签名在这一次调用里完成，没有导出待签名数据、再导入签名的参数。要按下面的流程做，就要用自己的程序换掉这一条命令。前面的关调试、写选项字节可以仍交给原来的脚本。

发现命令 `debugauth=2` 不需要私钥，可以继续在本地跑。

## 流程

从第 2 步到第 6 步必须是同一次 SWD。中间不复位、不拔线、不结束进程。芯片不会把这个随机数留到下一次连接。

1. 本地用 ST-LINK 连上芯片。复位线接在单片机 NRST 上。界面在执行擦除前提醒接好这两根线。
2. 本地把 `cert_leaf_chain.b64` 整段和权限 `per=a` 通过 SWD 发给芯片。
3. 芯片检查证书链。它算出根公钥哈希，和 obk 里的哈希比较，再看 `per=a` 是否落在 obk 的权限掩码里。然后生成这次认证的随机数，连同要被签名的整段数据发回本地。
4. 本地把这段待签名数据用 HTTPS 发给服务器。送出的是芯片要求签名的完整内容，包含随机数和这次的权限，不是只送一个随机数。证书链和私钥都不在这一步里传。
5. 服务器只用 `key_3_leaf.pem` 做 ECDSA P-256 签名，把签名结果返回。私钥不出服务器。签名必须在第 3 步那次 SWD 还连着的时候回来。
6. 本地在同一次 SWD 里把签名交回芯片。芯片用链里的叶子公钥验签，再对照 obk 里的根哈希和权限。
7. 通过后芯片执行全回归，产品状态回到 OPEN。回归时连接会掉，这是正常结束。本地把成功或失败写进擦除日志。
