#pragma once
#include "Graph.h"
#include "Socket.h"
#include "Route.h"
#include "json.hpp"
#include <optional> 

using json = nlohmann::json;

class RequestHandler {
public:
    RequestHandler();
    void handleRequest(Socket clientSocket);

private:
    using StationPair = std::pair<int, Graph::Station>;
    using StationList = std::vector<StationPair>;
    
    // --- Helper Structs  ---
    struct RequestData {
        Utilities::Coordinates startCoords; // User location
        Utilities::Coordinates endCoords;   // Destination
        int generations = 1000;
        double mutationRate = 0.3;
        int populationSize = 100;
    };

    struct NearbyStations {
        StationList startStations;
        StationList endStations;
    };

    struct BestRouteResult {
        Route route;
        double fitness = -1.0;
        int startStationId = -1;
        int endStationId = -1;
    };

    struct SelectedStations {
        StationList startStations;
        StationList endStations;
    };

    struct GaTaskResult {
        Route route;
        double fitness = -1.0;
        bool success = false;       // Indicate if GA completed successfully and produced a valid route
        int startStationId = -1;    // Store which start station this result is for
        int endStationId = -1;
    };

    // --- Private Helper Methods ---
    json handleGetLines(const json& request_json);
    json handleGetStationInfo(const json& request_json);

    // --- Genetic Algorithm Request Helpers ---
    json handleFindRouteCoordinates(const json& request_json); // Top level
    json extractAndValidateCoordinateInput(const json& request_json, RequestData& inputData);
    json findNearbyStationsForRoute(const RequestData& inputData, NearbyStations& foundStations); // Finds ALL nearby


    // Helper for finding best route
    std::optional<BestRouteResult> findBestRouteToDestination( // Renamed
        const StationList& selectedStartStations,
        const StationPair& endStationPair, // Takes single end station
        const RequestData& gaParams);

    // Additional Helpers
    std::optional<StationPair> selectClosestStation(double centerLat, double centerLon, const StationList& allNearby); 
    void selectRepresentativeStations(double centerLat, double centerLon, const StationList& allNearby, StationList& selected);
    static RequestHandler::GaTaskResult runSingleGaTask(int startId, int endId, const RequestHandler::RequestData& gaParams, const Graph& graph);

    json formatRouteResponse(const BestRouteResult& bestResult);

    static std::vector<Graph::Station> reconstructIntermediateStops(
        int segmentStartCode,
        int segmentEndCode,
        const std::string& lineId,
        const Graph& graph);


    // Member Variables
    Graph _graph;
};