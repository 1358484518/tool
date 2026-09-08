import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, StringVar
import socket
import threading
import time
import netifaces
from queue import Queue
import sys
import binascii

class NetworkDebugger:
    def __init__(self, root):
        self.root = root
        self.root.title("网络调试助手（UDP/TCP 双协议）")
        self.root.geometry("1100x750")
        self.root.resizable(width=False, height=False)

        # 核心配置
        self.log_queue = Queue(maxsize=5000)
        self.ui_refresh_interval = 200
        self.max_display_lines = 1000
        self.receive_buffer_size = 65536  # 64KB
        self.hex_line_length = 32  # 十六进制每行32字符（16字节）
        self.text_line_length = 80  # 文本每行80字符

        # 网络状态变量
        self.socket = None  # 通用socket
        self.is_running = False  # 监听/连接状态
        self.protocol = StringVar(value="udp")  # 默认UDP
        self.tcp_mode = StringVar(value="client")  # TCP模式：client/server
        self.bound_ip = None
        self.bound_port = None
        self.connected_ip = None  # TCP客户端连接的服务器IP
        self.connected_port = None  # TCP客户端连接的服务器端口
        self.tcp_clients = []  # TCP服务端的客户端列表
        self.packet_count = 0
        self.byte_count = 0

        # 解析模式
        self.parse_mode = StringVar(value="auto")  # auto/text/hex

        # 本地IP列表
        self.local_ips = self.get_local_ips()

        # 创建UI
        self.create_widgets()

        # 启动刷新
        self.last_stat_time = time.time()
        self.root.after(self.ui_refresh_interval, self.refresh_ui)

    def create_widgets(self):
        # 1. 协议选择区域
        self.protocol_frame = ttk.LabelFrame(self.root, text="协议选择", padding="10")
        self.protocol_frame.grid(row=0, column=0, padx=10, pady=5, sticky=(tk.W, tk.E))

        ttk.Radiobutton(self.protocol_frame, text="UDP", variable=self.protocol, value="udp", command=self.update_ui).grid(row=0, column=0, padx=10, pady=5)
        ttk.Radiobutton(self.protocol_frame, text="TCP", variable=self.protocol, value="tcp", command=self.update_ui).grid(row=0, column=1, padx=10, pady=5)
        ttk.Label(self.protocol_frame, text="TCP模式:").grid(row=0, column=2, padx=5, pady=5, sticky=tk.W)
        ttk.Radiobutton(self.protocol_frame, text="客户端", variable=self.tcp_mode, value="client").grid(row=0, column=3, padx=2, pady=5)
        ttk.Radiobutton(self.protocol_frame, text="服务端", variable=self.tcp_mode, value="server").grid(row=0, column=4, padx=2, pady=5)

        # 2. 本地配置区域
        self.local_frame = ttk.LabelFrame(self.root, text="本地配置", padding="10")
        self.local_frame.grid(row=1, column=0, padx=10, pady=5, sticky=(tk.W, tk.E))

        ttk.Label(self.local_frame, text="本地IP:").grid(row=0, column=0, padx=5, pady=5, sticky=tk.W)
        self.local_ip_var = StringVar()
        self.local_ip_combo = ttk.Combobox(self.local_frame, textvariable=self.local_ip_var, width=18)
        self.local_ip_combo['values'] = self.local_ips
        self.local_ip_combo.current(0)
        self.local_ip_combo.grid(row=0, column=1, padx=5, pady=5)

        ttk.Label(self.local_frame, text="本地端口:").grid(row=0, column=2, padx=5, pady=5, sticky=tk.W)
        self.local_port_entry = ttk.Entry(self.local_frame, width=10)
        self.local_port_entry.grid(row=0, column=3, padx=5, pady=5)
        self.local_port_entry.insert(0, "5000")

        # 3. 目标配置区域（TCP客户端/UDP需要）
        self.target_frame = ttk.LabelFrame(self.root, text="目标配置", padding="10")
        self.target_frame.grid(row=2, column=0, padx=10, pady=5, sticky=(tk.W, tk.E))

        ttk.Label(self.target_frame, text="目标IP:").grid(row=0, column=0, padx=5, pady=5, sticky=tk.W)
        self.target_ip_entry = ttk.Entry(self.target_frame, width=20)
        self.target_ip_entry.grid(row=0, column=1, padx=5, pady=5)
        self.target_ip_entry.insert(0, "127.0.0.1")

        ttk.Label(self.target_frame, text="目标端口:").grid(row=0, column=2, padx=5, pady=5, sticky=tk.W)
        self.target_port_entry = ttk.Entry(self.target_frame, width=10)
        self.target_port_entry.grid(row=0, column=3, padx=5, pady=5)
        self.target_port_entry.insert(0, "6000")

        # 4. 控制与状态区域
        self.control_frame = ttk.LabelFrame(self.root, text="控制与状态", padding="10")
        self.control_frame.grid(row=3, column=0, padx=10, pady=5, sticky=(tk.W, tk.E))

        self.control_btn = ttk.Button(self.control_frame, text="启动/连接", command=self.toggle_connection)
        self.control_btn.grid(row=0, column=0, padx=10, pady=5)

        self.status_var = StringVar(value="未启动 | 速率: 0 KB/s | 包数: 0")
        ttk.Label(self.control_frame, textvariable=self.status_var).grid(row=0, column=1, padx=20, pady=5, sticky=tk.W)

        # 5. 解析与发送模式区域
        self.mode_frame = ttk.LabelFrame(self.root, text="解析与发送模式", padding="10")
        self.mode_frame.grid(row=4, column=0, padx=10, pady=5, sticky=(tk.W, tk.E))

        ttk.Label(self.mode_frame, text="解析模式:").grid(row=0, column=0, padx=5, pady=5, sticky=tk.W)
        ttk.Radiobutton(self.mode_frame, text="自动", variable=self.parse_mode, value="auto").grid(row=0, column=1, padx=2, pady=5)
        ttk.Radiobutton(self.mode_frame, text="文本", variable=self.parse_mode, value="text").grid(row=0, column=2, padx=2, pady=5)
        ttk.Radiobutton(self.mode_frame, text="十六进制", variable=self.parse_mode, value="hex").grid(row=0, column=3, padx=2, pady=5)

        ttk.Label(self.mode_frame, text="发送模式:").grid(row=0, column=4, padx=15, pady=5, sticky=tk.W)
        self.send_mode = StringVar(value="text")
        ttk.Radiobutton(self.mode_frame, text="文本", variable=self.send_mode, value="text").grid(row=0, column=5, padx=2, pady=5)
        ttk.Radiobutton(self.mode_frame, text="十六进制", variable=self.send_mode, value="hex").grid(row=0, column=6, padx=2, pady=5)

        # 6. 数据显示区域
        self.log_frame = ttk.LabelFrame(self.root, text="数据日志", padding="10")
        self.log_frame.grid(row=5, column=0, padx=10, pady=5, sticky=(tk.W, tk.E, tk.N, tk.S))
        self.root.grid_rowconfigure(5, weight=1)
        self.root.grid_columnconfigure(0, weight=1)

        self.log_text = scrolledtext.ScrolledText(
            self.log_frame, 
            width=120, 
            height=18, 
            state=tk.DISABLED,
            wrap=tk.WORD
        )
        self.log_text.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        self.log_frame.grid_rowconfigure(0, weight=1)
        self.log_frame.grid_columnconfigure(0, weight=1)

        # 7. 发送区域
        self.send_frame = ttk.LabelFrame(self.root, text="发送数据", padding="10")
        self.send_frame.grid(row=6, column=0, padx=10, pady=5, sticky=(tk.W, tk.E))

        self.send_entry = ttk.Entry(self.send_frame, width=110)
        self.send_entry.grid(row=0, column=0, padx=5, pady=5, sticky=(tk.W, tk.E))
        self.send_frame.grid_columnconfigure(0, weight=1)

        self.send_btn = ttk.Button(self.send_frame, text="发送", command=self.send_data)
        self.send_btn.grid(row=0, column=1, padx=10, pady=5)
        self.clear_btn = ttk.Button(self.send_frame, text="清空日志", command=self.clear_log)
        self.send_btn.grid(row=0, column=1, padx=10, pady=5)
        self.clear_btn = ttk.Button(self.send_frame, text="清空日志", command=self.clear_log)
        self.clear_btn.grid(row=0, column=2, padx=10, pady=5)

        # 初始UI更新
        self.update_ui()

    def update_ui(self):
        """根据协议切换UI状态"""
        protocol = self.protocol.get()
        if protocol == "udp":
            # UDP模式：目标配置可见
            self.target_frame.grid()
            self.control_btn.config(text="启动监听")
        else:
            # TCP模式：根据客户端/服务端切换
            if self.tcp_mode.get() == "client":
                # TCP客户端：目标配置可见（连接服务器用）
                self.target_frame.grid()
                self.control_btn.config(text="连接服务器")
            else:
                # TCP服务端：目标配置不可见（无需目标）
                self.target_frame.grid_remove()
                self.control_btn.config(text="启动服务端")

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

    # 核心：启动/连接/停止控制
    def toggle_connection(self):
        if not self.is_running:
            self.start_connection()
        else:
            self.stop_connection()

    def start_connection(self):
        protocol = self.protocol.get()
        local_ip = self.local_ip_var.get().strip()
        local_port = self.local_port_entry.get().strip()

        # 校验本地端口
        if not local_port:
            messagebox.showerror("错误", "本地端口不能为空！")
            return
        try:
            local_port = int(local_port)
            if not (1 <= local_port <= 65535):
                raise ValueError
        except ValueError:
            messagebox.showerror("错误", "本地端口必须是1-65535的整数！")
            return

        try:
            if protocol == "udp":
                # UDP启动监听
                self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2 * 1024 * 1024)
                self.socket.bind((local_ip, local_port))
                self.bound_ip = local_ip
                self.bound_port = local_port
                self.is_running = True
                self.add_log(f"✅ UDP 已启动监听 {local_ip}:{local_port}")
                threading.Thread(target=self.udp_receive, daemon=True).start()

            else:  # TCP
                if self.tcp_mode.get() == "client":
                    # TCP客户端连接服务器
                    target_ip = self.target_ip_entry.get().strip()
                    target_port = self.target_port_entry.get().strip()
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

                    self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    self.socket.bind((local_ip, local_port))  # 绑定本地端口
                    self.socket.connect((target_ip, target_port))  # 连接服务器
                    self.bound_ip = local_ip
                    self.bound_port = local_port
                    self.connected_ip = target_ip
                    self.connected_port = target_port
                    self.is_running = True
                    self.add_log(f"✅ TCP客户端 已连接到 {target_ip}:{target_port}（本地 {local_ip}:{local_port}）")
                    threading.Thread(target=self.tcp_client_receive, daemon=True).start()

                else:  # TCP服务端
                    self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)  # 允许端口复用
                    self.socket.bind((local_ip, local_port))
                    self.socket.listen(5)  # 最大连接数5
                    self.bound_ip = local_ip
                    self.bound_port = local_port
                    self.is_running = True
                    self.add_log(f"✅ TCP服务端 已启动监听 {local_ip}:{local_port}（等待连接...）")
                    threading.Thread(target=self.tcp_server_accept, daemon=True).start()

            # 更新按钮状态
            self.control_btn.config(text="停止")
            self.packet_count = 0
            self.byte_count = 0
            self.last_stat_time = time.time()

        except Exception as e:
            messagebox.showerror("启动失败", f"错误: {str(e)}")
            self.socket = None

    def stop_connection(self):
        try:
            if self.socket:
                self.socket.close()
            # 关闭TCP服务端的客户端连接
            for client in self.tcp_clients:
                try:
                    client.close()
                except:
                    pass
            self.tcp_clients.clear()
        except Exception:
            pass
        finally:
            protocol = self.protocol.get()
            if protocol == "udp":
                self.add_log(f"❌ UDP 已停止监听 {self.bound_ip}:{self.bound_port}")
            else:
                if self.tcp_mode.get() == "client":
                    self.add_log(f"❌ TCP客户端 已断开与 {self.connected_ip}:{self.connected_port} 的连接")
                else:
                    self.add_log(f"❌ TCP服务端 已停止监听 {self.bound_ip}:{self.bound_port}")
            
            self.is_running = False
            self.socket = None
            self.bound_ip = None
            self.bound_port = None
            self.connected_ip = None
            self.connected_port = None
            self.control_btn.config(text="启动/连接")
            self.update_ui()

    # 数据解析与换行
    def wrap_text(self, text, line_length):
        return '\n'.join([text[i:i+line_length] for i in range(0, len(text), line_length)])

    def parse_data(self, data):
        mode = self.parse_mode.get()
        if mode == "hex":
            hex_str = binascii.hexlify(data).decode('utf-8', errors='ignore')
            return self.wrap_text(hex_str, self.hex_line_length)
        elif mode == "text":
            text = data.decode('utf-8', errors='replace')
            lines = []
            for line in text.split('\n'):
                lines.append(self.wrap_text(line, self.text_line_length))
            return '\n'.join(lines)
        else:  # auto
            try:
                text = data.decode('utf-8')
                if all(32 <= ord(c) <= 126 or c in '\r\n\t' for c in text):
                    lines = []
                    for line in text.split('\n'):
                        lines.append(self.wrap_text(line, self.text_line_length))
                    return '\n'.join(lines)
                else:
                    hex_str = binascii.hexlify(data).decode('utf-8')
                    return f"[二进制]\n{self.wrap_text(hex_str, self.hex_line_length)}"
            except UnicodeDecodeError:
                hex_str = binascii.hexlify(data).decode('utf-8')
                return f"[二进制]\n{self.wrap_text(hex_str, self.hex_line_length)}"

    # UDP接收
    def udp_receive(self):
        while self.is_running and self.socket:
            try:
                data, addr = self.socket.recvfrom(self.receive_buffer_size)
                data_len = len(data)
                if data_len == 0:
                    continue
                self.packet_count += 1
                self.byte_count += data_len
                parsed_data = self.parse_data(data)
                self.log_queue.put(f"[UDP接收] {addr[0]}:{addr[1]} ({data_len}字节):\n{parsed_data}")
            except Exception as e:
                if self.is_running:
                    self.add_log(f"UDP接收错误: {str(e)}")
                break

    # TCP客户端接收
    def tcp_client_receive(self):
        while self.is_running and self.socket:
            try:
                data = self.socket.recv(self.receive_buffer_size)
                data_len = len(data)
                if data_len == 0:
                    self.add_log(f"[TCP客户端] 服务器 {self.connected_ip}:{self.connected_port} 已断开连接")
                    self.stop_connection()
                    break
                self.packet_count += 1
                self.byte_count += data_len
                parsed_data = self.parse_data(data)
                self.log_queue.put(f"[TCP接收] 来自 {self.connected_ip}:{self.connected_port} ({data_len}字节):\n{parsed_data}")
            except Exception as e:
                if self.is_running:
                    self.add_log(f"TCP接收错误: {str(e)}")
                break

    # TCP服务端接受连接
    def tcp_server_accept(self):
        while self.is_running and self.socket:
            try:
                client_socket, client_addr = self.socket.accept()
                self.tcp_clients.append(client_socket)
                self.add_log(f"[TCP服务端] 新客户端连接: {client_addr[0]}:{client_addr[1]}（当前连接数: {len(self.tcp_clients)}）")
                # 为每个客户端启动接收线程
                threading.Thread(
                    target=self.tcp_server_receive,
                    args=(client_socket, client_addr),
                    daemon=True
                ).start()
            except Exception as e:
                if self.is_running:
                    self.add_log(f"TCP服务端错误: {str(e)}")
                break

    # TCP服务端接收客户端数据
    def tcp_server_receive(self, client_socket, client_addr):
        while self.is_running and client_socket in self.tcp_clients:
            try:
                data = client_socket.recv(self.receive_buffer_size)
                data_len = len(data)
                if data_len == 0:
                    self.tcp_clients.remove(client_socket)
                    client_socket.close()
                    self.add_log(f"[TCP服务端] 客户端 {client_addr[0]}:{client_addr[1]} 已断开（当前连接数: {len(self.tcp_clients)}）")
                    break
                self.packet_count += 1
                self.byte_count += data_len
                parsed_data = self.parse_data(data)
                self.log_queue.put(f"[TCP接收] 来自 {client_addr[0]}:{client_addr[1]} ({data_len}字节):\n{parsed_data}")
            except Exception as e:
                if self.is_running and client_socket in self.tcp_clients:
                    self.tcp_clients.remove(client_socket)
                    client_socket.close()
                    self.add_log(f"[TCP服务端] 客户端 {client_addr[0]}:{client_addr[1]} 异常断开: {str(e)}（当前连接数: {len(self.tcp_clients)}）")
                break

    # 发送数据
    def send_data(self):
        if not self.is_running or not self.socket:
            messagebox.showwarning("警告", "请先启动/连接！")
            return

        send_str = self.send_entry.get().strip()
        if not send_str:
            messagebox.showwarning("警告", "发送数据不能为空！")
            return

        try:
            # 处理发送数据
            if self.send_mode.get() == "hex":
                send_str_clean = send_str.replace(" ", "").replace("\n", "")
                if len(send_str_clean) % 2 != 0:
                    raise ValueError("十六进制数据长度必须为偶数")
                data = binascii.unhexlify(send_str_clean)
            else:
                data = send_str.encode('utf-8')
            data_len = len(data)

            # 根据协议和模式发送
            protocol = self.protocol.get()
            if protocol == "udp":
                target_ip = self.target_ip_entry.get().strip()
                target_port = int(self.target_port_entry.get().strip())
                self.socket.sendto(data, (target_ip, target_port))
                self.log_queue.put(f"[UDP发送] 到 {target_ip}:{target_port} ({data_len}字节):\n{send_str}")
            else:  # TCP
                if self.tcp_mode.get() == "client":
                    # TCP客户端发送给服务器
                    self.socket.sendall(data)
                    self.log_queue.put(f"[TCP发送] 到 {self.connected_ip}:{self.connected_port} ({data_len}字节):\n{send_str}")
                else:
                    # TCP服务端发送给所有客户端
                    if not self.tcp_clients:
                        messagebox.showwarning("警告", "没有连接的客户端！")
                        return
                    for client in self.tcp_clients[:]:  # 复制列表避免迭代中修改
                        try:
                            client.sendall(data)
                        except:
                            self.tcp_clients.remove(client)
                            client.close()
                    self.log_queue.put(f"[TCP服务端] 发送给所有客户端（{len(self.tcp_clients)}个）({data_len}字节):\n{send_str}")

            self.send_entry.delete(0, tk.END)
        except Exception as e:
            messagebox.showerror("发送失败", f"错误: {str(e)}")

    # 日志处理
    def add_log(self, content):
        current_time = time.strftime("%H:%M:%S", time.localtime())
        log_content = f"[{current_time}] {content}\n"
        if self.log_queue.full():
            self.log_queue.get()
        self.log_queue.put(log_content)

    # 刷新UI
    def refresh_ui(self):
        # 速率统计
        current_time = time.time()
        elapsed = current_time - self.last_stat_time
        if elapsed > 1.0 and self.is_running:
            speed_kb = (self.byte_count / 1024) / elapsed
            self.status_var.set(
                f"运行中 | 速率: {speed_kb:.1f} KB/s | 包数: {self.packet_count} | "
                f"本地: {self.bound_ip}:{self.bound_port}"
            )
            self.byte_count = 0
            self.packet_count = 0
            self.last_stat_time = current_time
        elif not self.is_running:
            self.status_var.set("未启动 | 速率: 0 KB/s | 包数: 0")

        # 日志刷新
        logs = []
        count = 0
        while not self.log_queue.empty() and count < 50:
            logs.append(self.log_queue.get())
            self.log_queue.task_done()
            count += 1

        if logs:
            self.log_text.config(state=tk.NORMAL)
            current_lines = int(self.log_text.index('end-1c').split('.')[0])
            estimated_new_lines = len(logs) * 5
            if current_lines + estimated_new_lines > self.max_display_lines:
                delete_lines = current_lines + estimated_new_lines - self.max_display_lines
                self.log_text.delete(f"1.0", f"{delete_lines + 1}.0")
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
    app = NetworkDebugger(root)
    root.mainloop()
