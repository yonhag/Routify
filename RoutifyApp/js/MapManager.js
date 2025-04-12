// Relies on the global L object from the Leaflet CDN script
// If using a bundler, you'd import L from 'leaflet';

/**
 * Manages the Leaflet map instance and basic controls like zooming.
 */
class MapManager {
    /**
     * @param {string} mapElementId - The ID of the div container for the map.
     * @param {object} initialOptions - Options like center {lat, lng} and zoom.
     */
    constructor(mapElementId, initialOptions) {
        this.mapElementId = mapElementId;
        this.initialOptions = initialOptions;
        this.map = null;
        this.tileLayer = null;

        if (!this.initialOptions || !this.initialOptions.center || typeof this.initialOptions.zoom !== 'number') {
            console.error("MapManager: Invalid initialOptions provided.", initialOptions);
            // Provide safe defaults?
            this.initialOptions = { center: { lat: 0, lng: 0 }, zoom: 2 };
        }
    }

    /**
     * Creates the Leaflet map instance and adds the tile layer.
     * @returns {L.Map|null} The Leaflet map instance or null on failure.
     */
    initializeMap() {
        const mapDiv = document.getElementById(this.mapElementId);
        if (!mapDiv) {
            console.error(`Map container element '#'${this.mapElementId}' not found!`);
            return null; // Return null instead of throwing to allow graceful failure maybe
        }
        if (typeof L === 'undefined') {
             console.error("Leaflet library (L) not loaded!");
             mapDiv.textContent = "Error: Leaflet library not found.";
             return null;
        }

        console.log("Initializing Leaflet map.");
        try {
            this.map = L.map(this.mapElementId).setView(
                [this.initialOptions.center.lat, this.initialOptions.center.lng],
                this.initialOptions.zoom
            );

             const osmTileUrl = 'https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png';
             const osmAttrib = '© <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors';
             this.tileLayer = L.tileLayer(osmTileUrl, {
                 attribution: osmAttrib,
                 maxZoom: 19
             }).addTo(this.map);

            console.log("Leaflet map instance created and tile layer added.");
            return this.map;

        } catch (error) {
             console.error("Error creating Leaflet map instance:", error);
             mapDiv.textContent = `Error creating map: ${error.message}`;
             // Consider throwing the error here if map creation is critical
             // throw error;
             return null;
        }
    }

    /**
     * @returns {L.Map|null} The Leaflet map instance.
     */
    getMapInstance() {
        // Maybe add a check if !this.map?
        return this.map;
    }

    /**
     * Smoothly zooms/pans the map to a given location.
     * @param {object} coordinates - An object {lat, lng}.
     * @param {number} [zoomLevel=15] - The target zoom level.
     */
    zoomToPlace(coordinates, zoomLevel = 15) { // Default zoom level adjusted
        if (!this.map) {
            console.error("Map is not initialized yet for zoomToPlace.");
            return;
        }
        if (!coordinates || typeof coordinates.lat !== 'number' || typeof coordinates.lng !== 'number') {
            console.error("Invalid coordinates provided for zoomToPlace.", coordinates);
            return;
        }
        this.map.flyTo([coordinates.lat, coordinates.lng], zoomLevel);
        console.log(`Zooming map to: ${coordinates.lat}, ${coordinates.lng}, zoom: ${zoomLevel}`);
    }
}

export default MapManager;