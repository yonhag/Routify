#include "RequestHandler.h"
#include "Population.h"
#include "Utilities.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>
#include <algorithm>
#include <unordered_set>
#include <thread>
#include <future>

using json = nlohmann::json;

static void to_json(json& j, const Graph::Station& p) {
    j = json{
        {"name", p.name},
        {"latitude", p.coordinates.latitude},
        {"longitude", p.coordinates.longitude}
    };
}

RequestHandler::RequestHandler() : _graph() {}

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
        case 2: response_json = handleFindRouteCoordinates(request_json); break;
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

// --- Helper Handlers for Different Request Types ---

// Handles request type 0: Get lines from a station
json RequestHandler::handleGetLines(const json& request_json) const {
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
            lineObj["to_name"] = _graph.getStationByCode(line.to).name;
        }
        catch (...) {
            lineObj["to_name"] = "[Station Code Not Found]"; // Handle case where 'to' code might be invalid
        }
        // Add other relevant line info if needed (e.g., type, travelTime)
        lineArray.push_back(lineObj);
    }
    return { {"stationId", stationId}, {"lines", lineArray} };
}

// Handles request type 1: Get station details
json RequestHandler::handleGetStationInfo(const json& request_json) const {
    int stationId = request_json.value("stationId", -1);
    if (stationId == -1 || !_graph.hasStation(stationId)) {
        return { {"error", "Invalid or missing stationId"} };
    }

    const Graph::Station& st = _graph.getStationByCode(stationId);
    json stationJson = st;
    
    stationJson["code"] = stationId;

    return stationJson;
}


