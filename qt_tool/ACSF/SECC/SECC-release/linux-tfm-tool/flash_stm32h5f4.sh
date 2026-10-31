#!/usr/bin/env bash
# Flash STM32H5F4 test or prod from this directory.
#
#   ./linux-tfm-tool/flash_stm32h5f4.sh                 # 选择 test 或 prod
#   ./linux-tfm-tool/flash_stm32h5f4.sh test            # linux-tfm-tool/test
#   ./linux-tfm-tool/flash_stm32h5f4.sh prod            # linux-tfm-tool/prod
#   ./linux-tfm-tool/flash_stm32h5f4.sh test erase      # 先整片擦除再烧
#   ./linux-tfm-tool/flash_stm32h5f4.sh prod unlock-only
#
# 默认从本目录的 test/ 或 prod/ 读取，不读 build_s。
# 多给一个目录时，改用那个外部目录，文件名不变。
# 发布版用同目录的 flash_stm32h5f4_release.sh，它只读 release/。
# 脚本结束时不复位。不写 PRODUCT_STATE，也不写发布版 WRP/HDP/Boot lock。
#
# HDP can be enlarged but not shrunk. Old BL2 sets HDP1=[0, 0x13], which
# covers BL2 at 0x0C00E000. Mass-erase first, then program.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOOT_ADDR=0x0C00E000
SLOT_S=0x0C078000
SLOT_NS=0x0C0F8000
# First mismatch when HDP still covers BL2: start of sector 8 / SECBOOTADD.
BL2_VEC_ADDR=0x0C010000

die() { echo "错误: $*" >&2; exit 1; }

usage() {
    cat <<'EOF'
用法:
  ./linux-tfm-tool/flash_stm32h5f4.sh                 # 选择 test 或 prod
  ./linux-tfm-tool/flash_stm32h5f4.sh test            # 烧 linux-tfm-tool/test
  ./linux-tfm-tool/flash_stm32h5f4.sh prod            # 烧 linux-tfm-tool/prod
  ./linux-tfm-tool/flash_stm32h5f4.sh test erase      # 整片擦除后再烧
  ./linux-tfm-tool/flash_stm32h5f4.sh prod unlock-only
  ./linux-tfm-tool/flash_stm32h5f4.sh test /path/to/bins
  ./linux-tfm-tool/flash_stm32h5f4.sh prod --dir /path/to/bins

不给目录时用脚本旁边的 test/ 或 prod/。给了目录就从那里读同样的文件名。
镜像目录（./buildtfm.sh test 或 prod 会写入）:
  bl2.hex 或 bl2.bin，以及 tfm_s_signed.bin、tfm_ns_signed.bin
  有 bl2.hex 就烧 hex；没有 hex 才烧 bl2.bin。版本标记从实际烧入的那一份里取。

地址: BL2 0x0C00E000  S 0x0C078000  NS 0x0C0F8000
发布版请用 ./linux-tfm-tool/flash_stm32h5f4_release.sh（只读 release/）。
本脚本不写 PRODUCT_STATE=0x72，也不写发布版 WRP/HDP/Boot lock。
EOF
}

ob_hex_field() {
    local dump="$1"
    local name="$2"
    printf '%s\n' "${dump}" | sed -n "s/.*${name}[[:space:]]*:[[:space:]]*\(0x[0-9A-Fa-f]\+\).*/\1/p" | head -n 1
}

# HDP is disabled when start > end (ST encoding: STRT=1 END=0).
hdp1_disabled() {
    local dump="$1"
    local strt end
    strt="$(ob_hex_field "${dump}" "HDP1_STRT")"
    end="$(ob_hex_field "${dump}" "HDP1_END")"
    [[ -n "${strt}" && -n "${end}" ]] || return 1
    [[ $((strt)) -gt $((end)) ]]
}

# BL2 download erases sectors [7 23] (0x0C00E000, 128 KB).
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

if [[ "${1:-}" == "--self-test" ]]; then
    sample='     PRODUCT_STATE: 0xED (Open)
     WRPSG11      : 0xFFFFFFFF  (0x8000000)
     HDP1_STRT    : 0x0  (0x0)
     HDP1_END     : 0x13  (0x26000)
     HDP2_STRT    : 0x1  (0x2000)
     HDP2_END     : 0x0  (0x0)'
    hdp1_overlaps_bl2 "${sample}" || die "self-test: HDP [0, 0x13] must overlap BL2"
    hdp1_disabled "${sample}" && die "self-test: HDP [0, 0x13] must not look disabled"
    sample_off='     HDP1_STRT    : 0x1  (0x2000)
     HDP1_END     : 0x0  (0x0)'
    hdp1_disabled "${sample_off}" || die "self-test: HDP STRT>END must be disabled"
    hdp1_overlaps_bl2 "${sample_off}" && die "self-test: disabled HDP must not overlap BL2"
    echo "flash_stm32h5f4.sh self-test OK"
    exit 0
