import socket
import json
import sys
import io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect(("127.0.0.1", 8200))
    print(f"Successfully connected")

    request = {
        'type': 2,
        'stationId': 39105,
        'startStationId': 39105, # Gur Shazar
        'destStationId': 20269,  # Begin Azrieli
        'gen': 100,
        'mut': 0.5
    }

    s.sendall(json.dumps(request).encode())
    x = s.recv(1024)
    try:
        print(x.decode())
    except Exception:
        print(x)
    s.close