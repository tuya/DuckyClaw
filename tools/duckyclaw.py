import serial
import serial.tools.list_ports
import subprocess
import threading
import time
import sys

# ===================== 配置参数（根据你的设备修改） =====================
SERIAL_PORT = "/dev/ttyACM2"  # Windows 示例，Linux/Mac 一般是 /dev/ttyUSB0 或 /dev/ttyACM0
BAUDRATE = 115200       # 串口波特率，需与设备匹配
TIMEOUT = 1           # 串口超时时间（秒）
ENCODING = "utf-8"    # 数据编码格式
CMD_END_MARK = "\n"   # 命令结束符（设备发送命令的结束标识，一般是换行）

# 全局串口对象
ser = None
# 程序运行标志
running = True

def list_available_ports():
    """列出可用的串口，方便你确认端口号"""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("未检测到可用串口！")
        return []
    print("可用串口列表：")
    for i, port in enumerate(ports):
        print(f"{i+1}. {port.device} - {port.description}")
    return [port.device for port in ports]

def init_serial():
    """初始化串口"""
    global ser
    try:
        ser = serial.Serial(
            port=SERIAL_PORT,
            baudrate=BAUDRATE,
            timeout=TIMEOUT,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            bytesize=serial.EIGHTBITS
        )
        if ser.is_open:
            print(f"串口 {SERIAL_PORT} 已成功打开（波特率：{BAUDRATE}）")
        return True
    except Exception as e:
        print(f"串口初始化失败：{e}")
        print("请检查端口号、波特率是否正确，或串口是否被占用")
        return False

def execute_command(cmd):
    """执行终端命令，返回执行结果（标准输出+标准错误）"""
    cmd = cmd.strip()  # 去除首尾空格/换行
    if not cmd:
        return "错误：空命令，无需执行\n"
    
    print(f"\n=== 开始执行命令：{cmd} ===")
    try:
        # 使用 shell=True 让 bash 解析命令，支持管道、重定向、cd && 等完整 shell 语法
        result = subprocess.run(
            cmd,
            shell=True,
            executable="/bin/bash",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding=ENCODING,
            timeout=30  # 命令超时时间（秒）
        )
        # 拼接输出和错误信息
        output = f"命令执行完成（返回码：{result.returncode}）\n"
        output += f"标准输出：\n{result.stdout}\n"
        if result.stderr:
            output += f"标准错误：\n{result.stderr}\n"
        return output
    except subprocess.TimeoutExpired:
        return f"错误：命令执行超时（30秒）\n"
    except Exception as e:
        return f"错误：命令执行失败 - {str(e)}\n"

def send_to_serial(data):
    """将数据发送到串口"""
    if ser and ser.is_open:
        try:
            # 编码为字节并发送
            ser.write(data.encode(ENCODING))
            print(f"已发送 {len(data)} 字节到串口")
        except Exception as e:
            print(f"串口发送失败：{e}")

def serial_listener():
    """串口监听线程：持续读取串口数据并处理"""
    global running
    buffer = ""  # 数据缓冲区（处理粘包/分段接收）
    print("\n开始监听串口数据...（按 Ctrl+C 退出）")
    
    while running:
        if ser and ser.is_open:
            try:
                # 读取串口可用数据（非阻塞）
                if ser.in_waiting > 0:
                    data = ser.read(ser.in_waiting).decode(ENCODING, errors="ignore")
                    buffer += data
                    # 检查是否收到完整命令（以结束符为标识）
                    if CMD_END_MARK in buffer:
                        # 拆分完整命令和剩余缓冲区
                        cmd, buffer = buffer.split(CMD_END_MARK, 1)
                        print(f"\n收到串口命令：{cmd}")
                        # 执行命令并获取结果
                        result = execute_command(cmd)
                        print(f"命令执行结果：{result}")
                        # 将结果发送回串口
                        send_to_serial(result)
                time.sleep(0.01)  # 降低CPU占用
            except Exception as e:
                print(f"串口读取失败：{e}")
                time.sleep(1)
        else:
            time.sleep(1)

def main():
    global running
    # 列出可用串口（方便调试）
    list_available_ports()
    
    # 初始化串口
    if not init_serial():
        sys.exit(1)
    
    try:
        # 启动串口监听线程（避免阻塞主线程）
        listener_thread = threading.Thread(target=serial_listener, daemon=True)
        listener_thread.start()
        
        # 主线程等待退出
        while running:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n\n接收到退出信号，正在关闭程序...")
        running = False
    finally:
        # 关闭串口
        if ser and ser.is_open:
            ser.close()
            print(f"串口 {SERIAL_PORT} 已关闭")
        print("程序已退出")

if __name__ == "__main__":
    main()
