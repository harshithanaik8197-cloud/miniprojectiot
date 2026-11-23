#!/usr/bin/env python3
"""
Simple HTTP Server for Smart RFID Inventory Mobile App
Serves the mobile-app/ folder and prints your local LAN IP to open on your phone.
"""

import http.server
import socket
import socketserver
import os
import sys

PORT = 8080

def get_local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # doesn't even have to be reachable
        s.connect(('10.255.255.255', 1))
        IP = s.getsockname()[0]
    except Exception:
        IP = '127.0.0.1'
    finally:
        s.close()
    return IP

class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        # Keep console clean
        pass

if __name__ == '__main__':
    script_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(script_dir)
    
    local_ip = get_local_ip()
    
    print("=" * 60)
    print("   📱 SMART RFID INVENTORY MOBILE APP SERVER")
    print("=" * 60)
    print(f"\n1. On your PC/Laptop:")
    print(f"   👉 Open in Chrome: http://localhost:{PORT}")
    print(f"\n2. On your Android Phone (same Wi-Fi):")
    print(f"   👉 Open in Chrome: http://{local_ip}:{PORT}")
    print("\n   Note: Web Bluetooth on Android Chrome requires either:")
    print("   - Opening via http://localhost (via USB cable with Chrome remote debug)")
    print("   - OR adding http://" + local_ip + f":{PORT} to Chrome flags:")
    print("     chrome://flags/#unsafely-treat-insecure-origin-as-secure")
    print("   - OR hosting on GitHub Pages (HTTPS)")
    print("\nPress Ctrl+C to stop the server.")
    print("=" * 60)

    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", PORT), QuietHandler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nServer stopped.")
            sys.exit(0)
