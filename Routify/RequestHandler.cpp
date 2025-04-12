#include "RequestHandler.h"
#include "Population.h"
#include "Utilities.hpp"
#include <iostream>
#include "json.hpp" // nlohmann/json
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>
#include <algorithm>
#include <optional> // For optional return
#include <unordered_set>
#include <thread>
#include <future>


// Use nlohmann::json namespace
using json = nlohmann::json;

static void to_json(json& j, const Graph::Station& p) {
    j = json{
        {"name", p.name},
        {"latitude", p.coordinates.latitude},
        {"longitude", p.coordinates.longitude}
    };
}

// --- RequestHandler Implementation ---

RequestHandler::RequestHandler() : _graph() {}

// Main handler function (remains mostly the same, calls refactored function for type 2)
void RequestHandler::handleRequest(Socket clientSocket)
{
    std::string response_str;
    std::string received = clientSocket.receiveMessage();
    std::cout << "Received: " << received << std::endl;

    if (received.empty()) {
        json error_resp = { {"error", "Empty request received"} };
        clientSocket.sendMessage(error_resp.dump());
        return;
    }

    try {
        json request_json = json::parse(received);
        int type = request_json.value("type", -1);
        json response_json;

        switch (type) {
        case 0: response_json = handleGetLines(request_json); break;
        case 1: response_json = handleGetStationInfo(request_json); break;
        case 2: response_json = handleFindRouteCoordinates(request_json); break; // Calls the refactored top-level handler
        default: response_json = { {"error", "Invalid request type"} }; break;
        }
        response_str = response_json.dump(2);

    }
    catch (const json::parse_error& e) {
        // ... (keep existing catch blocks) ...
        json error_resp = { {"error", "Invalid JSON format"}, {"details", e.what()} };
        response_str = error_resp.dump(2); std::cerr << "JSON Parse Error: " << e.what() << std::endl;
    }
    catch (const std::runtime_error& e) {
        json error_resp = { {"error", "Processing error during request"}, {"details", e.what()} };
        response_str = error_resp.dump(2); std::cerr << "Runtime Error: " << e.what() << std::endl;
    }
    catch (const std::exception& e) {
        json error_resp = { {"error", "An unexpected server error occurred"}, {"details", e.what()} };
        response_str = error_resp.dump(2); std::cerr << "Standard Exception: " << e.what() << std::endl;
    }
    catch (...) {
        json error_resp = { {"error", "An unknown server error occurred"} };
        response_str = error_resp.dump(2); std::cerr << "Unknown Error occurred." << std::endl;
    }

    std::cout << "Sending Response:\n" << response_str << std::endl;
    clientSocket.sendMessage(response_str);
    std::cout << "Response sent!" << std::endl;
    clientSocket.closeSocket();
    std::cout << "Socket closed!" << std::endl;
}


// --- Public Handlers (remain as entry points) ---

// --- Helper Handlers for Different Request Types ---

// Handles request type 0: Get lines from a station
json RequestHandler::handleGetLines(const json& request_json) {
    int stationId = request_json.value("stationId", -1);
    if (stationId == -1 || !_graph.hasStation(stationId)) {
        return { {"error", "Invalid or missing stationId"} };
    }

    const auto& lines = _graph.getLinesFrom(stationId);
    if (lines.empty()) {
        return { {"stationId", stationId}, {"lines", json::array()}, {"message", "No lines found"} };
    }

    json lineArray = json::array();
    for (const Graph::TransportationLine& line : lines) {
        json lineObj;
        lineObj["id"] = line.id;
        lineObj["to_code"] = line.to;
        try {
            // Attempt to add destination station name
            lineObj["to_name"] = _graph.getStationById(line.to).name;
        }
        catch (...) {
            lineObj["to_name"] = "[Station Code Not Found]"; // Handle case where 'to' code might be invalid
        }
        // Add other relevant line info if needed (e.g., type, travelTime?)
        lineArray.push_back(lineObj);
    }
    return { {"stationId", stationId}, {"lines", lineArray} };
}