// --- Top-Level Coordinate Route Handler ---
json RequestHandler::handleFindRouteCoordinates(const json& request_json) {
    std::cout << "Handling Coordinate Route Request..." << std::endl;

    // 1. Extract & Validate Input
    RequestData inputData; // Holds userCoords, destCoords, GA params
    json errorJson = extractAndValidateCoordinateInput(request_json, inputData);
    if (!errorJson.is_null()) return errorJson;

    // 2. Find Nearby Stations
    NearbyStations allFoundStations;
    errorJson = findNearbyStationsForRoute(inputData, allFoundStations);
    if (!errorJson.is_null()) return errorJson;

    // 3. Select Representative START Stations
    StationList selectedStartStations;
    selectRepresentativeStations(inputData.startCoords.latitude, inputData.startCoords.longitude, allFoundStations.startStations, selectedStartStations);
    if (selectedStartStations.empty()) {
        return { {"error", "Failed to select representative start stations"} };
    }

    // 4. Select CLOSEST END Station
    std::optional<StationPair> closestEndStationOpt = selectClosestStation(inputData.endCoords.latitude, inputData.endCoords.longitude, allFoundStations.endStations);
    if (!closestEndStationOpt.has_value()) {
        return { {"error", "Failed to select closest end station"} };
    }
    const StationPair& closestEndStationPair = closestEndStationOpt.value();

    // 5. Find Best Route (GA)
    std::optional<BestRouteResult> bestResultOpt = findBestRouteToDestination(
        selectedStartStations,
        closestEndStationPair,
        inputData // Pass GA params and coords needed by Population/Fitness
    );

    // 6. Post-Process Result: Compare Direct Walk vs Station Route
    double directWalkTime = 0.0;
    double directWalkDistance = Utilities::calculateHaversineDistance(inputData.startCoords, inputData.endCoords);
    // Use static const from Route if accessible, otherwise define locally
    if (Utilities::WALK_SPEED_KPH > 0) {
        directWalkTime = (directWalkDistance / Utilities::WALK_SPEED_KPH) * 60.0;
    }
    const double MAX_REASONABLE_WALK_KM = 2.0;

    // Case 1: No station route found by GA
    if (!bestResultOpt.has_value()) {
        if (directWalkDistance < MAX_REASONABLE_WALK_KM) {
            return {
               {"status", "Direct walk recommended"}, {"reason", "No public transport route found"},
               {"walk_distance_km", directWalkDistance}, {"walk_time_mins", directWalkTime},
               {"from_coords", {{"lat", inputData.startCoords.latitude}, {"lon", inputData.startCoords.longitude}}},
               {"to_coords", {{"lat", inputData.endCoords.latitude}, {"lon", inputData.endCoords.longitude}}}
            };
        }
        else { return { {"status", "No route found (and direct walk too long)"} }; }
    }

    // Case 2: Station route WAS found
    BestRouteResult bestResult = bestResultOpt.value(); // Get the result object

    // Calculate total time for the station route using the Route method
    double totalStationRouteTime = bestResult.route.calculateFullJourneyTime(
        _graph,
        bestResult.startStationId,
        bestResult.endStationId,
        inputData.startCoords,
        inputData.endCoords
    );

    // Decision Logic (Simplified walk check)
    bool onlyWalkingInStationRoute = true;
    if (!bestResult.route.getVisitedStations().empty()) {
        for (const auto& vs : bestResult.route.getVisitedStations()) {
            // Need Route::isPublicTransport or similar check. Assume simple check for now.
            if (vs.line.id != "Walk" && vs.line.id != "Start") {
                onlyWalkingInStationRoute = false;
                break;
            }
        }
    }

    // Rule 1: Route was only walking between stations
    if (onlyWalkingInStationRoute) { /* Return Direct Walk JSON if feasible */
        if (directWalkDistance < MAX_REASONABLE_WALK_KM) {
            return {
               {"status", "Direct walk recommended"}, {"reason", "Route involved no public transport"},
               // ... (Direct walk details) ...
                {"walk_distance_km", directWalkDistance}, {"walk_time_mins", directWalkTime},
               {"station_route_alternative_time_mins", totalStationRouteTime},
               {"from_coords", {{"lat", inputData.startCoords.latitude}, {"lon", inputData.startCoords.longitude}}},
               {"to_coords", {{"lat", inputData.endCoords.latitude}, {"lon", inputData.endCoords.longitude}}}
            };
        }
        else { /* Handle case where direct walk is too long, but route was only walking */
            std::cerr << "Warning: Route only involved walking, but direct walk too long. Formatting walk route." << std::endl;
            // Fall through to format the station-based walk route
        }
    }

    // Rule 2: Compare times if public transport was involved
    const double PREFER_WALK_THRESHOLD_MINS = 5.0;
    if (!onlyWalkingInStationRoute && directWalkTime < totalStationRouteTime + PREFER_WALK_THRESHOLD_MINS && directWalkDistance < MAX_REASONABLE_WALK_KM) { /* Return Direct Walk JSON */
        return {
           {"status", "Direct walk recommended"}, {"reason", "Direct walk is faster or comparable"},
           // ... (Direct walk details) ...
            {"walk_distance_km", directWalkDistance}, {"walk_time_mins", directWalkTime},
           {"station_route_alternative_time_mins", totalStationRouteTime},
           {"from_coords", {{"lat", inputData.startCoords.latitude}, {"lon", inputData.startCoords.longitude}}},
           {"to_coords", {{"lat", inputData.endCoords.latitude}, {"lon", inputData.endCoords.longitude}}}
        };
    }

    // Rule 3: Check final walk distance of the station route
    double finalWalkDist = 0.0;
    try {
        finalWalkDist = Utilities::calculateHaversineDistance(_graph.getStationByCode(bestResult.endStationId).coordinates, inputData.endCoords);
    }
    catch (...) {}
    const double MAX_FINAL_WALK_KM = 1.5; // Example limit
    if (finalWalkDist > MAX_FINAL_WALK_KM) {
        // Add warning when formatting
        json response = formatRouteResponse(bestResult, inputData); // Pass inputData
        response["warning"] = "Route requires a long final walk (" + std::to_string(finalWalkDist) + " km)";
        return response;
    }

    // Default: Format the station route
    return formatRouteResponse(bestResult, inputData); // Pass inputData
}



// --- PRIVATE HELPER FUNCTIONS ---

