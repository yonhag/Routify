import os
import socket
import json
import time
from flask import Flask, send_from_directory, abort, Response, request, jsonify
import mimetypes

CPP_BACKEND_HOST = 'localhost'
CPP_BACKEND_PORT = 8200
SOCKET_TIMEOUT = 180.0 # <--- USE THIS TIMEOUT
SOCKET_BUFFER_SIZE = 4096

app = Flask(__name__, static_folder=None)
APP_ROOT = os.path.dirname(os.path.abspath(__file__))
JS_DIR = os.path.join(APP_ROOT, 'js')

# --- File Serving Routes ---
@app.route('/')
def index():
    # ... (existing code) ...
    print(f"Serving index.html from: {APP_ROOT}")
    try: return send_from_directory(APP_ROOT, 'index.html')
    except FileNotFoundError: abort(404, description="index.html not found")


@app.route('/<path:filename>')
def serve_files(filename):
    """Serves CSS/ICO files from the root and JS files from the js/ subdirectory."""
    
    directory_to_serve = APP_ROOT
    file_to_serve = filename
    is_known_root_file = False # Flag for allowed root files

    if filename.startswith('js/'):
        directory_to_serve = JS_DIR
        # Remove 'js/' prefix to get the actual filename within the js directory
        file_to_serve = filename[len('js/'):]
    elif filename in ['style.css', 'favicon.ico']:
        is_known_root_file = True
    else:
        # If it's not starting with 'js/' and isn't an allowed root file, reject.
        abort(404, description="Resource not found or path not allowed")

    # --- Security Check: Prevent directory traversal ---
    safe_path = os.path.normpath(os.path.join(directory_to_serve, file_to_serve))
    if not safe_path.startswith(directory_to_serve):
        abort(403) # Forbidden access

    # --- Check if file exists and serve ---
    if os.path.exists(safe_path) and os.path.isfile(safe_path):
        print(f"Serving file: {safe_path}")
        try:
            mimetype, _ = mimetypes.guess_type(safe_path)
            if file_to_serve.endswith('.js'):
                 mimetype = 'application/javascript'
            elif file_to_serve.endswith('.ico'):
                 # Ensure correct MIME type for icons
                 mimetype = 'image/vnd.microsoft.icon'
            return send_from_directory(directory_to_serve, file_to_serve, mimetype=mimetype)
        except Exception as e:
            print(f"Error serving file {filename}: {e}")
            abort(500) # Internal Server Error
    else:
        # File does not exist at the calculated path
        print(f"Not Found: File does not exist at path: {safe_path}")
        if filename == 'favicon.ico':
             return Response(status=204) # Standard practice for missing default favicon
        else:
            abort(404, description=f"Resource '{filename}' not found.")



