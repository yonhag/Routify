
#include "Route.h"
#include "Graph.h" // Required for Graph types and access
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <algorithm> // For std::sort, std::max
#include <stdexcept> // For exceptions
#include <cmath>     // For M_PI, trig functions, abs, sqrt, atan2
#include <limits>    // For numeric_limits
#include <numeric>   // For std::accumulate
#include <vector>    // Explicit include
#include <unordered_set> // For visited checks in segment generation

// Define M_PI if not available
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Define MAX_WALKING_DISTANCE as a static const member if it belongs logically to Route
// Or keep it global/namespaced if used more widely. For now, assume it's conceptual for isValid.
// const double Route::MAX_WALKING_DISTANCE = 1.0; // Definition would go in Route.cpp if declared static in header

// Consistent Haversine function for internal use
static double calculateHaversineDistanceRoute(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371.0; // Earth radius in kilometers
    if (abs(lat1 - lat2) < 1e-9 && abs(lon1 - lon2) < 1e-9) { return 0.0; }
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    lat1 = lat1 * M_PI / 180.0; lat2 = lat2 * M_PI / 180.0;
    double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1) * cos(lat2) * sin(dLon / 2) * sin(dLon / 2);
    if (a < 0.0) a = 0.0; if (a > 1.0) a = 1.0; // Clamp 'a' to [0, 1] range
    double c = 2 * atan2(sqrt(a), sqrt(1.0 - a));
    return R * c;
}

// --- Route Class Implementation ---

// Add a visited station step
void Route::addVisitedStation(const VisitedStation& vs) {
    this->_stations.push_back(vs);
}

// Calculate total travel time from line segments
double Route::getTotalTime() const {
    if (_stations.size() < 2) return 0.0;
    double totalTime = 0.0;
    for (size_t i = 1; i < this->_stations.size(); ++i) {
        totalTime += this->_stations[i].line.travelTime;
    }
    return totalTime;
}

// Calculate total cost from line segments
double Route::getTotalCost() const {
    if (_stations.size() < 2) return 0.0;
    double totalCost = 0.0;
    for (size_t i = 1; i < this->_stations.size(); ++i) {
        totalCost += this->_stations[i].line.price;
    }
    return totalCost;
}

// Calculate number of transfers (line changes)
int Route::getTransferCount() const {
    if (_stations.size() <= 2) return 0; // Need at least 3 stations (2 lines) for a transfer

    int transfers = 0;
    for (size_t i = 2; i < this->_stations.size(); ++i) {
        const std::string& prevLineId = this->_stations[i - 1].line.id;
        const std::string& currentLineId = this->_stations[i].line.id;

        // Count as transfer if ID changes, EXCLUDING changes involving "Start" or "Walk"
        if (currentLineId != prevLineId &&
            currentLineId != "Start" && prevLineId != "Start" &&
            currentLineId != "Walk" && prevLineId != "Walk")
        {
            transfers++;
        }
    }
    return transfers;
}

// Get a copy of the visited stations vector
const std::vector<Route::VisitedStation> Route::getVisitedStations() const {
    return this->_stations;
}