// Helper 1: Extract and Validate Input
json RequestHandler::extractAndValidateCoordinateInput(const json& request_json, RequestData& inputData) const {
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
            return { {"error", "Invalid coordinates" } };
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
json RequestHandler::findNearbyStationsForRoute(const RequestData& inputData, NearbyStations& foundStations) const {
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
    const RequestData& gaParams) const
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
// Also finds stations along the way since the GA doesn't account for them
// RequestHandler.cpp

// Helper: Format the successful route response JSON (MODIFIED)
json RequestHandler::formatRouteResponse(const BestRouteResult& bestResult, const RequestData& inputData) const {
    json resultJson;
    try {
        // Use the stored start/end station IDs from BestRouteResult
        const Graph::Station& startStation = _graph.getStationByCode(bestResult.startStationId);
        const Graph::Station& endStation = _graph.getStationByCode(bestResult.endStationId);
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
        {"time_mins", bestResult.route.calculateFullJourneyTime(
                                        _graph,
                                        bestResult.startStationId,
                                        bestResult.endStationId,
                                        inputData.startCoords,
                                        inputData.endCoords) },
        {"cost", bestResult.route.getTotalCost(this->_graph)}, // Uses optimized cost
        {"transfers", bestResult.route.getTransferCount()}
    };

    json detailedStepsJson = json::array();

    if (visitedStations.size() >= 2) { // Need at least Start -> End
        for (size_t i = 0; i < visitedStations.size() - 1; ++i) {
            // --- Action Point Info ---
            // Action point where the segment *starts* (or the overall route start)
            const auto& startActionPointVs = visitedStations[i];
            // Action point where the segment *ends*
            const auto& endActionPointVs = visitedStations[i + 1];
            // Line taken *to reach* the endActionPointVs
            const auto& lineTaken = endActionPointVs.line;

            // --- Determine Actual Segment Start/End Codes ---
            int segmentStartCode;
            if (i == 0) {
                segmentStartCode = bestResult.startStationId; // Overall route start
            }
            else {
                // For segment i>0, it starts where segment i-1 ended.
                segmentStartCode = startActionPointVs.station.code; // The code of the station *at* the start action point
            }
            // The segment ends at the station represented by endActionPointVs
            int segmentEndCode = endActionPointVs.station.code; // The code of the station *at* the end action point


            json stepJson; // JSON object for this segment
            stepJson["segment_index"] = i;
            stepJson["line_id"] = lineTaken.id; // ID of the line used for *this* segment

            // --- Get Station Info for Segment Start/End ---
            try {
                const Graph::Station& fromStation = _graph.getStationByCode(segmentStartCode);
                stepJson["from_name"] = fromStation.name;
                stepJson["from_code"] = segmentStartCode;
                stepJson["from_lat"] = fromStation.coordinates.latitude;
                stepJson["from_long"] = fromStation.coordinates.longitude;

                const Graph::Station& toStation = _graph.getStationByCode(segmentEndCode);
                stepJson["to_name"] = toStation.name;
                stepJson["to_code"] = segmentEndCode;
                stepJson["to_lat"] = toStation.coordinates.latitude;
                stepJson["to_long"] = toStation.coordinates.longitude;
            }
            catch (const std::exception& e) {
                std::cerr << "Error formatting step " << i << ": Cannot get station info. " << e.what() << std::endl;
                // Add error info or skip step?
                stepJson["error"] = "Failed to get station details for segment.";
                detailedStepsJson.push_back(stepJson);
                continue;
            }


            // --- Reconstruct and Add Intermediate Stops using getStationsAlongLineSegment ---
            bool isPublic = lineTaken.id != "Walk" && lineTaken.id != "Start";
            json intermediateStopsJson = json::array();

            if (isPublic && segmentStartCode != segmentEndCode) {
                try {
                    // ***** CALL THE ORIGINAL FUNCTION *****
                    std::vector<Graph::Station> segmentPathStations = _graph.getStationsAlongLineSegment(
                        lineTaken.id,
                        segmentStartCode,
                        segmentEndCode);

                    // The result includes start and end, we only want intermediates
                    // Iterate from the second station up to the second-to-last station
                    if (segmentPathStations.size() > 2) {
                        for (size_t j = 1; j < segmentPathStations.size() - 1; ++j) {
                            const auto& interStation = segmentPathStations[j];
                            intermediateStopsJson.push_back({
                                {"code", interStation.code},
                                {"name", interStation.name},
                                {"lat", interStation.coordinates.latitude},
                                {"long", interStation.coordinates.longitude}
                                });
                        }
                    }
                    // Optional: Add logging if segmentPathStations is empty or has < 2 stations
                    // else if (segmentPathStations.empty()){ std::cerr << "Warning: getStationsAlongLineSegment returned empty for " << segmentStartCode << "->" << segmentEndCode << " line " << lineTaken.id << std::endl;}
                    // else {std::cerr << "Warning: getStationsAlongLineSegment returned only start/end for " << segmentStartCode << "->" << segmentEndCode << " line " << lineTaken.id << std::endl;}

                }
                catch (const std::exception& e) {
                    std::cerr << "Error during getStationsAlongLineSegment for " << segmentStartCode << "->" << segmentEndCode << " line " << lineTaken.id << ": " << e.what() << std::endl;
                    // Leave intermediateStopsJson empty on error
                }
            }
            stepJson["intermediate_stops"] = intermediateStopsJson; // Add intermediates (possibly empty)

            // --- Action Point Logic (should be mostly correct, review if needed) ---
            bool isStartPoint = (i == 0);
            // Endpoint check needs to look at the *index relative to the full path*
            bool isEndPoint = ((i + 1) == (visitedStations.size() - 1)); // Is this the *last segment*?

            bool isTransferPoint = false;
            // Check if a transfer happens *at the end* of this current segment (i.e., at segmentEndCode / endActionPointVs)
            if (!isEndPoint) { // Only check if not the absolute last station
                const auto& nextLine = visitedStations[i + 2].line; // The line taken *after* reaching endActionPointVs
                bool currentIsPublic = lineTaken.id != "Walk" && lineTaken.id != "Start";
                bool nextIsPublic = nextLine.id != "Walk" && nextLine.id != "Start";

                if (currentIsPublic && nextIsPublic && lineTaken.id != nextLine.id) {
                    isTransferPoint = true; // Bus/Train -> Different Bus/Train at endActionPointVs
                }
                else if (currentIsPublic && !nextIsPublic) {
                    isTransferPoint = true; // Bus/Train -> Walk at endActionPointVs
                }
                else if (!currentIsPublic && lineTaken.id != "Start" && nextIsPublic) {
                    isTransferPoint = true; // Walk -> Bus/Train at endActionPointVs
                }
            }

            // An action point is where you start, end, or transfer.
            stepJson["from_is_action_point"] = isStartPoint || (i > 0 && stepJson["action_description"] == "Transfer"); // Is the start of this segment an action point? (Overall start, or where previous transfer landed)
            stepJson["to_is_action_point"] = isEndPoint || isTransferPoint; // Is the end of this segment an action point? (Overall end, or start of next transfer)

            // Determine action description based on context
            // This logic might need refinement based on exactly what you want to display
            if (i == 0 && lineTaken.id == "Walk") stepJson["action_description"] = "Walk to first station";
            else if (i == 0) stepJson["action_description"] = "Depart"; // First public transport leg
            else if (!isPublic && lineTaken.id == "Walk" && isEndPoint) stepJson["action_description"] = "Walk to destination";
            else if (!isPublic && lineTaken.id == "Walk") stepJson["action_description"] = "Walk between stations"; // Intermediate walk
            else if (isTransferPoint) stepJson["action_description"] = "Transfer"; // Arrived at station, will transfer for next leg
            else if (isEndPoint) stepJson["action_description"] = "Arrive"; // Final public transport leg arrives
            else stepJson["action_description"] = "Continue on " + lineTaken.id; // Staying on the same line (no transfer at end point)


            detailedStepsJson.push_back(stepJson);
        }
    }

    resultJson["detailed_steps"] = detailedStepsJson;

    return resultJson;
}

std::optional<RequestHandler::StationPair> RequestHandler::selectClosestStation(
    double centerLat, double centerLon,
    const StationList& allNearby) const
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

bool RequestHandler::getStationInfo(const Graph& graph, const int stationId, json& stationJson) {
    // (Implementation as before)
    try {
        const Graph::Station& station = graph.getStationByCode(stationId);
        stationJson["name"] = station.name; stationJson["code"] = stationId;
        stationJson["lat"] = station.coordinates.latitude; stationJson["long"] = station.coordinates.longitude;
        return true;
    }
    catch (const std::exception&) {
        stationJson["error"] = "Station info lookup failed"; stationJson["code"] = stationId;
        return false;
    }
}

void RequestHandler::selectRepresentativeStations(
    double centerLat, double centerLon,
    const StationList& allNearby, // Input: all nearby stations found
    StationList& selected)        // Output: the selected stations (up to 3)
    const
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
    if (!selectedIds.contains(furthestStationPair.first)) {
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
        if (!selectedIds.contains(secondClosest.first)) {
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
        if (selectedIds.contains(candidate_Sk_pair.first)) {
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
        if (!selectedIds.contains(Sk_pair.first)) {
            selected.push_back(Sk_pair);
            selectedIds.insert(Sk_pair.first); // Add to set, though not strictly needed now
            std::cout << "  Selected SK (Most Different from S1): ID " << Sk_pair.first << " (Dist from S1: " << max_dist_from_S1 << ")" << std::endl;
        }
        else {
            std::cout << "  SK candidate ID " << Sk_pair.first << " was already selected?" << std::endl;
        }

    }
    else if (selected.size() < 3 &&
        stationsWithDistance.size() >= 2) {
        // Fallback if no suitable Sk was found (e.g., only 2 unique stations nearby, or all intermediates were already selected)
        // Try adding the second closest if it wasn't SN and hasn't been added yet.
        const StationPair& secondClosest = stationsWithDistance[1].second;
        if (!selectedIds.contains(secondClosest.first)) {
            selected.push_back(secondClosest);
            std::cout << "  Selected S2 (Second Closest) as fallback for SK: ID " << secondClosest.first << std::endl;
        }
    }

    // Ensure we don't exceed 3 selections (shouldn't happen with the logic, but as a safeguard)
    if (selected.size() > 3) {
        selected.resize(3);
    }

} // End of selectRepresentativeStations

std::vector<Graph::Station> RequestHandler::reconstructIntermediateStops(
    int segmentStartCode, // Correct order: start, end, lineId, graph
    int segmentEndCode,
    const std::string& lineId,
    const Graph& graph)
{
    // (Implementation as provided before - traces the path)
    std::vector<Graph::Station> intermediatePath;
    std::unordered_set<int> visitedInSegment;
    int currentCode = segmentStartCode;
    visitedInSegment.insert(currentCode);
    const int MAX_INTERMEDIATE_STEPS = 100;
    int steps = 0;

    while (currentCode != segmentEndCode && steps < MAX_INTERMEDIATE_STEPS) {
        steps++;
        bool foundNext = false;
        try {
            const auto& linesFromCurrent = graph.getLinesFrom(currentCode);
            const Graph::TransportationLine* nextLine = nullptr;
            for (const auto& line : linesFromCurrent) {
                // Match line ID and ensure next stop isn't backtracking (unless it's the end)
                if (line.id == lineId && (!visitedInSegment.contains(line.to) || line.to == segmentEndCode)) {
                    nextLine = &line;
                    break;
                }
            }
            if (nextLine != nullptr) {
                currentCode = nextLine->to;
                visitedInSegment.insert(currentCode);
                // Add the station *arrived at* (unless it's the final segment end)
                if (currentCode != segmentEndCode) {
                    // Ensure station exists before adding
                    intermediatePath.push_back(graph.getStationByCode(currentCode));
                }
                foundNext = true;
            }
            else {
                // Log warning: Failed to find next stop
                break;
            }
        }
        catch (const std::exception& e) {
            // Log warning: Exception during reconstruction
            break;
        }
        if (!foundNext) break;
    }
    // Add warnings for MAX_STEPS or not reaching end if desired
    return intermediatePath;
}


// CORRECTED Definition for addIntermediateStops
void RequestHandler::addIntermediateStops(
    json& stepJson, const Graph::TransportationLine& lineTaken,
    int segmentStartCode, int segmentEndCode, const Graph& graph)
{
    bool isPublic = lineTaken.id != "Walk" && lineTaken.id != "Start";
    stepJson["intermediate_stops"] = json::array();

    if (isPublic && segmentStartCode != segmentEndCode) {
        try {
            // *** CORRECTED CALL to reconstructIntermediateStops ***
            auto pathStations = RequestHandler::reconstructIntermediateStops(
                segmentStartCode, // Argument 1: Start Station Code
                segmentEndCode,   // Argument 2: End Station Code
                lineTaken.id,     // Argument 3: Line ID
                graph             // Argument 4: Graph object
            );

            // The rest of the logic remains the same
            if (pathStations.size() > 0) { // PathStations contains only INTERMEDIATE ones now
                for (const auto& st : pathStations) {
                    stepJson["intermediate_stops"].push_back({
                        {"code", st.code}, {"name", st.name},
                        {"lat", st.coordinates.latitude}, {"long", st.coordinates.longitude}
                        });
                }
            }
        }
        catch (const std::exception& e) {
            stepJson["intermediate_stops_error"] = e.what();
        }
    }
}