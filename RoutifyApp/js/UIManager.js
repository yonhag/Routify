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
        console.log(`Requesting route from User:${this.currentUserLocation.lat},${this.currentUserLocation.lng} to Dest:${endCoords.lat},${endCoords.lng}`);
        updateStatusMessage("Calculating route...", 'info');

        const payload = {
            type: 2,
            startLat: this.currentUserLocation.lat, // User's actual location
            startLong: this.currentUserLocation.lng,
            endLat: endCoords.lat, // Clicked destination
            endLong: endCoords.lng,
            gen: 150, mut: 0.3, popSize: 100
        };

        this.markerManager.clearRouteDisplay();
        // Store destination coords locally if needed later, e.g., for drawing final walk
        const destinationCoords = { lat: endCoords.lat, lng: endCoords.lng }; // Make a copy

        sendApiRequest('/api/route', payload)
            .then(response => {
                console.log("[UIManager] Received response:", response);

                // --- Robust checking from previous steps ---
                if (!response || response.error || !Array.isArray(response.detailed_steps)) {
                     // ... handle errors, clear display, update status ...
                     console.error("Invalid or error response from backend.");
                     updateStatusMessage("Failed to get a valid route.", 'error');
                     this.markerManager.clearRouteDisplay();
                     return;
                }
                if (response.detailed_steps.length === 0) {
                     // ... handle no steps, update status ...
                     updateStatusMessage("No route path found.", 'warning');
                     return;
                }
                // --- End Checks ---

                console.log("[UIManager] Valid steps received. Adding route display.");
                updateStatusMessage("Route found. Displaying route.", 'success');

                // *** PASS USER LOCATION AND DESTINATION COORDS ***
                if (this.markerManager && this.currentUserLocation) {
                     this.markerManager.addRouteDisplayFromSteps(
                         response.detailed_steps,
                         this.currentUserLocation, // Pass user's start location
                         destinationCoords       // Pass clicked destination location
                     );
                } else {
                    console.error("Cannot draw route: MarkerManager or currentUserLocation missing.");
                }
                // ... Optional: Display summary ...

            })
            .catch(error => {
                // ... handle errors ...
                console.error("[UIManager] Error during route request:", error);
                updateStatusMessage(`Network or processing error: ${error.message}`, 'error');
                this.markerManager.clearRouteDisplay();
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