// Handles request type 1: Get station details
json RequestHandler::handleGetStationInfo(const json& request_json) {
    int stationId = request_json.value("stationId", -1);
    if (stationId == -1 || !_graph.hasStation(stationId)) {
        return { {"error", "Invalid or missing stationId"} };
    }
    // No try-catch needed here as hasStation was checked, getStationById should succeed
    const Graph::Station& st = _graph.getStationById(stationId);
    json stationJson = st; // Uses the to_json overload for Graph::Station
    stationJson["code"] = stationId; // Add the code to the response
    return stationJson;
}


// --- Refactored Top-Level Coordinate Route Handler ---
json RequestHandler::handleFindRouteCoordinates(const json& request_json) {
    std::cout << "Handling Coordinate Route Request (Optimized Pairs)..." << std::endl;

    // 1. Extract & Validate Input (no change)
    RequestData inputData;
    json errorJson = extractAndValidateCoordinateInput(request_json, inputData);
    if (!errorJson.is_null()) return errorJson;

    // 2. Find ALL Nearby Stations for Start and End
    NearbyStations allFoundStations;
    errorJson = findNearbyStationsForRoute(inputData, allFoundStations);
    if (!errorJson.is_null()) return errorJson;

    // 3. Select Representative START Stations (Closest, Mid, Furthest)
    StationList selectedStartStations; // This will hold up to 3 start stations
    selectRepresentativeStations(inputData.startCoords.latitude, inputData.startCoords.longitude, allFoundStations.startStations, selectedStartStations);
    if (selectedStartStations.empty()) {
        return { {"error", "Failed to select any representative start stations (initial list empty?)"} };
    }

    // 4. Select ONLY the CLOSEST END Station
    std::optional<StationPair> closestEndStationOpt = selectClosestStation(inputData.endCoords.latitude, inputData.endCoords.longitude, allFoundStations.endStations);
    if (!closestEndStationOpt.has_value()) {
        return { {"error", "Failed to select the closest end station (no nearby end stations found?)"} };
    }
    const StationPair& closestEndStationPair = closestEndStationOpt.value(); // Get the pair

    // 5. Find Best Route Among SELECTED Start Stations and SINGLE End Station
    std::optional<BestRouteResult> bestResult = findBestRouteToDestination(
        selectedStartStations,      // List of start stations
        closestEndStationPair,      // Single end station
        inputData                   // GA Parameters
    );

    // 6. Format Response (no change)
    if (!bestResult.has_value()) {
        return { {"status", "No valid route found between selected start stations and the closest end station"} };
    }
    else {
        return formatRouteResponse(bestResult.value());
    }
}


// --- PRIVATE HELPER FUNCTIONS ---

// Helper 1: Extract and Validate Input
json RequestHandler::extractAndValidateCoordinateInput(const json& request_json, RequestData& inputData) {
    if (!request_json.contains("startLat") || !request_json.contains("startLong") ||
        !request_json.contains("endLat") || !request_json.contains("endLong")) {
        return { {"error", "Missing start or end coordinates (lat/long)"} };
    }
    try {
        inputData.startCoords.latitude = request_json["startLat"].get<double>();
        inputData.startCoords.longitude = request_json["startLong"].get<double>();
        inputData.endCoords.latitude = request_json["endLat"].get<double>();
        inputData.endCoords.longitude = request_json["endLong"].get<double>();
        if (!inputData.startCoords.isValid() || !inputData.endCoords.isValid()) {
            throw std::runtime_error("Invalid coordinate range.");
        }
        // Extract optional GA params
        inputData.generations = request_json.value("gen", 200);
        inputData.mutationRate = request_json.value("mut", 0.3);
        inputData.populationSize = request_json.value("popSize", 100);
        if (inputData.populationSize <= 1 || inputData.generations <= 0 || inputData.mutationRate < 0.0 || inputData.mutationRate > 1.0) {
            return { {"error", "Invalid GA parameters (popSize>1, gen>0, 0<=mut<=1)"} };
        }
    }
    catch (const json::exception& e) {
        return { {"error", "Invalid coordinate or parameter format"}, {"details", e.what()} };
    }
    catch (const std::exception& e) {
        return { {"error", "Coordinate/parameter validation failed"}, {"details", e.what()} };
    }
    return json(); // Return null json on success
}

