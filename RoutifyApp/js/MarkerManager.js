// Relies on the global L object from the Leaflet CDN script

/**
 * Manages markers displayed on the map, specifically route markers.
 */
class MarkerManager {
    constructor(mapManager) {
        this.mapManager = mapManager;

        // --- ALWAYS INITIALIZE LAYER GROUPS ---
        try {
            this.routeLayer = L.layerGroup();
            this.actionPointMarkers = L.layerGroup();
            console.log("MarkerManager: routeLayer and actionPointMarkers initialized.");
        } catch (e) {
            console.error("MarkerManager Error: Failed to initialize L.layerGroup(). Is Leaflet (L) loaded?", e);
            // Set them to null or an empty object to prevent later 'addLayer' errors,
            // though drawing will fail.
            this.routeLayer = null;
            this.actionPointMarkers = null;
        }
        // --- END INITIALIZATION ---

        this.destinationMarker = null;
        this.startMarker = null;

        // Attempt to add to map if ready, otherwise, maybe add later
        this.tryAddLayersToMap();
    }

    tryAddLayersToMap() {
        const map = this.mapManager.getMapInstance();
        if (map) {
            // Only add if the layer group was successfully created
            if (this.routeLayer && !map.hasLayer(this.routeLayer)) {
                 this.routeLayer.addTo(map);
                 console.log("MarkerManager: routeLayer added to map.");
            }
            if (this.actionPointMarkers && !map.hasLayer(this.actionPointMarkers)) {
                 this.actionPointMarkers.addTo(map);
                 console.log("MarkerManager: actionPointMarkers added to map.");
            }
        } else {
            console.warn("MarkerManager: Map not ready. Layers not added yet.");
            // You might need a mechanism to call tryAddLayersToMap() again
            // once the map *is* ready, e.g., triggered by an event from MapManager.
        }
    }

    /**
     * Initializes the layer group and adds it to the map.
     * Must be called after the map instance is created.
     */
    initializeLayers() {
        this.map = this.mapManager.getMapInstance();
        if (this.map) {
            this.routeLayer = L.layerGroup().addTo(this.map);
            console.log("Route display layer group created and added to map.");
        } else {
            console.error("Cannot initialize marker layers: Map instance not available.");
        }
    }

