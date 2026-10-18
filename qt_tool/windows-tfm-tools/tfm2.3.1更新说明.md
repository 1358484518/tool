# TF-M 2.3.0 → 2.3.1 更新说明

整理日期：2026-10-08  
适用仓库：https://github.com/18902101360/stm32h5f4tfm  
上游：https://github.com/TrustedFirmware-M/trusted-firmware-m  
上游标签：`TF-Mv2.3.1`（相对本仓库原先 vendored 的 TF-M **2.3.0**）

本文记录这次把官方 TF-M **v2.3.1** 合入本仓库两条板级支线时：改了什么、刻意没改什么、对现有功能有没有影响、怎么编译、以及 TF-PSA-Crypto 1.0 / 1.1 / 1.1.1 的差别。

---

## 1. 结论先看（对现有功能）

对你现在已经在用的功能（H5F4 双 bank SWAP 升级、H573 外部 W25Q32 覆盖升级、PS/ITS 大小和槽位、USART6、EC-P256、CubeIDE NS + Mbed TLS PSA 客户端、CSR），**协议和 Flash 布局没有改**。

| 问题 | 答案 |
|------|------|
| NS 要不要改 / 重编？ | **不用。** NS 仍通过 SG + `s_veneers.o` 调 SPE。这次 veneer 哈希与 2.3.0 SPE 相同。 |
| 只换签名后的 S bin 行不行？ | 可以。Windows 工具只替换对应板的 **已签名 S**（以及若你烧 BL2 则连 BL2 一起换）。NS CubeIDE 工程不用动。 |
| BL2 升级协议变了吗？ | **没有。** 2.3.1 的 BL2 主要是编译/依赖 plumbing（ECDSA `bootutil_key_cnt`、去掉 `MCUBOOT_USE_MBED_TLS`）。H573 仍从外部 SPI NOR 升级；H5F4 仍内部 4MB 双 bank SWAP。 |
| Flash 谁管？ | 仍是各平台 `flash_layout.h`（以及配套 `config.cmake`）。这次合入 **没有改** 这两块板的布局头。 |
| 要不要上 TF-M 2.4？ | 官方当时最新稳定是 **2.3.1**，不是 2.4.0。 |

这次真正多出来、和固件安全相关的，主要是 **TF-PSA-Crypto 从 1.1.0 升到 1.1.1**（若干 CVE），以及 TF-M 里 Crypto AEAD nonce 检查、ITS 写对齐清零 / invec unmap。

---

## 2. 两条目标分支（不要和别的支线搞混）

| 板子 | 新分支（合入 2.3.1） | 合入基线 | GitHub PR |
|------|----------------------|----------|-----------|
| STM32H5F4 | `stm32h5f4p256-usart6-bl2-public-key-ps256-1MNS-TFM2.3.1` | `…-ps256-1MNS`（S 512KB / NS 1MB，PS 256KB） | https://github.com/18902101360/stm32h5f4tfm/pull/10 |
| STM32H573 | `stm32H573P256-SPIFLASH-bl2-public-key-ps64-TFM2.3.1` | `…-ps64`（PS 64KB，S @ `0x0C044000`，外部 NOR） | https://github.com/18902101360/stm32h5f4tfm/pull/11 |

合入方式：**不是**整树覆盖官方。以本仓库板级改动为优先，把上游 2.3.1 相对 2.3.0 的文件铺进去；与本地冲突的文件 **保留本仓库版本**。

**不要**和下面这些支线搞混：

- `…-1MNS-test`：给 TLS 静默复位排查用的 **S 侧 panic/fault 打印**（`CONFIG_TFM_HALT_ON_CORE_PANIC`），**没有**合进 TFM2.3.1 分支。
- 已删除的 `cursor/h5f4-s-panic-dump-a11e`（原 PR #8 已关）。
- `ps256`（S 352KB / NS 1200KB）不是这次 2.3.1 的合入基线；H5F4 是从 **1MNS** 合的。

---

## 3. 合入时明确「不覆盖」的本地改动

上游 2.3.1 若改了同名文件，下列 **仍用本仓库**：

### 3.1 两板共用

- `otp_provision.c`：仍走 `scripts/sync_stm_otp_rotpk.py` + `otp_rotpk_hashes.inc`，**不用**上游 dummy ECDSA hash。
- 平台名、USART、密钥、槽位、PS/ITS、`CRYPTO_IOVEC_BUFFER_SIZE=20480` 等板级配置。