bool Route::isValid(int startId, int destinationId, const Graph& graph) const {
    if (_stations.empty()) return false;

    // Check Start Station (still needs name lookup for the very first station)
    try {
        // Ensure first VisitedStation's internal station matches the graph's station for startId
        if (_stations.front().station != graph.getStationById(startId)) {
            // Optional Debug: std::cerr << "isValid Fail: Start station object mismatch." << std::endl;
            return false;
        }
        // Also check the stored previous code for the start station (should be -1 or similar)
        if (_stations.front().prevStationCode != -1) {
            // Optional Debug: std::cerr << "isValid Fail: Start station has unexpected prev code " << _stations.front().prevStationCode << std::endl;
            return false;
        }
    }
    catch (const std::runtime_error&) { return false; } // Catch getStationById or name lookup errors
    catch (const std::out_of_range&) { return false; }

    // Handle special case: start == end
    if (_stations.size() == 1) { return (startId == destinationId); }

    // Check End Station (using line.to of last step)
    if (_stations.back().line.to != destinationId) {
        // Optional Debug: std::cerr << "isValid Fail: End station code mismatch." << std::endl;
        return false;
    }
    // Also check that the last station object matches the graph's station for destinationId
    try {
        if (_stations.back().station != graph.getStationById(destinationId)) {
            // Optional Debug: std::cerr << "isValid Fail: End station object mismatch." << std::endl;
            return false;
        }
    }
    catch (...) { return false; }


    // --- Check Transitions (USING prevStationCode) ---
    for (size_t i = 1; i < _stations.size(); ++i) {
        const auto& vs = _stations[i]; // Current VisitedStation
        const auto& line_taken = vs.line;
        int current_station_code = line_taken.to; // Infer current code from the line used to reach it
        int prev_station_code = vs.prevStationCode; // *** USE STORED PREVIOUS CODE ***

        // Basic check: previous code must be valid
        if (prev_station_code == -1 && i > 0) { // -1 only valid for the real start node (i=0 handled above)
            // Optional Debug: std::cerr << "isValid Fail (Step " << i << "): Invalid prevStationCode (-1)." << std::endl;
            return false;
        }

        // Check 1: Does the 'to' field match the station we *think* we arrived at?
        // We need to verify the station object stored at index 'i' corresponds to current_station_code
        try {
            if (graph.getStationById(current_station_code) != vs.station) {
                // Optional Debug: std::cerr << "isValid Fail (Step " << i << "): Station object mismatch for code " << current_station_code << "." << std::endl;
                return false;
            }
        }
        catch (...) { return false; } // Failed to get station by current_station_code

        // Skip line origin checks for special lines
        if (line_taken.id == "Start") { continue; } // Already checked start node validity
        if (line_taken.id == "Walk") { continue; }

        // Check 2: Does the line originate correctly from prev_station_code?
        bool line_found_at_source = false;
        try {
            // Use the reliable prev_station_code
            if (!graph.hasStation(prev_station_code)) { // Double check prev code exists
                // Optional Debug: std::cerr << "isValid Fail (Step " << i << "): Prev station code " << prev_station_code << " not in graph." << std::endl;
                return false;
            }
            const auto& lines_from_prev = graph.getLinesFrom(prev_station_code);
            for (const auto& available_line : lines_from_prev) {
                if (available_line.id == line_taken.id && available_line.to == current_station_code) {
                    line_found_at_source = true;
                    break;
                }
            }
        }
        catch (const std::out_of_range&) { return false; } // Error getting lines

        if (!line_found_at_source) {
            // Optional Debug: std::cerr << "isValid Fail (Step " << i << "): Line '" << line_taken.id << "' to " << current_station_code
            //           << " not found at source " << prev_station_code << "." << std::endl;
            return false;
        }
    } // End transition check loop

    return true; // All checks passed
}


// --- getFitness (MODIFIED Penalties) ---
double Route::getFitness(int startId, int destinationId, const Graph& graph) const {
    if (!isValid(startId, destinationId, graph)) { return 0.0; }

    // --- Calculate Score Components ---
    double totalTime = getTotalTime();
    double totalCost = getTotalCost();
    int transfers = getTransferCount();

    // --- Define weights/penalties (ADJUSTED) ---
    const double time_weight = 1.0;         // Keep time as base
    const double cost_weight = 0.1;         // Reduce cost importance further?
    const double transfer_penalty = 45.0;   // *** INCREASED SIGNIFICANTLY (e.g., 45 min equivalent) ***
    const double walk_penalty_factor = 2.0; // Keep walk penalty moderate
    const double stop_penalty = 2.0;        // *** INCREASED (e.g., 2 min equivalent per stop) ***

    // Calculate base score + transfer penalty
    double score = (time_weight * totalTime) + (cost_weight * totalCost) + (transfers * transfer_penalty);

    // Add walking penalty
    double walkPenalty = 0.0;
    for (size_t i = 1; i < _stations.size(); ++i) {
        if (_stations[i].line.id == "Walk") {
            walkPenalty += _stations[i].line.travelTime * walk_penalty_factor;
        }
    }
    score += walkPenalty;

    // Add stop penalty (number of segments/steps minus transfers)
    // Subtract 1 because _stations includes the start point
    int num_segments = static_cast<int>(_stations.size()) - 1;
    int num_stops = std::max(0, num_segments - transfers); // Approx. stops excluding transfer points
    score += num_stops * stop_penalty;

    // --- Convert Score to Fitness ---
    if (score <= std::numeric_limits<double>::epsilon()) {
        return std::numeric_limits<double>::max();
    }
    return 1.0 / score;
}