// Helper 2: Find Nearby Stations
json RequestHandler::findNearbyStationsForRoute(const RequestData& inputData, NearbyStations& foundStations) {
    std::cout << "Finding nearby stations for start: " << inputData.startCoords.latitude << "," << inputData.startCoords.longitude << std::endl;
    foundStations.startStations = _graph.getNearbyStations(Utilities::Coordinates(inputData.startCoords.latitude, inputData.startCoords.longitude));
    if (foundStations.startStations.empty()) {
        return { {"error", "No stations found near start coordinates"} };
    }

    std::cout << "Finding nearby stations for end: " << inputData.endCoords.latitude << "," << inputData.endCoords.longitude << std::endl;
    foundStations.endStations = _graph.getNearbyStations(Utilities::Coordinates(inputData.endCoords.latitude, inputData.endCoords.longitude));
    if (foundStations.endStations.empty()) {
        return { {"error", "No stations found near end coordinates"} };
    }

    std::cout << "Found " << foundStations.startStations.size() << " start candidates and "
        << foundStations.endStations.size() << " end candidates." << std::endl;
    return json(); // Return null json on success
}

RequestHandler::GaTaskResult RequestHandler::runSingleGaTask(
    int startId,
    int endId,
    const RequestHandler::RequestData& gaParams, // Use the correct struct name
    const Graph& graph) // Pass Graph by CONST reference
{
    RequestHandler::GaTaskResult result;
    result.startStationId = startId;
    result.endStationId = endId;
    // std::cout << "  [Thread " << std::this_thread::get_id() << "] Starting GA for pair (" << startId << " -> " << endId << ")" << std::endl;

    try {
        // --- UPDATED Population Constructor Call ---
        // Pass coordinates from gaParams
        Population pop(gaParams.populationSize, startId, endId, graph,
            gaParams.startCoords, gaParams.endCoords); // Pass user and destination coords

        pop.evolve(gaParams.generations, gaParams.mutationRate);

        // getBestSolution internally uses coords for fitness comparison during evolution
        const Route& pairBestRoute = pop.getBestSolution(); // Can throw if pop empty

        // --- UPDATED getFitness Call ---
        // Pass coordinates from gaParams
        double fitness = pairBestRoute.getFitness(startId, endId, graph,
            gaParams.startCoords, gaParams.endCoords); // Pass coords

        // Check validity (doesn't need coords) and fitness
        if (pairBestRoute.isValid(startId, endId, graph) && fitness > 0.0 && !std::isnan(fitness)) {
            result.route = pairBestRoute;   // Copy the valid route
            result.fitness = fitness;
            result.success = true;
            // std::cout << "  [Thread " << std::this_thread::get_id() << "] Success. Fitness: " << fitness << std::endl;
        }
        else {
            std::cerr << "  [Thread " << std::this_thread::get_id() << "] GA produced invalid/zero fitness route for pair (" << startId << " -> " << endId << ") Fitness: " << fitness << std::endl;
            result.success = false;
        }
    }
    catch (const std::runtime_error& ga_error) {
        std::cerr << "  [Thread " << std::this_thread::get_id() << "] GA Runtime Error pair (" << startId << " -> " << endId << "): " << ga_error.what() << std::endl;
        result.success = false;
    }
    catch (const std::exception& e) {
        std::cerr << "  [Thread " << std::this_thread::get_id() << "] GA Exception pair (" << startId << " -> " << endId << "): " << e.what() << std::endl;
        result.success = false;
    }
    catch (...) {
        std::cerr << "  [Thread " << std::this_thread::get_id() << "] Unknown GA Error pair (" << startId << " -> " << endId << ")" << std::endl;
        result.success = false;
    }

    return result; // Return the result struct
}

