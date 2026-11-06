#!/usr/bin/env python3
"""远程签名客户端：本地只持公钥，签名请求转发到内网签名服务。

为什么用"实例方法注入"而不是新建 key 子类
------------------------------------------
`imgtool.Image.create()` 已内置双分支：

    if hasattr(key, 'sign'):  sig = key.sign(bytes(self.payload))     # 全文签名
    else:                     sig = key.sign_digest(message)          # 摘要签名

而 `imgtool.image.ALLOWED_KEY_SHA` 是按 `type(key)` **精确查表**的，新建子类必须注册进去。
但编译期签名用的 imgtool 来自 FetchContent 拉取的 mcuboot（清编译/重新 configure 会重置），
对它打补丁不可靠。

因此这里先用 `imgtool.main.load_key(公钥pem)` 拿到上游**原生**公钥对象
（type 仍是已注册的 `ECDSA256P1Public`），再在该实例上挂 `sign` / `sign_digest`。
好处：
- 零 imgtool 源码改动，sign_kit 与 TF-M 编译期两条链路行为一致；
- `sign` 内部只做本地 SHA-256，随后转发摘要，**镜像全文不出本地**。

安全约束
--------
- 默认拒绝明文 HTTP（需要显式 `ALLOW_INSECURE_HTTP=1` 才放行），避免令牌与摘要裸奔；
- 绝不关闭 TLS 证书校验（内网自签/公司 CA 请用 `CA_BUNDLE` 指定）；
- 令牌只从环境变量或 `~/.tfm_sign_token` 读取，不落仓库、不进命令行、不进构建缓存。
"""

from __future__ import annotations

import base64
import hashlib
import json
import os
import ssl
import sys
import urllib.error
import urllib.request
from pathlib import Path

DIGEST_LEN = 32
DEFAULT_TIMEOUT = 30.0
TOKEN_ENV_DEFAULT = "TFM_SIGN_TOKEN"
TOKEN_FILE_DEFAULT = "~/.tfm_sign_token"


class RemoteSignError(Exception):
    """远程签名失败（含配置错误与协议错误）。"""


# --------------------------------------------------------------------------- #
# 配置解析（与 sign_kit/config、sign.sh 相同的 KEY=VALUE 语义）
# --------------------------------------------------------------------------- #
def load_conf(conf_path: str | os.PathLike[str]) -> dict[str, str]:
    """读取 `remote.conf`（KEY=VALUE，# 为注释）。

    服务地址支持用环境变量 `TFM_SIGN_SERVER_URL` 覆盖（配置里留空即可），
    这样团队共享同一份映射配置，而各人无需修改被跟踪的文件。

    `CA_BUNDLE` 支持相对路径（相对本配置文件所在目录解析，便于把证书随仓库携带）；
    若指向的文件不存在，则告警并回退系统信任库（不硬失败，避免远端快照缺文件就中断）。
    """
    path = Path(conf_path)
    if not path.is_file():
        raise RemoteSignError(
            f"找不到远程签名配置 {path}\n"
            "  该文件应随仓库提供（含团队共享的 key_id 映射）；若缺失请从版本库恢复。"
        )

    conf: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line or "=" not in line:
            continue
        key, value = line.split("=", 1)
        conf[key.strip()] = value.strip()

    # CA_BUNDLE：相对路径按配置文件所在目录解析（脚本运行前会 cd 到 scripts/，
    # 相对路径直接交给 ssl 会失效）。证书随仓库提交后，新机器无需导入系统信任库。
    ca_bundle = (conf.get("CA_BUNDLE") or "").strip()
    if ca_bundle:
        ca_path = Path(ca_bundle)
        if not ca_path.is_absolute():
            ca_path = (path.parent / ca_bundle).resolve()
        if ca_path.is_file():
            conf["CA_BUNDLE"] = str(ca_path)
        else:
            print(
                f"警告: CA_BUNDLE 指向的证书不存在: {ca_path}"
                "（已回退系统信任库；内网自签证书请修正该路径，或把 CA 导入系统信任库）",
                file=sys.stderr,
            )
            conf["CA_BUNDLE"] = ""

    if not conf.get("SIGN_SERVER_URL"):
        conf["SIGN_SERVER_URL"] = (os.environ.get("TFM_SIGN_SERVER_URL") or "").strip()
    return conf