# --- CORRECTED API Route ---
@app.route('/api/route', methods=['POST'])
def proxy_route_request():
    """
    Receives route request from frontend, sends it to C++ backend via raw socket,
    and returns the C++ backend's response.
    """
    print("Received request on /api/route")

    frontend_payload = None
    # *** FIX: Initialize payload_obtained HERE (Before the try block) ***
    payload_obtained = False
    # *** END FIX ***

    try:
        # Try get_json first
        frontend_payload = request.get_json()
        if frontend_payload is not None: # Check for None specifically
             print("Successfully parsed JSON using request.get_json()")
             payload_obtained = True
        else:
             print("request.get_json() returned None. Trying request.data.")
             raw_data = request.data
             if raw_data:
                 print(f"Received raw data ({len(raw_data)} bytes): {raw_data[:200]}...")
                 try:
                     frontend_payload = json.loads(raw_data.decode('utf-8'))
                     print("Successfully parsed JSON from request.data")
                     payload_obtained = True # Parsed successfully
                 except Exception as parse_err:
                      print(f"Failed to parse/decode request.data: {parse_err}")
                      # Keep payload_obtained as False
             else:
                 print("request.data is also empty.")

    except Exception as e:
        print(f"Error getting/parsing JSON payload: {e}")
        # Keep payload_obtained as False implicitly here, but it was already False

    # --- Check if payload was successfully obtained ---
    # Now payload_obtained is guaranteed to exist because it was initialized above
    if not payload_obtained:
        print("Error: Failed to obtain valid JSON payload from frontend request.")
        return jsonify({"error": "Invalid request", "details": "No valid JSON payload received."}), 400

    if not isinstance(frontend_payload, dict):
         print("Error: Parsed payload is not a JSON object (dictionary).")
         return jsonify({"error": "Invalid request", "details": "Payload must be a JSON object."}), 400

    print(f"Frontend Payload (parsed): {json.dumps(frontend_payload)}")

    # --- Communicate with C++ Backend (Keep this entire section as it was from the previous valid version) ---
    backend_response_str = None
    error_message = None
    status_code = 500
    parsed_backend_json = None # Store successfully parsed JSON here

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            # ... (the entire socket communication block: connect, settimeout, send, shutdown, receive loop, parse, error handling) ...
            sock.settimeout(SOCKET_TIMEOUT)
            print(f"Connecting to C++ backend (Timeout: {SOCKET_TIMEOUT}s)...")
            sock.connect((CPP_BACKEND_HOST, CPP_BACKEND_PORT))
            print("Connected.")
            payload_str = json.dumps(frontend_payload)
            print(f"Sending to C++ backend ({len(payload_str)} bytes)...")
            sock.sendall(payload_str.encode('utf-8'))
            print("Payload sent.")
            try:
                sock.shutdown(socket.SHUT_WR)
                print("Socket write shutdown signaled.")
            except OSError as shut_err: print(f"Warning: Socket shutdown(SHUT_WR) failed: {shut_err}")

            print("Waiting for response from C++ backend...")
            chunks = []; total_bytes_received = 0; start_time = time.time(); receive_error = None

            while True:
                try:
                    chunk = sock.recv(SOCKET_BUFFER_SIZE)
                    if not chunk: print("Backend closed connection gracefully."); break
                    else: chunks.append(chunk); total_bytes_received += len(chunk)
                except socket.timeout: receive_error = f"Timeout ({SOCKET_TIMEOUT}s)..."; status_code = 504; break
                except socket.error as sock_err:
                     if sock_err.winerror == 10054 and total_bytes_received > 0: print(f"Warning: WinError 10054 after receiving {total_bytes_received} bytes. Assuming complete."); break
                     else: receive_error = f"Socket error receiving data: {sock_err}..."; status_code = 502; break
                except Exception as e: receive_error = f"Unexpected error during receive: {e}..."; status_code = 500; break

            if not receive_error or (receive_error and "Timeout" not in receive_error and total_bytes_received > 0):
                 if total_bytes_received > 0:
                    backend_response_bytes = b"".join(chunks)
                    try:
                        backend_response_str = backend_response_bytes.decode('utf-8')
                        print(f"Full response received ({total_bytes_received} bytes) decoded.")
                        try: parsed_backend_json = json.loads(backend_response_str); print("Backend response parsed as valid JSON.")
                        except json.JSONDecodeError as json_parse_err: error_message = f"Backend invalid JSON: {json_parse_err}..."; status_code = 502; print(f"Error: {error_message}")
                    except UnicodeDecodeError as decode_err: error_message = f"Failed UTF-8 decode: {decode_err}"; status_code = 502; print(f"Error: {error_message}")
                 elif not receive_error: error_message = "Backend closed connection without sending data."; status_code = 502; print(f"Error: {error_message}")
            else: error_message = receive_error

    except socket.timeout: error_message = f"Connection timed out ({SOCKET_TIMEOUT}s)..."; status_code = 504; print(f"Error: {error_message}")
    except socket.error as sock_err: error_message = f"Socket error connecting/sending...: {sock_err}"; status_code = 502; print(f"Error: {error_message}")
    except Exception as e: error_message = f"Unexpected error during backend communication: {e}"; status_code = 500; print(f"Error: {error_message}")

    # --- Return Response to Frontend ---
    if parsed_backend_json is not None:
        print("Forwarding valid parsed JSON response to frontend.")
        return jsonify(parsed_backend_json)
    elif error_message:
        print(f"Returning error to frontend: {error_message}")
        return jsonify({"error": "Backend communication failed", "details": error_message}), status_code
    else:
        print("Error: Unknown state - no JSON parsed and no specific error recorded.")
        return jsonify({"error": "Unknown error", "details": "Failed to obtain a valid response from backend service."}), 500


    # ... (mimetypes, print statements, app.run) ...
if __name__ == '__main__':
    mimetypes.add_type('application/javascript', '.js')
    mimetypes.add_type('text/css', '.css')
    print(f"Serving files from root: {APP_ROOT}")
    print(f"Serving JS files from: {JS_DIR}")
    print(f"Proxying API requests to C++ backend at {CPP_BACKEND_HOST}:{CPP_BACKEND_PORT}")
    print("Starting Flask server on http://localhost:5000 (or http://0.0.0.0:5000)")
    app.run(host='0.0.0.0', port=5000, debug=True) # Use debug=False for production