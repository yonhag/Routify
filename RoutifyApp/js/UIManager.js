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
    requestRoute(endCoords) { // endCoords is the destination {lat, lng}
        console.log("[UIManager.requestRoute] Checking this.currentUserLocation:", this.currentUserLocation);
        if (!this.currentUserLocation) {
            updateStatusMessage("Your current location is not available. Cannot calculate route.", 'warning');
            // --- ADDED: Clear info bar if location is missing ---
            UIManager.updateRouteInfoBar(null, null);
            return;
        }
        if (!endCoords || typeof endCoords.lat !== 'number' || typeof endCoords.lng !== 'number') {
            updateStatusMessage("Internal error: Invalid destination coordinates.", 'error');
            // --- ADDED: Clear info bar on invalid destination ---
            UIManager.updateRouteInfoBar(null, null);
            return;
        }

        console.log(`Requesting route from User:${this.currentUserLocation.lat},${this.currentUserLocation.lng} to Dest:${endCoords.lat},${endCoords.lng}`);
        // --- UPDATE UI Before Request ---
        updateStatusMessage("Calculating route...", 'info'); // Set status message
        UIManager.updateRouteInfoBar(null, "Calculating route..."); // Show bar with loading text
        // --- END UI Update ---

        const payload = {
            type: 2,
            startLat: this.currentUserLocation.lat,
            startLong: this.currentUserLocation.lng,
            endLat: endCoords.lat,
            endLong: endCoords.lng,
            gen: 150, mut: 0.3, popSize: 100
        };

        // Clear previous graphics
        if(this.markerManager) {
             this.markerManager.clearRouteDisplay(); // Clear lines/markers
             // Decide if destination marker should persist or be cleared here too
             // this.markerManager.clearDestinationMarker();
        } else {
             console.error("Cannot clear route display: MarkerManager missing.");
        }

        const destinationCoords = { lat: endCoords.lat, lng: endCoords.lng };

        // --- Perform API Request ---
        sendApiRequest('/api/route', payload)
            .then(response => {
                console.log("[UIManager] Received response:", response);

                // --- Robust checking ---
                if (!response) {
                    updateStatusMessage("Error: No response from server.", 'error');
                    UIManager.updateRouteInfoBar(null, null); // <<< Hide/clear info bar
                    return;
                }
                if (response.error) {
                    updateStatusMessage(`Server error: ${response.error}. ${response.details || ''}`, 'error');
                    UIManager.updateRouteInfoBar(null, null); // <<< Hide/clear info bar
                    return;
                }
                if (!Array.isArray(response.detailed_steps)) {
                    if (response.status && response.status !== "Route found") {
                        updateStatusMessage(`Could not find route: ${response.status}`, 'warning');
                        UIManager.updateRouteInfoBar(null, `Could not find route: ${response.status}`); // <<< Show status in bar
                    } else {
                        updateStatusMessage("Error: Invalid route data received.", 'error');
                        UIManager.updateRouteInfoBar(null, null); // <<< Hide/clear info bar
                    }
                    // Also clear graphics if route failed but response was valid-ish
                    if(this.markerManager) this.markerManager.clearRouteDisplay();
                    return;
                }
                if (response.detailed_steps.length === 0) {
                    updateStatusMessage("No route path found.", 'warning');
                    const stats = response.summary ? { time: response.summary.time_mins, cost: response.summary.cost, transfers: response.summary.transfers } : null;
                    UIManager.updateRouteInfoBar(stats, "No route path to display."); // <<< Update info bar
                    return;
                }
                // --- End Checks ---

                // --- SUCCESS: Update UI & Draw Route ---
                console.log("[UIManager] Valid steps received. Updating UI and drawing route.");
                updateStatusMessage("Route found.", 'success'); // Simple status

                // 1. Extract stats
                const stats = response.summary ? {
                    time: response.summary.time_mins,
                    cost: response.summary.cost,
                    transfers: response.summary.transfers
                    // arrivalTime: response.summary.arrivalTime // Add when available
                } : null;

                // 2. Generate summary text
                const summaryText = UIManager.generateRouteSummaryText(response.detailed_steps, this.currentUserLocation, destinationCoords);

                // 3. Update the top info bar <<<< ----- THE CALLS ARE HERE ----- <<<<
                UIManager.updateRouteInfoBar(stats, summaryText);

                // 4. Draw route on map
                if (this.markerManager && this.currentUserLocation) {
                    this.markerManager.addRouteDisplayFromSteps(
                        response.detailed_steps,
                        this.currentUserLocation,
                        destinationCoords
                    );
                } else {
                    console.error("Cannot draw route: MarkerManager or currentUserLocation missing.");
                    // If drawing fails, maybe clear the info bar too?
                    UIManager.updateRouteInfoBar(null, "Error displaying route map.");
                }
                // --- END SUCCESS ---

            })
            .catch(error => {
                console.error("[UIManager] Error during route request:", error);
                updateStatusMessage(`Network or processing error: ${error.message}`, 'error');
                // --- UPDATE UI On Catch Error ---
                UIManager.updateRouteInfoBar(null, null); // Hide/clear info bar
                if(this.markerManager) this.markerManager.clearRouteDisplay(); // Clear graphics too
                // --- END UI Update ---
            });
    } // End requestRoute

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


    /**
     * Updates the top route information bar.
     * @param {object|null} stats - Object with { time, cost, transfers } or null to clear.
     * @param {string|null} summaryText - Text summary of the route or null to clear.
     */
    static updateRouteInfoBar(stats, summaryText) {
        const infoBar = document.getElementById('route-info-bar');
        const statsDiv = document.getElementById('route-stats');
        const summaryDiv = document.getElementById('route-summary-text');

        if (!infoBar || !statsDiv || !summaryDiv) {
            console.error("Could not find route info bar elements.");
            return;
        }

        if (stats || summaryText) {
            // Update Stats (Left Side)
            let statsHtml = '';
            if (stats) {
                statsHtml += `<span><strong>Time:</strong> ${stats.time !== undefined ? stats.time.toFixed(1) + ' min' : 'N/A'}</span>`;
                statsHtml += `<span><strong>Cost:</strong> ${stats.cost !== undefined ? stats.cost.toFixed(2) : 'N/A'}</span>`;
                statsHtml += `<span><strong>Transfers:</strong> ${stats.transfers !== undefined ? stats.transfers : 'N/A'}</span>`;
            } else {
                statsHtml = '<span> </span>'; // Placeholder if no stats but summary exists
            }
            statsDiv.innerHTML = statsHtml;

            // Update Summary Text (Right Side)
            // Use innerHTML now because generateRouteSummaryText includes <strong> tags
            summaryDiv.innerHTML = summaryText || 'Calculating...';

            // Show the Bar by adding the 'visible' class
            infoBar.classList.add('visible');

        } else {
            // Hide the Bar by removing the 'visible' class
            infoBar.classList.remove('visible');
            // Optionally clear content when hiding
            statsDiv.innerHTML = '';
            summaryDiv.innerHTML = '';
        }
    }

    // --- generateRouteSummaryText Method ---
    /**
     * Generates a textual summary of the route steps.
     * @param {Array} steps - The detailed_steps array from the backend.
     * @returns {string} - A concise text summary (HTML allowed for styling).
     */
    static generateRouteSummaryText(steps) {
        if (!steps || steps.length === 0) {
            return "No route steps to summarize.";
        }

        let summaryParts = [];
        let currentVehicleLine = null;
        let stopsOnCurrentVehicle = 0;
        let lastStationName = "first station"; // Initialize with a placeholder

        // --- 1. Initial Walk Instruction ---
        // The first step's "from" station is where the transit portion starts
        if (steps[0] && steps[0].from_name) {
            lastStationName = steps[0].from_name;
            summaryParts.push(`Walk to ${lastStationName}`);
        } else {
            summaryParts.push(`Walk to the first station`); // Fallback
        }


        // --- 2. Iterate through Transit/Walk Segments ---
        for (let i = 0; i < steps.length; i++) {
            const step = steps[i];
            const isPublicTransit = step.line_id && step.line_id !== 'Walk' && step.line_id !== 'Start';
            const nextStationName = step.to_name || (i === steps.length - 1 ? "destination" : "next station");

            if (isPublicTransit) {
                // --- On Public Transit ---
                if (step.line_id !== currentVehicleLine) {
                    // Boarding a new line (or the first line after walking)
                    if (currentVehicleLine !== null) {
                        // We were just on a different vehicle, finalize its description
                        summaryParts.push(`ride ${stopsOnCurrentVehicle} stop(s) to ${lastStationName}`);
                    }
                    // Start the description for the new line
                    summaryParts.push(`take line <strong class="line-id">${step.line_id}</strong>`);
                    currentVehicleLine = step.line_id;
                    stopsOnCurrentVehicle = 1; // Start counting stops for this line
                } else {
                    // Continuing on the same line
                    stopsOnCurrentVehicle++;
                }
                lastStationName = nextStationName; // Update the name of the station we just arrived at

            } else if (step.line_id === 'Walk') {
                // --- Walking Segment ---
                if (currentVehicleLine !== null) {
                    // We just got off a vehicle, finalize its description
                    summaryParts.push(`ride ${stopsOnCurrentVehicle} stop(s) to ${lastStationName}`); // lastStationName is where we alighted
                    currentVehicleLine = null; // Reset vehicle tracking
                    stopsOnCurrentVehicle = 0;
                }
                // Add walk instruction
                // TODO: Could improve this if walk distance/time is available in 'step'
                summaryParts.push(`walk to ${nextStationName}`);
                lastStationName = nextStationName; // Update location after walking

            } // Ignore 'Start' lines if they appear

        } // End loop through steps


        // --- 3. Finalize last vehicle segment if necessary ---
        // If the loop ended while on public transit, add the final ride segment description
        if (currentVehicleLine !== null) {
             // 'lastStationName' at this point holds the name of the final station reached by transit
            summaryParts.push(`ride ${stopsOnCurrentVehicle} stop(s) to ${lastStationName}`);
        }

        // --- 4. Add Final Walk (Implied by destination marker) ---
        // Optional: Add explicit "walk to destination" if last step wasn't already walk
        if (steps[steps.length-1].line_id !== 'Walk') {
             summaryParts.push("walk to destination");
        }


        // --- Join the parts ---
        return summaryParts.join(' → '); // Use an arrow or similar separator
    }

} // End of UIManager class

export default UIManager;