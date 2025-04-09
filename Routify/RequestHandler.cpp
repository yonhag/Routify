
#include "RequestHandler.h"
#include "Population.h"
#include <iostream>
#include "json.hpp" // Assuming nlohmann/json.hpp is included correctly
#include <stdexcept>
#include <string> // Include string

// Use nlohmann::json namespace
using json = nlohmann::json;

// JSON serialization for Graph::Station (move to Graph.h/cpp or keep static if only used here)
static void to_json(json& j, const Graph::Station& p) {
    j = json{
        {"name", p.name},
        {"latitude", p.latitude},
        {"longitude", p.longitude}
    };
}

// --- RequestHandler Implementation ---

RequestHandler::RequestHandler() : _graph() {}

// Main handler function
void RequestHandler::handleRequest(Socket clientSocket)
{
    std::string response_str; // String to send
    std::string received = clientSocket.receiveMessage();
    std::cout << "Received: " << received << std::endl;

    if (received.empty()) {
        // Send JSON error for empty request
        json error_resp = { {"error", "Empty request received"} };
        clientSocket.sendMessage(error_resp.dump());
        return;
    }

    // Use a top-level try-catch for all request handling
    try {
        json request_json = json::parse(received);
        int type = request_json.value("type", -1); // Get type, default to -1 if missing

        json response_json; // Build the response JSON object

        switch (type) {
        case 0: // Get lines from station
            response_json = handleGetLines(request_json);
            break;

        case 1: // Get station info
            response_json = handleGetStationInfo(request_json);
            break;

        case 2: // Find route using GA
            response_json = handleFindRoute(request_json);
            break;

        default: // Invalid type
            response_json = { {"error", "Invalid request type"} };
            break;
        } // End Switch

        response_str = response_json.dump(2); // Pretty-print JSON with indent 2

    }
    catch (const json::parse_error& e) {
        json error_resp = {
            {"error", "Invalid JSON format"},
            {"details", e.what()}
        };
        response_str = error_resp.dump(2);
        std::cerr << "JSON Parse Error: " << e.what() << std::endl;
    }
    catch (const std::out_of_range& e) { // Catch errors like getStationById failure
        json error_resp = {
            {"error", "Data lookup error"},
            {"details", e.what()}
        };
        response_str = error_resp.dump(2);
        std::cerr << "Out of Range Error: " << e.what() << std::endl;
    }
    catch (const std::exception& e) { // Catch other standard exceptions
        json error_resp = {
            {"error", "An unexpected server error occurred"},
            {"details", e.what()}
        };
        response_str = error_resp.dump(2);
        std::cerr << "Standard Exception: " << e.what() << std::endl;
    }
    catch (...) { // Catch anything else
        json error_resp = { {"error", "An unknown server error occurred"} };
        response_str = error_resp.dump(2);
        std::cerr << "Unknown Error occurred." << std::endl;
    }

    // Log and send the final response string
    std::cout << "Sending Response:\n" << response_str << std::endl;
    clientSocket.sendMessage(response_str);
}


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