def resolve_token(conf: dict[str, str], env: dict[str, str] | None = None) -> str:
    """按优先级取令牌：环境变量 → 用户主目录文件。

    优先级：`<TOKEN_ENV>`（默认 TFM_SIGN_TOKEN）→ `TOKEN_FILE`（默认 ~/.tfm_sign_token）
    """
    env = os.environ if env is None else env

    env_name = conf.get("TOKEN_ENV") or TOKEN_ENV_DEFAULT
    token = (env.get(env_name) or "").strip()
    if token:
        return token

    token_file = Path(os.path.expanduser(conf.get("TOKEN_FILE") or TOKEN_FILE_DEFAULT))
    if token_file.is_file():
        token = token_file.read_text(encoding="utf-8").strip()
        if token:
            if os.name != "nt":  # Windows 的 ACL 不映射到 Unix mode，跳过此项检查
                try:
                    mode = token_file.stat().st_mode
                    if mode & 0o077:
                        print(
                            f"警告: {token_file} 权限过于宽松（建议 chmod 600），仍继续使用",
                            file=sys.stderr,
                        )
                except OSError:  # pragma: no cover
                    pass
            return token

    raise RemoteSignError(
        f"未找到签名令牌。请任选一种方式提供：\n"
        f"  1) export {env_name}='<向密钥管理员申请的令牌>'\n"
        f"  2) 写入 {token_file}（内容为令牌，权限 600）"
    )


def resolve_profile(conf: dict[str, str], profile: str) -> tuple[str, str]:
    """把配置档名解析为 (SPE_KEY_ID, NS_KEY_ID)。

    **不做 `X` → `X-SPE` 的兜底猜测**：映射缺失直接报错，避免签错密钥。
    """
    spe = conf.get(f"PROFILE_{profile}_SPE", "").strip()
    ns = conf.get(f"PROFILE_{profile}_NS", "").strip()
    if not spe or not ns:
        available = sorted(
            {
                key[len("PROFILE_") : -len("_SPE")]
                for key in conf
                if key.startswith("PROFILE_") and key.endswith("_SPE")
            }
        )
        raise RemoteSignError(
            f"配置档 {profile!r} 未在 remote.conf 中完整映射。\n"
            f"  需要同时提供 PROFILE_{profile}_SPE 与 PROFILE_{profile}_NS。\n"
            f"  现有配置档: {', '.join(available) if available else '(无)'}\n"
            "  注意：不会按 '<名称>-SPE' 自动推断 key_id（防止签错密钥）。"
        )
    return spe, ns