### 3.2 仅 H573（`stm32h573i_dk/config.cmake` 等）

- 签名 **EC-P256**
- **OVERWRITE_ONLY**（不是 H5F4 那种 SWAP）
- 升级镜像在 **外部 W25Q32 SPI NOR**
- PS **64KB**，S 主槽 **`0x0C044000`**，NS Bank2 **1MB** @ `0x0C100000`
- `MCUBOOT_DOWNGRADE_PREVENTION` **ON**
- `TFM_FWU_QUERY_ONLY`

MCUBoot cmake 模板做了 3-way 合并：保留 `MCUBOOT_DOWNGRADE_PREVENTION` 的 `cmakedefine`，丢掉 `MCUBOOT_USE_MBED_TLS`；加密镜像相关 cmake（`ENC_IMAGES` / `ENC_KEY_LEN` / `ENCRYPT_RSA` / KW）按 2.3.1 补回，平台默认仍 OFF。

### 3.3 仅 H5F4

- 平台 `stm/stm32h5f4`，控制台 **USART6 PC6/PC7**
- 内部 Flash **4MB 双 bank SWAP**
- S **512KB** @ `0x0C078000`，NS **1024KB** @ `0x0C0F8000`
- PS **256KB** / ITS **32KB**
- `CRYPTO_IOVEC_BUFFER_SIZE=20480`（4KB PS AEAD 需要；旧默认 5120 会让 `TFM_S_PS_TEST_1022` / `TFM_NS_PS_TEST_1025` 失败）
- 跟踪的 `mcuboot-src` 里保留 **H5F4SWP2** swap 防护（离线编译必须带着，否则 FetchContent 会回到未打补丁的 MCUBoot）

---

## 4. 上游 TF-M 2.3.1 相对 2.3.0 合进来的内容

官方 release 要点（见上游 `docs/releases/2.3.1.rst`）在本仓库体现为：

1. **TF-PSA-Crypto `v1.1.0` → `v1.1.1`**  
   CMake：`config/config_base.cmake` 里 `TF_PSA_CRYPTO_VERSION`。  
   **没有**把整个 TF-PSA-Crypto 当独立子仓永久 vendored 进 `lib/`；构建时 FetchContent 按 GIT_TAG 拉。  
   为了你们 **始终离线编译**，后来把编出来的  
   `trusted-firmware-m/build_s/build-spe/lib/ext/tf-psa-crypto-src`  
   **整棵树从 1.1.0 换成 1.1.1 并提交进 TFM2.3.1 分支**。

2. **Crypto 分区 AEAD**：S 侧再检查 nonce 长度（`crypto_aead.c` 一类改动）。

3. **ITS**：写 Flash 对齐缓冲清零；`set` 路径 invec unmap。

4. **BL2 / MCUBoot**：ECDSA `bootutil_key_cnt`；去掉 `MCUBOOT_USE_MBED_TLS`；配套 patch（例如删掉已不需要的 0017 等）。

5. 文档、`version.cmake` 回落到 `TFM_VERSION_MANUAL=2.3.1**`（无 git tag 时的提示版本）。  
   **注意：** `versions/` 里给 imgtool 用的 **S 镜像版本号仍是 2.3.0、security_counter=1**（产品计数，不是 TF-M 源码版本）。换 TF-M 源码版本 **不会**自动改 MCUboot 镜像 version。

官方 2.3.1 里提到的 TFMV-10 / TFMV-11 等是 Arm 平台邮箱/指针类问题，**不是 STM32 TrustZone 这条线的必改项**。

---

## 5. 各板 Flash 布局（这次没改，仅备查）

布局仍由各板 `flash_layout.h` + `config.cmake` 管理。

### 5.1 H5F4（1MNS，内部 4MB 双 bank）

| 偏移 | 区 | 大小 | 安全别名 |
|------|----|------|----------|
| `0x00000000` | MCUBoot scratch | 48 KB | |
| `0x0000C000` | BL2 / MCUBoot / OTP / NV | 到 `0x30000` | BL2 `0x0C00E000` |
| `0x00030000` | PS | **256 KB** | |
| `0x00070000` | ITS | **32 KB** | |
| `0x00078000` | S 主槽 | **512 KB** | `0x0C078000` |
| `0x000F8000` | NS 主槽 | **1024 KB** | `0x0C0F8000` |
| `0x001F8000` | Bank1 空隙 | 32 KB | |
| `0x00200000` | S 升级槽 | 512 KB | `0x0C200000` |
| `0x00280000` | NS 升级槽 | 1024 KB | `0x0C280000` |
| `0x00380000` | NS 用户 Flash | 512 KB | `0x0C380000` |

