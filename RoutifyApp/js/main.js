
import MapManager from './MapManager.js';
import MarkerManager from './MarkerManager.js';
import UIManager from './UIManager.js';

// --- Global State ---
let currentUserLocation = null;
let markerManagerInstance = null;
let uiManagerInstance = null;

// --- Main Application Initialization ---
function initializeApp() {
    console.log("Initializing application...");

    // 1. Define Initial Map State & Selectors
    const initialMapOptions = { center: { lat: 48.8566, lng: 2.3522 }, zoom: 5 };

    // 2. Create Manager Instances
    const mapManager = new MapManager('map', initialMapOptions);
    markerManagerInstance = new MarkerManager(mapManager);
    uiManagerInstance = new UIManager(mapManager, markerManagerInstance);

    // 3. Initialize Map, Marker Layers, and UI Listeners
    try {
        const map = mapManager.initializeMap();
        if (!map) throw new Error("Map initialization failed.");
        
        if (markerManagerInstance) {
            markerManagerInstance.tryAddLayersToMap();
        } else {
            console.error("Failed to add marker layers: markerManagerInstance is null");
        }
        uiManagerInstance.setupEventListeners(); // Setup listeners

        // 5. Attempt Geolocation
        console.log("Attempting geolocation...");
        map.locate({ setView: false, maxZoom: 18 });
        

        map.on('locationfound', (e) => {
            // Update the global variable
            currentUserLocation = { lat: e.latlng.lat, lng: e.latlng.lng };
            console.log(`Geolocation success: Stored global user location`, currentUserLocation);
            console.log("[main.js:locationfound] Checking uiManagerInstance:", uiManagerInstance); 
            if (uiManagerInstance) {
                console.log("[main.js:locationfound] uiManagerInstance exists. Calling setCurrentUserLocation..."); // <-- ADD THIS
                uiManagerInstance.setCurrentUserLocation(currentUserLocation);
                console.log("[main.js:locationfound] Finished calling setCurrentUserLocation."); 
            }

            // Manually zoom map
            const desiredZoom = 17;
            mapManager.zoomToPlace(currentUserLocation, desiredZoom);
            L.circleMarker(e.latlng, { radius: 6, color: 'blue', fillColor: '#3f51b5', fillOpacity: 0.8 }).addTo(map);
        });

        map.on('locationerror', (e) => {
             currentUserLocation = null;
             if (uiManagerInstance) {
                 uiManagerInstance.setCurrentUserLocation(null);
             }
             console.warn(`Could not get user location: ${e.message}.`);
        });

        console.log("Leaflet App initialization sequence complete.");

    } catch (error) {
         console.error("Leaflet App initialization failed:", error);
         const mapDiv = document.getElementById('map');
         if (mapDiv) mapDiv.innerHTML = `<p style="color:red; padding: 10px;">App failed to load: ${error.message}</p>`;
    }
}

$(document).ready(initializeApp);
