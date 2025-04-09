import socket
import json
import sys
import io

# Ensure UTF-8 output
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect(("127.0.0.1", 8200))
    print("Successfully connected\n")

    request = {
        'type': 2,
        'stationId': 39105,
        'startStationId': 39105,  # Gur Shazar
        'destStationId': 10268,   # Begin Azrieli
        'gen': 200,
        'mut': 0.1,
        'pop': 20
    }

    s.sendall(json.dumps(request).encode())
    data = s.recv(4096)

    try:
        response = json.loads(data.decode())
        # Print JSON response
        print("JSON Response:\n")
        print(json.dumps(response, indent=2, ensure_ascii=False))

        print("\nStep-by-step bus instructions:\n")

        for step in response.get("steps", []):
            action = step.get("action")
            if action == "Take line":
                print(f"Get on line {step['line_id']} at '{step['from']}' and ride to '{step['to']}'.")
            elif action == "Transfer":
                print(f"Transfer at '{step['at']}'.")

        print(f"You will end your trip at '{response['to_station']['name']}'.")
        print(f"Total transfers: {response['summary']['transfers']}")
    except Exception as e:
        pass

    s.close()
