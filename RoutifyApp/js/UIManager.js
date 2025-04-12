import { updateStatusMessage, parseCoordinateString } from './utils.js';
import { sendApiRequest } from './ApiClient.js';

class UIManager {
    constructor(mapManager, markerManager) {
        // ... constructor remains the same ...
        this.mapManager = mapManager;
        this.markerManager = markerManager;
        this.currentUserLocation = null;
        // Make sure updateStatusMessage is accessible if it's not global
        // this.updateStatusMessage = updateStatusMessage;
    }

    setCurrentUserLocation(location) {
        console.log("[UIManager.setCurrentUserLocation] Received location:", location);
        this.currentUserLocation = location;
        console.log("[UIManager.setCurrentUserLocation] Updated this.currentUserLocation:", this.currentUserLocation);
    }

    /**
     * Prepares and sends a route request via the Flask Proxy using ApiClient.
     * @param {object} endCoords - The destination coordinates {lat, lng}.
     */
    requestRoute(endCoords) {
        console.log("[UIManager.requestRoute] Checking this.currentUserLocation:", this.currentUserLocation);
        if (!this.currentUserLocation) {
            updateStatusMessage("Your current location is not available. Cannot calculate route.", 'warning');
            return;
        }
        if (!endCoords || typeof endCoords.lat !== 'number' || typeof endCoords.lng !== 'number') {
            updateStatusMessage("Internal error: Invalid destination coordinates.", 'error');
            return;
        }

        console.log(`Requesting route via Flask proxy from ${this.currentUserLocation.lat}, ${this.currentUserLocation.lng} to ${endCoords.lat}, ${endCoords.lng}`);
        updateStatusMessage("Calculating route...", 'info'); // Inform user

        const payload = {
            type: 2, // Type for the C++ backend
            startLat: this.currentUserLocation.lat,
            startLong: this.currentUserLocation.lng,
            endLat: endCoords.lat,
            endLong: endCoords.lng,
            gen: 150, // Consider making these configurable later
            mut: 0.3,
            popSize: 100
        };

        // Clear previous route display before sending the request
        this.markerManager.clearRouteDisplay(); // Assuming this clears lines/markers for the route

        // Call the IMPORTED sendApiRequest function
        sendApiRequest('/api/route', payload) // Now correctly refers to the imported function
            .then(response => { // Process the PARSED JSON response
                console.log("[UIManager.requestRoute .then] Received response object:", response);

                // --- START OF ROBUST CHECKING ---

                // 1. Check if response exists at all
                if (!response) {
                    console.error("[UIManager.requestRoute] Error: Received null or undefined response from API.");
                    updateStatusMessage("Error: Failed to get a valid response from the server.", 'error');
                    this.markerManager.clearRouteDisplay(); // Ensure cleanup
                    return; // Stop processing
                }

                // 2. Check for backend-reported errors
                if (response.error) {
                    console.error("[UIManager.requestRoute] Backend returned an error:", response.error, response.details);
                    updateStatusMessage(`Server error: ${response.error}. ${response.details || ''}`, 'error');
                    this.markerManager.clearRouteDisplay(); // Ensure cleanup
                    return; // Stop processing
                }

                // 3. Check if 'detailed_steps' exists and is an array
                const steps = response.detailed_steps; // Assign for clarity
                const isStepsArray = Array.isArray(steps);
                console.log("[UIManager.requestRoute .then] response.detailed_steps:", steps);
                console.log("[UIManager.requestRoute .then] Is steps an array:", isStepsArray);

                if (!isStepsArray) {
                    // It could be a status message like "No route found"
                    if (response.status && response.status !== "Route found") {
                         console.warn("[UIManager.requestRoute] Backend status received:", response.status);
                         updateStatusMessage(`Could not find a route: ${response.status}`, 'warning');
                    } else {
                         console.error("[UIManager.requestRoute] Invalid route data received: 'detailed_steps' is missing or not an array.", response);
                         updateStatusMessage("Error: Received invalid route data from the server.", 'error');
                    }
                    this.markerManager.clearRouteDisplay(); // Ensure cleanup
                    return; // Stop processing
                }

                // 4. Check if the steps array is empty
                if (steps.length === 0) {
                    console.log("[UIManager.requestRoute] Backend returned an empty route (zero steps).");
                    updateStatusMessage("No route path found (start/end might be the same or no path exists).", 'warning');
                    // Optionally display start/end markers even if no steps
                    if (response.from_station && response.to_station) {
                         // Assuming markerManager has methods for start/end markers distinct from route steps
                         this.markerManager.addStartEndMarkers(response.from_station, response.to_station);
                    } else {
                         this.markerManager.clearRouteDisplay(); // Fallback cleanup
                    }
                    return; // Stop processing, it's a valid response but no path to draw
                }

                // 5. Success: We have a non-empty array of steps!
                console.log("[UIManager.requestRoute] Valid non-empty steps received. Adding route display.");
                updateStatusMessage("Route found. Displaying route.", 'success');
                this.markerManager.addRouteDisplayFromSteps(steps); // Pass the valid steps array

                // Display summary (Optional, if summary exists)
                if (response.summary) {
                    const summary = response.summary;
                    // Append summary to status or display elsewhere
                    const summaryText = ` Time: ${summary.time_mins.toFixed(1)}m, Cost: ${summary.cost.toFixed(2)}, Transfers: ${summary.transfers}`;
                    updateStatusMessage(`Route found.${summaryText}`, 'success'); // Example: Update status with details
                    console.log("Route Summary:", summaryText);
                }

                // --- END OF ROBUST CHECKING ---

            })
            .catch(error => { // Catch network errors or issues with sendApiRequest itself
                console.error("[UIManager.requestRoute .catch] Error during API request or processing:", error);
                updateStatusMessage(`Network or processing error: ${error.message}`, 'error');
                this.markerManager.clearRouteDisplay(); // Ensure cleanup on failure
            });
    } // End of requestRoute

    handleMapClick(latlng) {
        if (!latlng) return;

        const latStr = latlng.lat.toFixed(5);
        const lngStr = latlng.lng.toFixed(5);

        const confirmationMessage = `Navigate to coordinates: ${latStr}, ${lngStr}?`;
        if (confirm(confirmationMessage)) {
            console.log("User confirmed navigation to:", latlng);
            this.markerManager.clearDestinationMarker();
            this.markerManager.addDestinationMarker(latlng.lat, latlng.lng);     
            this.requestRoute(latlng);
        } else {
            console.log("User cancelled navigation.");
        }
    }

    setupEventListeners() {
        console.log("Setting up UI event listeners.");

        const map = this.mapManager.getMapInstance();
        if (map) {
            map.on('click', (event) => {
                this.handleMapClick(event.latlng);
            });
        } else {
             console.warn("Could not add map click listener: Map not ready during setup.");
        }
    }

} // End of UIManager class

export default UIManager;