// Handles request type 2: Find best route using GA
json RequestHandler::handleFindRoute(const json& request_json) {
    // Extract parameters with defaults and validation
    int startStationId = request_json.value("startStationId", -1);
    int destStationId = request_json.value("destStationId", -1);
    int generations = request_json.value("gen", 1000); // Default 1000 generations
    double mutationRate = request_json.value("mut", 0.3);  // Default 30% mutation
    int populationSize = request_json.value("popSize", 100);// Default population 100

    if (!_graph.hasStation(startStationId) || !_graph.hasStation(destStationId) ||
        populationSize <= 1 || generations <= 0 || mutationRate < 0.0 || mutationRate > 1.0) {
        return { {"error", "Invalid parameters for route finding (check IDs, popSize>1, gen>0, 0<=mut<=1)"} };
    }

    // --- Run Genetic Algorithm ---
    std::cout << "Starting GA: Pop=" << populationSize << ", Gen=" << generations << ", Mut=" << mutationRate << std::endl;
    Population pop(populationSize, startStationId, destStationId, this->_graph); // Throws if BFS fails
    pop.evolve(generations, mutationRate);
    const Route& bestRoute = pop.getBestSolution(); // Throws if pop is empty after evolution
    // --- End GA ---


    // --- Format Response JSON ---
    json resultJson;
    try {
        resultJson["from_station"] = { {"code", startStationId}, {"name", _graph.getStationById(startStationId).name} };
        resultJson["to_station"] = { {"code", destStationId}, {"name", _graph.getStationById(destStationId).name} };
    }
    catch (const std::out_of_range& e) {
        return { {"error", "Failed to lookup start/destination station name after GA"}, {"details", e.what()} };
    }

    auto visitedStations = bestRoute.getVisitedStations();

    // Check if the resulting best route is actually valid
    if (visitedStations.empty() || !bestRoute.isValid(startStationId, destStationId, _graph)) {
        resultJson["status"] = "No valid route found by GA";
        // Report the fitness even if invalid (likely 0.0)
        resultJson["best_fitness"] = bestRoute.getFitness(startStationId, destStationId, _graph);
    }
    else if (visitedStations.size() < 2) {
        // Handle trivial case where start == end
        resultJson["status"] = "Route is trivial (start = end?)";
        resultJson["summary"] = {
            {"fitness", bestRoute.getFitness(startStationId, destStationId, _graph)},
            {"time_mins", 0.0}, {"cost", 0.0}, {"transfers", 0}
        };
        json steps = json::array();
        json step;
        step["action"] = "Arrive";
        step["at"] = visitedStations[0].station.name;
        steps.push_back(step);
        resultJson["steps"] = steps;

    }
    else {
        // Valid route found, format steps
        resultJson["status"] = "Route found";
        resultJson["summary"] = {
            {"fitness", bestRoute.getFitness(startStationId, destStationId, _graph)},
            {"time_mins", bestRoute.getTotalTime()},
            {"cost", bestRoute.getTotalCost()},
            {"transfers", bestRoute.getTransferCount()}
        };

        json steps = json::array();
        size_t segmentStartIndex = 0; // Index of the station where the current segment started

        // Iterate through the steps (segments) of the route
        for (size_t i = 1; i < visitedStations.size(); ++i) { // i is the index of the ARRIVAL station for the segment
            const auto& arrivalVs = visitedStations[i];
            const std::string& currentSegmentLineId = arrivalVs.line.id; // Line taken TO reach station 'i'

            // Check if this line is different from the *next* line to be taken (if one exists)
            bool isLastStep = (i == visitedStations.size() - 1);
            const std::string& nextSegmentLineId = isLastStep ? "" : visitedStations[i + 1].line.id;

            // Segment ends if it's the last step OR the next line is different
            if (isLastStep || currentSegmentLineId != nextSegmentLineId) {

                // Don't print the dummy "Start" segment
                if (currentSegmentLineId != "Start") {
                    const std::string& departureStationName = visitedStations[segmentStartIndex].station.name;
                    const std::string& arrivalStationName = arrivalVs.station.name; // Name of station at index 'i'

                    json step;
                    step["action"] = (currentSegmentLineId == "Walk") ? "Walk" : "Take line";
                    // Only include line_id if it's not Walk/Start
                    if (currentSegmentLineId != "Walk" && currentSegmentLineId != "Start") {
                        step["line_id"] = currentSegmentLineId;
                    }
                    step["from"] = departureStationName;
                    step["to"] = arrivalStationName;
                    steps.push_back(step);
                }

                // If this isn't the final arrival, add a Transfer instruction
                if (!isLastStep && currentSegmentLineId != "Start") { // Also don't transfer after "Start"
                    json transferStep;
                    transferStep["action"] = "Transfer";
                    transferStep["at"] = arrivalVs.station.name; // Transfer at the arrival station of this segment
                    steps.push_back(transferStep);
                }

                // The next segment starts at the current arrival station
                segmentStartIndex = i;
            }
        } // End loop through visitedStations

        // Add final arrival step for clarity? Optional.
        // json arrivalStep;
        // arrivalStep["action"] = "Arrive";
        // arrivalStep["at"] = visitedStations.back().station.name;
        // steps.push_back(arrivalStep);

        resultJson["steps"] = steps;
    } // End if/else valid route formatting

    return resultJson; // Return the complete JSON response object
}