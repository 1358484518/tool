#!/usr/bin/env bash
# Standalone MCUboot signer. Pass --chip h573 or --chip h5f4.
# h573: layout/h573, S 512 KB @ 0x0C044000, NS 1 MB @ 0x0C100000.
# h5f4: layout/h5f4, S 512 KB @ 0x0C078000, NS 1024 KB @ 0x0C0F8000.
# Uses a local venv (does not use the repo-root TF-M .venv).
#
# 三种签名模式：
#   local （默认）       ：用仓库内的开发私钥 keys/dev/<key_id>.pem 本地签名，不联网、不需要令牌；
#   remote（--remote）   ：私钥在内网签名服务上，本地只持公钥，摘要发往服务端换取 DER 签名；
#   prod  （sign_prod.sh）：正式出包，强制远程并拒绝 --local。
#
# 用法：
#   ./sign.sh tfm_ns.bin                      # 按文件名判类型，使用默认配置档
#   ./sign.sh --chip h573 --profile DEV-CCU sapp.bin
#   ./sign.sh --key-id OCPP-FW-CCU app.bin    # 直接指定 key_id（覆盖配置档映射）
#   ./sign.sh --dev sapp.bin                  # 强制本地开发密钥签名
#   ./sign.sh --local sapp.bin                # 同上（兼容旧写法）
#   ./sign.sh --remote sapp.bin               # 强制远程签名
#   ./sign.sh ns app.bin                      # 显式指定非安全侧
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

KIT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

CONF_FILE="${SIGN_CONF:-$KIT/remote.conf}"

usage() {
    cat <<'EOF'
把未签名的 S / NS 固件放到本目录，执行：

  ./sign.sh [选项] <文件名>

选项：
  --profile <名称>  选择配置档（remote.conf 中 PROFILE_<名称>_SPE / _NS 的映射）
                    缺省用 remote.conf 的 DEFAULT_PROFILE
  --key-id <ID>     直接指定本次签名的 key_id，覆盖配置档映射
  --dev             本地开发密钥签名（keys/dev/<key_id>.pem，默认方式，不联网）
  --local           同 --dev（兼容旧写法）
  --remote          改为在内网签名服务上签名
  --prod            正式签名：强制远程并拒绝 --local（由 sign_prod.sh 调用）
  --chip h573|h5f4  选择槽位布局和 config.h573 / config.h5f4
  --version X.Y.Z   本次镜像版本。不写则用配置文件里的版本
  --conf <路径>     使用指定的 remote.conf（默认 sign_kit/remote.conf）

示例：
  ./sign.sh tfm_ns.bin                  # 非安全
  ./sign.sh ns.bin
  ./sign.sh sapp.bin                    # 安全
  ./sign.sh --chip h573 --profile DEV-CCU sapp.bin
  ./sign.sh --key-id OCPP-FW-CCU app.bin
  ./sign.sh --remote sapp.bin
  ./sign.sh ns  app.bin                 # 文件名看不出类型时显式指定
  ./sign.sh s   app.bin

# 输出：<输入文件所在目录>/<文件名去扩展名>_signed.bin（remote.conf 里 OUT_DIR=kit 时写回本目录）

本地签名只用开发密钥，不需要令牌。
正式出包请用 ./sign_prod.sh —— 正式私钥只在服务端，需要令牌（不要写进仓库）：
  export TFM_SIGN_TOKEN='<向密钥管理员申请的令牌>'      # 或
  echo '<令牌>' > ~/.tfm_sign_token && chmod 600 ~/.tfm_sign_token
EOF
}

die() { echo "错误: $*" >&2; exit 1; }

# 读取 remote.conf 中的一个键（不使用 source：键名含 '-' 不是合法 shell 变量名）
# 去掉行尾 CR，CRLF 和 LF 读到的值相同。Windows 的 sign.bat 另用 UTF-8 解析，避免 GBK 把中文注释和下一行粘在一起。
conf_get() {
    local key="$1" file="${2:-$CONF_FILE}"
    [[ -f "$file" ]] || return 1
    sed -n "s/^[[:space:]]*${key}[[:space:]]*=\(.*\)$/\1/p" "$file" \
        | tail -n1 \
        | sed -e 's/[[:space:]]*#.*$//' -e 's/\r$//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//'
}

