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
    using StationList = std::vector<Graph::Station>;
    
    // --- Helper Structs  ---
    struct RequestData {
        Utilities::Coordinates startCoords; 
        Utilities::Coordinates endCoords; 
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
        int startStationCode = -1;
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

    // --- Private Debug Helper Methods ---
    json handleGetLines(const json& request_json) const;
    json handleGetStationInfo(const json& request_json) const;

    // --- Genetic Algorithm Request Helpers ---
    json handleFindRouteCoordinates(const json& request_json) const; // Top level
    json extractAndValidateCoordinateInput(const json& request_json, RequestData& inputData) const;
    json findNearbyStationsForRoute(const RequestData& inputData, NearbyStations& foundStations) const; 

    static bool getStationInfo(const Graph& graph, const int stationCode, json& stationJson);

    // Helper for finding best route
    std::optional<BestRouteResult> findBestRouteToDestination(
        const StationList& selectedStartStations,
        const Graph::Station& endStationPair,
        const RequestData& gaParams) const;

    // Additional Helpers
    std::optional<Graph::Station> selectClosestStation(const Utilities::Coordinates& c, const StationList& allNearby) const;
    void selectRepresentativeStations(const Utilities::Coordinates& c, const StationList& allNearby, StationList& selected) const;
    static RequestHandler::GaTaskResult runSingleGaTask(const int startId, const int endId, const RequestHandler::RequestData& gaParams, const Graph& graph);

    json formatRouteResponse(const BestRouteResult& bestResult, const RequestData& inputData) const;

    static std::vector<Graph::Station> reconstructIntermediateStops(
        const int segmentStartCode,
        const int segmentEndCode,
        const std::string& lineId,
        const Graph& graph);

    static void addIntermediateStops(
        json& stepJson, const Graph::TransportationLine& lineTaken,
        const int segmentStartCode, const int segmentEndCode, const Graph& graph);

    static void addActionDetails(
        json& stepJson, size_t i,
        const std::vector<Route::VisitedStation>& visitedStations,
		const Graph::TransportationLine& lineTaken);

    // Member Variables
    Graph _graph;
};