#!/usr/bin/env python3
"""持续监听 USB-Serial-JTAG 串口日志，端口重枚举后自动重连。"""
import sys, time

import serial

PORT = "/dev/cu.usbmodem1301"
OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/stackchan_log.txt"

def main():
    with open(OUT, "ab", buffering=0) as f:
        while True:
            ser = None
            try:
                ser = serial.Serial(PORT, 115200, timeout=1)
                stamp = time.strftime("[%Y-%m-%d %H:%M:%S] port reopened\n").encode()
                f.write(stamp)
                print(f"opened {PORT}", flush=True)
                while True:
                    data = ser.read(4096)
                    if data:
                        f.write(data)
            except Exception as e:
                print(f"serial error: {e}, retry soon", flush=True)
            finally:
                if ser:
                    try:
                        ser.close()
                    except Exception:
                        pass
            time.sleep(0.05)

if __name__ == "__main__":
    main()
