// Relies on the global L object from the Leaflet CDN script

/**
 * Manages markers displayed on the map, specifically route markers.
 */
class MarkerManager {
    constructor(mapManager) {
        this.mapManager = mapManager;
        this.map = null;
        this.routeLayer = null; // Holds markers AND polylines for the route
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
    addRouteDisplayFromSteps(detailedStepsArray) { // Renamed for clarity
        if (!this.map) { console.error("Map not initialized for addRouteDisplayFromSteps."); return; }
        if (!this.routeLayer) { console.error("Route layer not initialized."); return; }

        this.clearRouteDisplay(); // Clear previous route visuals

        if (!Array.isArray(detailedStepsArray)) {
            console.error("Invalid data for route display: Expected a detailed_steps array.", detailedStepsArray);
            return;
        }
        if (detailedStepsArray.length === 0) {
            console.log("No steps received to display.");
            return;
        }

        console.log(`Displaying route for ${detailedStepsArray.length} segments.`);

        const allLatLngs = []; // Collect all points for fitting bounds later
        const actionPointLatLngs = new Set(); // Store unique action point coords as strings

        // --- Define Marker Styles ---
        const actionMarkerOptions = {
            radius: 8, // Larger radius
            fillColor: "#ff7800", // Orange
            color: "#000",
            weight: 1,
            opacity: 1,
            fillOpacity: 0.8
        };
        const passThroughMarkerOptions = {
            radius: 4, // Smaller radius
            fillColor: "#007bff", // Blue
            color: "#fff",
            weight: 1,
            opacity: 1,
            fillOpacity: 0.7
        };


        detailedStepsArray.forEach((step, index) => {
            if (typeof step.from_lat !== 'number' || typeof step.from_long !== 'number' ||
                typeof step.to_lat !== 'number' || typeof step.to_long !== 'number') {
                console.warn(`Skipping step ${index} due to missing coordinates:`, step);
                return;
            }

            const fromLatLng = L.latLng(step.from_lat, step.from_long);
            const toLatLng = L.latLng(step.to_lat, step.to_long);
            allLatLngs.push(fromLatLng, toLatLng); // Add both points for bounds calculation

            // --- Draw Polyline for the segment ---
            const polylineOptions = {
                className: step.line_id === 'Walk' ? 'route-polyline-walk' : 'route-polyline' // Use CSS classes
                // Or define styles directly:
                // color: step.line_id === 'Walk' ? 'green' : 'blue',
                // weight: 4,
                // opacity: 0.7,
                // dashArray: step.line_id === 'Walk' ? '2, 4' : '5, 10'
            };
            const segmentLine = L.polyline([fromLatLng, toLatLng], polylineOptions);
            this.routeLayer.addLayer(segmentLine);

            // --- Add Markers (Decide style based on action point) ---
            const fromKey = `${fromLatLng.lat},${fromLatLng.lng}`;
            const toKey = `${toLatLng.lat},${toLatLng.lng}`;

            // Add start marker (only once for the very first step)
            if (index === 0) {
                 const startMarker = L.circleMarker(fromLatLng, actionMarkerOptions).bindTooltip(
                    `Depart: ${step.from_name} (Code: ${step.from_code})`
                 );
                 this.routeLayer.addLayer(startMarker);
                 actionPointLatLngs.add(fromKey);
            }

             // Add arrival marker for this segment
             // Determine if it's an action point or pass-through
             const isAction = step.to_is_action_point;
             const markerOptions = isAction ? actionMarkerOptions : passThroughMarkerOptions;
             let hoverText = isAction
                 ? `${step.action_description || 'Action'}: ${step.to_name} (Code: ${step.to_code})`
                 : `Pass through: ${step.to_name} (Code: ${step.to_code})`;
             if(step.action === "Take line" && step.line_id) hoverText += `\n(Via Line: ${step.line_id})`;

             // Only add marker if it hasn't been added as an action point already
             // (avoids overlapping action markers on transfer points)
             // However, we might *want* to show pass-through markers even at action points?
             // Let's add it regardless for now, the style difference will show.
             const arrivalMarker = L.circleMarker(toLatLng, markerOptions).bindTooltip(hoverText);
             this.routeLayer.addLayer(arrivalMarker);

             // Keep track if it was an action point
             if (isAction) {
                 actionPointLatLngs.add(toKey);
             }

        }); // end forEach step

        console.log("Route polylines and markers added.");

        // Zoom/pan to fit the entire route if points were added
        if (allLatLngs.length > 0) {
            try {
                // Create bounds from all points collected
                const bounds = L.latLngBounds(allLatLngs);
                this.map.fitBounds(bounds.pad(0.1)); // pad adds margin
            } catch (boundsError) {
                 console.error("Error fitting map bounds to route display:", boundsError);
            }
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