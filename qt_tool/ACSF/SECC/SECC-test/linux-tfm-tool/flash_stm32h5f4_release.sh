#!/usr/bin/env bash
# STM32H5F4（PLC）发布版烧录。
# Windows 同一套：windows-tfm-tools\flash_stm32h5f4_release.bat
# 参数、地址、选项字节和两边一致。不写 PRODUCT_STATE=0x72。
#
#   ./linux-tfm-tool/flash_stm32h5f4_release.sh                 # 回归（全片擦除）后烧 BL2/S/NS，再锁
#   ./linux-tfm-tool/flash_stm32h5f4_release.sh download [SN]   # 只烧镜像，然后写本布局的 WRP/HDP/SECWM 和 Boot lock
#   ./linux-tfm-tool/flash_stm32h5f4_release.sh regression [SN] # 只回归。先把 SECBOOT_LOCK 改回 0xC3
#
# 默认从本目录的 release/ 读取。多给一个目录时改用那个外部目录，文件名不变。
# 先编译: ./buildtfm.sh release
# 槽位是本板，不是 H573：
#   BL2  0x0C00E000
#   S    0x0C078000
#   NS   0x0C0F8000
# 锁完：SECBOOTADD=0xC0100，SECWM1 [0, 123]，SECWM2 [0, 63]，
#       WRPSG11=0xFFFFFFE3，HDP1 [0, 19]，SECBOOT_LOCK=0xB4。
# 选项字节用 Under Reset。HotPlug 写 -ob 会段错误。
# 不写 PRODUCT_STATE。板子仍是 Open 时，发布版 BL2 会复位。SWD 还在。
# 脚本结束不复位，方便接着写 OBK。HDP 一旦写上，下次要先 regression。
#
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMG_DIR="${SCRIPT_DIR}/release"
BL2_BIN="${IMG_DIR}/bl2.bin"
# hex 自带地址，并含 0x0C028000 的 OTP。有这份文件就优先烧它。标记仍从 bin 读。
BL2_HEX="${IMG_DIR}/bl2.hex"
S_BIN="${IMG_DIR}/tfm_s_signed.bin"
NS_BIN="${IMG_DIR}/tfm_ns_signed.bin"

BOOT_ADDR=0x0C00E000
SLOT_S=0x0C078000
SLOT_NS=0x0C0F8000
# MCUboot image magic little-endian 0x96f3b83d
MAGIC_BYTES="3db8f396"
# OB_BOOT_LOCK_ENABLE。OB_BOOT_LOCK_DISABLE 是 0xC3。
BOOT_LOCK_ENABLE=0xB4
BOOT_LOCK_DISABLE=0xC3

die() { echo "错误: $*" >&2; exit 1; }

usage() {
    cat <<'EOF'
用法:
  ./linux-tfm-tool/flash_stm32h5f4_release.sh                 # 一键：回归 + 烧录 + Boot lock 0xB4
  ./linux-tfm-tool/flash_stm32h5f4_release.sh all [SN]        # 同上
  ./linux-tfm-tool/flash_stm32h5f4_release.sh download [目录] [SN]   # 只烧 BL2 + S + NS，然后写本布局的保护
  ./linux-tfm-tool/flash_stm32h5f4_release.sh regression [SN] # 回归（会全片擦除）。SECBOOT_LOCK 改回 0xC3
  ./linux-tfm-tool/flash_stm32h5f4_release.sh --dir /path/to/bins
  ./linux-tfm-tool/flash_stm32h5f4_release.sh --help

不给目录时只烧 linux-tfm-tool/release 里的发布版。给了目录就从那里读同样的文件名。
先编译: ./buildtfm.sh release
  bl2.bin  tfm_s_signed.bin  tfm_ns_signed.bin

本板地址（不要用 H573 的 0x0C044000 / 0x0C100000）:
  BL2  0x0C00E000
  S    0x0C078000
  NS   0x0C0F8000

写完镜像再锁:
  SECBOOTADD=0xC0100
  SECWM1 [0, 123]     bank1 的 S primary
  SECWM2 [0, 63]      bank2 的 S secondary（不是 H573 的 [127, 0]）
  WRPSG11=0xFFFFFFE3  （不是 WRPSGn1）
  HDP1 [0, 19]
  SECBOOT_LOCK=0xB4

不写 PRODUCT_STATE。板子仍是 Open 时，发布版 BL2 会复位。SWD 还在。
HDP 盖住 BL2 之后，download 会拒绝烧录，先跑不带参数的本脚本。
EOF
}

