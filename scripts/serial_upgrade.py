import serial
import time
import sys

END = b'\xC0'  # SLIP end character
ESC = b'\xDB'  # SLIP escape Characters
ESC_END = b'\xDC'  # replace END with this character
ESC_ESC = b'\xDD'  # replace ESC with this character

PACKET_SIZE = 512  # 每包数据长度


def slip_encode(packet: bytes) -> bytes:
    """
    use SLIP protocol to encode a packet
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
    send firmware to device via serial port, wait for device to confirm each packet before sending next packet.
    """
    try:
        ser = serial.Serial(serial_port, baudrate, timeout=5)
        print(f"[INFO] open UART {serial_port}，baudrate {baudrate}")
        
        with open(firmware_path, "rb") as f:
            firmware_data = f.read()
        
        total_size = len(firmware_data)
        print(f"[INFO] read image file {firmware_path}，file length is {total_size} bytes")
        
        # send "transfer(x bytes)" command with SLIP encoding
        transfer_command = f"transfer({total_size} bytes)".encode()
        ser.write(slip_encode(transfer_command))
        ser.flush()
        print("[INFO] send transfer command, wait for confirmation...")
        
        while True:
            response = ser.readline().decode(errors='ignore').strip()
            if response:
                print(f"[DEVICE] {response}")
            if "Fetch file length:" in response:
                print("[INFO] require file length, now start to transfer image data...")
                break
        
        offset = 0
        packet_index = 1
        total_packets = (total_size + PACKET_SIZE - 1) // PACKET_SIZE
        
        while offset < total_size:
            chunk = firmware_data[offset:offset + PACKET_SIZE]
            chunk_size = len(chunk)
            # slip_data = slip_encode(chunk)
            
            print(f"[INFO] send packet {packet_index} / {total_packets}, length is {chunk_size} bytes")
            ser.write(chunk)
            ser.flush()
            
            # # wait ack "Received image:"
            while True:
                response = ser.readline().decode(errors='ignore').strip()
                if response:
                    print(f"[DEVICE] {response}")
                if "Received image:" in response:
                    print("[INFO] device response ack, now send next packet...")
                    offset += chunk_size
                    packet_index += 1
                    break
            # # response = ser.read_until(END).strip(END)
            # response = ser.read_until(b"Received image:").decode(errors='ignore')
            # # print(f"response: {response}")
            # # if response == b'Received image:':
            # if "Received image:" in response:
            #     print("[INFO] device response ack, now send next packet...")
            #     offset += PACKET_SIZE
            #     packet_index += 1
            # else:
            #     print("[WARNING] Can not receive ack, now resend...")
            
        print("[INFO] firmware been sent finished！")
        # ser.close()
        while True:
            response = ser.readline().decode(errors='ignore').strip()
            if response:
                print(f"[DEVICE] {response}")
    except serial.SerialException as e:
        print(f"[ERROR] uart error: {e}")
    except FileNotFoundError:
        print(f"[ERROR] can not find file: {firmware_path}")
    except Exception as e:
        print(f"[ERROR] exception occurred: {e}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: python serial_upgrade.py <COM_id> <baudrate> <file path>")
        sys.exit(1)
    
    port = sys.argv[1]
    baud = int(sys.argv[2])
    firmware = sys.argv[3]
    
    send_firmware(port, baud, firmware)
