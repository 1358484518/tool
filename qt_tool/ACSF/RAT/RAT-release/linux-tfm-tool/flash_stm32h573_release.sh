#!/usr/bin/env bash
# STM32H573 / H5F4 发布版烧录（Linux）
#
# 默认只读本目录 release/。不读 build_s / build_ns，也不接受 test 或 prod。
# 多给一个目录时，改用那个外部目录，文件名不变。
#   ./linux-tfm-tool/flash_stm32h573_release.sh
#   ./linux-tfm-tool/flash_stm32h573_release.sh download [SN]
#   ./linux-tfm-tool/flash_stm32h573_release.sh regression [SN]
#   ./linux-tfm-tool/flash_stm32h573_release.sh /path/to/bins
#   ./linux-tfm-tool/flash_stm32h573_release.sh --dir /path/to/bins
#
# 与 flash_stm32h573.sh 相同，镜像写完后多写 SECBOOT_LOCK=0xB4。
# 0xC3 是 disable，只有开着 TFM_DEV_MODE 的 test/prod 才接受。
# 本脚本不写 PRODUCT_STATE，芯片保持回归留下的 Open（0xED）。
# 发布版 BL2 还要求至少 Closed（0x72），所以 Open 的板子烧完仍会复位。SWD 还在。
#
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMG_DIR="${SCRIPT_DIR}/release"
BL2_BIN="${IMG_DIR}/bl2.bin"
BL2_HEX="${IMG_DIR}/bl2.hex"
S_BIN="${IMG_DIR}/tfm_s_signed.bin"
NS_BIN="${IMG_DIR}/tfm_ns_signed.bin"

BOOT_ADDR=0xc00e000
SLOT_S=0xc044000
SLOT_NS=0xc100000
MAGIC_BYTES="3db8f396"
# OB_BOOT_LOCK_ENABLE。OB_BOOT_LOCK_DISABLE 是 0xC3。
BOOT_LOCK_ENABLE=0xB4

die() { echo "错误: $*" >&2; exit 1; }