fi

IMAGE=""
MODE="unlock"
# 外部镜像目录。空表示用脚本旁边的 test/ 或 prod/。
IMG_DIR_EXT=""
# 收下外部镜像目录。不传则用默认目录。文件名仍是 bl2.bin、tfm_s_signed.bin、tfm_ns_signed.bin。
accept_image_dir() {
    local path="$1"
    [[ -n "${path}" ]] || die "请在 --dir 后面写镜像目录"
    [[ -d "${path}" ]] || die "镜像目录不存在: ${path}"
    [[ -z "${IMG_DIR_EXT}" ]] || die "镜像目录重复: ${path}"
    IMG_DIR_EXT="$(cd "${path}" && pwd)"
}
while [[ $# -gt 0 ]]; do
    case "$1" in
        test|prod)
            IMAGE="$1"
            ;;
        erase|unlock|unlock-only)
            MODE="$1"
            ;;
        -h|--help|help)
            usage
            exit 0
            ;;
        release)
            die "发布版请用 ./linux-tfm-tool/flash_stm32h5f4_release.sh，本脚本只烧 test 或 prod"
            ;;
        --dir|-d)
            # 显式指定外部目录。这里多移走路径，循环末尾再移走 --dir。
            accept_image_dir "${2:-}"
            shift
            ;;
        *)
            if [[ -d "$1" ]]; then
                accept_image_dir "$1"
            elif [[ "$1" == */* || "$1" == *\\* ]]; then
                die "镜像目录不存在: $1"
            else
                die "未知参数 $1。用法: $0 [test|prod] [erase|unlock-only] [目录]"
            fi
            ;;
    esac
    shift
done

if [[ -z "${IMAGE}" ]]; then
    [[ -t 0 ]] || die "请指定 test 或 prod"
    echo "请选择要烧录的镜像:"
    echo "  1) test  测试版  ${SCRIPT_DIR}/test"
    echo "  2) prod  正式版  ${SCRIPT_DIR}/prod"
    echo -n "输入 1 或 2: "
    read -r choice
    case "${choice}" in
        1|test) IMAGE="test" ;;
        2|prod) IMAGE="prod" ;;
        *) die "无效选择" ;;
    esac
fi

# 指定了外部目录就用它，否则用脚本旁边的 test/ 或 prod/。
if [[ -n "${IMG_DIR_EXT}" ]]; then
    IMG_DIR="${IMG_DIR_EXT}"
else
    IMG_DIR="${SCRIPT_DIR}/${IMAGE}"
fi
BL2_BIN="${IMG_DIR}/bl2.bin"
# hex 自带地址，并含 0x0C028000 的 OTP 和 0x0C02E000 的计数。
BL2_HEX="${IMG_DIR}/bl2.hex"
S_BIN="${IMG_DIR}/tfm_s_signed.bin"
NS_BIN="${IMG_DIR}/tfm_ns_signed.bin"
[[ -f "${S_BIN}" && -f "${NS_BIN}" ]] \
    || die "缺少 ${IMG_DIR}/{tfm_s_signed.bin,tfm_ns_signed.bin}。请先: ./buildtfm.sh ${IMAGE}"

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

choose_bl2
# 不要用 strings | grep -q：pipefail 下 strings 会 SIGPIPE，误报没有标记。
grep -a -F -q "H5F4BL2" "${BL2_SCAN}" \
    || die "${BL2_LABEL} 没有 H5F4BL2"
grep -a -F -q "H5F4SWP2" "${BL2_SCAN}" \
    || die "${BL2_LABEL} 没有 H5F4SWP2"
# 发布版才编进 WRP 检查。test/prod 是 TFM_DEV_MODE，没有这句。
if grep -a -F -q "Unexpected value for write protection" "${BL2_SCAN}"; then
    die "${BL2_LABEL} 带发布版写保护检查。请用 ./linux-tfm-tool/flash_stm32h5f4_release.sh"
fi
if [[ "${IMAGE}" == "test" ]]; then
    grep -a -F -q "Starting bootloader S-sec=" "${BL2_SCAN}" \
        || die "${BL2_LABEL} 没有 S-sec 标记，不是测试版。请先: ./buildtfm.sh test"
else
    if grep -a -F -q "Starting bootloader S-sec=" "${BL2_SCAN}"; then
        die "${BL2_LABEL} 含 S-sec 标记，这是测试版，不能当正式版烧"
    fi
fi
if [[ "${BL2_KIND}" == "hex" ]]; then
    rm -f "${BL2_SCAN}"
fi

echo "将要烧录的目录: ${IMG_DIR}"
# 有 bl2.hex 就烧 hex。没有才把 bl2.bin 按 0x0C00E000 整段写下。
if [[ -f "${BL2_HEX}" ]]; then
    echo "BL2  (hex 内含地址和 OTP) <- ${BL2_HEX}"
else
    echo "BL2 ${BOOT_ADDR}  <- ${BL2_BIN}"
fi
echo "S   ${SLOT_S}  <- ${S_BIN}"
echo "NS  ${SLOT_NS}  <- ${NS_BIN}"
echo
command -v STM32_Programmer_CLI >/dev/null 2>&1 \
    || die "找不到 STM32_Programmer_CLI"

CONNECT_UR="-c port=SWD ap=1 mode=UR"

read_ob_dump() {
    STM32_Programmer_CLI ${CONNECT_UR} -ob displ
}

unlock_wrp_hdp() {
    echo ">>> 关掉 HDP，并清 H5F4 的 WRPSG11（不是 H573 的 WRPSGn1）"
    # 分两次：H5 option-byte 名写错时整条 -ob 都会被丢掉
    STM32_Programmer_CLI ${CONNECT_UR} -ob HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0 || true
    STM32_Programmer_CLI ${CONNECT_UR} -ob WRPSG11=0xffffffff WRPSG12=0xffffffff WRPSG21=0xffffffff WRPSG22=0xffffffff
}

# 全片擦除写在脚本里，不再调用 stm32h5f4/regression.sh。
# H5F4 一个 bank 是 256 个扇区，SECWM END 用 255。全部 Under Reset，不走 HotPlug -ob。
mass_erase_old_bl2() {
    echo ">>> PRODUCT_STATE=0xED  TZEN=0xB4（关着 TrustZone 时 SECBOOT_LOCK 写不进去）"
    STM32_Programmer_CLI ${CONNECT_UR} -ob PRODUCT_STATE=0xED TZEN=0xB4
    echo ">>> SECBOOT_LOCK=0xC3（发布版若已写成 0xB4，测试/正式版 BL2 对不上会复位）"
    STM32_Programmer_CLI ${CONNECT_UR} -ob SECBOOT_LOCK=0xC3 || true
    echo ">>> 整片擦除旧 BL2（HDP 覆盖 BL2 时，只改 option bytes 缩不了 HDP）"
    echo "    这会清空用户 Flash；PRODUCT_STATE 保持 Open (0xED)。"
    echo ">>> 去掉 bank1 保护并整片擦除"
    STM32_Programmer_CLI ${CONNECT_UR} \
        -ob SECWM1_STRT=255 SECWM1_END=0 WRPSG11=0xffffffff WRPSG12=0xffffffff \
        -e all \
        || die "整片擦除失败"
    echo ">>> 去掉 bank2 保护并整片擦除"
    STM32_Programmer_CLI ${CONNECT_UR} \
        -ob SECWM2_STRT=255 SECWM2_END=0 WRPSG21=0xffffffff WRPSG22=0xffffffff \
        -e all \
        || die "整片擦除失败"
    echo ">>> SECBOOTADD=0xC0100，回归期间两 bank 水印先铺满 [0, 255]"
    STM32_Programmer_CLI ${CONNECT_UR} -ob SECBOOTADD=0xC0100 HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0 SWAP_BANK=0 SRAM2_RST=0 SRAM2_ECC=0
    STM32_Programmer_CLI ${CONNECT_UR} -ob SECWM1_STRT=0 SECWM1_END=255 SECWM2_STRT=0 SECWM2_END=255
    echo ">>> 擦除后再关 HDP（此时旧 BL2 已不在，不会把 HDP 锁回去）"
    STM32_Programmer_CLI ${CONNECT_UR} -ob HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0 || true
    STM32_Programmer_CLI ${CONNECT_UR} -ob WRPSG11=0xffffffff WRPSG12=0xffffffff WRPSG21=0xffffffff WRPSG22=0xffffffff || true
}

assert_bl2_region_erased() {
    local head got
    head="$(mktemp --suffix=.bin)"
    STM32_Programmer_CLI ${CONNECT_UR} -r ${BL2_VEC_ADDR} 16 "${head}" \
        || die "擦除后读 0x0C010000 失败"
    got="$(od -An -tx1 "${head}" | tr -d ' \n')"
    rm -f "${head}"
    if [[ "${got}" != ffffffffffffffffffffffffffffffff ]]; then
        echo "erase check bytes: ${got}"
        die "擦除后 0x0C010000 不是 0xFF，HDP 连 mass erase 都挡住了。请用 CubeProgrammer GUI: Option Bytes → Reset MCU to Factory Settings"
    fi
    echo ">>> 0x0C010000 已是 0xFF，旧 BL2 已被擦掉"
}

case "${MODE}" in
    erase)
        mass_erase_old_bl2
        echo ">>> BOOT_UBE=0xB4"
        STM32_Programmer_CLI ${CONNECT_UR} -ob BOOT_UBE=0xB4 || true
        ;;
    unlock|unlock-only)
        echo ">>> 先读 option bytes，确认是不是 HDP 而不是 WRP"
        OB_DUMP="$(read_ob_dump)"
        printf '%s\n' "${OB_DUMP}" | grep -E "WRP|HDP|PRODUCT|SECWM" || true
        if hdp1_overlaps_bl2 "${OB_DUMP}"; then
            echo
            echo ">>> HDP1 仍覆盖 BL2。S/NS 在 0x0C078000 之外；"
            echo "    BL2 在 0x0C00E000，落在 HDP1 [STRT, END] 里面。"
            if [[ "${MODE}" == "unlock-only" ]]; then
                die "HDP 仍覆盖 BL2。请改跑: ./linux-tfm-tool/flash_stm32h5f4.sh ${IMAGE} erase"
            fi
            mass_erase_old_bl2
        else
            echo ">>> 不解整片，只解锁 WRP/HDP"
            unlock_wrp_hdp
        fi
        echo ">>> BOOT_UBE=0xB4"
        STM32_Programmer_CLI ${CONNECT_UR} -ob BOOT_UBE=0xB4 || true
        ;;
    *)
        die "未知参数 ${MODE}"
        ;;
esac

OB_DUMP="$(read_ob_dump)"
printf '%s\n' "${OB_DUMP}" | grep -E "WRP|HDP|PRODUCT|SECWM" || true
if hdp1_overlaps_bl2 "${OB_DUMP}"; then
    echo "警告: 擦除/解锁后 HDP1 仍然覆盖 BL2。若 0x0C010000 已是 0xFF，仍可在 HDPL1 下写入。"
    assert_bl2_region_erased
else
    echo ">>> HDP1 已关闭（STRT > END）"
fi

echo ">>> 写入 Secure ${SLOT_S}"
STM32_Programmer_CLI ${CONNECT_UR} -d "${S_BIN}" ${SLOT_S} -v \
    || die "S 镜像下载失败"
echo ">>> 写入 Non-Secure ${SLOT_NS}"
STM32_Programmer_CLI ${CONNECT_UR} -d "${NS_BIN}" ${SLOT_NS} -v \
    || die "NS 镜像下载失败"
echo ">>> 写 BL2 前再关一次 HDP/WRP"
unlock_wrp_hdp
# 有 bl2.hex 就按文件内地址写，OTP（0x0C028000）和计数（0x0C02E000）一起下去。
if [[ -f "${BL2_HEX}" ]]; then
    echo ">>> 写入 BL2 hex ${BL2_HEX}"
    STM32_Programmer_CLI ${CONNECT_UR} -d "${BL2_HEX}" -v \
        || die "BL2 下载失败。若地址是 0x0C010000：这是 HDP，不是 WRP。请 ./linux-tfm-tool/flash_stm32h5f4.sh ${IMAGE} erase"
else
    echo ">>> 写入 BL2 ${BOOT_ADDR}"
    STM32_Programmer_CLI ${CONNECT_UR} -d "${BL2_BIN}" ${BOOT_ADDR} -v \
        || die "BL2 下载失败。若地址是 0x0C010000：这是 HDP，不是 WRP。请 ./linux-tfm-tool/flash_stm32h5f4.sh ${IMAGE} erase"
fi

echo ">>> 从芯片回读 BL2"
DUMP="$(mktemp --suffix=.bin)"
STM32_Programmer_CLI ${CONNECT_UR} -u ${BOOT_ADDR} 0x20000 "${DUMP}" \
    || die "回读 BL2 失败"
grep -a -F -q "H5F4BL2" "${DUMP}" \
    || die "片上 BL2 没有 H5F4BL2。写保护还在，bootloader 没换掉"
rm -f "${DUMP}"

echo
echo "片上已经是 ${IMAGE} 的 BL2。脚本不再复位，以便接着写 OBK。"
echo "复位后串口必须有 H5F4BL2。本脚本不写发布版 WRP/HDP/Boot lock。"