// Helper 4: Iterate through pairs and find the best route

std::optional<RequestHandler::BestRouteResult> RequestHandler::findBestRouteToDestination(
    const StationList& selectedStartStations,
    const StationPair& endStationPair,
    const RequestData& gaParams)
{
    BestRouteResult overallBest; // Holds the final best result (from BestRouteResult struct)
    overallBest.fitness = -1.0; // Initialize fitness to invalid
    int endId = endStationPair.first;

    // Store the futures returned by std::async
    std::vector<std::future<GaTaskResult>> futures;

    std::cout << "Launching GA tasks asynchronously for " << selectedStartStations.size() << " start stations..." << std::endl;

    // --- Launch Phase ---
    for (const auto& startPair : selectedStartStations) {
        int startId = startPair.first;
        if (startId == endId) {
            std::cout << "  Skipping GA task for start=end station: " << startId << std::endl;
            continue; // Skip if start is the same as the chosen end
        }

        // Launch the task asynchronously.
        // std::launch::async policy requests execution on a new thread if possible.
        // std::cref(_graph) passes the graph by const reference without copying.
        futures.push_back(
            std::async(std::launch::async, runSingleGaTask, startId, endId, gaParams, std::cref(_graph))
        );
        std::cout << "  Launched GA task for pair (" << startId << " -> " << endId << ")" << std::endl;
    }

    if (futures.empty()) {
        std::cout << "No GA tasks were launched." << std::endl;
        return std::nullopt; // No tasks to run
    }

    std::cout << "Waiting for " << futures.size() << " GA tasks to complete..." << std::endl;

    // --- Collect Results Phase ---
    for (size_t i = 0; i < futures.size(); ++i) { // Loop through futures
        try {
            // fut.get() blocks until the task is complete and returns the GaTaskResult.
            // It will re-throw any exception caught from the task function.
            GaTaskResult currentResult = futures[i].get();

            std::cout << "  Task completed for start station " << currentResult.startStationId << ". Success: " << currentResult.success << ", Fitness: " << currentResult.fitness << std::endl;

            // Check if this result is valid and better than the current overall best
            if (currentResult.success && currentResult.fitness > overallBest.fitness) {
                overallBest.fitness = currentResult.fitness;
                overallBest.route = std::move(currentResult.route); // Move the route data
                overallBest.startStationId = currentResult.startStationId; // Record the start ID
                overallBest.endStationId = currentResult.endStationId;     // Record the end ID
                std::cout << "    *** New overall best route found! Start: " << overallBest.startStationId << ", Fitness: " << overallBest.fitness << " ***" << std::endl;
            }
            else if (!currentResult.success) {
                std::cout << "    Task for start station " << currentResult.startStationId << " failed or produced invalid result." << std::endl;
            }

        }
        catch (const std::exception& e) {
            // Catch exceptions re-thrown by fut.get()
            // We don't know which startId failed just from the index 'i' easily,
            // but we can log the general failure. The task itself logs specifics.
            std::cerr << "  Exception caught while getting result from future #" << i << ": " << e.what() << std::endl;
        }
        catch (...) {
            std::cerr << "  Unknown exception caught while getting result from future #" << i << "." << std::endl;
        }
    } // End of collecting results

    std::cout << "Finished collecting results. Overall best fitness found: " << overallBest.fitness << std::endl;

    // Check if a valid route was actually found (fitness > 0 and startId assigned)
    if (overallBest.fitness > 0.0 && overallBest.startStationId != -1) {
        return overallBest; // Return the best result found across all threads
    }
    else {
        std::cout << "No valid route found across all successful GA tasks." << std::endl;
        return std::nullopt; // No valid route found
    }
}


// Helper 5: Format the successful route response JSON
// RequestHandler.cpp

