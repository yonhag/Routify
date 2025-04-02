import socket
import json

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect(("46.116.236.185", 8200))
    print(f"Successfully connected")

    request = {
        'type': 1,
        'stationId': 39107
    }

    s.sendall(json.dumps(request).encode())
    x = s.recv(1024)
    print(x.decode())
    s.close