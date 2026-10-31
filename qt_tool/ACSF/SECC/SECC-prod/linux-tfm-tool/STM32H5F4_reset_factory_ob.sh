#!/usr/bin/env bash
# 把当前连接的 STM32H5F4 选项字节写成出厂默认值。
# 对应 windows-tfm-tools/STM32H5F4_reset_factory_ob.bat，选项字节数值与那份 bat 相同。
#
#   ./linux-tfm-tool/STM32H5F4_reset_factory_ob.sh [SN]
#
# 产品状态保持 Open（0xED）。不写 Closed，不写 Locked。
# 不把 SECBOOT_LOCK 写进出厂值。TrustZone 关掉后这项读出来是 0x0，
# 命令行不接受 SECBOOT_LOCK=0x0。
# 连接是 SWD、Under Reset、AP=1。TrustZone 还开着时不带 AP=1 连不上。
# 写出厂值最多试 3 次。三次都失败则不擦 Flash。写成功后再整片擦除。
#
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

# 打印错误并退出。给本脚本各步失败时用。
die() { echo "错误: $*" >&2; exit 1; }

# 打印用法。无参数时直接复位当前这一块板。
usage() {
    cat <<'EOF'
用法:
  ./linux-tfm-tool/STM32H5F4_reset_factory_ob.sh [SN]

把 STM32H5F4 选项字节写成出厂值。PRODUCT_STATE 保持 0xED。
TrustZone 关掉（TZEN=0xC3）。不写 SECBOOT_LOCK。
写出厂值最多 3 次。失败后先解开启动锁和隐藏保护再试，这几步不擦 Flash。
三次都失败则退出，不擦除。写成功后回读选项字节，再整片擦除用户 Flash。
Bank2 水印若已经被收成空安全区，硬件不允许再加大，这项失败时其余值仍然保留。
EOF
}

# 找 STM32_Programmer_CLI。本机装在用户目录或 /usr/local、/opt 时都能用。
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

SN=""
case "${1:-}" in
    "" ) ;;
    -h|--help|help)
        usage
        exit 0
        ;;
    *)
        SN="$1"
        ;;
esac

sn_option=""
[[ -n "${SN}" ]] && sn_option="sn=${SN}"
CLI="$(find_stm32_cli)" || die "找不到 STM32_Programmer_CLI"
CONNECT="-c port=SWD ap=1 ${sn_option} mode=UR"

# 出厂值，不含 SECWM2，也不含 SECBOOT_LOCK。与 bat 的 :write_factory 相同。
FACTORY_OB=(
    PRODUCT_STATE=0xED
    BOR_LEV=0x0
    BORH_EN=0x0
    IO_VDD_HSLV=0x0
    IO_VDDIO2_HSLV=0x0
    IWDG_STOP=0x1
    IWDG_STDBY=0x1
    IWDG_SW=0x1
    BOOT_UBE=0xB4
    SWAP_BANK=0x0
    NRST_STOP=0x1
    NRST_STDBY=0x1
    TZEN=0xC3
    USBPD_DIS=0x1
    SRAM2_ECC=0x1
    SRAM3_ECC=0x1
    BKPRAM_ECC=0x1
    SRAM2_RST=0x1
    SRAM1_3_4_5_RST=0x1
    NSBOOTADD=0x80000
    NSBOOT_LOCK=0xC3
    SECBOOTADD=0x0
    LOCKBL=0x0
    WRPSG12=0xFFFFFFFF
    WRPSG11=0xFFFFFFFF
    WRPSG22=0xFFFFFFFF
    WRPSG21=0xFFFFFFFF
    SECWM1_STRT=0x0
    SECWM1_END=0xFF
    EDATA1_EN=0x0
    EDATA1_STRT=0x0
    EDATA2_EN=0x0
    EDATA2_STRT=0x0
    HDP1_STRT=0x1
    HDP1_END=0x0
    HDP2_STRT=0x1
    HDP2_END=0x0
)