find_stm32_cli() {
    if command -v STM32_Programmer_CLI >/dev/null 2>&1; then
        command -v STM32_Programmer_CLI
        return 0
    fi
    local d
    for d in \
        /usr/local/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin \
        "${HOME}/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin" \
        /opt/st/stm32cubeprogrammer/bin \
        /opt/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin
    do
        if [[ -x "${d}/STM32_Programmer_CLI" ]]; then
            echo "${d}/STM32_Programmer_CLI"
            return 0
        fi
    done
    return 1
}

ob_hex_field() {
    local dump="$1"
    local name="$2"
    printf '%s\n' "${dump}" | sed -n "s/.*${name}[[:space:]]*:[[:space:]]*\(0x[0-9A-Fa-f]\+\).*/\1/p" | head -n 1
}

# BL2 download covers sectors [7, 23] (0x0C00E000, 128 KB).
hdp1_overlaps_bl2() {
    local dump="$1"
    local strt end
    strt="$(ob_hex_field "${dump}" "HDP1_STRT")"
    end="$(ob_hex_field "${dump}" "HDP1_END")"
    [[ -n "${strt}" && -n "${end}" ]] || return 0
    local s=$((strt)) e=$((end))
    [[ "${s}" -le "${e}" ]] || return 1
    local bl2_s=7 bl2_e=23
    [[ "${s}" -le "${bl2_e}" && "${e}" -ge "${bl2_s}" ]]
}

# 决定烧哪一份 BL2，并把即将烧入的内容解成字节放到 BL2_SCAN。
# 有 bl2.hex 就烧 hex；没有 hex 才烧 bl2.bin。版本标记只搜这一份。
choose_bl2() {
    if [[ -f "${BL2_HEX}" ]]; then
        BL2_KIND="hex"
        BL2_LABEL="${BL2_HEX}"
        BL2_SCAN="$(mktemp --suffix=.bin)"
        command -v python3 >/dev/null 2>&1 || die "烧 bl2.hex 需要 python3"
        python3 - "${BL2_HEX}" "${BL2_SCAN}" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
parts = []
with open(src, "r", errors="replace") as handle:
    for line in handle:
        line = line.strip()
        if len(line) < 11 or not line.startswith(":"):
            continue
        count = int(line[1:3], 16)
        kind = int(line[7:9], 16)
        if kind == 0:
            parts.append(bytes.fromhex(line[9:9 + count * 2]))
        elif kind == 1:
            break
with open(dst, "wb") as handle:
    handle.write(b"".join(parts))
PY
    elif [[ -f "${BL2_BIN}" ]]; then
        BL2_KIND="bin"
        BL2_LABEL="${BL2_BIN}"
        BL2_SCAN="${BL2_BIN}"
    else
        die "缺少 ${IMG_DIR}/bl2.hex 或 bl2.bin"
    fi
}

# 发布版下载前检查。S/NS 仍要 bin。BL2 有 hex 就认 hex，没有才认 bin。
require_release_bl2() {
    [[ -f "${S_BIN}" && -f "${NS_BIN}" ]] \
        || die "缺少 ${IMG_DIR}/{tfm_s_signed.bin,tfm_ns_signed.bin}。请先: ./buildtfm.sh release"
    choose_bl2
    grep -a -F -q "H5F4BL2" "${BL2_SCAN}" \
        || die "${BL2_LABEL} 没有 H5F4BL2。请先: ./buildtfm.sh release"
    # 只有 TFM_NO_DEV_MODE 会编进这句。test/prod 没有。
    grep -a -F -q "Unexpected value for write protection" "${BL2_SCAN}" \
        || die "${BL2_LABEL} 没有发布版写保护检查。请先: ./buildtfm.sh release"
    if grep -a -F -q "Starting bootloader S-sec=" "${BL2_SCAN}"; then
        die "${BL2_LABEL} 含 S-sec 标记，是测试版。本脚本只烧 ${IMG_DIR}"
    fi
    if [[ "${BL2_KIND}" == "hex" ]]; then
        rm -f "${BL2_SCAN}"
    fi
}

