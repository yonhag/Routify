#pragma once
#include "Graph.h"
#include "Socket.h"
#include "Route.h" // Include Route header
#include "json.hpp"
#include <vector>   // Include vector
#include <optional> // Include optional

using json = nlohmann::json;

class RequestHandler {
public:
    RequestHandler();
    void handleRequest(Socket clientSocket);

private:
    using StationPair = std::pair<int, Graph::Station>; // Added alias for clarity
    using StationList = std::vector<StationPair>;
    
    // --- Helper Structs (defined within RequestHandler or globally) ---
    struct CoordinateRouteInput {
        double startLat = 0.0, startLong = 0.0, endLat = 0.0, endLong = 0.0;
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
    json handleNearbyStations(const json& request_json);

    // Refactored handlers for Type 2
    json handleFindRouteCoordinates(const json& request_json); // Top level
    json extractAndValidateCoordinateInput(const json& request_json, CoordinateRouteInput& inputData);
    json findNearbyStationsForRoute(const CoordinateRouteInput& inputData, NearbyStations& foundStations); // Finds ALL nearby

    // Updated/New helpers
    void selectRepresentativeStations(double centerLat, double centerLon, const StationList& allNearby, StationList& selected);
    std::optional<StationPair> selectClosestStation(double centerLat, double centerLon, const StationList& allNearby); 

    // Updated helper for finding best route
    std::optional<BestRouteResult> findBestRouteToDestination( // Renamed
        const StationList& selectedStartStations,
        const StationPair& endStationPair, // Takes single end station
        const CoordinateRouteInput& gaParams);

    // Unchanged helpers
    double runGAForPair(int startId, int endId, const CoordinateRouteInput& gaParams, Route& outBestRoute) const;
    json formatRouteResponse(const BestRouteResult& bestResult);
    static RequestHandler::GaTaskResult runSingleGaTask(int startId, int endId, const RequestHandler::CoordinateRouteInput& gaParams, const Graph& graph);
    static std::vector<Graph::Station> reconstructIntermediateStops(
        int segmentStartCode,
        int segmentEndCode,
        const std::string& lineId,
        const Graph& graph);


    // Member Variables
    Graph _graph;
};