KIND=""
IN_NAME=""
PROFILE=""
KEY_ID_OVERRIDE=""
MODE_OVERRIDE=""
PROD=""
CHIP=""
VERSION_OVERRIDE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --profile|-p)
            [[ $# -ge 2 ]] || die "--profile 需要一个名称"
            PROFILE="$2"
            shift 2
            ;;
        --key-id)
            [[ $# -ge 2 ]] || die "--key-id 需要一个标识"
            KEY_ID_OVERRIDE="$2"
            shift 2
            ;;
        --conf)
            [[ $# -ge 2 ]] || die "--conf 需要一个路径"
            CONF_FILE="$2"
            shift 2
            ;;
        --remote)
            MODE_OVERRIDE="remote"
            shift
            ;;
        --local)
            MODE_OVERRIDE="local"
            shift
            ;;
        --dev)
            MODE_OVERRIDE="local"
            shift
            ;;
        --prod)
            MODE_OVERRIDE="remote"
            PROD=1
            shift
            ;;
        --chip)
            [[ $# -ge 2 ]] || die "--chip 需要 h573 或 h5f4"
            CHIP="$2"
            shift 2
            ;;
        --version)
            [[ $# -ge 2 ]] || die "--version 需要一个版本号，例如 2.3.1"
            VERSION_OVERRIDE="$2"
            shift 2
            ;;
        ns|NS|nspe|NSPE)
            KIND="ns"
            shift
            ;;
        s|S|sapp|SAPP|spe|SPE)
            KIND="s"
            shift
            ;;
        -*)
            die "未知选项 $1（用 --help 查看用法）"
            ;;
        *)
            IN_NAME="$1"
            shift
            break
            ;;
    esac
done

[[ $# -eq 0 ]] || die "多余参数: $*"
[[ -n "$IN_NAME" ]] || { usage >&2; exit 2; }

case "$(printf '%s' "$CHIP" | tr '[:upper:]' '[:lower:]')" in
    h573)
        CHIP="h573"
        SLOT_S="S   512KB @ 0x0C044000"
        SLOT_NS="NS  1024KB @ 0x0C100000"
        ;;
    h5f4)
        CHIP="h5f4"
        SLOT_S="S   512KB @ 0x0C078000"
        SLOT_NS="NS  1024KB @ 0x0C0F8000"
        ;;
    *)
        die "请指定 --chip h573 或 --chip h5f4"
        ;;
esac
# shellcheck disable=SC1090
source "$KIT/config.$CHIP"

resolve_input() {
    local name="$1"
    if [[ -f "$name" ]]; then
        printf '%s' "$(cd "$(dirname "$name")" && pwd)/$(basename "$name")"
        return
    fi
    if [[ -f "$KIT/$name" ]]; then
        printf '%s' "$KIT/$name"
        return
    fi
    return 1
}

IN_BIN=""
if IN_BIN="$(resolve_input "$IN_NAME")"; then
    :
else
    die "找不到文件: $IN_NAME（请放到 $KIT 或给出路径）"
fi

base="$(basename "$IN_BIN")"
base_lc="$(printf '%s' "$base" | tr '[:upper:]' '[:lower:]')"

if [[ -z "$KIND" ]]; then
    if [[ "$base_lc" == *ns* ]]; then
        KIND="ns"
    elif [[ "$base_lc" == *sapp* || "$base_lc" == *tfm_s* || "$base_lc" == *_s.bin || "$base_lc" == *_s_* ]]; then
        KIND="s"
    else
        die "无法从文件名判断是 NS 还是 S，请用: ./sign.sh ns $base  或  ./sign.sh s $base"
    fi
fi

stem="${base%.*}"
# 产物输出目录：默认与输入同目录；OUT_DIR=kit 时写回本 sign_kit 目录（CubeIDE 工程沿用该约定）
out_dir="$(conf_get OUT_DIR || true)"
case "$out_dir" in
    [Kk][Ii][Tt]) OUT_BIN="$KIT/${stem}_signed.bin" ;;
    *)            OUT_BIN="$(dirname "$IN_BIN")/${stem}_signed.bin" ;;
esac

# --------------------------------------------------------------------------- #
# 签名模式与 key_id
# --------------------------------------------------------------------------- #
MODE="${MODE_OVERRIDE:-$(conf_get SIGN_MODE || true)}"
if [[ -z "$MODE" ]]; then
    if [[ -f "$CONF_FILE" ]]; then
        MODE="remote"
    else
        MODE="local"
    fi
fi

# --prod 只允许远程：正式私钥不在本地
if [[ -n "$PROD" && "$MODE" == "local" ]]; then
    die "--prod 始终走内网签名服务，拒绝 --local
  正式私钥只存在于服务端；开发镜像请不要同时加这两个选项。"