    /**
     * Clears existing route markers/lines and adds new ones based on the detailed steps array.
     * @param {Array} detailedStepsArray - Array of detailed step objects from the backend.
     */
    addRouteDisplayFromSteps(steps, userLocation, destinationCoords) {
        const map = this.mapManager.getMapInstance();
        if (!map) { /* ... error handling ... */ return; }
        if (!steps || steps.length === 0) { /* ... error handling ... */ return; }
        if (!userLocation) { console.error("User location missing for drawing initial walk."); return; }
        if (!destinationCoords) { console.error("Destination coords missing for drawing final walk."); return; }


        this.clearRouteDisplay(); // Clear previous route

        let allRoutePoints = []; // For bounds fitting
        const walkStyle = { color: 'green', weight: 4, opacity: 0.7, dashArray: '8, 8' }; // Distinct walk style

        // --- 1. Draw Initial Walk Line ---
        const firstStationCoords = [steps[0].from_lat, steps[0].from_long];
        const userCoords = [userLocation.lat, userLocation.lng];
        if (userCoords[0] !== firstStationCoords[0] || userCoords[1] !== firstStationCoords[1]) { // Avoid zero-length line
             const initialWalkPolyline = L.polyline([userCoords, firstStationCoords], walkStyle);
             this.routeLayer.addLayer(initialWalkPolyline);
             allRoutePoints.push(userCoords, firstStationCoords);
             console.log("Drawing initial walk line.");
        } else {
            allRoutePoints.push(userCoords); // Still add user location for bounds
        }


        // --- 2. Draw Transit/Intermediate Walk Segments (Loop) ---
        steps.forEach(step => {
            const fromCoords = [step.from_lat, step.from_long];
            const toCoords = [step.to_lat, step.to_long];
            let segmentPoints = [fromCoords];

            if (step.intermediate_stops && Array.isArray(step.intermediate_stops)) {
                step.intermediate_stops.forEach(interStop => {
                    segmentPoints.push([interStop.lat, interStop.long]);
                });
            }
            segmentPoints.push(toCoords);
            // Add points from this segment (excluding first point if it was added by previous segment's end)
            allRoutePoints.push(...segmentPoints.slice(1));


            // Determine line style
            let style;
            if (step.line_id === 'Walk') {
                style = walkStyle;
            } else if (step.line_id === 'Start') {
                style = { color: 'transparent' }; // Don't draw "Start" line
            } else {
                 // Basic example: Blue for public transport
                style = { color: 'blue', weight: 5, opacity: 0.8 };
                 // Add more specific colors based on line_id or type if needed
            }

            if (step.line_id !== 'Start') { // Avoid drawing for the conceptual 'Start' segment if present
                const polyline = L.polyline(segmentPoints, style);
                this.routeLayer.addLayer(polyline);
            }


            // Add markers for action points (Orange markers)
            let actionDesc = step.action_description || 'Waypoint'; // Default text
            if (step.from_is_action_point) {
                 // If it's the very first step's "from", it's the effective Start Station
                const markerTitle = (step.segment_index === 0) ? `Start Station: ${step.from_name}` : step.from_name;
                const markerPopup = (step.segment_index === 0) ? `<b>Start Station</b><br>${step.from_name}` : `<b>${actionDesc}</b><br>${step.from_name}`;
                const marker = L.marker(fromCoords, { title: markerTitle }).bindPopup(markerPopup);
                this.actionPointMarkers.addLayer(marker);
            }
             // Check if 'to' is an action point AND it's the very last point of the whole route
             const isFinalStation = (step.segment_index === steps.length - 1);
             if (step.to_is_action_point) {
                 const markerTitle = isFinalStation ? `End Station: ${step.to_name}` : step.to_name;
                 const markerPopup = isFinalStation ? `<b>End Station</b><br>${step.to_name}` : `<b>${actionDesc}</b><br>${step.to_name}`;
                 const marker = L.marker(toCoords, { title: markerTitle }).bindPopup(markerPopup);
                 this.actionPointMarkers.addLayer(marker);
             }

        }); // End loop through steps


        // --- 3. Draw Final Walk Line ---
        const lastStationCoords = [steps[steps.length - 1].to_lat, steps[steps.length - 1].to_long];
        const destCoords = [destinationCoords.lat, destinationCoords.lng];
        if (lastStationCoords[0] !== destCoords[0] || lastStationCoords[1] !== destCoords[1]) { // Avoid zero-length line
             const finalWalkPolyline = L.polyline([lastStationCoords, destCoords], walkStyle);
             this.routeLayer.addLayer(finalWalkPolyline);
             allRoutePoints.push(destCoords); // Add destination for bounds fitting
             console.log("Drawing final walk line.");
        } else {
             // If last station IS the destination, ensure it's in bounds calculation
             if (allRoutePoints.length === 0 || allRoutePoints[allRoutePoints.length-1][0] !== lastStationCoords[0] || allRoutePoints[allRoutePoints.length-1][1] !== lastStationCoords[1] ){
                  allRoutePoints.push(lastStationCoords);
             }
        }


        // --- Fit Bounds ---
        if (allRoutePoints.length > 0) {
            try{
                 map.flyToBounds(L.latLngBounds(allRoutePoints), { padding: [50, 50], maxZoom: 17 }); // Add padding & limit zoom
            } catch (e) {
                 console.error("Error fitting map bounds:", e, allRoutePoints);
            }
        }
    } // End addRouteDisplayFromSteps