// Helper: Format the successful route response JSON (MODIFIED)
json RequestHandler::formatRouteResponse(const BestRouteResult& bestResult) {
    json resultJson;
    try {
        // Use the stored start/end station IDs from BestRouteResult
        const Graph::Station& startStation = _graph.getStationById(bestResult.startStationId);
        const Graph::Station& endStation = _graph.getStationById(bestResult.endStationId);
        resultJson["from_station"] = { {"code", bestResult.startStationId}, {"name", startStation.name} };
        resultJson["to_station"] = { {"code", bestResult.endStationId}, {"name", endStation.name} };
    }
    catch (const std::exception& e) {
        return { {"error", "Internal error: Failed to lookup best station names"}, {"details", e.what()} };
    }

    const auto& visitedStations = bestResult.route.getVisitedStations(); // These are the ACTION points

    resultJson["status"] = "Route found";
    resultJson["summary"] = {
        {"fitness", bestResult.fitness},
        {"time_mins", bestResult.route.getTotalTime()},
        {"cost", bestResult.route.getTotalCost(this->_graph)},
        // Make sure getTransferCount() is updated as per previous discussion
        {"transfers", bestResult.route.getTransferCount()}
    };

    json detailedStepsJson = json::array(); // Rename to avoid confusion

    if (visitedStations.size() >= 2) {
        for (size_t i = 0; i < visitedStations.size() - 1; ++i) {
            const auto& currentVs = visitedStations[i];     // Action point where segment starts
            const auto& nextVs = visitedStations[i + 1];    // Action point where segment ends
            const auto& lineTaken = nextVs.line;            // Line taken for this segment

            // --- Get Codes for this Segment ---
            // Code of the station where this segment *actually* starts
            int segmentStartCode = (i == 0) ? bestResult.startStationId : visitedStations[i].line.to;
            // Code of the station where this segment *actually* ends
            int segmentEndCode = nextVs.line.to;

            json stepJson; // JSON object for this segment
            stepJson["segment_index"] = i;
            stepJson["line_id"] = lineTaken.id;

            // --- Action Point Info (Departure) ---
            const Graph::Station& fromStation = _graph.getStationById(segmentStartCode); // Get station object
            stepJson["from_name"] = fromStation.name;
            stepJson["from_code"] = segmentStartCode;
            stepJson["from_lat"] = fromStation.coordinates.latitude;
            stepJson["from_long"] = fromStation.coordinates.longitude;

            // --- Action Point Info (Arrival) ---
            const Graph::Station& toStation = _graph.getStationById(segmentEndCode); // Get station object
            stepJson["to_name"] = toStation.name;
            stepJson["to_code"] = segmentEndCode;
            stepJson["to_lat"] = toStation.coordinates.latitude;
            stepJson["to_long"] = toStation.coordinates.longitude;

            // --- Reconstruct and Add Intermediate Stops ---
            bool isPublic = lineTaken.id != "Walk" && lineTaken.id != "Start"; // Check if public transport
            json intermediateStopsJson = json::array(); // Array for intermediate stops of *this* segment

            if (isPublic && segmentStartCode != segmentEndCode) {
                std::vector<Graph::Station> intermediateStations = reconstructIntermediateStops(
                    segmentStartCode,
                    segmentEndCode,
                    lineTaken.id,
                    _graph); // Pass the graph by const reference

                for (const auto& interStation : intermediateStations) {
                    intermediateStopsJson.push_back({
                        {"code", _graph.getStationIdByName(interStation.name)}, // Need a way to get ID back - might need graph changes or store ID in Station
                        {"name", interStation.name},
                        {"lat", interStation.coordinates.latitude},
                        {"long", interStation.coordinates.longitude}
                        });
                }
            }
            stepJson["intermediate_stops"] = intermediateStopsJson; // Add intermediates to the step object

            // --- Action Point Logic (same as before) ---
            bool isStartPoint = (i == 0);
            bool isEndPoint = (i == visitedStations.size() - 2);
            bool isTransferPoint = false;
            if (!isEndPoint && visitedStations.size() > i + 2) {
                const auto& lineAfterNext = visitedStations[i + 2].line;
                // Use the transfer logic from getTransferCount or adapt it
                bool currentIsPublic = lineTaken.id != "Walk" && lineTaken.id != "Start";
                bool nextIsPublic = lineAfterNext.id != "Walk" && lineAfterNext.id != "Start";
                if (currentIsPublic && nextIsPublic && lineTaken.id != lineAfterNext.id) {
                    isTransferPoint = true; // Bus/Train -> Different Bus/Train
                }
                else if (currentIsPublic && !nextIsPublic && lineAfterNext.id != "Start") {
                    isTransferPoint = true; // Bus/Train -> Walk
                }
                else if (!currentIsPublic && currentVs.line.id != "Start" && nextIsPublic) {
                    isTransferPoint = true; // Walk -> Bus/Train
                }
                // This simplified logic might slightly differ from getTransferCount, ensure consistency if needed
            }

            stepJson["from_is_action_point"] = isStartPoint || isTransferPoint; // Start or arrival point of a transfer-inducing segment
            stepJson["to_is_action_point"] = isEndPoint || isTransferPoint;   // End or departure point of a transfer-inducing segment

            // Determine action description based on context
            if (isStartPoint) stepJson["action_description"] = "Depart";
            else if (isTransferPoint && i > 0) stepJson["action_description"] = "Transfer"; // Transfer occurs *at* the start of this segment (end of previous)
            else if (isEndPoint) stepJson["action_description"] = "Arrive";
            else stepJson["action_description"] = "Continue on " + lineTaken.id; // Or "Pass through"

            detailedStepsJson.push_back(stepJson);
        }
    }

    resultJson["detailed_steps"] = detailedStepsJson;

    return resultJson;
}