# 写出厂选项字节。Bank2 水印单独写，避免它失败时把整次编程判失败。
write_factory() {
    echo ">>> 写出厂选项字节（不含 SECWM2，不写 SECBOOT_LOCK）"
    "${CLI}" ${CONNECT} -ob "${FACTORY_OB[@]}"
}

# 一次写出厂值失败后调用。解开启动锁并关掉隐藏保护，给下一次写入用。
# 发布版 SECBOOT_LOCK=0xB4 时，BOOT_UBE 和 SECBOOTADD 被冻住。
# 必须在 TrustZone 仍开启时先写成 0xC3。关掉之后这项读出 0x0，再写 0xC3 会失败。
# 这里不擦 Flash。整片擦除只在出厂选项字节写成功之后做。
tz_unlock() {
    echo
    echo "这次没有写成。解开启动锁和隐藏保护后再试。"
    echo ">>> SECBOOT_LOCK=0xC3"
    "${CLI}" ${CONNECT} -ob SECBOOT_LOCK=0xC3 || true
    echo ">>> BOOT_UBE=0xC3 TZEN=0xC3"
    "${CLI}" ${CONNECT} -ob BOOT_UBE=0xC3 TZEN=0xC3 SRAM2_ECC=0x1 SRAM2_RST=0x1 || true
    echo ">>> 关掉写保护和隐藏保护"
    "${CLI}" ${CONNECT} -ob \
        WRPSG11=0xFFFFFFFF WRPSG12=0xFFFFFFFF WRPSG21=0xFFFFFFFF WRPSG22=0xFFFFFFFF \
        HDP1_STRT=0x1 HDP1_END=0 HDP2_STRT=0x1 HDP2_END=0 || true
    echo ">>> BOOT_UBE=0xB4"
    "${CLI}" ${CONNECT} -ob BOOT_UBE=0xB4 || true
}

# 出厂选项字节已经写上之后整片擦除用户 Flash。擦除失败由调用处退出。
erase_flash() {
    echo ">>> 选项字节已写成出厂值，开始整片擦除"
    "${CLI}" ${CONNECT} -e all
}

# 恢复 Bank2 水印。已被收成空安全区时硬件不允许再加大，失败由调用处单独处理。
write_secwm2() {
    echo ">>> SECWM2_STRT=0x0 SECWM2_END=0xFF"
    "${CLI}" ${CONNECT} -ob SECWM2_STRT=0x0 SECWM2_END=0xFF
}

echo "正在通过 SWD 写入 STM32H5F4 出厂选项字节..."
echo "产品状态保持 Open。不会写成 Closed 或 Locked。"
if [[ -n "${SN}" ]]; then
    echo "ST-LINK SN=${SN}"
fi

# 最多写 3 次。前一次失败只解锁，不擦除；三次都失败就退出。
factory_ok=0
attempt=1
while [[ "${attempt}" -le 3 ]]; do
    echo
    echo ">>> 第 ${attempt}/3 次写出厂选项字节"
    if write_factory; then
        factory_ok=1
        break
    fi
    if [[ "${attempt}" -lt 3 ]]; then
        tz_unlock
    fi
    attempt=$((attempt + 1))
done
[[ "${factory_ok}" -eq 1 ]] || die "三次写出厂选项字节都失败，未擦除 Flash"

wm_kept=0

echo
echo "尝试恢复 Bank2 水印 SECWM2..."
if ! write_secwm2; then
    wm_kept=1
    echo "Bank2 水印仍是 SECWM2_STRT=0xFF SECWM2_END=0x0。"
    echo "TrustZone 已关闭时，这块水印不生效，其余选项字节按出厂值写入。"
fi

echo
echo "回读选项字节..."
"${CLI}" ${CONNECT} -ob displ
echo
erase_flash || die "选项字节已写成出厂值，但整片擦除失败"
echo
if [[ "${wm_kept}" -eq 1 ]]; then
    echo "完成。Bank2 水印无法退回出厂编码。用户 Flash 已整片擦除。"
else
    echo "完成。用户 Flash 已整片擦除。"
fi
