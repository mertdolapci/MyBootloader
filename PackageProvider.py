#!/usr/bin/env python3
"""
STM32F401RE Bootloader Firmware Downloader
Protocol Packet format:
| START(1) | CMD(1) | LENGTH(2) | DATA(LENGTH) | CRC16(2) | STOP(1) |
START = 0xA5, STOP = 0x5A
CRC16 = CCITT-FALSE (poly=0x1021, init=0xFFFF, xorOut=0x0000, no reflect)

Commands (suggested):
0x01 HELO      : Bootloader connection test
0x02 VERSION   : Request version (optional)
0x03 ERASE     : Erase flash (DATA: 4 byte start_addr + 4 byte size) or just area size
0x04 WRITE     : Write to flash (DATA Payload: payload // Future Extension: 2 byte packet_index + 4 byte flash_offset)
0x05 FINISH    : Transfer finished / send total CRC
0x06 RESET     : Start application / MCU reset
ACK = 0x79 (example) | NACK = 0x1F (example)

Note: The bootloader side should be adjusted to support these commands and fields.
"""

import serial
import struct
import time
import sys
import os
from typing import Optional

# Serial port settings
PORT = "COM3"          # Linux: "/dev/ttyUSB0" or "/dev/ttyACM0"
BAUDRATE = 115200
TIMEOUT = 10.0          # seconds

# Packet / protocol constants
START_BYTE = 0xA5
STOP_BYTE  = 0x5A

CMD_HELO   = 0x01
CMD_VERSION= 0x02
CMD_ERASE  = 0x03
CMD_WRITE  = 0x04
CMD_FINISH = 0x05
CMD_RESET  = 0x06

ACK_BYTE   = 0x79      # Example byte using ST ROM bootloader ACK convention
NACK_BYTE  = 0x1F

PACKET_SIZE = 256      # Desired initial packet size
MAX_RETRIES = 5

# Flash Start address (bootloader application start)
APP_FLASH_START = 0x08020000  # Application Backup Start
ALIGN_WORD = 4                # Writing should be 4-byte aligned

# ---------------------------------------------------------
# CRC16-CCITT (FALSE) calculator (independent, slow but sufficient)
# ---------------------------------------------------------
def crc16_ccitt(data: bytes,
                poly: int = 0x1021,
                init_val: int = 0xFFFF,
                xor_out: int = 0x0000) -> int:
    crc = init_val
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) & 0xFFFF) ^ poly
            else:
                crc = (crc << 1) & 0xFFFF
    return crc ^ xor_out

# ---------------------------------------------------------
# CRC32 (for total firmware verification)
# ---------------------------------------------------------
def crc32(data: bytes) -> int:
    # Simple software implementation (optional acceleration possible)
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            mask = -(crc & 1)
            crc = (crc >> 1) ^ (0xEDB88320 & mask)
    return crc ^ 0xFFFFFFFF

# ---------------------------------------------------------
# Packet sending
# ---------------------------------------------------------

# New: Function that sends each field individually and expects an ACK
def send_field_and_expect_ack(ser: serial.Serial, field: bytes, context: str = "") -> bool:
    ser.write(field)
    print(f"[TX] {context}: {field.hex()} ({len(field)} bytes)")
    ack = read_response(ser, 1)
    if len(ack) == 0:
        print(f"[WARN] ACK timeout ({context})")
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        return False
    if ack[0] == ACK_BYTE:
        return True
    print(f"[WARN] NACK or unknown response (0x{ack[0]:02X}) ({context})")
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    return False

def read_response(ser: serial.Serial, expected_len: int = 1) -> bytes:
    resp = ser.read(expected_len)
    return resp

def expect_ack(ser: serial.Serial, context: str = "") -> bool:
    b = read_response(ser, 1)
    if len(b) == 0:
        print(f"[WARN] ACK timeout ({context})")
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        return False
    if b[0] == ACK_BYTE:
        return True
    print(f"[WARN] NACK or unknown response (0x{b[0]:02X}) ({context})")
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    return False

# ---------------------------------------------------------
# Command helpers
# ---------------------------------------------------------
def cmd_helo(ser: serial.Serial) -> bool:
    
    fields = [
        struct.pack("<B", START_BYTE),
        struct.pack("<B", CMD_HELO),
        struct.pack("<H", 0), # LENGTH=0
    ]
    raw = b''.join(fields)
    crc = crc16_ccitt(raw)
    fields.append(struct.pack("<H", crc))
    fields.append(struct.pack("<B", STOP_BYTE))

    for i, field in enumerate(fields):
        if not send_field_and_expect_ack(ser, field, f"HELO field {i}"):
            return False
    return True

def cmd_erase(ser: serial.Serial) -> bool:
    fields = [
        struct.pack("<B", START_BYTE),
        struct.pack("<B", CMD_ERASE),
        struct.pack("<H", 0),
 
    ]
    raw = b''.join(fields)
    crc = crc16_ccitt(raw)
    fields.append(struct.pack("<H", crc))
    fields.append(struct.pack("<B", STOP_BYTE))

    for i, field in enumerate(fields):
        if not send_field_and_expect_ack(ser, field, f"ERASE field {i}"):
            return False
    return True