 // --- Definition of addDestinationMarker ---
    /**
     * Adds or updates the destination marker on the map.
     * Clears any previous destination marker before adding the new one.
     * @param {number} lat - Latitude of the destination.
     * @param {number} lng - Longitude of the destination.
     */
    addDestinationMarker(lat, lng) {
        const map = this.mapManager.getMapInstance();
        if (!map) {
            console.error("Cannot add destination marker: Map instance not available.");
            return; // Exit if map isn't ready
        }

        // 1. Clear any existing destination marker first
        this.clearDestinationMarker(); // Ensure the previously defined clear function is called

        // 2. Define marker options (customize icon later if desired)
        const markerOptions = {
            title: "Destination" // Tooltip text on hover
            // Example custom icon (requires defining destinationIcon):
            // icon: L.icon({
            //     iconUrl: 'path/to/destination-flag-icon.png',
            //     iconSize: [25, 41], // size of the icon
            //     iconAnchor: [12, 41], // point of the icon which will correspond to marker's location
            //     popupAnchor: [1, -34] // point from which the popup should open relative to the iconAnchor
            // })
        };

        // 3. Create the new Leaflet marker
        this.destinationMarker = L.marker([lat, lng], markerOptions);

        // 4. Add a popup to the marker (optional but helpful)
        this.destinationMarker.bindPopup(`<b>Destination</b><br>Lat: ${lat.toFixed(5)}, Lng: ${lng.toFixed(5)}`);

        // 5. Add the marker to the map
        this.destinationMarker.addTo(map);

        console.log(`Destination marker added at: Lat=${lat}, Lng=${lng}`);
    }
    // --- End of addDestinationMarker ---

    clearDestinationMarker() {
        const map = this.mapManager.getMapInstance();
        if (this.destinationMarker) {
            if (map) {
                map.removeLayer(this.destinationMarker);
            } else {
                 console.warn("Could not remove destination marker from map: Map instance unavailable.");
            }
            this.destinationMarker = null; // Clear the reference
            console.log("Destination marker cleared.");
        }
    }

    /**
     * Removes all markers and polylines from the route layer group.
     */
    clearRouteDisplay() { // Renamed for clarity
        if (this.routeLayer) {
            this.routeLayer.clearLayers();
            console.log("Route display cleared.");
        } else {
            console.warn("Attempted to clear route display, but routeLayer was not initialized.");
        }
    }

    /**
     * Clears existing route markers and adds new ones based on the steps array
     * received from the backend.
     * @param {Array} stepsArray - Array of step objects from the backend response.
     * Each step should have coordinates (e.g., to_lat/to_long or lat/long) and action.
     */
    addMarkersFromSteps(stepsArray) {
        if (!this.map) { console.error("Map not initialized for addMarkersFromSteps."); return; }
        if (!this.routeLayer) { console.error("Route layer not initialized. Cannot add markers."); return; }

        this.clearRouteMarkers(); // Clear previous route first

        if (!Array.isArray(stepsArray)) {
            console.error("Invalid data for route markers: Expected a steps array.", stepsArray);
            return;
        }
        console.log(`Adding markers for ${stepsArray.length} steps.`);

        stepsArray.forEach((step, index) => {
            let title = `Step ${index + 1}: ${step.action || 'Unknown Action'}`;
            let position = null;

            try {
                if ((step.action === "Take line" || step.action === "Walk") &&
                    typeof step.to_lat === 'number' && typeof step.to_long === 'number') {
                        position = { lat: step.to_lat, lng: step.to_long };
                        title += ` to ${step.to || 'destination'}`;
                        if(step.line_id) title += ` (Line: ${step.line_id})`;

                } else if (step.action === "Transfer" &&
                           typeof step.lat === 'number' && typeof step.long === 'number') {
                        position = { lat: step.lat, lng: step.long };
                        title += ` at ${step.at || 'transfer point'}`;
                }
                // Add handling for other potential 'action' types if needed

                if (position) {
                    const marker = L.marker([position.lat, position.lng], {
                        title: title // Set hover text
                    });
                    this.routeLayer.addLayer(marker);
                } else {
                    console.warn(`Skipping step ${index} due to missing/invalid coordinates or unknown action:`, step);
                }
            } catch (error) {
                console.error(`Error processing step ${index}:`, error, step);
            }
        }); // end forEach

        console.log("Route markers added from steps.");

        // Zoom/pan to fit the new route markers if any were added
        if (this.routeLayer.getLayers().length > 0) {
            try {
                 this.map.fitBounds(this.routeLayer.getBounds().pad(0.1)); // pad adds margin
            } catch (boundsError) {
                 console.error("Error fitting map bounds to route markers:", boundsError);
            }
        }
    }

    /**
     * Removes all markers from the route layer group.
     */
    clearRouteMarkers() {
        // Check if the layer group is defined before calling clearLayers
        if (this.routeLayer) {
            this.routeLayer.clearLayers();
            console.log("Route markers cleared.");
        } else {
            console.warn("Attempted to clear route markers, but routeLayer was not initialized.");
        }
    }
}

export default MarkerManager;