usage() {
    cat <<'EOF'
用法:
  ./linux-tfm-tool/flash_stm32h573_release.sh                 # 回归 + 烧录 + SECBOOT_LOCK 0xB4
  ./linux-tfm-tool/flash_stm32h573_release.sh all [SN]        # 同上
  ./linux-tfm-tool/flash_stm32h573_release.sh download [目录] [SN]   # 只烧 BL2 + S + NS，然后上锁
  ./linux-tfm-tool/flash_stm32h573_release.sh regression [SN] # 只回归（全片擦除，不改 Boot lock）
  ./linux-tfm-tool/flash_stm32h573_release.sh --dir /path/to/bins
  ./linux-tfm-tool/flash_stm32h573_release.sh --help

不给目录时镜像来自 linux-tfm-tool/release/，由 ./buildtfm.sh release 复制。
给了目录就从那里读 bl2.bin、tfm_s_signed.bin、tfm_ns_signed.bin。
不写 PRODUCT_STATE。板子仍是 Open 时，发布版 BL2 也会复位。SWD 还在。
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

file_contains() {
    grep -a -F -q -- "$2" "$1"
}

# 收下外部镜像目录。不传则用脚本旁边的 release/。
# 目录里的文件名与默认目录相同：bl2.bin、tfm_s_signed.bin、tfm_ns_signed.bin。
accept_image_dir() {
    local path="$1"
    [[ -n "${path}" ]] || die "请在 --dir 后面写镜像目录"
    [[ -d "${path}" ]] || die "镜像目录不存在: ${path}"
    [[ -z "${IMG_DIR_EXT}" ]] || die "镜像目录重复: ${path}"
    IMG_DIR_EXT="$(cd "${path}" && pwd)"
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
    [[ -f "${S_BIN}" ]] || die "缺少 ${S_BIN}。请先 ./buildtfm.sh release"
    [[ -f "${NS_BIN}" ]] || die "缺少 ${NS_BIN}。请先 ./buildtfm.sh release"
    choose_bl2
    if file_contains "${BL2_SCAN}" "set wrp1"; then
        die "这份 BL2 仍是 TFM_DEV_MODE（含 set wrp1）。请先 ./buildtfm.sh release，不要用 test/prod 镜像。"
    fi
    if ! file_contains "${BL2_SCAN}" "Unexpected value for write protection "; then
        die "${BL2_LABEL} 没有发布版写保护检查字符串。请先 ./buildtfm.sh release"
    fi
    if file_contains "${BL2_SCAN}" "Starting bootloader"; then
        die "${BL2_LABEL} 含 Starting bootloader。这是测试版，不能当发布版烧"
    fi
    if [[ "${BL2_KIND}" == "hex" ]]; then
        rm -f "${BL2_SCAN}"
    fi
}

MODE="all"
SN=""
# 外部镜像目录。空表示用脚本旁边的 release/。
IMG_DIR_EXT=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        test|prod)
            die "本脚本只烧 release/。测试版和正式版用 ./linux-tfm-tool/flash_stm32h573.sh"
            ;;
        release|RELEASE)
            shift
            ;;
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
        -h|--help|help)
            usage
            exit 0
            ;;
        --dir|-d)
            # 显式指定外部目录，下一个参数是路径。
            accept_image_dir "${2:-}"
            shift 2
            ;;
        *)
            if [[ -d "$1" ]]; then
                accept_image_dir "$1"
                shift
            elif [[ "$1" == */* || "$1" == *\\* ]]; then
                die "镜像目录不存在: $1"
            elif [[ "$1" =~ ^[0-9A-Fa-f]+$ ]]; then
                [[ -z "${SN}" ]] || die "ST-LINK 序列号重复: $1"
                SN="$1"
                shift
            else
                usage >&2
                die "未知参数: $1"
            fi
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
CONNECT_HP="-c port=SWD ap=1 ${sn_option} mode=HotPlug"
# 回归擦除用 Under Reset。芯片在复位循环里时 HotPlug 连不上。
CONNECT_UR="-c port=SWD ap=1 ${sn_option} mode=UR"

chip_info="$("${CLI}" ${CONNECT_HP} 2>&1 || true)"
if printf '%s\n' "${chip_info}" | grep -E -q '0x47A|STM32H5F4'; then
    CHIP_H5F4=1
    echo ">>> 芯片 STM32H5F4，写保护用 WRPSG11"
else
    CHIP_H5F4=0
    echo ">>> 芯片按 H573，写保护用 WRPSGn1"
fi

# 发布版全片回归。步骤与 stm32h573i_dk/regression.sh 相同，写在本脚本里。
# 单独拷走时不再依赖仓库里的那份文件。SECWM END 保持 127，不能写成 255。
run_regression() {
    echo "=== 回归（写 option bytes，会全片擦除）==="
    echo "镜像目录: ${IMG_DIR}"
    if [[ -n "${SN}" ]]; then
        echo "ST-LINK SN=${SN}"
    fi
    echo ">>> PRODUCT_STATE=0xED  TZEN=0xB4"
    "${CLI}" ${CONNECT_UR} -ob PRODUCT_STATE=0xED TZEN=0xB4
    # H5F4 写保护名是 WRPSG11/12、WRPSG21/22。H573 是 WRPSGn1/WRPSGn2。
    if [[ "${CHIP_H5F4}" == "1" ]]; then
        echo ">>> 去掉 bank1 保护并整片擦除"
        "${CLI}" ${CONNECT_UR} -ob SECWM1_STRT=127 SECWM1_END=0 WRPSG11=0xffffffff WRPSG12=0xffffffff -e all
        echo ">>> 去掉 bank2 保护并整片擦除"
        "${CLI}" ${CONNECT_UR} -ob SECWM2_STRT=127 SECWM2_END=0 WRPSG21=0xffffffff WRPSG22=0xffffffff -e all
    else
        echo ">>> 去掉 bank1 保护并整片擦除"
        "${CLI}" ${CONNECT_UR} -ob SECWM1_STRT=127 SECWM1_END=0 WRPSGn1=0xffffffff -e all
        echo ">>> 去掉 bank2 保护并整片擦除"
        "${CLI}" ${CONNECT_UR} -ob SECWM2_STRT=127 SECWM2_END=0 WRPSGn2=0xffffffff -e all
    fi
    echo ">>> 关掉 HDP"
    "${CLI}" ${CONNECT_HP} -ob HDP1_END=0 HDP2_END=0
    echo ">>> SECBOOTADD=0xC0100，两 bank 水印先写成整片 Secure"
    "${CLI}" ${CONNECT_HP} -ob SECBOOTADD=0xC0100 HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0 SWAP_BANK=0 SRAM2_RST=0 SRAM2_ECC=0
    "${CLI}" ${CONNECT_HP} -ob SECWM2_STRT=0 SECWM2_END=127 SECWM1_STRT=0 SECWM1_END=127
    echo ">>> BOOT_UBE=0xB4（OEM-iRoT）"
    "${CLI}" ${CONNECT_HP} -ob BOOT_UBE=0xB4 || true
}

readback_magic() {
    local addr="$1"
    local label="$2"
    local tmp
    tmp="$(mktemp --suffix=.bin)"
    echo ">>> 回读 ${label} @ ${addr}（检查 MCUboot magic）"
    "${CLI}" ${CONNECT_HP} -u "${addr}" 0x10 "${tmp}" \
        || die "回读 ${label} 失败（可能没烧上，或 AP/连接不对）"
    local got
    got="$(od -An -tx1 -N4 "${tmp}" | tr -d ' \n')"
    rm -f "${tmp}"
    echo "    片上前 4 字节: ${got}"
    if [[ "${got}" != "${MAGIC_BYTES}" ]]; then
        die "${label} 没有 MCUboot magic（期望 ${MAGIC_BYTES}）。烧录未成功，请检查上方 Download/Verify 是否 OK。"
    fi
    echo "    ${label} magic OK"
}

run_download() {
    require_release_bl2
    echo "=== 发布版下载 BL2 + SPE + NS ==="
    echo "目录 ${IMG_DIR}"
    echo "S    ${SLOT_S}  <- ${S_BIN}"
    echo "NS   ${SLOT_NS}  <- ${NS_BIN}"
    if [[ -f "${BL2_HEX}" ]]; then
        echo "BL2  (hex 内含地址) <- ${BL2_HEX}"
    else
        echo "BL2  ${BOOT_ADDR}  <- ${BL2_BIN}"
    fi
    if [[ -n "${SN}" ]]; then
        echo "ST-LINK SN=${SN}"
    fi

    echo ">>> BOOT_UBE=0xB4"
    "${CLI}" ${CONNECT_HP} -ob BOOT_UBE=0xB4 || true

    echo ">>> Write Secure（须出现 Download verified successfully）"
    "${CLI}" ${CONNECT_HP} -d "${S_BIN}" ${SLOT_S} -v
    echo ">>> Write Non-Secure"
    "${CLI}" ${CONNECT_HP} -d "${NS_BIN}" ${SLOT_NS} -v
    echo ">>> Write BL2"
    if [[ -f "${BL2_HEX}" ]]; then
        "${CLI}" ${CONNECT_HP} -d "${BL2_HEX}" -v
    else
        "${CLI}" ${CONNECT_HP} -d "${BL2_BIN}" ${BOOT_ADDR} -v
    fi

    readback_magic "${SLOT_S}" "Secure primary"
    readback_magic "${SLOT_NS}" "Non-Secure primary"

    if [[ "${CHIP_H5F4}" == "1" ]]; then
        echo ">>> H5F4 SECWM2=[127,0] WRPSG11=0xFFFFFFE3（保护组 0x1C） HDP1=[0,19]"
        "${CLI}" ${CONNECT_HP} -ob SECWM2_STRT=127 SECWM2_END=0 WRPSG11=0xFFFFFFE3 HDP1_STRT=0 HDP1_END=19
    else
        echo ">>> SECWM2=[127,0] WRPSGn1=0xFFFFFFE3（保护组 0x1C） HDP1=[0,19]"
        "${CLI}" ${CONNECT_HP} -ob SECWM2_STRT=127 SECWM2_END=0 WRPSGn1=0xFFFFFFE3 HDP1_STRT=0 HDP1_END=19
    fi

    # 发布版期望 enable。0xC3 是 disable。
    # 不写 PRODUCT_STATE=0x72。Closed 会关掉 SWD。
    # H573 的安全侧锁是 SECBOOT_LOCK。BOOT_LOCK 这个名字不会改它。
    # TrustZone 关掉时 SECBOOT_LOCK 读回 0x0，0xB4 写不进去。先打开 TZEN。
    echo ">>> SECBOOT_LOCK=${BOOT_LOCK_ENABLE}（enable；不是 0xC3）"
    if ! "${CLI}" ${CONNECT_HP} -ob SECBOOT_LOCK=${BOOT_LOCK_ENABLE}; then
        echo ">>> 启动锁没写上。先写 TZEN=0xB4 再锁"
        "${CLI}" ${CONNECT_HP} -ob TZEN=0xB4
        "${CLI}" ${CONNECT_HP} -ob SECBOOT_LOCK=${BOOT_LOCK_ENABLE}
    fi

    echo "download Done"
    echo
    echo "发布版 BL2 在 Open（0xED）上会复位，直到 PRODUCT_STATE 写成 0x72。"
    echo "Boot lock 已是 0xB4。再烧 test/prod 时，flash_stm32h573.sh 会把 SECBOOT_LOCK 写回 0xC3。"
    echo "串口 USART10 PE3/PE2 115200 8N1。发布版日志是 ERROR，没有 INFO。烧完不复位。"
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
