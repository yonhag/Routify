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
        {"latitude", p.latitude},
        {"longitude", p.longitude}
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
        case 3: response_json = handleNearbyStations(request_json); break;
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

json RequestHandler::handleNearbyStations(const json& request_json)
{
    json response;
    // Initialize "stations" as an empty JSON array.
    response["stations"] = json::array();

    // Retrieve nearby stations from the graph.
    auto stations = this->_graph.getNearbyStations(double(request_json["lat"]), double(request_json["long"]));

    // Loop through each station and append it to the "stations" JSON array.
    for (const auto& station : stations) {
        response["stations"][station.first] = station.second;
    }

    return response;
}


// --- Refactored Top-Level Coordinate Route Handler ---
json RequestHandler::handleFindRouteCoordinates(const json& request_json) {
    std::cout << "Handling Coordinate Route Request (Optimized Pairs)..." << std::endl;

    // 1. Extract & Validate Input (no change)
    CoordinateRouteInput inputData;
    json errorJson = extractAndValidateCoordinateInput(request_json, inputData);
    if (!errorJson.is_null()) return errorJson;

    // 2. Find ALL Nearby Stations for Start and End
    NearbyStations allFoundStations;
    errorJson = findNearbyStationsForRoute(inputData, allFoundStations);
    if (!errorJson.is_null()) return errorJson;

    // 3. Select Representative START Stations (Closest, Mid, Furthest)
    StationList selectedStartStations; // This will hold up to 3 start stations
    selectRepresentativeStations(inputData.startLat, inputData.startLong, allFoundStations.startStations, selectedStartStations);
    if (selectedStartStations.empty()) {
        return { {"error", "Failed to select any representative start stations (initial list empty?)"} };
    }
    logSelectedStations(selectedStartStations, "Start"); // Updated logging call

    // 4. Select ONLY the CLOSEST END Station
    std::optional<StationPair> closestEndStationOpt = selectClosestStation(inputData.endLat, inputData.endLong, allFoundStations.endStations);
    if (!closestEndStationOpt.has_value()) {
        return { {"error", "Failed to select the closest end station (no nearby end stations found?)"} };
    }
    const StationPair& closestEndStationPair = closestEndStationOpt.value(); // Get the pair
    logSelectedStations({ closestEndStationPair }, "End (Closest Only)"); // Log the single station

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
json RequestHandler::extractAndValidateCoordinateInput(const json& request_json, CoordinateRouteInput& inputData) {
    if (!request_json.contains("startLat") || !request_json.contains("startLong") ||
        !request_json.contains("endLat") || !request_json.contains("endLong")) {
        return { {"error", "Missing start or end coordinates (lat/long)"} };
    }
    try {
        inputData.startLat = request_json["startLat"].get<double>();
        inputData.startLong = request_json["startLong"].get<double>();
        inputData.endLat = request_json["endLat"].get<double>();
        inputData.endLong = request_json["endLong"].get<double>();
        if (inputData.startLat < -90 || inputData.startLat > 90 || inputData.startLong < -180 || inputData.startLong > 180 ||
            inputData.endLat < -90 || inputData.endLat > 90 || inputData.endLong < -180 || inputData.endLong > 180) {
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
json RequestHandler::findNearbyStationsForRoute(const CoordinateRouteInput& inputData, NearbyStations& foundStations) {
    std::cout << "Finding nearby stations for start: " << inputData.startLat << "," << inputData.startLong << std::endl;
    foundStations.startStations = _graph.getNearbyStations(inputData.startLat, inputData.startLong);
    if (foundStations.startStations.empty()) {
        return { {"error", "No stations found near start coordinates"} };
    }

    std::cout << "Finding nearby stations for end: " << inputData.endLat << "," << inputData.endLong << std::endl;
    foundStations.endStations = _graph.getNearbyStations(inputData.endLat, inputData.endLong);
    if (foundStations.endStations.empty()) {
        return { {"error", "No stations found near end coordinates"} };
    }

    std::cout << "Found " << foundStations.startStations.size() << " start candidates and "
        << foundStations.endStations.size() << " end candidates." << std::endl;
    return json(); // Return null json on success
}

// Helper 3: Run GA for a single pair
// Returns fitness, or -1.0 on failure/invalid route
double RequestHandler::runGAForPair(int startId, int endId, const CoordinateRouteInput& gaParams, Route& outBestRoute) const {
    std::cout << "  Testing route from station " << startId << " to " << endId << "..." << std::endl;
    try {
        Population pop(gaParams.populationSize, startId, endId, _graph);
        pop.evolve(gaParams.generations, gaParams.mutationRate);
        const Route& pairBestRoute = pop.getBestSolution(); // Can throw
        double fitness = pairBestRoute.getFitness(startId, endId, _graph);

        // Ensure the route is valid before returning fitness > 0
        if (pairBestRoute.isValid(startId, endId, _graph) && fitness > 0.0 && !std::isnan(fitness)) {
            outBestRoute = pairBestRoute; // Copy only if valid and fitness > 0
            std::cout << "  -> Fitness: " << fitness << std::endl;
            return fitness;
        }
        else {
            std::cerr << "  -> GA produced invalid route or zero/NaN fitness for pair (" << startId << " -> " << endId << ")" << std::endl;
            return -1.0; // Indicate failure/invalidity
        }
    }
    catch (const std::runtime_error& ga_error) {
        std::cerr << "  -> GA Runtime Error for pair (" << startId << " -> " << endId << "): " << ga_error.what() << std::endl;
        return -1.0; // Indicate failure
    }
    catch (const std::exception& e) {
        std::cerr << "  -> Unexpected GA Exception for pair (" << startId << " -> " << endId << "): " << e.what() << std::endl;
        return -1.0;
    }
    catch (...) {
        std::cerr << "  -> Unknown GA Error for pair (" << startId << " -> " << endId << ")" << std::endl;
        return -1.0;
    }
}

RequestHandler::GaTaskResult RequestHandler::runSingleGaTask(
    int startId,
    int endId,
    const RequestHandler::CoordinateRouteInput& gaParams, // Assuming CoordinateRouteInput is accessible
    const Graph& graph) // Pass Graph by CONST reference
{
    RequestHandler::GaTaskResult result;
    result.startStationId = startId;
    result.endStationId = endId;
    // Optional: Log thread ID
    // std::cout << "  [Thread " << std::this_thread::get_id() << "] Starting GA for pair (" << startId << " -> " << endId << ")" << std::endl;

    try {
        // Create population INSIDE the task scope
        Population pop(gaParams.populationSize, startId, endId, graph); // graph is const&
        pop.evolve(gaParams.generations, gaParams.mutationRate);
        const Route& pairBestRoute = pop.getBestSolution(); // Can throw if pop empty after evolution
        double fitness = pairBestRoute.getFitness(startId, endId, graph);

        // Check validity and fitness BEFORE assigning to result
        if (pairBestRoute.isValid(startId, endId, graph) && fitness > 0.0 && !std::isnan(fitness)) {
            result.route = pairBestRoute;   // Copy the valid route
            result.fitness = fitness;
            result.success = true;
            // Optional: Log success
            // std::cout << "  [Thread " << std::this_thread::get_id() << "] Success. Fitness: " << fitness << std::endl;
        }
        else {
            std::cerr << "  [Thread " << std::this_thread::get_id() << "] GA produced invalid/zero fitness route for pair (" << startId << " -> " << endId << ")" << std::endl;
            result.success = false; // Mark as failed quality-wise
        }
    }
    catch (const std::runtime_error& ga_error) {
        std::cerr << "  [Thread " << std::this_thread::get_id() << "] GA Runtime Error for pair (" << startId << " -> " << endId << "): " << ga_error.what() << std::endl;
        result.success = false; // Mark as failed due to exception
    }
    catch (const std::exception& e) {
        std::cerr << "  [Thread " << std::this_thread::get_id() << "] Unexpected GA Exception for pair (" << startId << " -> " << endId << "): " << e.what() << std::endl;
        result.success = false;
    }
    catch (...) {
        std::cerr << "  [Thread " << std::this_thread::get_id() << "] Unknown GA Error for pair (" << startId << " -> " << endId << ")" << std::endl;
        result.success = false;
    }

    return result; // Return the result struct
}


// Helper 4: Iterate through pairs and find the best route

std::optional<RequestHandler::BestRouteResult> RequestHandler::findBestRouteToDestination(
    const StationList& selectedStartStations,
    const StationPair& endStationPair,
    const CoordinateRouteInput& gaParams)
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
        resultJson["from_station"] = { {"code", bestResult.startStationId}, {"name", _graph.getStationById(bestResult.startStationId).name} };
        resultJson["to_station"] = { {"code", bestResult.endStationId}, {"name", _graph.getStationById(bestResult.endStationId).name} };
    }
    catch (const std::exception& e) {
        return { {"error", "Internal error: Failed to lookup best station names"}, {"details", e.what()} };
    }

    const auto& visitedStations = bestResult.route.getVisitedStations();

    resultJson["status"] = "Route found";
    resultJson["summary"] = {
        {"fitness", bestResult.fitness},
        {"time_mins", bestResult.route.getTotalTime()},
        {"cost", bestResult.route.getTotalCost()},
        {"transfers", bestResult.route.getTransferCount()}
    };

    json steps = json::array();

    if (visitedStations.size() < 2) {
        // Handle trivial route (start == end, though this case might be filtered earlier)
        if (!visitedStations.empty()) {
            json step;
            step["action"] = "Arrive";
            step["at"] = visitedStations[0].station.name;
            step["lat"] = visitedStations[0].station.latitude;
            step["long"] = visitedStations[0].station.longitude;
            step["is_action_point"] = true; // Start/end is an action point
            steps.push_back(step);
        }
    }
    else {
        // Iterate through ALL segments (pairs of consecutive stations)
        for (size_t i = 0; i < visitedStations.size() - 1; ++i) {
            const auto& currentVs = visitedStations[i];     // Departure station for this segment
            const auto& nextVs = visitedStations[i + 1]; // Arrival station for this segment
            const auto& lineTaken = nextVs.line;         // Line taken FROM current TO next

            json step;
            step["segment_index"] = i; // Add segment index for frontend clarity
            step["line_id"] = lineTaken.id; // Include line ID ("Walk", "Start", or actual ID)

            // Departure Point
            step["from_name"] = currentVs.station.name;
            step["from_code"] = (i == 0) ? bestResult.startStationId : currentVs.line.to; // Get code carefully
            step["from_lat"] = currentVs.station.latitude;
            step["from_long"] = currentVs.station.longitude;

            // Arrival Point
            step["to_name"] = nextVs.station.name;
            step["to_code"] = nextVs.line.to; // The code of the station arrived at
            step["to_lat"] = nextVs.station.latitude;
            step["to_long"] = nextVs.station.longitude;

            // Determine if the endpoints are "action points" (start, end, transfer)
            bool isStartPoint = (i == 0);
            bool isEndPoint = (i == visitedStations.size() - 2); // Is this the *last segment*?
            bool isTransferPoint = false;
            // Check if a line change happens *at the arrival station* of this segment (nextVs)
            if (!isEndPoint) { // Only check for transfers if not the very last station
                const auto& lineAfterNext = visitedStations[i + 2].line; // Line taken AFTER arriving at nextVs
                // Different lines, excluding Walk/Start transitions
                if (lineTaken.id != lineAfterNext.id &&
                    lineTaken.id != "Walk" && lineTaken.id != "Start" &&
                    lineAfterNext.id != "Walk" && lineAfterNext.id != "Start")
                {
                    isTransferPoint = true;
                }
            }

            // Mark stations requiring action
            step["from_is_action_point"] = isStartPoint; // Start is always an action point
            step["to_is_action_point"] = isEndPoint || isTransferPoint; // End or Transfer points require action

            // Add action description (optional but helpful for frontend)
            if (isStartPoint) { step["action_description"] = "Depart"; }
            else if (isTransferPoint) { step["action_description"] = "Transfer here"; }
            else if (isEndPoint) { step["action_description"] = "Arrive"; }
            else { step["action_description"] = "Pass through"; } // Passing through station

            steps.push_back(step);
        } // End loop through segments
    } // End if/else trivial route

    resultJson["detailed_steps"] = steps; // Use a new key for clarity
    // Keep the old 'steps' format for compatibility, or remove it if frontend is fully updated
    // resultJson["steps"] = generateSimplifiedSteps(visitedStations); // Optional: generate old format too

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
        double dist = Utilities::calculateHaversineDistance(
            centerLat, centerLon,
            stationPair.second.latitude, stationPair.second.longitude);
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
    const StationList& allNearby, // Input: all nearby stations
    StationList& selected)        // Output: the selected stations
{
    selected.clear();
    if (allNearby.empty()) return;

    // Calculate distance for each station FROM THE EXACT COORDINATE
    std::vector<std::pair<double, std::pair<int, Graph::Station>>> stationsWithDistance;
    stationsWithDistance.reserve(allNearby.size());
    for (const auto& stationPair : allNearby) {
        double dist = Utilities::calculateHaversineDistance(
            centerLat, centerLon,
            stationPair.second.latitude, stationPair.second.longitude);
        stationsWithDistance.push_back({ dist, stationPair });
    }

    // Sort by distance (ascending)
    std::sort(stationsWithDistance.begin(), stationsWithDistance.end(),
        [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

    // Select up to 3 stations: closest, mid, furthest
    std::unordered_set<int> selectedIds; // Use a set to avoid duplicates if mid == closest/furthest

    // 1. Closest
    if (!stationsWithDistance.empty()) {
        selected.push_back(stationsWithDistance[0].second);
        selectedIds.insert(stationsWithDistance[0].second.first); // Add ID to set
    }

    // 2. Midrange (only if size > 1)
    if (stationsWithDistance.size() > 1) {
        size_t midIndex = stationsWithDistance.size() / 2; // Integer division gives floor
        // Check if this ID hasn't already been selected
        if (selectedIds.find(stationsWithDistance[midIndex].second.first) == selectedIds.end()) {
            selected.push_back(stationsWithDistance[midIndex].second);
            selectedIds.insert(stationsWithDistance[midIndex].second.first);
        }
    }

    // 3. Furthest (only if size > 2 and different from closest/mid)
    if (stationsWithDistance.size() > 2) { // Need at least 3 distinct stations for 3 selections
        // Check if the last station's ID hasn't already been selected
        if (selectedIds.find(stationsWithDistance.back().second.first) == selectedIds.end()) {
            selected.push_back(stationsWithDistance.back().second);
            // No need to add to selectedIds set anymore as we're done
        }
    }
    // Note: If size is 1 or 2, selected will contain just 1 or 2 stations.
    // If size is 3+, it will contain 3 unless mid/furthest were duplicates of closest.
}

void RequestHandler::logSelectedStations(const StationList& selected, const std::string& type) {
    std::cout << "Selected " << type << " Stations (" << selected.size() << "): ";
    for (const auto& p : selected) std::cout << p.first << " "; // p.first is the ID
    std::cout << std::endl;
}