MODE="all"
SN=""
# 外部镜像目录。空表示用脚本旁边的 release/。
IMG_DIR_EXT=""
# 收下外部镜像目录。不传则用默认 release/。文件名仍是 bl2.bin、tfm_s_signed.bin、tfm_ns_signed.bin。
accept_image_dir() {
    local path="$1"
    [[ -n "${path}" ]] || die "请在 --dir 后面写镜像目录"
    [[ -d "${path}" ]] || die "镜像目录不存在: ${path}"
    [[ -z "${IMG_DIR_EXT}" ]] || die "镜像目录重复: ${path}"
    IMG_DIR_EXT="$(cd "${path}" && pwd)"
}
while [[ $# -gt 0 ]]; do
    case "$1" in
        all)
            MODE="all"
            shift
            ;;
        download|flash|update)
            MODE="download"
            shift
            ;;
        regression|erase|regress)
            MODE="regression"
            shift
            ;;
        --dir|-d)
            accept_image_dir "${2:-}"
            shift 2
            ;;
        --self-test)
            sample='     HDP1_STRT    : 0x0  (0x0)
     HDP1_END     : 0x13  (0x26000)'
            hdp1_overlaps_bl2 "${sample}" || die "self-test: HDP [0, 0x13] must overlap BL2"
            sample_off='     HDP1_STRT    : 0x1  (0x2000)
     HDP1_END     : 0x0  (0x0)'
            hdp1_overlaps_bl2 "${sample_off}" && die "self-test: disabled HDP must not overlap BL2"
            echo "flash_stm32h5f4_release.sh self-test OK"
            exit 0
            ;;
        -h|--help|help)
            usage
            exit 0
            ;;
        *)
            if [[ -d "$1" ]]; then
                accept_image_dir "$1"
            elif [[ "$1" == */* || "$1" == *\\* ]]; then
                die "镜像目录不存在: $1"
            elif [[ "$1" =~ ^[0-9A-Fa-f]+$ ]]; then
                [[ -z "${SN}" ]] || die "ST-LINK 序列号重复: $1"
                SN="$1"
            else
                usage >&2
                die "未知参数: $1"
            fi
            shift
            ;;
    esac
done

# 指定了外部目录就改到那里，文件名仍是 bl2.bin / tfm_s_signed.bin / tfm_ns_signed.bin。
if [[ -n "${IMG_DIR_EXT}" ]]; then
    IMG_DIR="${IMG_DIR_EXT}"
    BL2_BIN="${IMG_DIR}/bl2.bin"
    BL2_HEX="${IMG_DIR}/bl2.hex"
    S_BIN="${IMG_DIR}/tfm_s_signed.bin"
    NS_BIN="${IMG_DIR}/tfm_ns_signed.bin"
fi

sn_option=""
[[ -n "${SN}" ]] && sn_option="sn=${SN}"

CLI="$(find_stm32_cli)" || die "找不到 STM32_Programmer_CLI（请安装 STM32CubeProgrammer 并加入 PATH）"
# 选项字节必须 Under Reset。HotPlug -ob 在长连接后会段错误（exit 139）。
CONNECT_UR="-c port=SWD ap=1 ${sn_option} mode=UR"

run_regression() {
    echo "=== H5F4 回归（写 option bytes，会全片擦除）==="
    echo "    步骤与 stm32h5f4/regression.sh 相同，全部走 Under Reset。"
    echo "    不调用那份脚本末尾的 HotPlug -ob（长连接后会段错误）。"
    if [[ -n "${SN}" ]]; then
        echo "ST-LINK SN=${SN}"
    fi
    echo ">>> PRODUCT_STATE=0xED  TZEN=0xB4（先开 TrustZone。关着时 SECBOOT_LOCK 写不进去）"
    "${CLI}" ${CONNECT_UR} -ob PRODUCT_STATE=0xED TZEN=0xB4
    echo ">>> SECBOOT_LOCK=${BOOT_LOCK_DISABLE}（解开后才能改 BOOT_UBE / SECBOOTADD）"
    "${CLI}" ${CONNECT_UR} -ob SECBOOT_LOCK=${BOOT_LOCK_DISABLE}
    echo ">>> 去掉 bank1 保护并整片擦除"
    "${CLI}" ${CONNECT_UR} -ob SECWM1_STRT=255 SECWM1_END=0 WRPSG11=0xffffffff WRPSG12=0xffffffff -e all
    echo ">>> 去掉 bank2 保护并整片擦除"
    "${CLI}" ${CONNECT_UR} -ob SECWM2_STRT=255 SECWM2_END=0 WRPSG21=0xffffffff WRPSG22=0xffffffff -e all
    echo ">>> 关掉 HDP"
    "${CLI}" ${CONNECT_UR} -ob HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0
    echo ">>> SECBOOTADD=0xC0100（BL2 向量 0x0C010000）"
    "${CLI}" ${CONNECT_UR} -ob SECBOOTADD=0xC0100 HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0 SWAP_BANK=0 SRAM2_RST=0 SRAM2_ECC=0
    echo ">>> 回归期间两 bank 水印先铺满 [0, 255]，下载后再收成本布局"
    "${CLI}" ${CONNECT_UR} -ob SECWM1_STRT=0 SECWM1_END=255 SECWM2_STRT=0 SECWM2_END=255
    echo ">>> BOOT_UBE=0xB4（OEM-iRoT）"
    "${CLI}" ${CONNECT_UR} -ob BOOT_UBE=0xB4
}

readback_magic() {
    local addr="$1"
    local label="$2"
    local tmp
    tmp="$(mktemp --suffix=.bin)"
    echo ">>> 回读 ${label} @ ${addr}（检查 MCUboot magic）"
    "${CLI}" ${CONNECT_UR} -u "${addr}" 0x10 "${tmp}" \
        || die "回读 ${label} 失败（可能没烧上，或 AP/连接不对）"
    local got
    got="$(od -An -tx1 -N4 "${tmp}" | tr -d ' \n')"
    rm -f "${tmp}"
    echo "    片上前 4 字节: ${got}"
    if [[ "${got}" != "${MAGIC_BYTES}" ]]; then
        die "${label} 没有 MCUboot magic（期望 ${MAGIC_BYTES}）。烧录未成功。"
    fi
    echo "    ${label} magic OK"
}

run_download() {
    local ob_dump
    require_release_bl2

    echo ">>> 读 option bytes，确认 HDP 没有盖住 BL2"
    ob_dump="$("${CLI}" ${CONNECT_UR} -ob displ)"
    if hdp1_overlaps_bl2 "${ob_dump}"; then
        die "HDP 仍覆盖 BL2。download 不会缩 HDP。请先: ./linux-tfm-tool/flash_stm32h5f4_release.sh"
    fi

    echo "=== H5F4 发布版下载 ${IMG_DIR} ==="
    echo "S    ${SLOT_S}  <- ${S_BIN}"
    echo "NS   ${SLOT_NS}  <- ${NS_BIN}"
    # 有 bl2.hex 就烧 hex。没有才把 bl2.bin 按 0x0C00E000 整段写下。
    if [[ -f "${BL2_HEX}" ]]; then
        echo "BL2  (hex 内含地址和 OTP) <- ${BL2_HEX}"
    else
        echo "BL2  ${BOOT_ADDR}  <- ${BL2_BIN}"
    fi
    if [[ -n "${SN}" ]]; then
        echo "ST-LINK SN=${SN}"
    fi

    echo ">>> BOOT_UBE=0xB4 SECBOOTADD=0xC0100"
    "${CLI}" ${CONNECT_UR} -ob BOOT_UBE=0xB4 SECBOOTADD=0xC0100

    echo ">>> Write Secure"
    "${CLI}" ${CONNECT_UR} -d "${S_BIN}" ${SLOT_S} -v
    echo ">>> Write Non-Secure"
    "${CLI}" ${CONNECT_UR} -d "${NS_BIN}" ${SLOT_NS} -v
    # 有 bl2.hex 就按文件内地址写，OTP（0x0C028000）和计数（0x0C02E000）一起下去。
    if [[ -f "${BL2_HEX}" ]]; then
        echo ">>> Write BL2 hex"
        "${CLI}" ${CONNECT_UR} -d "${BL2_HEX}" -v
    else
        echo ">>> Write BL2"
        "${CLI}" ${CONNECT_UR} -d "${BL2_BIN}" ${BOOT_ADDR} -v
    fi

    readback_magic "${SLOT_S}" "Secure primary"
    readback_magic "${SLOT_NS}" "Non-Secure primary"

    local bl2_dump
    bl2_dump="$(mktemp --suffix=.bin)"
    echo ">>> 回读 BL2，确认片上是 H5F4BL2（锁 HDP 之前）"
    "${CLI}" ${CONNECT_UR} -u ${BOOT_ADDR} 0x20000 "${bl2_dump}" \
        || die "回读 BL2 失败"
    grep -a -F -q "H5F4BL2" "${bl2_dump}" \
        || die "片上 BL2 没有 H5F4BL2"
    grep -a -F -q "Unexpected value for write protection" "${bl2_dump}" \
        || die "片上 BL2 没有发布版写保护检查"
    if grep -a -F -q "Starting bootloader S-sec=" "${bl2_dump}"; then
        rm -f "${bl2_dump}"
        die "片上 BL2 含 S-sec 标记，这不是发布版"
    fi
    rm -f "${bl2_dump}"

    # 镜像写完再锁。寄存器里 0 表示保护，BL2 核对的保护组是 0x1C，取反后是 0xFFFFFFE3。
    # SECWM2 [0, 63] 是 bank2 的 S secondary。写成 [127, 0] 会把这块标成非安全。
    echo ">>> SECWM1 [0, 123]  SECWM2 [0, 63]"
    "${CLI}" ${CONNECT_UR} -ob SECWM1_STRT=0 SECWM1_END=123 SECWM2_STRT=0 SECWM2_END=63
    echo ">>> WRPSG11=0xFFFFFFE3"
    "${CLI}" ${CONNECT_UR} -ob WRPSG11=0xFFFFFFE3
    echo ">>> HDP1 [0, 19]"
    "${CLI}" ${CONNECT_UR} -ob HDP1_STRT=0 HDP1_END=19
    echo ">>> SECBOOT_LOCK=${BOOT_LOCK_ENABLE}（enable；不是 0xC3）"
    "${CLI}" ${CONNECT_UR} -ob SECBOOT_LOCK=${BOOT_LOCK_ENABLE}

    echo "download Done"
    echo
    echo "发布版 BL2 在 Open（0xED）上会复位，直到 PRODUCT_STATE 写成 0x72。"
    echo "本脚本不写 PRODUCT_STATE。Boot lock 已是 0xB4。"
    echo "再烧 test/prod 前先 ./linux-tfm-tool/flash_stm32h5f4_release.sh regression，把 Boot lock 改回 0xC3 并擦掉 HDP。"
    echo "串口 USART6 PC6/PC7 115200 8N1。发布版日志是 ERROR，只有 H5F4BL2。"
}

case "${MODE}" in
    regression)
        run_regression
        ;;
    download)
        run_download
        ;;
    all)
        run_regression
        run_download
        echo "全部完成。"
        ;;
esac
