# STM32H5F4 独立签名工具包（EC-P256 / 内网签名服务）

把未签名的 Secure / Non-Secure `.bin` 放进本目录，执行脚本即可得到 MCUboot 签名镜像。

**签名私钥托管在公司内网签名服务上，本工具包内不含任何私钥，只持公钥。**
本地完成镜像头 / TLV / 对齐 / 填充组装，只把 32 字节 SHA-256 摘要发往服务端换取
DER 签名（镜像正文不出本地）。

更完整的中文说明见同目录 **`使用说明.txt`**（含配置档、密钥增删、故障排查）。

## 快速用法

```bash
cd sign_kit

# 首次准备：系统只需提供 python3 -m venv（本工具包用独立 .venv，不用系统 pip）
sudo apt install -y python3-venv        # Debian/Ubuntu；Python 需 ≥3.10（用 python3 -VV 确认）

export TFM_SIGN_SERVER_URL='https://<内网主机>:8443'
export TFM_SIGN_TOKEN='<向密钥管理员申请的令牌>'

./sign.sh --profile DEV-CCU tfm_ns.bin          # NS；首次运行会自动创建 .venv 并装依赖
./sign.sh --profile DEV-CCU sapp.bin            # S
```

> **不要**执行 `python3 -m pip install -r requirements.txt`：Ubuntu 24.04 起（含 26.04）默认不预装
> pip，且系统 Python 受 PEP 668 保护，会直接报 `No module named pip` 或被拒绝安装到系统环境。
> 依赖一律装在本目录的 `.venv` 里（脚本自动创建，与仓库根 `.venv` 相互独立）。
> 想先手动装好也可以：
>
> ```bash
> python3 -m venv .venv
> .venv/bin/python -m pip install -r requirements.txt
> .venv/bin/python -c "import click, cryptography, cbor2, intelhex"   # 自检
> ```
>
> 若本机同时用于 TF-M 编译（`buildtfm.sh`），另需 cmake ≥3.21、ninja、`arm-none-eabi-gcc` ≥12.2。

Windows：

```bat
cd sign_kit
set TFM_SIGN_SERVER_URL=https://<内网主机>:8443
set TFM_SIGN_TOKEN=<令牌>
sign.bat --profile DEV-CCU tfm_ns.bin           :: 首次运行会自动创建 .venv 并装依赖
```

也可把 `.bin` 拖到 `sign.bat` 上。


## 签名模式：本地开发签名（默认） / 正式远程签名

| 场景 | 命令 | 密钥 | 令牌/网络 |
|---|---|---|---|
| 日常开发（默认） | `sign.bat <bin>` / `./sign.sh <bin>` | 仓库内 `keys/dev/<key_id>.pem`（DEV 开发密钥） | 不需要 |
| 临时远程联调 | `sign.bat --remote <bin>` | 服务端 DEV 密钥 | 需要 |
| 正式出包 | `sign_prod.bat <bin>` / `./sign_prod.sh <bin>` | 服务端正式密钥（`PROD_PROFILE`） | 需要 |

- 本地模式：`--dev` 显式指定（`--local` 等价保留）；key_id 解析同一套配置档映射
  （`--key-id` > `--profile` > `DEFAULT_PROFILE`），私钥取 `keys/dev/<key_id>.pem`，
  兼容旧的 `keys/image_{s,ns}_signing_private_key.pem`。
- **服务器没开/连不上也不影响日常开发**：本地模式不发起任何网络请求、不读令牌。
- **正式签名只能手工执行**：`sign_prod` 强制远程、默认用 `remote.conf` 的 `PROD_PROFILE`，
  并**拒绝 `--local`**；缺令牌或服务不可达时直接失败，**不会**退回开发密钥。
- 安全边界：`keys/dev/` 只放 **DEV-*** 开发密钥（团队约定随仓库提交）；正式私钥只在内网
  签名服务上，永不落地。开发密钥泄露时轮换 DEV 密钥并重刷开发板 BL2/OTP。
## 选项

| 选项 | 说明 |
|---|---|
| `--profile <名称>` / `-p` | 使用 `remote.conf` 中该配置档的 SPE/NS key_id（缺省用 `DEFAULT_PROFILE`） |
| `--key-id <ID>` | 本次签名直接指定 key_id（覆盖配置档映射） |
| `--remote` / `--local` | 强制远程签名 / 本地私钥签名（后者仅灰度回退用） |
| `--conf <路径>` | 使用指定的 remote.conf |

产物默认写到**输入 `.bin` 所在目录**（`<名称>_signed.bin`）；若在 `remote.conf` 中设置 `OUT_DIR=kit`，
则写回本 `sign_kit` 目录 —— CubeIDE 工程（`tfmcubeideproject/STM32CubeIDE/sign_kit`）沿用后者，
其 `remote.conf` 已写 `OUT_DIR=kit`，产物仍落 `sign_kit\tfm_ns_signed.bin`（`tfm_update.bat` 取件路径不变）。

