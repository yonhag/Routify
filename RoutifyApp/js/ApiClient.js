import { updateStatusMessage } from './utils.js'; 
/**
 * Sends an HTTP POST request with JSON payload TO THE FLASK SERVER.
 * @param {string} endpoint - The Flask API endpoint (e.g., '/api/route').
 * @param {object} payload - The JavaScript object to send.
 * @returns {Promise<object>} A promise that resolves with the parsed JSON response or rejects on error.
 */
export async function sendApiRequest(endpoint, payload) {
    // Use relative URL to target the same Flask server origin
    const url = endpoint;
    console.log(`[  Client] Sending HTTP POST to Flask proxy ${url}:`, payload);

    try {
        const response = await fetch(url, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Accept': 'application/json'
            },
            body: JSON.stringify(payload)
        });

        if (!response.ok) {
            let errorData;
            try {
                // Try to parse error JSON from backend/proxy
                errorData = await response.json();
            } catch (e) {
                // If parsing fails, use status text
                errorData = {
                    error: `Server responded with status ${response.status}`,
                    details: response.statusText
                };
                
                updateStatusMessage(`Error: ${errorData.details}`, 'error');
            }
            console.error(`[ApiClient] HTTP Error ${response.status}:`, errorData);
            // Throw an error that includes details if possible
            throw new Error(errorData.error || `Server error ${response.status}`);
        }

        // If response is OK, parse the JSON body
        const data = await response.json();
        console.log("[ApiClient] Response received from Flask proxy:", data);
        return data; // Resolve promise with parsed data

    } catch (error) {
        // Catch fetch network errors or errors thrown from response handling
        console.error('[ApiClient] API Request failed:', error);
        updateStatusMessage(`Error: ${error.message}`, 'error');
        // Re-throw the error so the caller's .catch can handle it if needed
        throw error;
    }
}