fi

KEY_ID=""
KEY_ARGS=()
if [[ "$MODE" == "remote" ]]; then
    [[ -f "$CONF_FILE" ]] || die "SIGN_MODE=remote 但找不到配置 $CONF_FILE
  该文件随仓库提供（含团队共享的 key_id 映射）；若缺失请从版本库恢复。"

    SERVER_URL="${TFM_SIGN_SERVER_URL:-$(conf_get SIGN_SERVER_URL || true)}"
    [[ -n "$SERVER_URL" ]] || die "未配置签名服务地址
  任选一种方式提供（推荐前者，各人本地设置即可）：
    1) export TFM_SIGN_SERVER_URL='https://<内网主机>:8443'
    2) 在 $CONF_FILE 中填写 SIGN_SERVER_URL=https://<内网主机>:8443"

    if [[ -n "$KEY_ID_OVERRIDE" ]]; then
        KEY_ID="$KEY_ID_OVERRIDE"
    else
        if [[ -z "$PROFILE" ]]; then
            if [[ -n "$PROD" ]]; then
                PROFILE="$(conf_get PROD_PROFILE || true)"
                [[ -n "$PROFILE" ]] || die "正式签名但 $CONF_FILE 未配置 PROD_PROFILE
  请在该文件补上 PROD_PROFILE=<正式配置档>，或用 --profile 显式指定"
            else
                PROFILE="$(conf_get DEFAULT_PROFILE || true)"
            fi
        fi
        [[ -n "$PROFILE" ]] || die "未指定配置档
  请用 --profile <名称>，或在 $CONF_FILE 设置 DEFAULT_PROFILE，或用 --key-id 直接指定"

        if [[ "$KIND" == "ns" ]]; then
            KEY_ID="$(conf_get "PROFILE_${PROFILE}_NS" || true)"
            missing_hint="PROFILE_${PROFILE}_NS"
        else
            KEY_ID="$(conf_get "PROFILE_${PROFILE}_SPE" || true)"
            missing_hint="PROFILE_${PROFILE}_SPE"
        fi
        [[ -n "$KEY_ID" ]] || die "配置档 '$PROFILE' 缺少 ${KIND^^} 侧的 key_id 映射
  请在 $CONF_FILE 中补上：${missing_hint}=<key_id>
  注意：脚本不会按 '<名称>-SPE' 自动推断 key_id（防止签错密钥）"
    fi

    PUBKEY="$KIT/keys/${KEY_ID}.pem"
    [[ -f "$PUBKEY" ]] || die "缺少 key_id '$KEY_ID' 的本地公钥: $PUBKEY
  请导出后提交到仓库（签名前会用它做指纹强校验）：
    python3 $KIT/scripts/fetch_pubkeys.py --key-id $KEY_ID
  若服务端没有该密钥，请先由密钥管理员导入（密钥对在外部受控环境生成）。"

    KEY_ARGS=(--key "$PUBKEY" --remote-sign "$CONF_FILE" --key-id "$KEY_ID")
else
    # 本地开发签名：keys/dev/<key_id>.pem 随仓库提交；不联网、不需要令牌
    if [[ -n "$KEY_ID_OVERRIDE" ]]; then
        KEY_ID="$KEY_ID_OVERRIDE"
    else
        [[ -n "$PROFILE" ]] || PROFILE="$(conf_get DEFAULT_PROFILE || true)"
        [[ -n "$PROFILE" ]] || die "未指定配置档
  请用 --profile <名称>，或在 $CONF_FILE 设置 DEFAULT_PROFILE，或用 --key-id 直接指定"
        if [[ "$KIND" == "ns" ]]; then
            KEY_ID="$(conf_get "PROFILE_${PROFILE}_NS" || true)"
            missing_hint="PROFILE_${PROFILE}_NS"
        else
            KEY_ID="$(conf_get "PROFILE_${PROFILE}_SPE" || true)"
            missing_hint="PROFILE_${PROFILE}_SPE"
        fi
        [[ -n "$KEY_ID" ]] || die "配置档 '$PROFILE' 缺少 ${KIND^^} 侧的 key_id 映射
  请在 $CONF_FILE 中补上：${missing_hint}=<key_id>"
    fi
    PRIV_KEY="$KIT/keys/dev/${KEY_ID}.pem"
    [[ -f "$PRIV_KEY" ]] || PRIV_KEY="$KIT/keys/image_${KIND}_signing_private_key.pem"
    [[ -f "$PRIV_KEY" ]] || die "缺少 key_id '$KEY_ID' 的开发私钥
  期望：$KIT/keys/dev/${KEY_ID}.pem
  兼容：$KIT/keys/image_${KIND}_signing_private_key.pem
  正式私钥只在签名服务上；正式出包请用 ./sign_prod.sh。"
    echo "提示: 本地开发签名（key_id=$KEY_ID，不联网、不需要令牌）" >&2
    KEY_ARGS=(--key "$PRIV_KEY")
