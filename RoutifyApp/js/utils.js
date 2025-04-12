/**
 * Parses a string containing latitude and longitude separated by space.
 * @param {string} coordString - The input string (e.g., "51.5 -0.1").
 * @returns {object|null} An object {lat, lng} or null if parsing fails.
 */
export function parseCoordinateString(coordString) {
    if (!coordString || typeof coordString !== 'string') return null;

    const parts = coordString.trim().split(/\s+/); // Split by one or more spaces
    if (parts.length !== 2) return null;

    const lat = parseFloat(parts[0]);
    const lng = parseFloat(parts[1]); // Assume input order is lat long

    if (isNaN(lat) || isNaN(lng)) return null;

    // Basic validation (adjust ranges if needed)
    if (lat < -90 || lat > 90 || lng < -180 || lng > 180) {
        console.warn("Parsed coordinates out of valid range:", lat, lng);
        return null;
    }

    return { lat: lat, lng: lng };
}


/**
 * Updates the content and style of the status message element.
 * @param {string} message - The text message to display.
 * @param {string} [type='info'] - The type of message ('info', 'success', 'warning', 'error'). Affects styling (optional).
 */
export function updateStatusMessage(message, type = 'info') {
    const statusElement = document.getElementById('status-message');
    if (!statusElement) {
        console.warn("Status message element (#status-message) not found in DOM.");
        return;
    }

    statusElement.textContent = message;
    statusElement.className = ''; // Clear previous classes

    switch (type) {
        case 'success':
            statusElement.classList.add('status-success'); // Add CSS class for styling
            break;
        case 'warning':
            statusElement.classList.add('status-warning');
            break;
        case 'error':
            statusElement.classList.add('status-error');
            break;
        case 'info':
        default:
             statusElement.classList.add('status-info');
            break;
    }
     // Make sure it's visible (might be hidden initially)
    statusElement.style.display = 'block';
}

/**
 * Clears the status message.
 */
export function clearStatusMessage() {
    const statusElement = document.getElementById('status-message');
    if (statusElement) {
        statusElement.textContent = '';
        statusElement.className = '';
        statusElement.style.display = 'none'; // Hide element when empty
    }
}