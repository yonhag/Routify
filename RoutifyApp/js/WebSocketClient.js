const WEBSOCKET_URL = "ws://localhost:8200"; // WebSocket URL

let webSocket = null;
let isConnected = false;
let onMessageHandler = null; // Callback for received messages
let onOpenHandler = null;    // Callback for connection open
let onCloseHandler = null;   // Callback for connection close
let onErrorHandler = null;   // Callback for connection error

/**
 * Attempts to establish a WebSocket connection and sets up handlers.
 * @param {function} onMessage - Callback function for when a message is received (receives parsed JSON).
 * @param {function} [onOpen] - Optional callback for when the connection opens.
 * @param {function} [onClose] - Optional callback for when the connection closes.
 * @param {function} [onError] - Optional callback for connection errors.
 */

function connectWebSocket(onMessage, onOpen, onClose, onError) {
    if (webSocket && (webSocket.readyState === WebSocket.OPEN || webSocket.readyState === WebSocket.CONNECTING)) {
        console.log("WebSocket connection already open or connecting.");
        return;
    }

    onMessageHandler = onMessage;
    onOpenHandler = onOpen;
    onCloseHandler = onClose;
    onErrorHandler = onError;

    console.log(`Attempting to connect WebSocket to ${WEBSOCKET_URL}...`);
    try {
        webSocket = new WebSocket(WEBSOCKET_URL);
    } catch (error) {
        console.error("Failed to create WebSocket:", error);
        if (onErrorHandler) onErrorHandler(error);
        return; // Stop if creation fails
    }


    webSocket.onopen = (event) => {
        isConnected = true;
        console.log("WebSocket connection established.");
        if (onOpenHandler) onOpenHandler();
    };

    webSocket.onmessage = (event) => {
        console.log("WebSocket message received raw:", event.data);
        try {
            const response = JSON.parse(event.data);
            if (onMessageHandler) {
                onMessageHandler(response); // Pass parsed data to handler
            } else {
                console.warn("WebSocket message received, but no handler is set.");
            }
        } catch (e) {
            console.error("Failed to parse incoming WebSocket message:", e);
            console.error("Received data:", event.data);
            // Optionally notify error handler about parse failure
            // if(onErrorHandler) onErrorHandler(new Error("Failed to parse message"));
        }
    };

    webSocket.onerror = (event) => {
        console.error("WebSocket error observed:", event);
        isConnected = false; // Assume connection is lost on error
        if (onErrorHandler) {
            onErrorHandler(event); // Pass the event object
        } else {
            alert("WebSocket connection error. Please ensure the backend server is running and accessible.");
        }
    };

    webSocket.onclose = (event) => {
        isConnected = false;
        webSocket = null; // Clear the instance
        console.log(`WebSocket connection closed: Code=${event.code}, Reason=${event.reason}`);
        if (onCloseHandler) {
             onCloseHandler(event);
        } else {
             // alert("WebSocket connection closed."); // Can be annoying
        }
        // Optional: Implement automatic reconnection logic here?
        // setTimeout(connectWebSocket, 5000); // Example: Try reconnecting after 5s
    };
}

/**
 * Sends a JSON payload over the WebSocket connection.
 * @param {object} payload - The JavaScript object to send.
 * @returns {boolean} True if the message was sent, false otherwise.
 */
function sendWebSocketMessage(payload) {
    if (!webSocket || !isConnected) {
        console.error("WebSocket is not connected. Cannot send message.");
        alert("Cannot send request: Not connected to the server.");
        return false;
    }
    try {
        const message = JSON.stringify(payload);
        console.log("Sending WebSocket message:", message);
        webSocket.send(message);
        return true;
    } catch (e) {
        console.error("Failed to stringify or send WebSocket message:", e, payload);
        return false;
    }
}

export { connectWebSocket, sendWebSocketMessage };