# --------------------------------------------------------------------------- #
# 远程签名器
# --------------------------------------------------------------------------- #
class RemoteSigner:
    """把 32 字节摘要发到内网签名服务，返回 DER 签名；只依赖标准库。"""

    def __init__(
        self,
        server_url: str,
        key_id: str,
        token: str | None = None,
        ca_bundle: str | None = None,
        timeout: float = DEFAULT_TIMEOUT,
        expected_pubkey_sha256: str | None = None,
        context: dict[str, str] | None = None,
        allow_insecure_http: bool = False,
    ) -> None:
        if not server_url:
            raise RemoteSignError("SIGN_SERVER_URL 为空，无法进行远程签名")

        self.server_url = server_url.rstrip("/")
        self.key_id = key_id
        self.token = token
        self.ca_bundle = ca_bundle or None
        self.timeout = float(timeout or DEFAULT_TIMEOUT)
        self.expected_pubkey_sha256 = (
            expected_pubkey_sha256.lower() if expected_pubkey_sha256 else None
        )
        self.context = context or {}

        if self.server_url.startswith("http://") and not allow_insecure_http:
            raise RemoteSignError(
                "SIGN_SERVER_URL 使用了明文 http://，令牌与摘要会明文传输。\n"
                "  推荐改用 https:// 并用 CA_BUNDLE 指定公司内部 CA；\n"
                "  若确认内网可信且必须明文，请在 remote.conf 中显式设置 ALLOW_INSECURE_HTTP=1。"
            )

    # -- HTTP ---------------------------------------------------------------
    def _ssl_context(self) -> ssl.SSLContext:
        # 绝不关闭证书校验
        if self.ca_bundle:
            return ssl.create_default_context(cafile=self.ca_bundle)
        return ssl.create_default_context()

    def _request(self, method: str, path: str, payload: dict | None = None) -> dict:
        url = f"{self.server_url}{path}"
        headers = {"Accept": "application/json"}
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"

        data = None
        if payload is not None:
            data = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"

        request = urllib.request.Request(url, data=data, headers=headers, method=method)
        opener_args = {"timeout": self.timeout}
        if url.startswith("https://"):
            opener_args["context"] = self._ssl_context()

        try:
            with urllib.request.urlopen(request, **opener_args) as response:
                body = response.read().decode("utf-8")
        except urllib.error.HTTPError as exc:
            raise RemoteSignError(self._format_http_error(exc)) from exc
        except urllib.error.URLError as exc:
            raise RemoteSignError(
                f"无法连接签名服务 {self.server_url}（{exc.reason}）。\n"
                "  请确认：已连公司网络/VPN、服务地址与端口正确、CA_BUNDLE 配置正确。"
            ) from exc
        except TimeoutError as exc:
            raise RemoteSignError(
                f"签名服务 {self.server_url} 响应超时（>{self.timeout:.0f}s）"
            ) from exc

        try:
            return json.loads(body)
        except json.JSONDecodeError as exc:  # pragma: no cover - 服务端异常
            raise RemoteSignError(f"签名服务返回了非 JSON 内容: {body[:200]}") from exc

    @staticmethod
    def _format_http_error(exc: urllib.error.HTTPError) -> str:
        reason, message = "", ""
        try:
            body = json.loads(exc.read().decode("utf-8"))
            detail = body.get("error", {})
            reason = str(detail.get("reason", ""))
            message = str(detail.get("message", ""))
        except Exception:  # noqa: BLE001 - 退化到原始状态码
            pass

        hints = {
            "unauthorized": "令牌缺失/无效或已吊销：检查 TFM_SIGN_TOKEN 或 ~/.tfm_sign_token",
            "forbidden": "该令牌未授权此 key_id：请向密钥管理员申请对应授权",
            "key_disabled": "该密钥已下线：请联系密钥管理员",
            "key_not_found": "key_id 未登记或私钥加载失败：核对 remote.conf 映射并检查 /v1/health",
            "rate_limited": "触发限流：按 Retry-After 退避后重试",
            "signing_unavailable": "服务端密钥未加载：联系密钥管理员查看 /v1/health",
            "bad_request": "请求参数非法：检查 key_id 与摘要长度（必须 32 字节）",
        }
        hint = hints.get(reason, "")
        parts = [f"签名服务返回 HTTP {exc.code}"]
        if reason:
            parts.append(f"reason={reason}")
        if message:
            parts.append(f"message={message}")
        text = "（" + "，".join(parts) + "）"
        if hint:
            text += f"\n  处理建议：{hint}"
        return text

    # -- 协议 ---------------------------------------------------------------
    def fetch_pubkey(self) -> tuple[bytes, str]:
        """取该 key_id 的公钥与指纹，返回 (SPKI DER, sha256 hex)。"""
        data = self._request("GET", f"/v1/keys/{self.key_id}")
        pem = data.get("public_pem", "")
        if not pem:
            raise RemoteSignError(f"服务端未返回 {self.key_id} 的公钥")

        try:
            from cryptography.hazmat.primitives import serialization
        except ImportError as exc:  # pragma: no cover - imgtool 必然依赖 cryptography
            raise RemoteSignError("缺少 cryptography，无法解析公钥 PEM") from exc

        public_key = serialization.load_pem_public_key(pem.encode("utf-8"))
        der = public_key.public_bytes(
            serialization.Encoding.DER,
            serialization.PublicFormat.SubjectPublicKeyInfo,
        )
        return der, hashlib.sha256(der).hexdigest()

    def sign_digest(self, digest: bytes) -> bytes:
        """对 32 字节摘要签名，返回 DER。

        服务端**不会**再哈希；因此传入的必须是最终摘要本身。
        """
        if not isinstance(digest, (bytes, bytearray)):
            raise RemoteSignError("sign_digest 需要 bytes 摘要")
        digest = bytes(digest)
        if len(digest) != DIGEST_LEN:
            raise RemoteSignError(
                f"摘要长度必须为 {DIGEST_LEN} 字节（SHA-256），实际 {len(digest)} 字节"
            )

        payload = {
            "key_id": self.key_id,
            "algorithm": "ecdsa-p256",
            "digest": base64.b64encode(digest).decode("ascii"),
            "context": self.context or None,
        }
        result = self._request("POST", "/v1/sign", payload)

        returned_key = result.get("key_id")
        if returned_key and returned_key != self.key_id:
            raise RemoteSignError(
                f"服务端返回的 key_id 与请求不一致（请求 {self.key_id}，返回 {returned_key}）"
            )

        fingerprint = str(result.get("pubkey_sha256", "")).lower()
        if self.expected_pubkey_sha256:
            if fingerprint != self.expected_pubkey_sha256:
                raise RemoteSignError(
                    "公钥指纹不一致，已中止使用该签名（避免签出板上无法启动的镜像）：\n"
                    f"  key_id        : {self.key_id}\n"
                    f"  本地公钥指纹  : {self.expected_pubkey_sha256}\n"
                    f"  服务端返回指纹: {fingerprint or '(缺失)'}\n"
                    "  可能原因：本地公钥与板上 BL2/OTP 不一致，或服务端该 key_id 换了密钥。"
                )
        elif not fingerprint:
            raise RemoteSignError("服务端未返回 pubkey_sha256，无法做密钥一致性校验")

        try:
            return base64.b64decode(result["signature"], validate=True)
        except (KeyError, ValueError) as exc:
            raise RemoteSignError("服务端返回的 signature 缺失或不是合法 base64") from exc