> 本目录**不含任何私钥**（已从工作区与版本控制移除）。需要 `--local` 应急出包时，请从离线归档取回
> `keys/image_{s,ns}_signing_private_key.pem`：`C:\work\PLC\CCU-SECC-1\priv-backup-20260925\`。

## 配置档（profile）

`remote.conf`（**随仓库提交**，团队共享；令牌绝不写进去）：

```ini
SIGN_MODE=remote
SIGN_SERVER_URL=            # 留空时回退环境变量 TFM_SIGN_SERVER_URL
CA_BUNDLE=                  # 公司内部 CA / 自签证书路径
DEFAULT_PROFILE=DEV-CCU

PROFILE_DEV-CCU_SPE=DEV-CCU-SPE
PROFILE_DEV-CCU_NS=DEV-CCU-NS

# 非规范命名同样支持（同一把密钥可用于两侧）
PROFILE_OCPP-FW-CCU_SPE=OCPP-FW-CCU
PROFILE_OCPP-FW-CCU_NS=OCPP-FW-CCU
```

规则：必须同时给出 SPE 与 NS 两条映射；**不会**按 `<名称>-SPE` 自动推断 key_id
（刻意的设计，防止签错密钥）；缺映射直接报错。

## 公钥锚点

`keys/<key_id>.pem`：每个 key_id 一个**公钥**文件，与服务端一一对应。
签名前客户端会用它的指纹与服务端返回的 `pubkey_sha256` 强校验，不一致立即中止。

新增/轮换密钥后导出公钥：

```bash
python3 scripts/fetch_pubkeys.py --key-id <KEY_ID>       # 或 --profile DEV-CCU
# 人工 review 后提交；内容有变化时会要求 --force，避免公钥静默变更
```

当前已提供（DEV-CCU 开发密钥，指纹由 `openssl` 独立核对）：

| key_id | 公钥文件 | pubkey_sha256 |
|---|---|---|
| `DEV-CCU-SPE` | `keys/DEV-CCU-SPE.pem` | `b5d8b37f006efde3d6da35d7efdf7e60bdfd0cb967b6358f25fdbc1025f5b66d` |
| `DEV-CCU-NS` | `keys/DEV-CCU-NS.pem` | `a12b9daf88ce343a15e65c2aceac0e5939e2b55598deb5592cd612168da736e1` |

## 烧录地址（本分支布局）

| 镜像 | 主槽地址 | 签完槽大小 |
|------|----------|------------|
| `*_s_signed.bin` | `0x0C078000` | 512 KB |
| `*_ns_signed.bin` | `0x0C0F8000` | 1024 KB |

升级副槽：S `0x0C200000`，NS `0x0C280000`。不要用 H573 地址。

## 验证

```bash
# 必须用本目录的 venv，且用 -m 形式（直接跑 scripts/imgtool/main.py 会 ModuleNotFoundError: imgtool）
PYTHONPATH=scripts .venv/bin/python -m imgtool.main verify -k keys/DEV-CCU-SPE.pem tfm_s_signed.bin
# 预期：Image was correctly validated
```

ECDSA 每次签名结果都不同，**不要用字节比对验收**；请用公钥验签 + 板上实测启动。
Linux 侧完整验证手册见仓库 `docs/remote-sign-verification.md`。

## Windows 批处理（`sign.bat`）必须保持纯 ASCII

`sign.bat` 的注释与提示文本已全部改为 ASCII（英文）。原因：cmd 用**当前代码页**解码批处理文件，
注释/回显行里的多字节（中文）字符会被从中间截断，行尾被当成命令执行 —— postbuild 日志里出现
`'...' 不是内部或外部命令`，回显也会乱码（`配置档` → `配置�?`）。这不是编码转换问题，
`chcp 65001` 也修不好，**唯一可靠办法是让批处理文件只含 ASCII**。

- 改 `sign.bat` 时不要引入任何非 ASCII 字符（含全角括号 `（）`、全角冒号 `：`、中文标点）
- 检查：`grep -nP '[^\x00-\x7F]' sign_kit/sign.bat`（应无输出）；仓库根执行 `bash scripts/check_bat_ascii.sh`
- 中文说明保留在 `README.md`、`使用说明.txt`、`sign.sh`（bash 解析 UTF-8 无此问题）
- `sign.bat` 必须保持 CRLF 行尾（`.gitattributes` 已强制）

## 相关文档

- 服务端部署与接口：`../sign_server/README.md`、`../sign_server/docs/API.md`
- 中文使用说明（含密钥管理与排障）：`使用说明.txt`