fi
if [[ "$KIND" == "ns" ]]; then
    LAYOUT="$KIT/layout/$CHIP/signing_layout_ns.o"
    VERSION="${MCUBOOT_IMAGE_VERSION_NS}"
    SEC_CNT="${MCUBOOT_SECURITY_COUNTER_NS}"
    DEP="(0, ${MCUBOOT_S_IMAGE_MIN_VER})"
    SLOT_HINT="$SLOT_NS"
else
    LAYOUT="$KIT/layout/$CHIP/signing_layout_s.o"
    VERSION="${MCUBOOT_IMAGE_VERSION_S}"
    SEC_CNT="${MCUBOOT_SECURITY_COUNTER_S}"
    DEP="(1, ${MCUBOOT_NS_IMAGE_MIN_VER})"
    SLOT_HINT="$SLOT_S"
fi
if [[ -n "$VERSION_OVERRIDE" ]]; then
    VERSION="$VERSION_OVERRIDE"
fi

for f in "$LAYOUT" "$KIT/scripts/wrapper.py" "$KIT/scripts/remote_sign.py" "$KIT/bl2/macro_parser.py"; do
    [[ -f "$f" ]] || die "缺少签名文件: $f"
done

# 远程模式：提前检查令牌，给出比 Python 栈更友好的提示
if [[ "$MODE" == "remote" ]]; then
    TOKEN_ENV_NAME="$(conf_get TOKEN_ENV || true)"
    TOKEN_ENV_NAME="${TOKEN_ENV_NAME:-TFM_SIGN_TOKEN}"
    if [[ -z "${!TOKEN_ENV_NAME:-}" ]]; then
        TOKEN_FILE_PATH="$(conf_get TOKEN_FILE || true)"
        TOKEN_FILE_PATH="${TOKEN_FILE_PATH:-$HOME/.tfm_sign_token}"
        TOKEN_FILE_PATH="${TOKEN_FILE_PATH/#\~/$HOME}"
        if [[ ! -s "$TOKEN_FILE_PATH" ]]; then
            die "未找到签名令牌
  任选一种方式提供（令牌请勿写进仓库）：
    export ${TOKEN_ENV_NAME}='<令牌>'              # 或
    echo '<令牌>' > $TOKEN_FILE_PATH && chmod 600 $TOKEN_FILE_PATH"
        fi
    fi
fi

py_ok() {
    local py="$1"
    [[ -x "$py" ]] || command -v "$py" >/dev/null 2>&1 || return 1
    "$py" -c "import click, cryptography, cbor2, intelhex" 2>/dev/null
}

ensure_python() {
    local cands=() p
    # Do not use the repo-root TF-M .venv (cryptography ABI mismatch).
    if [[ -n "${PYTHON:-}" ]]; then
        cands+=("$PYTHON")
    fi
    cands+=(
        "$KIT/.venv/bin/python"
        "$KIT/../.sign-venv/bin/python"
    )
    for p in "${cands[@]}"; do
        if py_ok "$p"; then
            if command -v "$p" >/dev/null 2>&1 && [[ ! -x "$p" ]]; then
                PYTHON="$(command -v "$p")"
            else
                PYTHON="$p"
            fi
            return 0
        fi
    done

    echo "imgtool: creating $KIT/.venv (not using a broken TF-M .venv)" >&2
    # 优先用调用者指定的解释器（PYTHON=/path/to/python3），否则退回 PATH 上的 python3
    local venv_py="${PYTHON:-}"
    if [[ "$venv_py" == *"/.venv/"* ]]; then
        venv_py=""
    fi
    if [[ -z "$venv_py" ]]; then
        command -v python3 >/dev/null 2>&1 || die "找不到 python3。请先安装 Python（≥3.10）：
  Debian/Ubuntu:  sudo apt install -y python3 python3-venv
  或显式指定解释器：PYTHON=/path/to/python3 $0 ..."
        venv_py="python3"
    fi
    "$venv_py" -VV >&2
    if ! "$venv_py" -m venv "$KIT/.venv"; then
        die "创建 ${KIT}/.venv 失败。Debian/Ubuntu 请先执行：
  sudo apt install -y python3-venv

注意：Ubuntu 24.04 起（含 26.04）系统 Python 默认不装 pip，且受 PEP 668 保护，
**不要**执行 python3 -m pip install -r requirements.txt（会报 No module named pip）。
依赖一律装在本目录的 .venv 内。"
    fi
    "$KIT/.venv/bin/python" -m pip install -q --upgrade pip || true
    if ! "$KIT/.venv/bin/python" -m pip install -q -r "$KIT/requirements.txt"; then
        die "依赖安装失败（$KIT/requirements.txt）。可手动重试：
  $KIT/.venv/bin/python -m pip install -r $KIT/requirements.txt
若 Python 3.14 下缺少 wheel（cryptography 需 Rust 源码编译），可改用 Python 3.12
重新创建 venv，或用 PYTHON=<解释器路径> 指定已有的可用解释器。"
    fi
    PYTHON="$KIT/.venv/bin/python"
    py_ok "$PYTHON" || die "Python 依赖校验失败（import click, cryptography, cbor2, intelhex）。
  请查看上方 pip 输出；可手动: $PYTHON -m pip install -r $KIT/requirements.txt"
}