// --- generatePathSegment (MODIFIED to use new VisitedStation) ---
bool Route::generatePathSegment(int segmentStartId, int segmentEndId, const Graph& graph, std::mt19937& gen, std::vector<VisitedStation>& segment) {
    segment.clear();
    int currentCode = segmentStartId;
    int prevCode = -1; // Track previous code for VisitedStation constructor
    const int maxSteps = 75;
    int steps = 0;
    const double epsilon = 1e-6;
    std::unordered_set<int> visitedCodesSegment;

    try {
        if (!graph.hasStation(segmentStartId) || !graph.hasStation(segmentEndId)) return false;
        const Graph::Station& destStation = graph.getStationById(segmentEndId);
        double destLat = destStation.latitude; double destLon = destStation.longitude;
        visitedCodesSegment.insert(currentCode);

        while (currentCode != segmentEndId && steps < maxSteps) {
            if (!graph.hasStation(currentCode)) return false;
            const Graph::Station& currentStation = graph.getStationById(currentCode);

            double distanceToSegmentEnd = calculateHaversineDistanceRoute(currentStation.latitude, currentStation.longitude, destLat, destLon);
            const double MAX_WALKING_DISTANCE_SEGMENT = 1.0;
            if (distanceToSegmentEnd < MAX_WALKING_DISTANCE_SEGMENT) {
                Graph::TransportationLine walkingEdge("Walk", segmentEndId, (distanceToSegmentEnd / 5.0) * 60.0, 0, Graph::TransportMethod::Walk);
                // *** Use correct prevCode when adding final walking step ***
                segment.push_back(VisitedStation(destStation, walkingEdge, currentCode)); // currentCode is the station *before* the walk
                currentCode = segmentEndId;
                break;
            }

            const auto& availableLines = graph.getLinesFrom(currentCode);
            if (availableLines.empty()) return false;

            // (Guided Selection Logic - remains the same)
            std::vector<const Graph::TransportationLine*> validLines; std::vector<double> weights;
            for (const auto& line : availableLines) { /* ... find valid lines/weights ... */
                int nextCode = line.to;
                if (graph.hasStation(nextCode) && visitedCodesSegment.find(nextCode) == visitedCodesSegment.end()) {
                    const Graph::Station& nextStation = graph.getStationById(nextCode);
                    double distToDest = calculateHaversineDistanceRoute(nextStation.latitude, nextStation.longitude, destLat, destLon);
                    weights.push_back(distToDest + epsilon); validLines.push_back(&line);
                }
            }
            if (validLines.empty()) return false;
            std::vector<double> probabilities; double sumInverseWeights = 0.0;
            for (double w : weights) { sumInverseWeights += 1.0 / std::max(w, epsilon); }
            const Graph::TransportationLine* chosenLinePtr = nullptr;
            if (sumInverseWeights <= epsilon) { /* ... uniform fallback ... */
                std::uniform_int_distribution<> uniform_dis(0, static_cast<int>(validLines.size() - 1)); chosenLinePtr = validLines[uniform_dis(gen)];
            }
            else { /* ... weighted choice ... */
                for (double w : weights) { probabilities.push_back((1.0 / std::max(w, epsilon)) / sumInverseWeights); }
                std::discrete_distribution<> dist(probabilities.begin(), probabilities.end()); chosenLinePtr = validLines[dist(gen)];
            }

            // Add the chosen step to the segment
            const Graph::Station& nextStation = graph.getStationById(chosenLinePtr->to);
            // *** Pass the correct previous code (currentCode) ***
            segment.push_back(VisitedStation(nextStation, *chosenLinePtr, currentCode));
            prevCode = currentCode; // Update previous code for the *next* iteration
            currentCode = chosenLinePtr->to; // Move to the next station
            visitedCodesSegment.insert(currentCode);
            steps++;
        }
        return (currentCode == segmentEndId);

    }
    catch (...) { /* Error handling */ return false; }
}



// --- Mutate (No changes needed, uses fixed generatePathSegment) ---
void Route::mutate(double mutationRate, std::mt19937& gen, int startId, int destinationId, const Graph& graph) {
    // (Keep existing mutate logic)
    std::uniform_real_distribution<> prob_dis(0.0, 1.0); if (prob_dis(gen) >= mutationRate) return;
    if (this->_stations.size() <= 2) return;
    std::uniform_int_distribution<> index_dis(1, static_cast<int>(_stations.size() - 1)); int restart_index = index_dis(gen);
    int segment_start_code; try { segment_start_code = graph.getStationIdByName(_stations[restart_index - 1].station.name); }
    catch (...) { return; }
    std::vector<VisitedStation> new_segment;
    bool success = generatePathSegment(segment_start_code, destinationId, graph, gen, new_segment);
    if (success && !new_segment.empty()) { _stations.resize(restart_index); _stations.insert(_stations.end(), new_segment.begin(), new_segment.end()); }
}
