#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""在线证书全回归。替换 regression.bat 最后一条 debugauth=1。

不读 key_3_leaf.pem。待签名字节交给已经在跑的签名服务。
锁住的芯片不能用 connectStLink（内核 AP 已关），信箱走 ST-LINK 的 DAP。
从写下 ADTS 到交回签名是同一次 SWD，中间不能重新连接。全回归成功时芯片会自己复位。
"""
import argparse
import base64
import ctypes
import json
import os
import struct
import sys
import time
import urllib.error
import urllib.request

DBGMCU = 0x44024000
AUTH_HOST = DBGMCU + 0x100
AUTH_DEV = DBGMCU + 0x104
AUTH_ACK = DBGMCU + 0x108
ACK_HOST = 0x1
ACK_DEV = 0x2
# SDM 写进 DBG_AUTH_HOST 的是字符串 STDA 的字节反序，芯片用 KOAD 应答。
REQ_MAGIC = 0x53544441
READY_PATTERN = 0x44414F4B
CPUID = 0xE000ED00
# 非安全 FLASH 寄存器。产品状态在 OPTSR_CUR 的 bit15:8。
OPTSR_CUR = 0x40022050

ADAC_DISCOVERY = 0x0001
ADAC_AUTH_START = 0x0002
ADAC_AUTH_RESPONSE = 0x0003
CERT_TYPE = 0x02010000
TOKEN_TYPE = 0x02000000

# AN6008：per=a 是 bit14 全回归，per=b 是 bit12 部分回归。
PERMISSIONS = {
    "a": 0x00004000,
    "b": 0x00001000,
}
PRODUCT_STATE = {
    0xED: "OPEN",
    0x17: "PROVISIONING",
    0x72: "CLOSED",
    0xC6: "TZ-CLOSED",
    0x5C: "LOCKED",
}

# 锁住时内核在 AP1，DBGMCU 信箱在 AP0。回归固定走 AP0。
MAILBOX_AP = 0
DP_PORT = 0xFFFF
# 32 位、地址不自增。自增会让下一次写跑到相邻寄存器。
CSW_32 = 0x23000002
STLINK_OK = 0x80
# 系统上电、调试上电。固件不接受 CORUNDETECT，所以不带那一位。
DP_POWER = 0x50000000

WORD_TIMEOUT = 2.0
FIRST_WORD_TIMEOUT = 5.0


class DebugError(Exception):
    """调试口或协议失败。调用方据此退出，不当成回归成功。"""


def script_dir():
    """证书和脚本约定放在同一目录。"""
    return os.path.dirname(os.path.abspath(__file__))


def programmer_folder(programmer):
    """STLinkUSBDriver.dll 和 CLI 在同一个 bin 目录。"""
    if not programmer:
        programmer = r"D:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
    programmer = programmer.strip('"')
    return programmer if os.path.isdir(programmer) else os.path.dirname(programmer)


def check_server(url):
    """确认签名服务还在，并且报的是 P-256。不发送待签名数据。"""
    health = url.rstrip("/") + "/health"
    try:
        with urllib.request.urlopen(health, timeout=3) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
        raise DebugError("签名服务没有应答 %s：%s" % (health, exc))
    if not payload.get("ok") or payload.get("curve") != "P-256":
        raise DebugError("签名服务应答异常：%s" % payload)
    print("sign server: %s" % health)
    print("leaf pub sha256: %s" % payload.get("leaf_pub_sha256", ""))


def load_certs(path):
    """拆开 cert_leaf_chain.b64。顺序必须是根、中间、叶子，每段 220 字节。"""
    text = open(path, "rb").read().strip()
    try:
        blob = base64.b64decode(text, validate=True)
    except Exception as exc:
        raise DebugError("证书链不是 Base64：%s" % exc)
    certs = []
    offset = 0
    while offset + 8 <= len(blob):
        cert_type, size = struct.unpack_from("<II", blob, offset)
        if cert_type != CERT_TYPE or size <= 0 or offset + 8 + size > len(blob):
            break
        certs.append(blob[offset:offset + 8 + size])
        offset += 8 + size
    if len(certs) != 3 or offset != len(blob):
        raise DebugError("证书链应是根、中间、叶子三段，实际解析到 %d 段" % len(certs))
    print("certificate chain: %s (%d bytes, 3 certs)" % (path, len(blob)))
    return certs


def permission_bytes(symbol):
    """per=a/b 转成令牌里的 16 字节小端权限。其余 12 字节为 0。"""
    if symbol not in PERMISSIONS:
        raise DebugError("只支持 per=a 全回归或 per=b 部分回归")
    return struct.pack("<I", PERMISSIONS[symbol]) + b"\x00" * 12


def sign_online(url, message):
    """把芯片要求签名的字节交给签名服务，取回 64 字节 R||S。"""
    body = json.dumps({
        "data_b64": base64.b64encode(message).decode("ascii"),
        "hash": True,
    }).encode("utf-8")
    request = urllib.request.Request(
        url.rstrip("/") + "/sign",
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", "replace")
        raise DebugError("签名服务拒绝：%s" % detail)
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
        raise DebugError("签名请求失败：%s" % exc)
    raw = base64.b64decode(payload.get("signature_rs_b64", ""), validate=True)
    if len(raw) != 64:
        raise DebugError("签名长度不是 64 字节 R||S")
    return raw


class Probe(object):
    """ST-LINK 的 DAP。一次打开保持到签名交回，中间不 CloseDevice。"""

    def __init__(self, folder):
        self.folder = folder  # CubeProgrammer 的 bin，里面有 STLinkUSBDriver.dll
        self.dll = None  # 已加载的驱动
        self.handle = None  # STLink_OpenDevice 返回的句柄
        self._out = None  # 应答缓冲，命令返回前不能被回收

    def open(self):
        """枚举并打开第一块 ST-LINK，然后进入 SWD、给调试口上电。"""
        dll_path = os.path.join(self.folder, "STLinkUSBDriver.dll")
        if not os.path.isfile(dll_path):
            raise DebugError("找不到 STLinkUSBDriver.dll：%s" % dll_path)
        if hasattr(os, "add_dll_directory"):
            os.add_dll_directory(self.folder)
        os.environ["PATH"] = self.folder + os.pathsep + os.environ.get("PATH", "")
        dll = ctypes.WinDLL(dll_path)
        dll.STLink_Reenumerate.argtypes = [ctypes.c_int, ctypes.c_byte]
        dll.STLink_Reenumerate.restype = ctypes.c_int
        dll.STLink_OpenDevice.argtypes = [
            ctypes.c_int, ctypes.c_byte, ctypes.c_byte, ctypes.POINTER(ctypes.c_void_p),
        ]
        dll.STLink_OpenDevice.restype = ctypes.c_int
        dll.STLink_CloseDevice.argtypes = [ctypes.c_void_p]
        dll.STLink_CloseDevice.restype = ctypes.c_int
        dll.STLink_SendCommand.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint]
        dll.STLink_SendCommand.restype = ctypes.c_int
        self.dll = dll
        count = int(dll.STLink_Reenumerate(0, 0))
        if count <= 0:
            raise DebugError("没有枚举到 ST-LINK")
        handle = ctypes.c_void_p()
        opened = int(dll.STLink_OpenDevice(0, 0, 0, ctypes.byref(handle)))
        if opened != 1 or not handle.value:
            raise DebugError("打开 ST-LINK 失败")
        self.handle = handle
        self._enter_swd()

    def close(self):
        """松开复位并关掉设备。回归成功后芯片可能已经自己断开。"""
        if self.dll is None or self.handle is None:
            return
        try:
            # 命令没走完时信箱会停在半开。脉冲复位让芯片回到原来的锁住状态。
            self.drive_nrst(2)
            self.drive_nrst(1)
        except DebugError:
            pass
        try:
            self._xfer(bytes([0xF2, 0x21]), 2, 400)
        except DebugError:
            pass
        self.dll.STLink_CloseDevice(self.handle)
        self.handle = None

    def _xfer(self, command, resp_len, timeout):
        """按驱动要求的结构发一条 ST-LINK 命令。应答在 data 指针里。"""

        class Command(ctypes.Structure):
            """STLink_SendCommand 的打包参数。cmd_len 是后面命令的字节数。"""

            _pack_ = 1
            _fields_ = [
                ("cmd_len", ctypes.c_uint8),  # 从 cmd 里拷走的字节数
                ("cmd", ctypes.c_uint8 * 16),  # 原始命令，第一条是 0xF2
                ("direction", ctypes.c_uint8),  # 1 表示 data 是应答缓冲
                ("data", ctypes.c_void_p),  # 应答写入这里
                ("data_len", ctypes.c_uint32),  # 期望的应答字节数
                ("resp_len", ctypes.c_uint8),  # 0：应答不走结构尾部
            ]

        if self.handle is None:
            raise DebugError("ST-LINK 还没有打开")
        out = (ctypes.c_ubyte * max(resp_len, 1))()
        packed = Command()
        packed.cmd_len = len(command)
        for index, byte in enumerate(command[:16]):
            packed.cmd[index] = byte
        packed.direction = 1 if resp_len else 0
        packed.data = ctypes.cast(out, ctypes.c_void_p)
        packed.data_len = resp_len
        # 缓冲挂在对象上，调用返回前不能被垃圾回收。
        self._out = out
        status = int(self.dll.STLink_SendCommand(self.handle, ctypes.byref(packed), timeout))
        if status != 1:
            raise DebugError("ST-LINK 命令失败，错误码 %d" % status)
        return bytes(out[:resp_len])

    def _enter_swd(self):
        """进入 SWD，清掉粘滞错误，打开 AP0。不复位芯片。"""
        version = self._xfer(bytes([0xF1, 0x80, 0, 0, 0, 0]), 6, 2000)
        print("ST-LINK version: %s" % version.hex())
        entered = self._xfer(bytes([0xF2, 0x30, 0xA3]), 2, 2000)
        if entered[0] != STLINK_OK:
            raise DebugError("进入 SWD 失败：%s" % entered.hex())
        self._write_dap(DP_PORT, 0x0, 0x1E)
        self._write_dap(DP_PORT, 0x4, DP_POWER)
        ctrl, _ = self._read_dap(DP_PORT, 0x4)
        if ctrl != STLINK_OK:
            raise DebugError("调试口上电失败")
        inited = self._xfer(bytes([0xF2, 0x4B, MAILBOX_AP]), 2, 2000)
        if inited[0] != STLINK_OK:
            raise DebugError("打开 AP%d 失败" % MAILBOX_AP)

    def _read_dap(self, port, addr):
        """读 DP（port=0xFFFF）或 AP 寄存器。返回状态字节和值。"""
        raw = self._xfer(bytes([0xF2, 0x45]) + struct.pack("<HH", port, addr), 8, 1500)
        value = struct.unpack_from("<I", raw, 4)[0]
        return raw[0], value

    def _write_dap(self, port, addr, value):
        """写 DP 或 AP 寄存器。状态不是 0x80 就是失败。"""
        raw = self._xfer(bytes([0xF2, 0x46]) + struct.pack("<HHI", port, addr, value), 2, 1500)
        return raw[0]

    def read_mem(self, address):
        """经 AP0 读一个字。锁住时只能读到 DBGMCU，读内核会失败。"""
        return self._mem(address, None)

    def write_mem(self, address, value):
        """经 AP0 写一个字。只用于 DBGMCU 信箱。"""
        self._mem(address, value)

    def _mem(self, address, value):
        """一次 32 位访问。先清粘滞错误，否则锁住的核会让这次读直接失败。"""
        self._write_dap(DP_PORT, 0x0, 0x1E)
        wrote = self._write_dap(MAILBOX_AP, 0x0, CSW_32)
        wrote |= self._write_dap(MAILBOX_AP, 0x4, address)
        if value is None:
            status, got = self._read_dap(MAILBOX_AP, 0x0C)
            if wrote == STLINK_OK and status == STLINK_OK:
                return got
            raise DebugError("读 0x%08X 失败，状态 0x%02X" % (address, status))
        wrote |= self._write_dap(MAILBOX_AP, 0x0C, value)
        if wrote != STLINK_OK:
            raise DebugError("写 0x%08X 失败" % address)

    def drive_nrst(self, level):
        """探头拉 NRST。0 拉低，1 放开，2 是脉冲。"""
        raw = self._xfer(bytes([0xF2, 0x3C, level]), 2, 2000)
        if raw[0] != STLINK_OK:
            raise DebugError("NRST 操作失败：%s" % raw.hex())


class Mailbox(object):
    """DBGMCU 信箱。bit0 由主机置位、芯片清掉；bit1 由芯片置位、主机清掉。"""

    def __init__(self, probe):
        self.probe = probe  # 已经进入 SWD 的探头，本对象不负责开关

    def write_word(self, value):
        """写一个字。写 HOST 时硬件自己把 ACK 的 bit0 置上，不用再写 ACK。"""
        if self.probe.read_mem(AUTH_ACK) & ACK_HOST:
            raise DebugError("上一个字还没被取走，不能写 0x%08X" % value)
        self.probe.write_mem(AUTH_HOST, value)
        deadline = time.monotonic() + WORD_TIMEOUT
        while time.monotonic() < deadline:
            if (self.probe.read_mem(AUTH_ACK) & ACK_HOST) == 0:
                return
            time.sleep(0.001)
        raise DebugError("芯片没有取走信箱数据 0x%08X" % value)

    def read_word(self, timeout):
        """ACK 变成 2 时读 DEV。这一次读会把 ACK 清掉。"""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.probe.read_mem(AUTH_ACK) == ACK_DEV:
                return self.probe.read_mem(AUTH_DEV)
            time.sleep(0.001)
        raise DebugError("等待芯片应答超时")

    def send_command(self, command, payload):
        """命令在第一个字的高 16 位，第二个字是载荷字数，后面才是载荷。"""
        if len(payload) % 4 != 0:
            payload += b"\x00" * (4 - (len(payload) % 4))
        count = len(payload) // 4
        if count > 0xFFFF:
            raise DebugError("命令太长")
        print("TX cmd=0x%04X words=%d" % (command, count))
        self.write_word(command << 16)
        self.write_word(count)
        for index in range(count):
            self.write_word(struct.unpack_from("<I", payload, index * 4)[0])

    def receive(self):
        """先收命令字和长度字，再按长度把载荷收完。"""
        header = self.read_word(FIRST_WORD_TIMEOUT)
        length = self.read_word(WORD_TIMEOUT)
        if length > 128:
            raise DebugError("应答长度异常：0x%08X" % length)
        words = [header, length]
        for _ in range(length):
            words.append(self.read_word(WORD_TIMEOUT))
        raw = b"".join(struct.pack("<I", word) for word in words)
        print("RX header=0x%08X length=%d bytes=%s" % (header, length, raw.hex()))
        return words


def extract_challenge(words):
    """从 AUTH_START 应答里取出 32 字节随机数。

    头后面若是 32 字节，整段就是随机数。
    若是 36 字节，前 4 字节是前缀，后面才是随机数。
    """
    payload = b"".join(struct.pack("<I", word) for word in words[2:])
    if len(payload) == 32:
        return payload
    if len(payload) >= 36:
        return payload[4:36]
    raise DebugError("应答里没有 32 字节随机数，收到 %d 字节" % (len(payload) + 4))


def build_token(challenge, permissions, signature):
    """令牌格式 0x200。正文是随机数、权限、64 字节 R||S。

    签名覆盖前两段，不覆盖类型字和签名本身。
    """
    body = challenge + permissions + signature
    return struct.pack("<II", TOKEN_TYPE, len(body)) + body


def open_comms(probe):
    """写入 ADTS 后先退出调试，再拉复位，让启动代码在没有调试器时看到口令。

    调试器还连着时拉 NRST，RSS 不会重新起来。
    重连后若已经是 KOAD 且 ACK 为 0，不要再写一次 ADTS，否则 ACK 会停在 1。
    """
    try:
        ident = probe.read_mem(DBGMCU)
    except DebugError:
        probe.drive_nrst(0)
        time.sleep(0.05)
        probe._enter_swd()
        ident = probe.read_mem(DBGMCU)
    print("DBGMCU IDCODE: 0x%08X" % ident)
    host = probe.read_mem(AUTH_HOST)
    dev = probe.read_mem(AUTH_DEV)
    ack = probe.read_mem(AUTH_ACK)
    print("before HOST=0x%08X DEV=0x%08X ACK=0x%08X" % (host, dev, ack))
    if not (dev == READY_PATTERN and ack == 0):
        if ack & ACK_HOST:
            raise DebugError("信箱 ACK 停在 1，先把芯片重新上电")
        probe.write_mem(AUTH_HOST, REQ_MAGIC)
        time.sleep(0.05)
        # 退出可能超时，复位仍然要做。启动代码只在调试器脱开时处理 ADTS。
        try:
            probe._xfer(bytes([0xF2, 0x21]), 2, 400)
        except DebugError:
            pass
        probe.drive_nrst(0)
        time.sleep(0.15)
        probe._enter_swd()
    probe.drive_nrst(1)
    deadline = time.monotonic() + FIRST_WORD_TIMEOUT
    while time.monotonic() < deadline:
        try:
            ack = probe.read_mem(AUTH_ACK)
            dev = probe.read_mem(AUTH_DEV)
        except DebugError:
            probe._enter_swd()
            continue
        if ack == ACK_DEV:
            dev = probe.read_mem(AUTH_DEV)
            ack = probe.read_mem(AUTH_ACK)
        if dev == READY_PATTERN and ack == 0:
            print("mailbox ready DEV=0x%08X ACK=0x%08X" % (dev, ack))
            return
        time.sleep(0.02)
    raise DebugError("芯片没有进入调试认证")


def core_is_open(probe):
    """内核 CPUid 能读到，说明这次认证已经把调试口打开。"""
    try:
        value = probe.read_mem(CPUID)
    except DebugError:
        return False
    print("CPUID: 0x%08X" % value)
    return value != 0 and value != 0xFFFFFFFF


def product_state(probe):
    """读 OPTSR_CUR 里的产品状态。读不到就返回空，不当成失败。"""
    try:
        value = probe.read_mem(OPTSR_CUR)
    except DebugError:
        return ""
    code = (value >> 8) & 0xFF
    name = PRODUCT_STATE.get(code, "0x%02X" % code)
    print("product state: %s (OPTSR_CUR=0x%08X)" % (name, value))
    return name


def run_regression(server, certs, per_symbol, folder):
    """同一次连接里：发现、要随机数、在线签名、送证书和令牌。"""
    probe = Probe(folder)
    probe.open()
    try:
        open_comms(probe)
        box = Mailbox(probe)
        box.send_command(ADAC_DISCOVERY, b"")
        discovery = box.receive()
        if (discovery[0] & 0xFFFF) not in (0, ADAC_DISCOVERY):
            raise DebugError("发现命令被拒绝：0x%08X" % discovery[0])
        box.send_command(ADAC_AUTH_START, b"")
        challenge = extract_challenge(box.receive())
        print("challenge: %s" % challenge.hex())
        permissions = permission_bytes(per_symbol)
        # 芯片验的是这段原文的 SHA-256，再验 ECDSA。私钥只在签名服务里。
        signature = sign_online(server, challenge + permissions)
        for index, cert in enumerate(certs):
            print("send certificate %d/%d" % (index + 1, len(certs)))
            box.send_command(ADAC_AUTH_RESPONSE, cert)
            reply = box.receive()
            if (reply[0] & 0xFFFF) not in (0, ADAC_AUTH_RESPONSE):
                raise DebugError("证书 %d 被拒绝：0x%08X" % (index + 1, reply[0]))
        print("send token")
        box.send_command(ADAC_AUTH_RESPONSE, build_token(challenge, permissions, signature))
        try:
            reply = box.receive()
        except DebugError as exc:
            # 全回归会擦除并复位，信箱可能在状态字回来之前断开。
            print("link dropped after token: %s" % exc)
            reply = None
        if reply is not None and (reply[0] & 0xFFFF) not in (0, ADAC_AUTH_RESPONSE):
            raise DebugError("令牌被拒绝：0x%08X" % reply[0])
        time.sleep(0.3)
        try:
            probe._enter_swd()
        except DebugError as exc:
            print("re-enter after regression: %s" % exc)
        opened = core_is_open(probe)
        state = product_state(probe)
        if reply is None and not opened and state != "OPEN":
            raise DebugError("令牌发出后链路断开，但还不能确认芯片已经打开")
        if state and state != "OPEN" and not opened:
            raise DebugError("认证后产品状态仍是 %s" % state)
        print("online regression finished")
    finally:
        probe.close()


def do_check(args):
    """回归动手之前确认服务、证书和探头。不写芯片。"""
    check_server(args.server)
    load_certs(args.cert)
    permission_bytes(args.per)
    probe = Probe(programmer_folder(args.programmer))
    probe.open()
    try:
        ident = probe.read_mem(DBGMCU)
        print("DBGMCU IDCODE: 0x%08X" % ident)
    finally:
        probe.close()


def main():
    """检查，或者对已经 DA 锁住的芯片做在线全回归。"""
    parser = argparse.ArgumentParser(description="在线证书回归的最后一步")
    parser.add_argument("--check", action="store_true", help="只检查签名服务、证书和探头")
    parser.add_argument("--regress", action="store_true", help="对芯片做在线全回归")
    parser.add_argument("--server", default="http://127.0.0.1:8765")
    parser.add_argument("--cert", default=os.path.join(script_dir(), "cert_leaf_chain.b64"))
    parser.add_argument("--programmer", default="")
    parser.add_argument("--per", default="a", choices=("a", "b"))
    args = parser.parse_args()
    if args.check == args.regress:
        parser.error("指定 --check 或 --regress 其中一个")
    if args.check:
        do_check(args)
        return
    check_server(args.server)
    certs = load_certs(args.cert)
    run_regression(args.server, certs, args.per, programmer_folder(args.programmer))


if __name__ == "__main__":
    try:
        main()
    except DebugError as exc:
        print(str(exc))
        sys.exit(1)
    except Exception as exc:
        print(str(exc))
        sys.exit(1)
