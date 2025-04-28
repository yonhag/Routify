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
            null;
        }
    }

    /**
     * Clears existing route markers/lines and adds new ones based on the detailed steps array.
     * @param {Array} detailedStepsArray - Array of detailed step objects from the backend.
     */
    addRouteDisplayFromSteps(steps, userLocation, destinationCoords) {
        const map = this.mapManager.getMapInstance();
        // --- Initial validation ---
        if (!map || !this.routeLayer || !this.actionPointMarkers) { console.error("Cannot display route: Map or layers not ready."); return; }
        if (!steps || steps.length === 0) { console.warn("Cannot display route: No steps provided."); return; }
        if (!userLocation || !this.isValidCoordinate([userLocation.lat, userLocation.lng])) { console.error("Cannot display route: Invalid userLocation."); return; }
        if (!destinationCoords || !this.isValidCoordinate([destinationCoords.lat, destinationCoords.lng])) { console.error("Cannot display route: Invalid destinationCoords."); return; }
        // ---

        this.clearRouteDisplay();
        let allRoutePoints = []; // For bounds calculation
        const walkStyle = { color: 'green', weight: 4, opacity: 0.7, dashArray: '8, 8' };
        const actionMarkerOptions = { title: "Action Point" };
        const intermediateStopOptions = { radius: 3, color: '#555', weight: 1, fillColor: '#888', fillOpacity: 0.7, title: "Intermediate Stop" };

        // --- 1. Initial Walk ---
        const firstStepFromCoords = [steps[0].from.lat, steps[0].from.long];
        const userCoords = [userLocation.lat, userLocation.lng];
        if (!this.isValidCoordinate(firstStepFromCoords)) { console.error("Invalid coords for first step 'from'.", steps[0]); return; }
        // Draw initial walk if user location is different from the first station
        if (Math.abs(userCoords[0] - firstStepFromCoords[0]) > 0.00001 || Math.abs(userCoords[1] - firstStepFromCoords[1]) > 0.00001) {
            const initialWalkPolyline = L.polyline([userCoords, firstStepFromCoords], walkStyle);
            this.routeLayer.addLayer(initialWalkPolyline);
            allRoutePoints.push(userCoords, firstStepFromCoords);
        } else {
            allRoutePoints.push(userCoords); // Start point for bounds
        }

        // --- 2. Determine Loop End and Final Step Type ---
        const lastStepIndex = steps.length - 1;
        const isLastStepWalk = steps[lastStepIndex].line_id === 'Walk';
        // Loop up to the second-to-last step, or not at all if only one step
        const loopEndIndex = steps.length > 1 ? lastStepIndex - 1 : -1;

        // --- 3. Loop Through Steps (Excluding the Final One) ---
        for (let i = 0; i <= loopEndIndex; i++) {
            const step = steps[i];
            const fromCoords = [step.from.lat, step.from.long];
            const toCoords = [step.to.lat, step.to.long];

            if (!this.isValidCoordinate(fromCoords) || !this.isValidCoordinate(toCoords)) {
                console.warn(`Invalid coords in step ${i}, skipping drawing.`, step);
                continue;
            }

            let segmentPoints = [fromCoords];
            let intermediateMarkers = [];

            // Add intermediate stops for this segment
            if (step.intermediate_stops && Array.isArray(step.intermediate_stops)) {
                step.intermediate_stops.forEach(interStop => {
                    const interCoords = [interStop.lat, interStop.long];
                    if (this.isValidCoordinate(interCoords)) {
                        segmentPoints.push(interCoords);
                        const interMarker = L.circleMarker(interCoords, { ...intermediateStopOptions, title: interStop.name || 'Intermediate Stop' }).bindPopup(`Pass through: ${interStop.name || 'Stop'}`);
                        intermediateMarkers.push(interMarker);
                    }
                });
            }
            segmentPoints.push(toCoords);
            allRoutePoints.push(...segmentPoints.slice(1)); // Add segment points for bounds

            // Determine style and draw polyline
            let style;
            if (step.line_id === 'Walk') { style = walkStyle; }
            else if (step.line_id === 'Start') { style = { color: 'transparent' }; }
            else { style = { color: 'blue', weight: 5, opacity: 0.8 }; }

            if (step.line_id !== 'Start') {
                const polyline = L.polyline(segmentPoints, style);
                this.routeLayer.addLayer(polyline);
            }

            // Add Markers (Intermediate and Action Points for this step)
            intermediateMarkers.forEach(mkr => this.actionPointMarkers.addLayer(mkr));
            let actionDesc = step.action_description || 'Waypoint';
            // FROM marker
            if (step.from_is_action_point) {
                const markerTitle = (i === 0) ? `Start Station: ${step.from_name}` : step.from_name;
                const markerPopup = (i === 0) ? `<b>Start Station</b><br>${step.from_name}` : `<b>${actionDesc}</b><br>${step.from_name}`;
                const marker = L.marker(fromCoords, { ...actionMarkerOptions, title: markerTitle }).bindPopup(markerPopup);
                this.actionPointMarkers.addLayer(marker);
            }
            // TO marker
            if (step.to_is_action_point) {
                 const markerTitle = step.to_name;
                 const markerPopup = `<b>${actionDesc}</b><br>${step.to_name}`;
                 const marker = L.marker(toCoords, { ...actionMarkerOptions, title: markerTitle }).bindPopup(markerPopup);
                 this.actionPointMarkers.addLayer(marker);
            }
        } // --- End Main Loop ---


        // --- 4. Handle the FINAL Connection ---
        const actualDestCoords = [destinationCoords.lat, destinationCoords.lng];
        let finalOriginCoords; // Coords of the point where the *very last segment* starts

        if (steps.length === 1) { // Only one step in the whole route
            finalOriginCoords = [steps[0].from.lat, steps[0].from.long];
             if (isLastStepWalk && (Math.abs(userCoords[0] - finalOriginCoords[0]) < 0.00001 && Math.abs(userCoords[1] - finalOriginCoords[1]) < 0.00001)) {
                  finalOriginCoords = userCoords; 
             }

        } else {
            finalOriginCoords = [steps[lastStepIndex - 1].to.lat, steps[lastStepIndex - 1].to.long];
        }

        // --- Validate finalOriginCoords ---
        if (!this.isValidCoordinate(finalOriginCoords)) {
            console.error("Could not determine valid origin for the final segment connection.");
        } else {
            // --- Draw the final segment ---
            if (isLastStepWalk) {
                console.log("[MarkerManager] Final step is Walk. Drawing connection from previous step end directly to destination.");
                const finalWalkPolyline = L.polyline([finalOriginCoords, actualDestCoords], walkStyle);
                this.routeLayer.addLayer(finalWalkPolyline);
                allRoutePoints.push(actualDestCoords); // Add actual destination for bounds


            } else {
                // Last calculated step was NOT 'Walk' (e.g., Bus/Train).
                // Draw that last transit step first.
                console.log("[MarkerManager] Final step is transit. Drawing transit segment first.");
                const lastStep = steps[lastStepIndex];
                const lastStepFromCoords = [lastStep.from.lat, lastStep.from.long]; 
                const lastStepToCoords = [lastStep.to.lat, lastStep.to.long]; // Endpoint of the transit

                if (this.isValidCoordinate(lastStepFromCoords) && this.isValidCoordinate(lastStepToCoords)) {
                     let segmentPoints = [lastStepFromCoords];
                     // Add intermediate stops for the *very last* transit step
                     if (lastStep.intermediate_stops && Array.isArray(lastStep.intermediate_stops)) {
                         lastStep.intermediate_stops.forEach(interStop => {
                             const interCoords = [interStop.lat, interStop.long];
                             if (this.isValidCoordinate(interCoords)) {
                                 segmentPoints.push(interCoords);
                                 const interMarker = L.circleMarker(interCoords, { ...intermediateStopOptions, title: interStop.name || 'Intermediate Stop' }).bindPopup(`Pass through: ${interStop.name || 'Stop'}`);
                                 this.actionPointMarkers.addLayer(interMarker); // Add intermediate markers
                             }
                         });
                     }
                     segmentPoints.push(lastStepToCoords);
                     allRoutePoints.push(...segmentPoints.slice(1)); // Add points for bounds

                    // Draw the final transit polyline
                     const style = { color: 'blue', weight: 5, opacity: 0.8 }; // Assume transit style
                     const polyline = L.polyline(segmentPoints, style);
                     this.routeLayer.addLayer(polyline);

                     // Add marker at the end of the final transit step
                     if (lastStep.to_is_action_point) {
                          const markerTitle = `End Station: ${lastStep.to_name}`;
                          const markerPopup = `<b>${lastStep.action_description || 'Arrive'}</b><br>${lastStep.to_name}`;
                          const marker = L.marker(lastStepToCoords, { ...actionMarkerOptions, title: markerTitle }).bindPopup(markerPopup);
                          this.actionPointMarkers.addLayer(marker);
                      }

                    if (Math.abs(lastStepToCoords[0] - actualDestCoords[0]) > 0.00001 || Math.abs(lastStepToCoords[1] - actualDestCoords[1]) > 0.00001) {
                        console.log("[MarkerManager] Drawing final connecting walk from end of transit to actual destination.");
                        const finalConnectingWalk = L.polyline([lastStepToCoords, actualDestCoords], walkStyle);
                        this.routeLayer.addLayer(finalConnectingWalk);
                        allRoutePoints.push(actualDestCoords);
                    }
                } else {
                     console.error("Invalid coordinates for the final transit step.", lastStep);
                 }
            }
        }


        // --- 5. Fit Bounds ---
        const validPointsForBounds = allRoutePoints.filter(coord => this.isValidCoordinate(coord));
        if (validPointsForBounds.length > 0) {
            console.log(`[MarkerManager] Fitting bounds to ${validPointsForBounds.length} valid points.`);
            map.flyToBounds(L.latLngBounds(validPointsForBounds), { padding: [50, 50], maxZoom: 17 });
        } else {
            console.warn("Cannot fit bounds: No valid points found in the route.");
        }

    } // End addRouteDisplayFromSteps

    isValidCoordinate(coord) {
        return Array.isArray(coord) && coord.length === 2 && typeof coord[0] === 'number' && typeof coord[1] === 'number' && !isNaN(coord[0]) && !isNaN(coord[1]);
    }

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

        // 2. Define marker options
        const markerOptions = {
            title: "Destination" // Tooltip text on hover
        };
        this.clearRouteDisplay();

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
            this.destinationMarker = null;
            console.log("Destination marker cleared.");
        }
    }

    /**
     * Removes all markers and polylines from the route layer group.
     */
    clearRouteDisplay() {
        let clearedSomething = false;
        if (this.routeLayer) {
            this.routeLayer.clearLayers();
            clearedSomething = true;
        } else {
            console.warn("Attempted to clear route display lines, but routeLayer was not initialized.");
        }

        if (this.actionPointMarkers) {
            this.actionPointMarkers.clearLayers();
            clearedSomething = true;
        } else {
            console.warn("Attempted to clear route action point markers, but actionPointMarkers was not initialized.");
        }

        if (clearedSomething) {
             console.log("Route display (lines and markers) cleared.");
        }
    }

    /**
     * Clears existing route markers and adds new ones based on the steps array
     * received from the backend.
     * @param {Array} stepsArray - Array of step objects from the backend response.
     * Each step should have coordinates (e.g., to.lat/to.long or lat/long) and action.
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
                    typeof step.to.lat === 'number' && typeof step.to.long === 'number') {
                        position = { lat: step.to.lat, lng: step.to.long };
                        title += ` to ${step.to || 'destination'}`;
                        if(step.line_id) title += ` (Line: ${step.line_id})`;

                } else if (step.action === "Transfer" &&
                           typeof step.lat === 'number' && typeof step.long === 'number') {
                        position = { lat: step.lat, lng: step.long };
                        title += ` at ${step.at || 'transfer point'}`;
                }

                if (position) {
                    const marker = L.marker([position.lat, position.lng], {
                        title: title
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
        if (this.routeLayer) {
            this.routeLayer.clearLayers();
            console.log("Route markers cleared.");
        } else {
            console.warn("Attempted to clear route markers, but routeLayer was not initialized.");
        }
    }
}

export default MarkerManager;