NSC veneer 约在 `0x0C0F4FC0`（1MNS）。NS 调 SPE：普通函数调用链到 `s_veneers.o` 里的 **SG** 桩，不是自己写寄存器。

### 5.2 H573（ps64，内部 Bank2 + 外部 W25Q32）

| 项 | 值 |
|----|----|
| BL2 | `0x0C00E000` |
| S 主槽 | `0x0C044000`（512 KB 量级） |
| NS 主槽 | `0x0C100000`（Bank2 1MB） |
| 升级 | **OVERWRITE_ONLY**，下载槽在外部 SPI NOR（W25Q32），不是内部第二 bank SWAP |
| PS | 64 KB |
| 防回滚 | `MCUBOOT_DOWNGRADE_PREVENTION` |

---

## 6. TF-PSA-Crypto 版本关系（1.0 / 1.1 / 1.1.1）

来源：官方 `ChangeLog`（Mbed-TLS/TF-PSA-Crypto）。

| 版本 | 日期 | 谁在用 |
|------|------|--------|
| 1.0.0 | 2025-10-15 | 更早的 TF-M 线 |
| 1.1.0 | 2026-03-31 | 本仓库 TF-M **2.3.0** |
| 1.1.1 | 2026-07-07 | 本仓库 TF-M **2.3.1** |

SPE 里的密码学是 **TF-PSA-Crypto**，不是 CubeIDE NS 目录里那份 mbedtls-4.1.1 的完整 PSA core。NS 只当 PSA **客户端**（`psa_*` + veneer）。NS 工程里的 `tf-psa-crypto` 主要是 ASN.1 / PK / PEM，不要编 `core/psa_crypto*.c`。

### 6.1 1.1.0 相对 1.0.0（功能代）

**新能力**

- SHAKE128 / SHAKE256（`PSA_ALG_SHAKE128` / `SHAKE256`）
- `mbedtls_pk_write_pubkey_psa()`、`mbedtls_pk_get_key_type()`
- `psa_random_reseed()` / `psa_random_deplete()` / `psa_random_set_prediction_resistance()`
- 无系统熵接口时可用 `MBEDTLS_PLATFORM_DEV_RANDOM`
- 静态 key slot 缓冲把非对称 / cipher / AEAD / CMAC / HMAC 都算进去
- ChaCha20 Arm Neon（Armv7+），AArch64 大约 2～2.7×

**安全**

- 默认熵设备 `/dev/urandom` → `/dev/random`（嵌入式 STM32 不走这条）
- FFDH 拒绝低阶元素；FFDH `psa_export_public_key()` 缓冲过小溢出
- CCM `mbedtls_ccm_finish()` 校验 tag 长度
- `fork()` 后 RNG 状态重复 → 子进程强制 reseed（CVE-2026-25835）

**修复 / 工程**

- 缓解 1.0 安全补丁导致的 RSA 私钥变慢
- GCC 10–14.2、-O3、无 AESNI/AESCE 时 GCM tag 错误
- GCC 14.3 `mbedtls_xor` 数组边界
- 关 ECDSA 时 interruptible ECDH 实际不可中断
- PK 大缓冲从栈改堆
- 目录拆成 `extras` / `platform` / `utilities` / `dispatch`

**删除**

- 不能再加载用户 key ID 范围外的 persistent key（builtin keys 除外）
- 头文件不再把 `inline` 定义成宏

对 STM32H5 TF-M 日常（ECDSA、AES-GCM/CCM AEAD for PS、PSA RNG）影响有限；1.1.0 已在 2.3.0 里。

### 6.2 1.1.1 相对 1.1.0（安全补丁，这次 2.3.1 的核心）

- RSA PKCS#1 v1.5 解密时序 → Bleichenbacher（CVE-2026-50587）
- ChaCha20 32-bit 块计数回绕复用 keystream（CVE-2026-50584）
- ECC 侧信道，本地高权限攻击者可恢复长期私钥（CVE-2026-54435）
- Everest x25519 低阶点把共享密钥打成全 0（CVE-2026-54434，非默认配置）
- RSA 密钥生成 / 素性检测在除法非恒定时的时序（当时未证明可利用）
- PK 解析畸形 ECC 公钥 1 字节越读（CVE-2026-50583）
- Zephyr `__packed` 与 IAR 冲突、Doxygen 输出目录、用户配置头无法 include 标准 C 头等 bugfix

