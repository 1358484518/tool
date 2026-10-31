#!/usr/bin/env bash
# STM32H573 / H5F4 测试版、正式版烧录（Linux）
#
# 默认从本目录的 test/ 或 prod/ 读取，不读 build_s / build_ns。
# 多给一个目录时，改用那个外部目录，文件名不变。
#   ./linux-tfm-tool/flash_stm32h573.sh              # 交互选择 test 或 prod，然后回归并烧录
#   ./linux-tfm-tool/flash_stm32h573.sh test         # 回归后烧测试版
#   ./linux-tfm-tool/flash_stm32h573.sh prod download
#   ./linux-tfm-tool/flash_stm32h573.sh test regression [SN]
#   ./linux-tfm-tool/flash_stm32h573.sh test /path/to/bins
#   ./linux-tfm-tool/flash_stm32h573.sh prod --dir /path/to/bins
#
# 地址（安全别名）：BL2 0x0C00E000，S 0x0C044000，NS 0x0C100000（Bank2）
# 镜像写完后写 SECWM2=[127,0]、写保护 0xFFFFFFE3、HDP1=[0,19]。
# H573 的写保护名是 WRPSGn1，H5F4 是 WRPSG11。
# 先把 SECBOOT_LOCK 写成 0xC3（disable）。发布版留下的 0xB4 会让开发版 BL2 复位。
# 不写 PRODUCT_STATE=0x72。
# 发布版用 flash_stm32h573_release.sh。
#
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BOOT_ADDR=0xc00e000
SLOT_S=0xc044000
SLOT_NS=0xc100000
# MCUboot image magic little-endian 0x96f3b83d
MAGIC_BYTES="3db8f396"

die() { echo "错误: $*" >&2; exit 1; }

usage() {
    cat <<'EOF'
用法:
  ./linux-tfm-tool/flash_stm32h573.sh                         # 交互选择 test 或 prod，然后回归并烧录
  ./linux-tfm-tool/flash_stm32h573.sh test [all|download|regression] [目录] [SN]
  ./linux-tfm-tool/flash_stm32h573.sh prod [all|download|regression] [目录] [SN]
  ./linux-tfm-tool/flash_stm32h573.sh test --dir /path/to/bins
  ./linux-tfm-tool/flash_stm32h573.sh --help

镜像目录（由 ./buildtfm.sh 复制）:
  linux-tfm-tool/test/{bl2.bin,bl2.hex,tfm_s_signed.bin,tfm_ns_signed.bin}
  linux-tfm-tool/prod/  同上
不给目录时用上面的默认路径。给了目录就从那里读同样的文件名。

all        回归（全片擦除）后烧录，再写水印、写保护、隐藏保护
download   只烧镜像，然后写上述保护。不擦片
regression 只回归，不烧镜像。回归前先写 SECBOOT_LOCK=0xC3

发布版不要传给本脚本，用 ./linux-tfm-tool/flash_stm32h573_release.sh。
SECBOOT_LOCK 写成 0xC3。不写 PRODUCT_STATE=0x72。烧完不复位。
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

# 收下外部镜像目录。不传则调用方继续用脚本旁边的默认目录。
# 目录里的文件名与默认目录相同：bl2.bin、tfm_s_signed.bin、tfm_ns_signed.bin。
accept_image_dir() {
    local path="$1"
    [[ -n "${path}" ]] || die "请在 --dir 后面写镜像目录"
    [[ -d "${path}" ]] || die "镜像目录不存在: ${path}"
    [[ -z "${IMG_DIR_EXT}" ]] || die "镜像目录重复: ${path}"
    IMG_DIR_EXT="$(cd "${path}" && pwd)"
}

prompt_image() {
    local choice
    echo "选择要烧录的镜像:"
    echo "  1) test    测试版"
    echo "  2) prod    正式版"
    echo "发布版请用 ./linux-tfm-tool/flash_stm32h573_release.sh"
    read -r -p "输入 1 或 2: " choice
    case "${choice}" in
        1|test) IMAGE="test" ;;
        2|prod) IMAGE="prod" ;;
        *) die "请输入 1 或 2" ;;
    esac
}

# 决定烧哪一份 BL2，并把即将烧入的内容解成字节放到 BL2_SCAN。
# 有 bl2.hex 就烧 hex；没有 hex 才烧 bl2.bin。两份都没有就退出。
# 版本标记只搜 BL2_SCAN，避免 bin 和 hex 不是同一次编译时认错版本。
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

