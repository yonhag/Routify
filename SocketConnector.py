import socket
import json

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect(("127.0.0.1", 8200))
    print(f"Successfully connected")

    request = {
        'type': 0,
        'stationId': 39107
    }

    s.sendall(json.dumps(request).encode())
    x = s.recv(1024)
    print(x.decode())
    s.close