STM32 TrustZone SPE 若用软件 ECC/RSA，1.1.1 的 ECC/RSA 侧信道修复是有意义的；PS AEAD 走硬件加速时路径不同，但仍建议跟 2.3.1。

---

## 7. 编译、离线缓存、工具链

### 7.1 工具链（硬性）

- 必须 **Arm GNU Toolchain 14.3.Rel1**（`arm-none-eabi-gcc` 14.3.1）
- Ubuntu 仓库 **gcc-arm-none-eabi 13.2** 会被 `check_config.cmake` 拒绝（精确匹配 `15:13.2.rel1-2`）
- 另需：`cmake`、`ninja-build`、`python3-venv`

### 7.2 一键脚本

仓库根目录：

```bash
./buildtfm.sh test    # TEST_S+TEST_NS，INFO 日志；windows-tfm-tools 预置镜像按此刷新
./buildtfm.sh prod    # TEST_S 关，TEST_NS 开，ERROR 日志
```

- H5F4 脚本平台：`stm/stm32h5f4`
- H573 脚本平台：`stm/stm32h573i_dk`

### 7.3 `.deps-cache` 是什么（最容易误解）

路径：`trusted-firmware-m/.deps-cache/`（在 `.gitignore` 里）。

`./buildtfm.sh` 默认会清 `build_s` / `build_ns`，但把 FetchContent 源码（mcuboot、cmsis、qcbor、**tf-psa-crypto** 等）拷到 `.deps-cache`，下次 **不联网**。  
**`git pull` 不会改、也不会删这个目录。**

2.3.0 编过的机器上，缓存经常是 **crypto 1.1.0**。2.3.1 配置要 **v1.1.1**。若脚本发现 `lib/ext` 里已经有 `tf-psa-crypto-src`，会开 `FETCHCONTENT_FULLY_DISCONNECTED`，**继续用 1.1.0**，补丁失败或悄悄编错版本。

TFM2.3.1 分支上的 `buildtfm.sh` 会比对：

- `config_base.cmake` 的 `TF_PSA_CRYPTO_VERSION`（要 1.1.1）
- 缓存 / 还原树里 `include/tf-psa-crypto/build_info.h` 的 `TF_PSA_CRYPTO_VERSION_STRING`

不一致就删掉 `tf-psa-crypto-src` 再允许联网拉一次。

**你怎么做最省事：** 本地整份工程删掉（连 `.deps-cache` 一起没了），再 clone/checkout 对应 **TFM2.3.1** 分支。仓库里已经提交了 1.1.1 源码树和 hex/bin，可以直接离线编。

只 `git pull`、旧缓存还在时：删  
`trusted-firmware-m/.deps-cache/spe/tf-psa-crypto-src`  
再编。不必清空整个 `.deps-cache`。

不要手动 `rm -rf trusted-firmware-m/build_s` 当「彻底重来」——会丢掉 FetchContent 源码，下次又要联网。用 `scripts/clean_tfm_build.sh` 或 `./buildtfm.sh` 自带的清理。

### 7.4 本环境验证结果（GNU 14.3）

| 分支 | 命令 | 结果 |
|------|------|------|
| H5F4 TFM2.3.1 | `./buildtfm.sh test` | 通过。slot0=`0xc078000` slot1=`0xc0f8000` slot2=`0xc200000` slot3=`0xc280000` boot=`0xc00e000` |
| H573 TFM2.3.1 | `./buildtfm.sh test` | 通过。slot0=`0xc044000` slot1=`0xc100000` boot=`0xc00e000` |

两板 **`s_veneers.o` 内容与合入前 SPE 相同**（SG 符号未变），因此没有「新 veneer 二进制」可提交；仍须与 **同一轮** 烧进去的 `tfm_s` 配对。CubeIDE **不必**改 `signing_layout_ns.o` 才能编 NS；布局 `.o` 只给 imgtool 填槽大小。覆盖 `spe/api_ns` 时 **必须带同一轮 `s_veneers.o`**。

已刷新并提交（各 TFM2.3.1 分支）：