std::optional<RequestHandler::StationPair> RequestHandler::selectClosestStation(
    double centerLat, double centerLon,
    const StationList& allNearby)
{
    if (allNearby.empty()) {
        return std::nullopt;
    }

    double minDistance = (std::numeric_limits<double>::max)();
    StationPair closestStationPair; // Default constructor
    bool found = false;

    for (const auto& stationPair : allNearby) {
		auto nearbyStationCoords = stationPair.second.coordinates;
        double dist = Utilities::calculateHaversineDistance(
            Utilities::Coordinates(centerLat, centerLon),
            Utilities::Coordinates(nearbyStationCoords.latitude, nearbyStationCoords.longitude));
        if (dist < minDistance) {
            minDistance = dist;
            closestStationPair = stationPair;
            found = true;
        }
    }

    if (found) {
        return closestStationPair;
    }
    else {
        // Should only happen if allNearby was somehow non-empty but contained no valid stations?
        return std::nullopt;
    }
}

void RequestHandler::selectRepresentativeStations(
    double centerLat, double centerLon,
    const StationList& allNearby, // Input: all nearby stations found
    StationList& selected)        // Output: the selected stations (up to 3)
{
    selected.clear();
    if (allNearby.empty()) {
        std::cerr << "Warning: No nearby stations provided to selectRepresentativeStations." << std::endl;
        return;
    }

    // Calculate distance for each station FROM THE USER'S EXACT COORDINATE
    std::vector<std::pair<double, StationPair>> stationsWithDistance;
    stationsWithDistance.reserve(allNearby.size());
    for (const auto& stationPair : allNearby) {
		auto nearbyStationCoords = stationPair.second.coordinates;
        double dist = Utilities::calculateHaversineDistance(
            Utilities::Coordinates(centerLat, centerLon),
            Utilities::Coordinates(nearbyStationCoords.latitude, nearbyStationCoords.longitude));
        stationsWithDistance.push_back({ dist, stationPair });
    }

    // Sort by distance to user (ascending)
    std::sort(stationsWithDistance.begin(), stationsWithDistance.end(),
        [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

    // --- Selection Logic ---
    std::unordered_set<int> selectedIds; // Keep track to avoid duplicates

    // 1. Select Closest to User (S_1)
    const StationPair& closestStationPair = stationsWithDistance[0].second;
    selected.push_back(closestStationPair);
    selectedIds.insert(closestStationPair.first);
    std::cout << "  Selected S1 (Closest): ID " << closestStationPair.first << " (Dist: " << stationsWithDistance[0].first << ")" << std::endl;


    // Handle cases with fewer than 3 stations
    if (stationsWithDistance.size() == 1) {
        return; // Only one station, we're done
    }

    // 2. Select Furthest from User (S_N) - if different from S_1
    const StationPair& furthestStationPair = stationsWithDistance.back().second;
    if (selectedIds.find(furthestStationPair.first) == selectedIds.end()) {
        selected.push_back(furthestStationPair);
        selectedIds.insert(furthestStationPair.first);
        std::cout << "  Selected SN (Furthest): ID " << furthestStationPair.first << " (Dist: " << stationsWithDistance.back().first << ")" << std::endl;
    }
    else {
        std::cout << "  SN (Furthest) is the same as S1, skipping." << std::endl;
    }


    // Handle cases with exactly 2 unique stations
    if (selected.size() < 2 && stationsWithDistance.size() >= 2) {
        // If furthest was same as closest, add the second closest if it exists and is unique
        const StationPair& secondClosest = stationsWithDistance[1].second;
        if (selectedIds.find(secondClosest.first) == selectedIds.end()) {
            selected.push_back(secondClosest);
            selectedIds.insert(secondClosest.first);
            std::cout << "  Selected S2 (Second Closest) as fallback for SN: ID " << secondClosest.first << std::endl;
        }
    }


    if (stationsWithDistance.size() < 3 || selected.size() >= 3) {
        // Need at least 3 distinct stations total to find a 3rd unique representative,
        // or we already have 3 selected (e.g. S1, SN, S2 were all unique)
        return;
    }


    // 3. Select "Most Different" Intermediate (S_k)
    // Find the station between index 1 and size-2 (exclusive ends)
    // that is furthest from S_1 (closestStationPair)

    int best_Sk_index = -1;
    double max_dist_from_S1 = -1.0;

    // Iterate through the stations *excluding* the already selected closest (S1) and furthest (SN)
    // indices considered: 1 to stationsWithDistance.size() - 2
    for (size_t i = 1; i < stationsWithDistance.size() - 1; ++i) {
        const StationPair& candidate_Sk_pair = stationsWithDistance[i].second;

        // Ensure this candidate hasn't already been selected (e.g., if SN was index 1)
        if (selectedIds.find(candidate_Sk_pair.first) != selectedIds.end()) {
            continue; // Skip already selected stations
        }

		auto closestStationCoords = closestStationPair.second.coordinates;
		auto candidateSkCoords = candidate_Sk_pair.second.coordinates;

        // Calculate distance between this candidate (Si) and the closest station (S1)
        double dist_S1_Si = Utilities::calculateHaversineDistance(
            Utilities::Coordinates(closestStationCoords.latitude, closestStationCoords.longitude),
            Utilities::Coordinates(candidateSkCoords.latitude, candidateSkCoords.longitude)
        );

        if (dist_S1_Si > max_dist_from_S1) {
            max_dist_from_S1 = dist_S1_Si;
            best_Sk_index = static_cast<int>(i);
        }
    }

    // Add the best S_k found, if any
    if (best_Sk_index != -1) {
        const StationPair& Sk_pair = stationsWithDistance[best_Sk_index].second;
        // Final check for uniqueness (should be redundant due to loop check, but safe)
        if (selectedIds.find(Sk_pair.first) == selectedIds.end()) {
            selected.push_back(Sk_pair);
            selectedIds.insert(Sk_pair.first); // Add to set, though not strictly needed now
            std::cout << "  Selected SK (Most Different from S1): ID " << Sk_pair.first << " (Dist from S1: " << max_dist_from_S1 << ")" << std::endl;
        }
        else {
            std::cout << "  SK candidate ID " << Sk_pair.first << " was already selected?" << std::endl;
        }

    }
    else if (selected.size() < 3) {
        // Fallback if no suitable Sk was found (e.g., only 2 unique stations nearby, or all intermediates were already selected)
        // Try adding the second closest if it wasn't SN and hasn't been added yet.
        if (stationsWithDistance.size() >= 2) {
            const StationPair& secondClosest = stationsWithDistance[1].second;
            if (selectedIds.find(secondClosest.first) == selectedIds.end()) {
                selected.push_back(secondClosest);
                // selectedIds.insert(secondClosest.first); // Not needed as we exit
                std::cout << "  Selected S2 (Second Closest) as fallback for SK: ID " << secondClosest.first << std::endl;
            }
        }
    }

    // Ensure we don't exceed 3 selections (shouldn't happen with the logic, but as a safeguard)
    if (selected.size() > 3) {
        selected.resize(3);
    }

} // End of selectRepresentativeStations

std::vector<Graph::Station> RequestHandler::reconstructIntermediateStops(
    int segmentStartCode,
    int segmentEndCode,
    const std::string& lineId,
    const Graph& graph)
{
    std::vector<Graph::Station> intermediatePath;
    std::unordered_set<int> visitedInSegment; // Prevent infinite loops in case of cycles

    int currentCode = segmentStartCode;
    visitedInSegment.insert(currentCode);

    const int MAX_INTERMEDIATE_STEPS = 100; // Safety limit
    int steps = 0;

    while (currentCode != segmentEndCode && steps < MAX_INTERMEDIATE_STEPS) {
        steps++;
        bool foundNext = false;
        try {
            const auto& linesFromCurrent = graph.getLinesFrom(currentCode);
            const Graph::TransportationLine* nextLine = nullptr;

            // Find the specific line ID going towards *any* next station
            for (const auto& line : linesFromCurrent) {
                if (line.id == lineId) {
                    // Basic check: Just take the first match for this line ID.
                    // More sophisticated: Check arrival times? Requires knowing departure time... complex.
                    // Simplest approach: Assume there's only one 'lineId' edge relevant here.
                    if (visitedInSegment.find(line.to) == visitedInSegment.end()) {
                        nextLine = &line;
                        break; // Take the first valid, unvisited 'to' for this line ID
                    }
                    // If the only way is back to a visited node (or the target), consider it
                    else if (line.to == segmentEndCode) {
                        nextLine = &line; // Allow going to the target even if visited (shouldn't happen often)
                        break;
                    }
                }
            }

            if (nextLine != nullptr) {
                currentCode = nextLine->to;
                visitedInSegment.insert(currentCode);
                // Add the station *arrived at* (unless it's the final segment end)
                if (currentCode != segmentEndCode) {
                    intermediatePath.push_back(graph.getStationById(currentCode));
                }
                foundNext = true;
            }
            else {
                // Couldn't find the next step for this line ID from current station
                std::cerr << "Warning: reconstructIntermediateStops failed to find next stop for line "
                    << lineId << " from station " << currentCode << std::endl;
                break; // Stop reconstruction for this segment
            }

        }
        catch (const std::exception& e) {
            std::cerr << "Warning: Exception during reconstructIntermediateStops: " << e.what() << std::endl;
            break; // Stop reconstruction on error
        }
        if (!foundNext) break; // Break if no next step was identified
    }

    if (steps >= MAX_INTERMEDIATE_STEPS) {
        std::cerr << "Warning: reconstructIntermediateStops hit max steps limit for line " << lineId
            << " from " << segmentStartCode << " to " << segmentEndCode << std::endl;
    }
    if (currentCode != segmentEndCode && steps < MAX_INTERMEDIATE_STEPS) {
        std::cerr << "Warning: reconstructIntermediateStops did not reach segment end " << segmentEndCode
            << " for line " << lineId << " from " << segmentStartCode << std::endl;
    }


    return intermediatePath;
}