require_images() {
    [[ -f "${S_BIN}" ]] || die "缺少 ${S_BIN}。请先 ./buildtfm.sh ${IMAGE}"
    [[ -f "${NS_BIN}" ]] || die "缺少 ${NS_BIN}。请先 ./buildtfm.sh ${IMAGE}"
    choose_bl2
    if ! file_contains "${BL2_SCAN}" "set wrp1"; then
        die "${BL2_LABEL} 没有 set wrp1。这不是测试版或正式版（开发模式）BL2。发布版请用 flash_stm32h573_release.sh"
    fi
    if [[ "${IMAGE}" == "test" ]]; then
        if ! file_contains "${BL2_SCAN}" "Starting bootloader"; then
            die "${BL2_LABEL} 没有 Starting bootloader。测试版 INFO 日志应包含这串字，这份更像正式版"
        fi
    else
        if file_contains "${BL2_SCAN}" "Starting bootloader"; then
            die "${BL2_LABEL} 含 Starting bootloader。正式版是 ERROR 日志，不应有这串字。请把测试版放在 test/ "
        fi
    fi
    if [[ "${BL2_KIND}" == "hex" ]]; then
        rm -f "${BL2_SCAN}"
    fi
}

IMAGE=""
MODE="all"
SN=""
# 外部镜像目录。空表示用脚本旁边的 test/ 或 prod/。
IMG_DIR_EXT=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        test|prod)
            IMAGE="$1"
            shift
            ;;
        release|RELEASE)
            die "发布版请用 ./linux-tfm-tool/flash_stm32h573_release.sh"
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

if [[ -z "${IMAGE}" ]]; then
    [[ -t 0 ]] || die "请指定 test 或 prod。非交互时: ./linux-tfm-tool/flash_stm32h573.sh test"
    prompt_image
fi

# 指定了外部目录就用它，否则用脚本旁边的 test/ 或 prod/。
if [[ -n "${IMG_DIR_EXT}" ]]; then
    IMG_DIR="${IMG_DIR_EXT}"
else
    IMG_DIR="${SCRIPT_DIR}/${IMAGE}"
fi
BL2_BIN="${IMG_DIR}/bl2.bin"
BL2_HEX="${IMG_DIR}/bl2.hex"
S_BIN="${IMG_DIR}/tfm_s_signed.bin"
NS_BIN="${IMG_DIR}/tfm_ns_signed.bin"

sn_option=""
[[ -n "${SN}" ]] && sn_option="sn=${SN}"

CLI="$(find_stm32_cli)" || die "找不到 STM32_Programmer_CLI（请安装 STM32CubeProgrammer 并加入 PATH）"
# H5 + TZEN 后烧录用 HotPlug + AP=1。烧完不复位，避免核跑起来后影响紧接着的 OBK 烧录。
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

# 给 test/prod 解开启动锁。H573 和 H5F4 都用。
# 0xC3 是关闭。锁着时 BOOT_UBE 和 SECBOOTADD 改不动。
# TrustZone 关掉时这项读回 0x0，写成 0xC3 不会生效，所以先打开 TZEN 再写。
unlock_boot() {
    echo ">>> SECBOOT_LOCK=0xC3（disable，开发版 BL2 需要这个值）"
    if "${CLI}" ${CONNECT_HP} -ob SECBOOT_LOCK=0xC3; then
        return 0
    fi
    echo ">>> SECBOOT_LOCK 读回不是 0xC3。TrustZone 关着时这项是 0x0，先写 TZEN=0xB4 再试"
    "${CLI}" ${CONNECT_HP} -ob TZEN=0xB4
    "${CLI}" ${CONNECT_HP} -ob SECBOOT_LOCK=0xC3
}

# 全片回归，给 test/prod 用。步骤与 stm32h573i_dk/regression.sh 相同，写在本脚本里。
# 单独拷走 linux-tfm-tool 时不再依赖仓库里的那份文件。
# SECWM END 保持 127。H5F4 一个 bank 是 2 MB，NS 在 0x0C100000，不能写成 255。
run_regression() {
    unlock_boot
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
    require_images
    unlock_boot
    echo "=== 下载 ${IMAGE}  BL2 + SPE + NS ==="
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

    # 镜像写完再锁。寄存器里 0 表示保护，BL2 核对的保护组是 0x1C，
    # 取反后是 0xFFFFFFE3。HDP/WRP 盖住 BL2，不能放到烧录之前。
    if [[ "${CHIP_H5F4}" == "1" ]]; then
        echo ">>> H5F4 SECWM2=[127,0] WRPSG11=0xFFFFFFE3（保护组 0x1C） HDP1=[0,19]"
        "${CLI}" ${CONNECT_HP} -ob SECWM2_STRT=127 SECWM2_END=0 WRPSG11=0xFFFFFFE3 HDP1_STRT=0 HDP1_END=19
    else
        echo ">>> SECWM2=[127,0] WRPSGn1=0xFFFFFFE3（保护组 0x1C） HDP1=[0,19]"
        "${CLI}" ${CONNECT_HP} -ob SECWM2_STRT=127 SECWM2_END=0 WRPSGn1=0xFFFFFFE3 HDP1_STRT=0 HDP1_END=19
    fi

    echo "download Done"
    echo "串口 USART10 PE3/PE2 115200 8N1。SECBOOT_LOCK=0xC3。未写 PRODUCT_STATE=0x72。烧完不复位。"
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
