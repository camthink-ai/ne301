#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Serial command helper: send one CLI line to COM7, capture output.
Usage: python serial_cmd.py <port> <command> [capture_seconds]"""
import sys, time, serial

port = sys.argv[1]
cmd = sys.argv[2]
cap = float(sys.argv[3]) if len(sys.argv) > 3 else 2.0

ser = serial.Serial(port, 115200, timeout=0.2)
time.sleep(0.1)
ser.reset_input_buffer()
ser.write((cmd + "\r\n").encode())
ser.flush()

deadline = time.time() + cap
out = bytearray()
while time.time() < deadline:
    chunk = ser.read(4096)
    if chunk:
        out += chunk
ser.close()
sys.stdout.write(out.decode('utf-8', 'replace'))