# --------------------------------------------------------------------------- #
# 与 imgtool 对接：加载公钥 + 实例方法注入
# --------------------------------------------------------------------------- #
def _assert_public_key(key: object) -> None:
    """拒绝把私钥当公钥用（remote 模式本地不应存在私钥）。"""
    try:
        from cryptography.hazmat.primitives.asymmetric import ec
    except ImportError:  # pragma: no cover
        return
    inner = getattr(key, "key", None)
    if isinstance(inner, ec.EllipticCurvePrivateKey):
        raise RemoteSignError(
            "remote 模式要求 --key 指向**公钥** PEM，但检测到私钥。\n"
            "  请用 imgtool 导出公钥：imgtool getpub -k <私钥> -e pem > <key_id>.pem"
        )


def load_remote_key(
    pubkey_pem_path: str | os.PathLike[str],
    conf: dict[str, str],
    key_id: str,
    extra_context: dict[str, str] | None = None,
) -> object:
    """加载公钥并注入远程签名方法，返回可直接交给 imgtool 的 key 对象。

    绝不在实例上定义 `pad_sig` 语义；`sign()` 只做本地 SHA-256 后转发摘要。
    """
    import imgtool.keys  # noqa: F401 - 由调用方保证 imgtool 可导入
    import imgtool.main

    pem_path = Path(pubkey_pem_path)
    if not pem_path.is_file():
        raise RemoteSignError(
            f"找不到 {key_id} 的本地公钥 {pem_path}\n"
            "  请用 sign_kit/scripts/fetch_pubkeys.py 从服务端导出，或由管理员提供并提交到仓库。"
        )

    key = imgtool.main.load_key(str(pem_path))
    _assert_public_key(key)

    if getattr(key, "sig_type", lambda: "")() != "ECDSA256_SHA256":
        raise RemoteSignError(
            f"{pem_path} 不是 ECDSA P-256 公钥（sig_type={getattr(key, 'sig_type', lambda: '?')()}）；"
            "本服务只支持 ecdsa-p256"
        )

    local_der = key.get_public_bytes()
    local_fp = hashlib.sha256(local_der).hexdigest()

    token = resolve_token(conf)
    context = {
        "profile": conf.get("_PROFILE", ""),
        "kind": conf.get("_KIND", ""),
    }
    context.update(extra_context or {})
    context = {k: v for k, v in context.items() if v}

    signer = RemoteSigner(
        server_url=conf.get("SIGN_SERVER_URL", ""),
        key_id=key_id,
        token=token,
        ca_bundle=conf.get("CA_BUNDLE") or None,
        timeout=float(conf.get("TIMEOUT") or DEFAULT_TIMEOUT),
        expected_pubkey_sha256=local_fp,
        context=context,
        allow_insecure_http=str(conf.get("ALLOW_INSECURE_HTTP", "")).strip() in ("1", "on", "true", "yes"),
    )

    # 实例方法注入（见模块 docstring 的说明）
    key.sign = lambda payload: signer.sign_digest(hashlib.sha256(payload).digest())
    key.sign_digest = signer.sign_digest
    key.remote_signer = signer  # 供诊断/日志使用
    return key