- `windows-tfm-tools` 的 `bl2` / `tfm_s_signed` / `tfm_ns_signed` / `tfm_s_ns_signed` 的 `.bin` 与 `.hex`
- CubeIDE / makefile 工程里的 `bl2.bin`、`tfm_s.bin`、`tfm_s_signed.bin`、导出 `build_info.h`（1.1.1）
- H5F4：`signing_layout_*.o`、跟踪树里带 H5F4SWP2 的 `mcuboot-src`

H5F4 烧录请用 `trusted-firmware-m/build_s/api_ns` 下脚本；串口第一行正式版也应有 **`H5F4BL2`**。不要单独把拼接的 `tfm_s_ns_signed` 当完整镜像烧（Bank1 空隙会对不齐）。

---

## 8. NS 如何调用 SPE（未改）

1. NS 链接 `s_veneers.o`（NSC 区，H5F4 1MNS 约 `0x0C0F4FC0`）。
2. 调用 `psa_*` / `tfm_*` API → 编译进 `tfm_*_api.c` → veneer 里的 **SG**。
3. CPU 切到 S，SPE 调度对应安全分区。

不是 NS 自己写 SAU/MPU 去「调寄存器进 S」。只换 NS、不换配套 veneer/S，NSC 会跑飞。

---

## 9. 建议操作步骤（Windows 本机）

1. 备份后 **删除本地整份目录**（含隐藏的 `.deps-cache`）。
2. 克隆仓库，检出：
   - H5F4：`stm32h5f4p256-usart6-bl2-public-key-ps256-1MNS-TFM2.3.1`
   - H573：`stm32H573P256-SPIFLASH-bl2-public-key-ps64-TFM2.3.1`
3. PATH 里放 **Arm GNU 14.3**，不要用 Ubuntu/Msys 自带的 gcc 13。
4. 仓库根目录 `./buildtfm.sh test` 或 `prod`（离线应能过）。
5. 烧录：
   - H5F4：`./flash_stm32h5f4.sh` 或 `windows-tfm-tools\tfm_update.bat`
   - H573：`./flash_stm32h573.sh` 或同目录 Windows 脚本
6. CubeIDE NS：**只覆盖**与板上 S 同一轮的 `spe/api_ns`（至少 `s_veneers.o` + 签名 S）。不必为 2.3.1 改 NS 应用代码。

Git clone 若遇 HTTP/2 `curl 16`：改用 HTTP/1.1 并 `--depth 1`。

---

## 10. 相关提交（便于对照）

H5F4 `…-1MNS-TFM2.3.1`：

- 合入上游 2.3.1（保留本地 STM/H5F4）
- `buildtfm.sh`：crypto 缓存版本不一致则丢弃再拉
- 提交跟踪的 **tf-psa-crypto 1.1.1** + 测试镜像 hex/bin + MCUBoot swap 防护

H573 `…-ps64-TFM2.3.1`：

- 合入上游 2.3.1（保留 OVERWRITE_ONLY / SPI NOR / PS64 / otp_provision）
- 同样的缓存版本检查
- 提交跟踪的 **tf-psa-crypto 1.1.1** + 测试镜像 hex/bin

镜像版本文件 `versions/`：**未**改成 2.3.1（仍是 S `2.3.0` counter 1）。若量产要体现「新固件版本」，需另改 `versions/` 再编、再烧。

---

## 11. 文档与代码对照路径（拉到 TFM2.3.1 分支后）

| 内容 | 路径 |
|------|------|
| 板级说明 | 仓库根 `readme.md` |
| Crypto 版本 | `trusted-firmware-m/config/config_base.cmake`（`TF_PSA_CRYPTO_VERSION`） |
| 源码版本回落 | `trusted-firmware-m/cmake/version.cmake` |
| 官方 2.3.1 说明 | `trusted-firmware-m/docs/releases/2.3.1.rst` |
| 离线 crypto 树 | `trusted-firmware-m/build_s/build-spe/lib/ext/tf-psa-crypto-src/ChangeLog` |
| 清理/缓存 | `scripts/clean_tfm_build.sh`、`buildtfm.sh` |
| H5F4 布局 | `trusted-firmware-m/platform/ext/target/stm/stm32h5f4/.../flash_layout.h` |
| H573 布局 | `trusted-firmware-m/platform/ext/target/stm/stm32h573i_dk/.../flash_layout.h` |
| NS veneer | `tfmcubeideproject/STM32CubeIDE/spe/api_ns/interface/lib/s_veneers.o` |

---

*本文根据 2026-09 合入 TF-M v2.3.1、GNU 14.3 编译验证，以及官方 TF-PSA-Crypto ChangeLog 整理。*