def cmd_write(ser: serial.Serial, packet_index: int, flash_offset: int, data: bytes) -> bool:
    # DATA: 2 byte packet_index + 2 byte flash_offset + payload
    payload =  struct.pack("<H H", packet_index, flash_offset) + data
    fields = [
        struct.pack("<B", START_BYTE),
        struct.pack("<B", CMD_WRITE),
        struct.pack("<H", len(payload)),
        payload,
    ]
    raw = b''.join(fields)
    crc = crc16_ccitt(raw)
    fields.append(struct.pack("<H", crc))
    fields.append(struct.pack("<B", STOP_BYTE))

    for i, field in enumerate(fields):
        if not send_field_and_expect_ack(ser, field, f"WRITE idx={packet_index} field {i}"):
            return False
    return True

def cmd_finish(ser: serial.Serial, total_size: int, crc32_total: int) -> bool:
    payload = struct.pack("<I I", total_size, crc32_total)
    fields = [
        struct.pack("<B", START_BYTE),
        struct.pack("<B", CMD_FINISH),
        struct.pack("<H", len(payload)),
        payload,
    ]
    raw = b''.join(fields)
    crc = crc16_ccitt(raw)
    fields.append(struct.pack("<H", crc))
    fields.append(struct.pack("<B", STOP_BYTE))

    for i, field in enumerate(fields):
        if not send_field_and_expect_ack(ser, field, f"FINISH field {i}"):
            return False
    return True

def cmd_reset(ser: serial.Serial) -> bool:
    fields = [
        struct.pack("<B", START_BYTE),
        struct.pack("<B", CMD_RESET),
        struct.pack("<H", 0), # LENGTH=0
    ]
    raw = b''.join(fields)
    crc = crc16_ccitt(raw)
    fields.append(struct.pack("<H", crc))
    fields.append(struct.pack("<B", STOP_BYTE))

    for i, field in enumerate(fields):
        if not send_field_and_expect_ack(ser, field, f"RESET field {i}"):
            return False
    # Reset sonrası cevap alamayabilirsin; burada ACK beklemek opsiyonel
    return True

# ---------------------------------------------------------
# Firmware parçalama ve gönderme
# ---------------------------------------------------------
def chunk_firmware(fw: bytes, chunk_size: int) -> list[bytes]:
    chunks = []
    for i in range(0, len(fw), chunk_size):
        c = fw[i:i+chunk_size]
        # Word alignment padding if needed
        if len(c) % ALIGN_WORD != 0:
            c += b'\xFF' * (ALIGN_WORD - (len(c) % ALIGN_WORD))
        chunks.append(c)
    return chunks

def transfer_firmware(ser: serial.Serial, fw: bytes, base_addr: int) -> bool:
    chunks = chunk_firmware(fw, PACKET_SIZE)
    total_chunks = len(chunks)
    print(f"[INFO] Total chunks: {total_chunks}, Original size: {len(fw)} bytes")

    packet_start = time.time()

    for idx, ch in enumerate(chunks):
        flash_offset = idx * PACKET_SIZE
        # Retry mechanism
        for attempt in range(1, MAX_RETRIES + 1):
            ok = cmd_write(ser, idx, flash_offset, ch)
            if ok:
                break
            else:
                print(f"[RETRY] Paket {idx} attempt {attempt}")
                time.sleep(0.05)
        else:
            print(f"[ERROR] Paket {idx} göndermede basarisiz, abort.")
            return False

        # Progress display
        progress = (idx + 1) / total_chunks * 100
        sys.stdout.write(f"\r[PROGRESS] % {progress:6.2f}")
        sys.stdout.flush()
    
    packet_end = time.time()
    print(f"\n[INFO] Writing phase completed in {packet_end - packet_start:.2f} seconds.")
    
    return True

# ---------------------------------------------------------
# Main flow
# ---------------------------------------------------------
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} firmware.bin [port] [baud]")
        sys.exit(1)

    fw_path = sys.argv[1]
    port = sys.argv[2] if len(sys.argv) > 2 else PORT
    baud = int(sys.argv[3]) if len(sys.argv) > 3 else BAUDRATE

    if not os.path.isfile(fw_path):
        print(f"[ERROR] File not found: {fw_path}")
        sys.exit(1)

    with open(fw_path, "rb") as f:
        firmware = f.read()

    crc_total = crc32(firmware)
    print(f"[INFO] Firmware size: {len(firmware)} bytes, CRC32: 0x{crc_total:08X}")

    start = time.time()

    ser = serial.Serial(port, baud, timeout=TIMEOUT)
    time.sleep(0.2)

    print("[STEP] HELO...")
    if not cmd_helo(ser):
        print("[ERROR] Bootloader HELO failed.")
        ser.close()
        sys.exit(1)

    # Optional: Version query (if bootloader supports)
    # send_packet(ser, CMD_VERSION, b"")
    # version_resp = ser.read(4)  # Example
    # print("Bootloader version response:", version_resp.hex())

    # Erase (area where firmware will be written)
    print("[STEP] ERASE...")
    if not cmd_erase(ser):
        print("[ERROR] Erase failed.")
        ser.close()
        sys.exit(1)

    # Transfer
    print("[STEP] WRITE (packets)...")
    if not transfer_firmware(ser, firmware, APP_FLASH_START):
        print("[ERROR] Transfer failed.")
        ser.close()
        sys.exit(1)

    # Finish
    print("[STEP] FINISH...")
    if not cmd_finish(ser, len(firmware), crc_total):
        print("[ERROR] Finish / verification failed.")
        ser.close()
        sys.exit(1)

    # Reset (start application)
    print("[STEP] RESET (switching to application)...")
    cmd_reset(ser)

    print("[DONE] Transfer completed.")

    end = time.time()
    print(f"[TIME] Total time: {end - start:.2f} seconds")

    ser.close()

if __name__ == "__main__":
    main()