import serial
import time
import sys

END = b'\xC0'  # SLIP 结束标志
ESC = b'\xDB'  # SLIP 转义字符
ESC_END = b'\xDC'  # 替代 0xC0
ESC_ESC = b'\xDD'  # 替代 0xDB

PACKET_SIZE = 1024  # 每包数据长度


def slip_encode(packet: bytes) -> bytes:
    """
    使用 SLIP 协议封装数据
    """
    encoded = END
    for byte in packet:
        if byte == 0xC0:
            encoded += ESC + ESC_END
        elif byte == 0xDB:
            encoded += ESC + ESC_ESC
        else:
            encoded += bytes([byte])
    encoded += END
    return encoded


def send_firmware(serial_port: str, baudrate: int, firmware_path: str):
    """
    通过串口发送固件文件，等待设备返回 "OK" 继续下一包
    """
    try:
        ser = serial.Serial(serial_port, baudrate, timeout=2)
        print(f"[INFO] 打开串口 {serial_port}，波特率 {baudrate}")
        
        with open(firmware_path, "rb") as f:
            firmware_data = f.read()
        
        total_size = len(firmware_data)
        print(f"[INFO] 读取固件文件 {firmware_path}，大小 {total_size} 字节")
        
        offset = 0
        while offset < total_size:
            chunk = firmware_data[offset:offset + PACKET_SIZE]
            slip_data = slip_encode(chunk)
            
            print(f"[INFO] 发送数据包 {offset // PACKET_SIZE + 1} / {(total_size + PACKET_SIZE - 1) // PACKET_SIZE}")
            ser.write(slip_data)
            ser.flush()
            
            # 等待设备返回 "OK"
            response = ser.read_until(END).strip(END)
            if response == b'OK':
                print("[INFO] 设备确认接收，发送下一包...")
                offset += PACKET_SIZE
            else:
                print("[WARNING] 设备未正确响应，重试发送...")
            
        print("[INFO] 固件发送完成！")
        ser.close()
    except serial.SerialException as e:
        print(f"[ERROR] 串口错误: {e}")
    except FileNotFoundError:
        print(f"[ERROR] 固件文件未找到: {firmware_path}")
    except Exception as e:
        print(f"[ERROR] 发生异常: {e}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("用法: python upgrade.py <串口号> <波特率> <固件文件路径>")
        sys.exit(1)
    
    port = sys.argv[1]
    baud = int(sys.argv[2])
    firmware = sys.argv[3]
    
    send_firmware(port, baud, firmware)
