import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, StringVar
import socket
import threading
import time
import netifaces
from queue import Queue
import sys
import binascii

class UdpNewLineDebugger:
    def __init__(self, root):
        self.root = root
        self.root.title("UDP 调试助手（自动换行版）")
        self.root.geometry("1000x700")
        self.root.resizable(width=False, height=False)

        # 核心配置
        self.log_queue = Queue(maxsize=5000)
        self.ui_refresh_interval = 200
        self.max_display_lines = 1000
        self.receive_buffer_size = 65536  # 64KB
        self.hex_line_length = 32  # 十六进制每行显示32个字符（16字节）
        self.text_line_length = 80  # 文本每行显示80个字符

        # 网络状态
        self.udp_socket = None
        self.is_running = False
        self.bound_ip = None
        self.bound_port = None
        self.packet_count = 0
        self.byte_count = 0

        # 解析模式
        self.parse_mode = StringVar(value="auto")  # auto/text/hex

        # 本地IP
        self.local_ips = self.get_local_ips()

        # 1. 配置区域
        self.config_frame = ttk.LabelFrame(root, text="网卡与端口配置", padding="10")
        self.config_frame.grid(row=0, column=0, padx=10, pady=5, sticky=(tk.W, tk.E))

        ttk.Label(self.config_frame, text="本地网卡IP:").grid(row=0, column=0, padx=5, pady=5, sticky=tk.W)
        self.local_ip_var = StringVar()
        self.local_ip_combo = ttk.Combobox(self.config_frame, textvariable=self.local_ip_var, width=18)
        self.local_ip_combo['values'] = self.local_ips
        self.local_ip_combo.current(0)
        self.local_ip_combo.grid(row=0, column=1, padx=5, pady=5)

        ttk.Label(self.config_frame, text="本地端口:").grid(row=0, column=2, padx=5, pady=5, sticky=tk.W)
        self.local_port_entry = ttk.Entry(self.config_frame, width=10)
        self.local_port_entry.grid(row=0, column=3, padx=5, pady=5)
        self.local_port_entry.insert(0, "5000")

        # 解析模式选择
        ttk.Label(self.config_frame, text="解析模式:").grid(row=0, column=4, padx=5, pady=5, sticky=tk.W)
        ttk.Radiobutton(self.config_frame, text="自动识别", variable=self.parse_mode, value="auto").grid(row=0, column=5, padx=2, pady=5, sticky=tk.W)
        ttk.Radiobutton(self.config_frame, text="文本", variable=self.parse_mode, value="text").grid(row=0, column=6, padx=2, pady=5, sticky=tk.W)
        ttk.Radiobutton(self.config_frame, text="十六进制", variable=self.parse_mode, value="hex").grid(row=0, column=7, padx=2, pady=5, sticky=tk.W)

        self.bind_btn = ttk.Button(self.config_frame, text="绑定并启动", command=self.toggle_bind)
        self.bind_btn.grid(row=0, column=8, padx=10, pady=5)

        # 2. 状态统计区域
        self.status_frame = ttk.LabelFrame(root, text="状态统计", padding="10")
        self.status_frame.grid(row=1, column=0, padx=10, pady=5, sticky=(tk.W, tk.E))

        self.status_var = StringVar(value="未绑定 | 速率: 0 KB/s | 包数: 0")
        ttk.Label(self.status_frame, textvariable=self.status_var).grid(row=0, column=0, padx=10, pady=5, sticky=tk.W)

        # 3. 目标配置区域
        self.target_frame = ttk.LabelFrame(root, text="目标配置", padding="10")
        self.target_frame.grid(row=2, column=0, padx=10, pady=5, sticky=(tk.W, tk.E))

        ttk.Label(self.target_frame, text="目标IP:").grid(row=0, column=0, padx=5, pady=5, sticky=tk.W)
        self.target_ip_entry = ttk.Entry(self.target_frame, width=20)
        self.target_ip_entry.grid(row=0, column=1, padx=5, pady=5)
        self.target_ip_entry.insert(0, "127.0.0.1")

        ttk.Label(self.target_frame, text="目标端口:").grid(row=0, column=2, padx=5, pady=5, sticky=tk.W)
        self.target_port_entry = ttk.Entry(self.target_frame, width=10)
        self.target_port_entry.grid(row=0, column=3, padx=5, pady=5)
        self.target_port_entry.insert(0, "6000")

        # 发送模式选择
        ttk.Label(self.target_frame, text="发送模式:").grid(row=0, column=4, padx=5, pady=5, sticky=tk.W)
        self.send_mode = StringVar(value="text")
        ttk.Radiobutton(self.target_frame, text="文本", variable=self.send_mode, value="text").grid(row=0, column=5, padx=2, pady=5, sticky=tk.W)
        ttk.Radiobutton(self.target_frame, text="十六进制", variable=self.send_mode, value="hex").grid(row=0, column=6, padx=2, pady=5, sticky=tk.W)

        # 4. 数据显示区域（启用自动换行）
        self.log_frame = ttk.LabelFrame(root, text="数据日志（自动换行）", padding="10")
        self.log_frame.grid(row=3, column=0, padx=10, pady=5, sticky=(tk.W, tk.E, tk.N, tk.S))
        root.grid_rowconfigure(3, weight=1)
        root.grid_columnconfigure(0, weight=1)

        self.log_text = scrolledtext.ScrolledText(
            self.log_frame, 
            width=110, 
            height=18, 
            state=tk.DISABLED,
            wrap=tk.WORD  # 文本自动换行
        )
        self.log_text.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        self.log_frame.grid_rowconfigure(0, weight=1)
        self.log_frame.grid_columnconfigure(0, weight=1)

        # 5. 发送区域
        self.send_frame = ttk.LabelFrame(root, text="发送数据", padding="10")
        self.send_frame.grid(row=4, column=0, padx=10, pady=5, sticky=(tk.W, tk.E))

        self.send_entry = ttk.Entry(self.send_frame, width=100)
        self.send_entry.grid(row=0, column=0, padx=5, pady=5, sticky=(tk.W, tk.E))
        self.send_frame.grid_columnconfigure(0, weight=1)

        self.send_btn = ttk.Button(self.send_frame, text="发送", command=self.send_data)
        self.send_btn.grid(row=0, column=1, padx=10, pady=5)
        self.clear_btn = ttk.Button(self.send_frame, text="清空日志", command=self.clear_log)
        self.clear_btn.grid(row=0, column=2, padx=10, pady=5)

        # 启动刷新
        self.last_stat_time = time.time()
        self.root.after(self.ui_refresh_interval, self.refresh_ui)

    # 获取本地IP
    def get_local_ips(self):
        ips = set()
        try:
            for iface in netifaces.interfaces():
                addrs = netifaces.ifaddresses(iface)
                if netifaces.AF_INET in addrs:
                    for addr_info in addrs[netifaces.AF_INET]:
                        ip = addr_info.get('addr')
                        if ip:
                            ips.add(ip)
            ip_list = list(ips)
            ip_list.sort(key=lambda x: x.startswith('127.'))
            return ip_list if ip_list else ["127.0.0.1"]
        except Exception as e:
            self.add_log(f"获取网卡IP失败: {str(e)}")
            return ["127.0.0.1"]

    # 绑定/解绑
    def toggle_bind(self):
        if not self.is_running:
            local_ip = self.local_ip_var.get().strip()
            local_port = self.local_port_entry.get().strip()
            if not local_ip or not local_port:
                messagebox.showerror("错误", "IP和端口不能为空！")
                return
            try:
                local_port = int(local_port)
                if not (1 <= local_port <= 65535):
                    raise ValueError
            except ValueError:
                messagebox.showerror("错误", "端口必须是1-65535的整数！")
                return

            try:
                if self.udp_socket:
                    self.udp_socket.close()
                self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                self.udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2 * 1024 * 1024)
                self.udp_socket.bind((local_ip, local_port))
                self.is_running = True
                self.bound_ip = local_ip
                self.bound_port = local_port
                self.bind_btn.config(text="解绑并停止")
                self.add_log(f"✅ 成功绑定到 {local_ip}:{local_port}")
                self.packet_count = 0
                self.byte_count = 0
                self.last_stat_time = time.time()
                self.receive_thread = threading.Thread(target=self.receive_data, daemon=True)
                self.receive_thread.start()
            except Exception as e:
                messagebox.showerror("绑定失败", f"错误: {str(e)}")
                self.udp_socket = None
        else:
            try:
                if self.udp_socket:
                    self.udp_socket.close()
            except Exception:
                pass
            finally:
                self.is_running = False
                self.bound_ip = None
                self.bound_port = None
                self.bind_btn.config(text="绑定并启动")
                self.add_log("❌ 已解绑")

    # 核心：带换行的解析逻辑
    def wrap_text(self, text, line_length):
        """将文本按指定长度换行"""
        return '\n'.join([text[i:i+line_length] for i in range(0, len(text), line_length)])

    def parse_data(self, data):
        """解析数据并自动换行"""
        mode = self.parse_mode.get()
        if mode == "hex":
            # 十六进制换行：每32个字符（16字节）换一行
            hex_str = binascii.hexlify(data).decode('utf-8', errors='ignore')
            return self.wrap_text(hex_str, self.hex_line_length)
        elif mode == "text":
            # 文本换行：每80个字符换一行，保留原始换行符
            text = data.decode('utf-8', errors='replace')
            # 先按原始换行符分割，再对每行进行长度换行
            lines = []
            for line in text.split('\n'):
                lines.append(self.wrap_text(line, self.text_line_length))
            return '\n'.join(lines)
        else:  # auto模式
            try:
                text = data.decode('utf-8')
                if all(32 <= ord(c) <= 126 or c in '\r\n\t' for c in text):
                    # 文本模式换行
                    lines = []
                    for line in text.split('\n'):
                        lines.append(self.wrap_text(line, self.text_line_length))
                    return '\n'.join(lines)
                else:
                    # 二进制按十六进制换行
                    hex_str = binascii.hexlify(data).decode('utf-8')
                    return f"[二进制]\n{self.wrap_text(hex_str, self.hex_line_length)}"
            except UnicodeDecodeError:
                # 解码失败按十六进制换行
                hex_str = binascii.hexlify(data).decode('utf-8')
                return f"[二进制]\n{self.wrap_text(hex_str, self.hex_line_length)}"

    # 接收数据并解析（带换行）
    def receive_data(self):
        while self.is_running and self.bound_ip and self.bound_port:
            try:
                data, addr = self.udp_socket.recvfrom(self.receive_buffer_size)
                data_len = len(data)
                if data_len == 0:
                    continue

                self.packet_count += 1
                self.byte_count += data_len

                # 解析并换行
                parsed_data = self.parse_data(data)

                # 长数据不截断，依赖换行展示
                self.log_queue.put(f"[接收] {addr[0]}:{addr[1]} ({data_len}字节):\n{parsed_data}")
            except Exception as e:
                if self.is_running:
                    self.add_log(f"接收错误: {str(e)}")
                break

    # 发送数据
    def send_data(self):
        if not self.is_running:
            messagebox.showwarning("警告", "请先绑定并启动监听！")
            return

        target_ip = self.target_ip_entry.get().strip()
        target_port = self.target_port_entry.get().strip()
        send_str = self.send_entry.get().strip()

        if not target_ip or not target_port:
            messagebox.showerror("错误", "目标IP和端口不能为空！")
            return
        try:
            target_port = int(target_port)
            if not (1 <= target_port <= 65535):
                raise ValueError
        except ValueError:
            messagebox.showerror("错误", "目标端口必须是1-65535的整数！")
            return
        if not send_str:
            messagebox.showwarning("警告", "发送数据不能为空！")
            return

        try:
            if self.send_mode.get() == "hex":
                send_str = send_str.replace(" ", "").replace("\n", "")
                if len(send_str) % 2 != 0:
                    raise ValueError("十六进制数据长度必须为偶数")
                data = binascii.unhexlify(send_str)
            else:
                data = send_str.encode('utf-8')

            self.udp_socket.sendto(data, (target_ip, target_port))
            self.add_log(f"[发送] 到 {target_ip}:{target_port} ({len(data)}字节):\n{send_str}")
            self.send_entry.delete(0, tk.END)
        except Exception as e:
            messagebox.showerror("发送失败", f"错误: {str(e)}")

    # 日志处理（保留换行符）
    def add_log(self, content):
        current_time = time.strftime("%H:%M:%S", time.localtime())
        log_content = f"[{current_time}] {content}\n"  # 每条日志末尾加空行分隔
        if self.log_queue.full():
            self.log_queue.get()
        self.log_queue.put(log_content)

    # 刷新UI（保留换行格式）
    def refresh_ui(self):
        # 速率统计
        current_time = time.time()
        elapsed = current_time - self.last_stat_time
        if elapsed > 1.0 and self.is_running:
            speed_kb = (self.byte_count / 1024) / elapsed
            self.status_var.set(
                f"已绑定 {self.bound_ip}:{self.bound_port} | "
                f"速率: {speed_kb:.1f} KB/s | "
                f"包数: {self.packet_count}"
            )
            self.byte_count = 0
            self.packet_count = 0
            self.last_stat_time = current_time
        elif not self.is_running:
            self.status_var.set("未绑定 | 速率: 0 KB/s | 包数: 0")

        # 日志刷新（保留原始换行）
        logs = []
        count = 0
        while not self.log_queue.empty() and count < 50:  # 减少单次处理量，避免卡顿
            logs.append(self.log_queue.get())
            self.log_queue.task_done()
            count += 1

        if logs:
            self.log_text.config(state=tk.NORMAL)
            current_lines = int(self.log_text.index('end-1c').split('.')[0])
            # 估算新增行数（每条日志平均5行）
            estimated_new_lines = len(logs) * 5
            if current_lines + estimated_new_lines > self.max_display_lines:
                delete_lines = current_lines + estimated_new_lines - self.max_display_lines
                self.log_text.delete(f"1.0", f"{delete_lines + 1}.0")
            # 直接插入带换行的日志
            self.log_text.insert(tk.END, ''.join(logs))
            self.log_text.see(tk.END)
            self.log_text.config(state=tk.DISABLED)

        self.root.after(self.ui_refresh_interval, self.refresh_ui)

    # 清空日志
    def clear_log(self):
        self.log_text.config(state=tk.NORMAL)
        self.log_text.delete(1.0, tk.END)
        self.log_text.config(state=tk.DISABLED)
        while not self.log_queue.empty():
            self.log_queue.get()
            self.log_queue.task_done()

if __name__ == "__main__":
    try:
        import netifaces
    except ImportError:
        print("正在安装必要依赖...")
        import subprocess
        subprocess.check_call([sys.executable, "-m", "pip", "install", "netifaces"])
    root = tk.Tk()
    app = UdpNewLineDebugger(root)
    root.mainloop()