ensure_python

if [[ "${MCUBOOT_HW_KEY}" == "ON" ]]; then
    PUB_FMT="full"
else
    PUB_FMT="hash"
fi

ARGS=(
    --version "$VERSION"
    --layout "$LAYOUT"
    --public-key-format "$PUB_FMT"
    --align "$MCUBOOT_ALIGN_VAL"
    --pad
    --pad-header
    -H "$BL2_HEADER_SIZE"
    -s "$SEC_CNT"
    -L "$MCUBOOT_ENC_KEY_LEN"
    -d "$DEP"
)
ARGS+=("${KEY_ARGS[@]}")

if [[ "${MCUBOOT_UPGRADE_STRATEGY}" == "OVERWRITE_ONLY" ]]; then
    ARGS+=(--overwrite-only)
fi
if [[ "${MCUBOOT_CONFIRM_IMAGE}" == "ON" ]]; then
    ARGS+=(--confirm)
fi
if [[ "${MCUBOOT_MEASURED_BOOT}" == "ON" ]]; then
    ARGS+=(--measured-boot-record)
fi
if [[ "${MCUBOOT_ENC_IMAGES}" == "ON" ]]; then
    ENC_KEY="$KIT/keys/image_enc_${KIND}_key.pem"
    [[ -f "$ENC_KEY" ]] || die "已打开加密但找不到密钥: $ENC_KEY"
    ARGS+=(-E "$ENC_KEY")
fi

ARGS+=("$IN_BIN" "$OUT_BIN")

if [[ "$MODE" == "remote" ]]; then
    if [[ -n "$PROD" ]]; then
        echo "签名 ${KIND^^} 镜像  ($SLOT_HINT)  [正式签名 / 远程]"
    else
        echo "签名 ${KIND^^} 镜像  ($SLOT_HINT)  [远程签名]"
    fi
    echo "  服务    $SERVER_URL"
    echo "  key_id  $KEY_ID"
    [[ -n "$PROFILE" ]] && echo "  配置档  $PROFILE"
    [[ -n "$PROD" ]] && echo "  提示    正式私钥只在服务端，请勿泄露令牌"
    echo "  公钥    $PUBKEY"
else
    echo "签名 ${KIND^^} 镜像  ($SLOT_HINT)  [本地开发密钥]"
    echo "  key_id  $KEY_ID"
    [[ -n "$PROFILE" ]] && echo "  配置档  $PROFILE"
    echo "  私钥    $PRIV_KEY"
fi
echo "  输入  $IN_BIN"
echo "  输出  $OUT_BIN"
echo "  版本  $VERSION  header $BL2_HEADER_SIZE  align $MCUBOOT_ALIGN_VAL  计数器 $SEC_CNT"
echo "  python $PYTHON"

# cwd 必须是 scripts/，wrapper.py 才会优先用自带的 imgtool
cd "$KIT/scripts"
PYTHONPATH="$KIT${PYTHONPATH:+:$PYTHONPATH}" "$PYTHON" "$KIT/scripts/wrapper.py" "${ARGS[@]}"

ls -l "$OUT_BIN"
echo "完成: $OUT_BIN"
