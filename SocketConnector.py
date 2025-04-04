import socket
import json
import sys
import io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect(("127.0.0.1", 8200))
    print(f"Successfully connected")

    request = {
        'type': 0,
        'stationId': 39107
    }

    s.sendall(json.dumps(request).encode())
    x = s.recv(1024)
    print('Output: ' + x.decode())
    s.close