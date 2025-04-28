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
json RequestHandler::handleFindRouteCoordinates(const json& request_json) const {
    std::cout << "Handling Coordinate Route Request..." << std::endl;

    // 1. Extract & Validate Input
    RequestData inputData;
    json errorJson = extractAndValidateCoordinateInput(request_json, inputData);
    if (!errorJson.is_null()) return errorJson;

    // 2. Find Nearby Stations
    NearbyStations allFoundStations;
    errorJson = findNearbyStationsForRoute(inputData, allFoundStations);
    if (!errorJson.is_null()) return errorJson;

    // 3. Select Representative START Stations
    StationList selectedStartStations;
    selectRepresentativeStations(inputData.startCoords, allFoundStations.startStations, selectedStartStations);
    if (selectedStartStations.empty()) {
        return { {"error", "Failed to select representative start stations"} };
    }

    // 4. Select CLOSEST END Station
    std::optional<Graph::Station> closestEndStationOpt = selectClosestStation(inputData.endCoords, allFoundStations.endStations);
    if (!closestEndStationOpt.has_value()) {
        return { {"error", "Failed to select closest end station"} };
    }
    const Graph::Station& closestEndStationPair = closestEndStationOpt.value();

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
        bestResult.startStationCode,
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
    const Graph::Station& endStationPair,
    const RequestData& gaParams) const
{
    BestRouteResult overallBest; // Holds the final best result (from BestRouteResult struct)
    overallBest.fitness = -1.0; // Initialize fitness to invalid
    int endCode = endStationPair.code;

    // Store the futures returned by std::async
    std::vector<std::future<GaTaskResult>> futures;

    std::cout << "Launching GA tasks asynchronously for " << selectedStartStations.size() << " start stations..." << std::endl;

    // --- Launch Phase ---
    for (const auto& startPair : selectedStartStations) {
        int startCode = startPair.code;
        if (startCode == endCode) {
            std::cout << "  Skipping GA task for start=end station: " << startCode << std::endl;
            continue; // Skip if start is the same as the chosen end
        }

        // Launch the task asynchronously.
        // std::launch::async policy requests execution on a new thread if possible.
        // std::cref(_graph) passes the graph by const reference without copying.
        futures.push_back(
            std::async(std::launch::async, runSingleGaTask, startCode, endCode, gaParams, std::cref(_graph))
        );
        std::cout << "  Launched GA task for pair (" << startCode << " -> " << endCode << ")" << std::endl;
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
                overallBest.startStationCode = currentResult.startStationId; // Record the start ID
                overallBest.endStationId = currentResult.endStationId;     // Record the end ID
                std::cout << "    *** New overall best route found! Start: " << overallBest.startStationCode << ", Fitness: " << overallBest.fitness << " ***" << std::endl;
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
    if (overallBest.fitness > 0.0 && overallBest.startStationCode != -1) {
        return overallBest; // Return the best result found across all threads
    }
    else {
        std::cout << "No valid route found across all successful GA tasks." << std::endl;
        return std::nullopt; // No valid route found
    }
}

// Helper: Format the successful route response JSON
json RequestHandler::formatRouteResponse(const BestRouteResult& bestResult, const RequestData& inputData) const {
    json resultJson;
    resultJson["status"] = "Route found";

    // Populate From/To Station Info
    json fromStationJson, toStationJson;
    bool startOk = RequestHandler::getStationInfo(_graph, bestResult.startStationCode, fromStationJson); 
    bool endOk = RequestHandler::getStationInfo(_graph, bestResult.endStationId, toStationJson); 
    resultJson["from_station"] = fromStationJson; 
    resultJson["to_station"] = toStationJson;

    // Populate Summary
    resultJson["summary"] = { 
        {"fitness", bestResult.fitness}, 
        {"time_mins", bestResult.route.calculateFullJourneyTime( 
            _graph, bestResult.startStationCode, bestResult.endStationId,
            inputData.startCoords, inputData.endCoords)},
        {"cost", bestResult.route.getTotalCost(_graph)}, 
        {"transfers", bestResult.route.getTransferCount()} 
    }; 

    // Build Detailed Steps
    resultJson["detailed_steps"] = json::array(); 
    const auto& visitedStations = bestResult.route.getVisitedStations(); 
    const Graph::Station* segmentStartStationPtr = nullptr; 
    try { segmentStartStationPtr = &_graph.getStationByCode(bestResult.startStationCode); } 
    catch (...)  {  } 

    // Loop through each step/VisitedStation in the route
    for (size_t i = 0; i < visitedStations.size(); ++i) {
        const auto& currentVs = visitedStations[i];
        const auto& lineTaken = currentVs.line;
        json stepJson;
        stepJson["segment_index"] = i;
        stepJson["line_id"] = lineTaken.id;

        // Get station info for where segment STARTED and ENDED (current step)
        json segmentStartJson, segmentEndJson;
        if (segmentStartStationPtr) { RequestHandler::getStationInfo(_graph, segmentStartStationPtr->code, segmentStartJson); }
        RequestHandler::getStationInfo(_graph, currentVs.station.code, segmentEndJson);
        stepJson["from"] = segmentStartJson;
        stepJson["to"] = segmentEndJson;

        // Determine codes needed for intermediate stops helper
        int segmentStartCode = segmentStartStationPtr ? segmentStartStationPtr->code : -1;
        int segmentEndCode = currentVs.station.code;

        RequestHandler::addIntermediateStops(stepJson, lineTaken, segmentStartCode, segmentEndCode, _graph);
        RequestHandler::addActionDetails(stepJson, i, visitedStations, lineTaken);

        resultJson["detailed_steps"].push_back(stepJson);

        // Update pointer for the *next* segment's start station
        segmentStartStationPtr = &currentVs.station;
    }

    return resultJson; 
}

std::optional<Graph::Station> RequestHandler::selectClosestStation(
    const Utilities::Coordinates& c,
    const StationList& allNearby) const
{
    if (allNearby.empty()) {
        return std::nullopt;
    }

    double minDistance = (std::numeric_limits<double>::max)();
    Graph::Station closestStation;
    bool found = false;

    for (const auto& station : allNearby) {
		auto nearbyStationCoords = station.coordinates;
        double dist = Utilities::calculateHaversineDistance(
            c,
            Utilities::Coordinates(nearbyStationCoords.latitude, nearbyStationCoords.longitude));
        if (dist < minDistance) {
            minDistance = dist;
            closestStation = station;
            found = true;
        }
    }

    if (found) {
        return closestStation;
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
    const Utilities::Coordinates& c,
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
    std::vector<std::pair<double, Graph::Station>> stationsWithDistance;
    stationsWithDistance.reserve(allNearby.size());
    for (const auto& station : allNearby) {
		auto nearbyStationCoords = station.coordinates;
        double dist = Utilities::calculateHaversineDistance(
            Utilities::Coordinates(c.longitude, c.latitude),
            Utilities::Coordinates(nearbyStationCoords.latitude, nearbyStationCoords.longitude));
        stationsWithDistance.push_back({ dist, station });
    }

    // Sort by distance to user (ascending)
    std::sort(stationsWithDistance.begin(), stationsWithDistance.end(),
        [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

    // --- Selection Logic ---
    std::unordered_set<int> selectedIds; // Keep track to avoid duplicates

    // 1. Select Closest to User (S_1)
    const Graph::Station& closestStation = stationsWithDistance[0].second;
    selected.push_back(closestStation);
    selectedIds.insert(closestStation.code);
    std::cout << "  Selected S1 (Closest): ID " << closestStation.code << " (Dist: " << stationsWithDistance[0].first << ")" << std::endl;


    // Handle cases with fewer than 3 stations
    if (stationsWithDistance.size() == 1) {
        return; // Only one station, we're done
    }

    // 2. Select Furthest from User (S_N) - if different from S_1
    const Graph::Station& furthestStationPair = stationsWithDistance.back().second;
    if (!selectedIds.contains(furthestStationPair.code)) {
        selected.push_back(furthestStationPair);
        selectedIds.insert(furthestStationPair.code);
        std::cout << "  Selected SN (Furthest): ID " << furthestStationPair.code << " (Dist: " << stationsWithDistance.back().first << ")" << std::endl;
    }
    else {
        std::cout << "  SN (Furthest) is the same as S1, skipping." << std::endl;
    }


    // Handle cases with exactly 2 unique stations
    if (selected.size() < 2 && stationsWithDistance.size() >= 2) {
        // If furthest was same as closest, add the second closest if it exists and is unique
        const Graph::Station& secondClosest = stationsWithDistance[1].second;
        if (!selectedIds.contains(secondClosest.code)) {
            selected.push_back(secondClosest);
            selectedIds.insert(secondClosest.code);
            std::cout << "  Selected S2 (Second Closest) as fallback for SN: ID " << secondClosest.code << std::endl;
        }
    }


    if (stationsWithDistance.size() < 3 || selected.size() >= 3) {
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
        const Graph::Station& candidate_Sk = stationsWithDistance[i].second;

        // Ensure this candidate hasn't already been selected (e.g., if SN was index 1)
        if (selectedIds.contains(candidate_Sk.code)) {
            continue; // Skip already selected stations
        }

		auto closestStationCoords = closestStation.coordinates;
		auto candidateSkCoords = candidate_Sk.coordinates;

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
        const Graph::Station& Sk = stationsWithDistance[best_Sk_index].second;
        // Final check for uniqueness (should be redundant due to loop check, but safe)
        if (!selectedIds.contains(Sk.code)) {
            selected.push_back(Sk);
            selectedIds.insert(Sk.code); // Add to set, though not strictly needed now
            std::cout << "  Selected SK (Most Different from S1): ID " << Sk.code << " (Dist from S1: " << max_dist_from_S1 << ")" << std::endl;
        }
        else {
            std::cout << "  SK candidate ID " << Sk.code << " was already selected?" << std::endl;
        }

    }
    else if (selected.size() < 3 &&
        stationsWithDistance.size() >= 2) {
        // Fallback if no suitable Sk was found (e.g., only 2 unique stations nearby, or all intermediates were already selected)
        // Try adding the second closest if it wasn't SN and hasn't been added yet.
        const Graph::Station& secondClosest = stationsWithDistance[1].second;
        if (!selectedIds.contains(secondClosest.code)) {
            selected.push_back(secondClosest);
            std::cout << "  Selected S2 (Second Closest) as fallback for SK: ID " << secondClosest.code << std::endl;
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
                segmentStartCode,
                segmentEndCode,
                lineTaken.id,
                graph
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

void RequestHandler::addActionDetails(
    json& stepJson,
    size_t i,
    const std::vector<Route::VisitedStation>& visitedStations,
    const Graph::TransportationLine& lineTaken)
{
    // --- Determine Basic Segment Properties ---
    bool isStartPointOfRoute = (i == 0); 
    bool isEndPointOfRoute = ((i + 1) == visitedStations.size());

    // Check if the line taken for *this* step is public transport
    bool currentStepIsPublic = lineTaken.id != "Walk" && lineTaken.id != "Start";

    // --- Determine if a Transfer Happens *After* This Step ---
    bool isTransferPoint = false;
    if (!isEndPointOfRoute) {
        const auto& nextLine = visitedStations[i + 1].line; 
        bool nextStepIsPublic = nextLine.id != "Walk" && nextLine.id != "Start";

        // Transfer conditions:
        // 1. Public -> Different Public
        if (currentStepIsPublic && nextStepIsPublic && lineTaken.id != nextLine.id) {
            isTransferPoint = true;
        }
        // 2. Public -> Walk
        else if (currentStepIsPublic && !nextStepIsPublic) {
            isTransferPoint = true;
        }
        // 3. Walk -> Public
        else if (!currentStepIsPublic && lineTaken.id != "Start" && nextStepIsPublic) {
            isTransferPoint = true;
        }
    }

    // --- Determine Action Description for the Current Segment ---
    std::string actionDesc = "Unknown Action";
    if (isStartPointOfRoute) {
        actionDesc = (lineTaken.id == "Walk") ? "Walk to first station" : "Depart";
    }
    else if (isEndPointOfRoute) {
        actionDesc = (lineTaken.id == "Walk") ? "Walk to destination" : "Arrive";
    }
    else { 
        if (lineTaken.id == "Walk") {
            actionDesc = "Walk between stations";
        }
        else if (isTransferPoint) {
            actionDesc = "Transfer";
        }
        else {
            actionDesc = "Continue on " + lineTaken.id;
        }
    }
    stepJson["action_description"] = actionDesc;

    // --- Determine if Start/End Stations of *This Segment* are Action Points ---
    stepJson["to_is_action_point"] = isEndPointOfRoute || isTransferPoint;
    stepJson["from_is_action_point"] = isStartPointOfRoute;
}