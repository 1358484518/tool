#!/usr/bin/env bash
# 正式签名入口：强制远程（内网签名服务）、默认用 PROD_PROFILE、拒绝 --local。
# 只能手工执行，不要接进构建流程：./sign_prod.sh <未签名 bin>
# 正式私钥只在服务端，本地只持公钥；令牌不要写进仓库。
set -u
KIT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[[ -f "$KIT/sign.sh" ]] || { echo "错误: 找不到 $KIT/sign.sh" >&2; exit 1; }
exec bash "$KIT/sign.sh" --prod "$@"
