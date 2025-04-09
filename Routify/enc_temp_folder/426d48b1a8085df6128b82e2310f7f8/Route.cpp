#include "Route.h"
#include "Graph.h" // Required for Graph types and access
#include <iostream>
#include <cstdlib> // Included for completeness, but std::rand not used now
#include <ctime>   // Included for completeness, but time() not used now
#include <algorithm> // For std::sort, std::max
#include <stdexcept> // For exceptions
#include <cmath>     // For M_PI, trig functions, abs, sqrt, atan2
#include <limits>    // For numeric_limits
#include <numeric>   // For std::accumulate
#include <vector>    // Explicit include
#include <unordered_set> // For visited checks in segment generation

// Define M_PI if not available (e.g., on Windows with MSVC)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
const double Route::MAX_WALKING_DISTANCE = 1.0;

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


// Add a visited station step
void Route::addVisitedStation(const VisitedStation& vs) {
    this->_stations.push_back(vs);
}

// Calculate total travel time from line segments
double Route::getTotalTime() const {
    if (_stations.size() < 2) return 0.0; // Need at least one step (line segment)
    double totalTime = 0.0;
    // Start from index 1, as station[0] has no preceding line time
    for (size_t i = 1; i < this->_stations.size(); ++i) {
        totalTime += this->_stations[i].line.travelTime; // Assumes travelTime is set correctly
    }
    return totalTime;
}

// Calculate total cost from line segments
double Route::getTotalCost() const {
    if (_stations.size() < 2) return 0.0;
    double totalCost = 0.0;
    for (size_t i = 1; i < this->_stations.size(); ++i) {
        totalCost += this->_stations[i].line.price; // Assumes price is set correctly
    }
    return totalCost;
}

// Calculate number of transfers (line changes)
int Route::getTransferCount() const {
    if (_stations.size() <= 2) return 0; // Need at least 3 stations (2 lines) for a transfer

    int transfers = 0;
    // Start comparing from the third station (index 2), checking the line taken TO REACH station i vs station i-1
    for (size_t i = 2; i < this->_stations.size(); ++i) {
        // Ignore changes involving "Walk" or the initial "Start" dummy line?
        // Current logic: Count any change in line ID as a transfer.
        const std::string& prevLineId = this->_stations[i - 1].line.id;
        const std::string& currentLineId = this->_stations[i].line.id;

        // Don't count starting/ending a walk as a transfer if desired? More complex logic needed.
        // Simple version: If IDs are different, it's a transfer.
        if (currentLineId != prevLineId) {
            transfers++;
        }
    }
    return transfers;
}

// Get a copy of the visited stations vector
const std::vector<Route::VisitedStation> Route::getVisitedStations() const {
    return this->_stations;
}


// Check if the route is valid according to the graph
bool Route::isValid(int startId, int destinationId, const Graph& graph) const {
    if (_stations.empty()) {
        // std::cerr << "Validation failed: Route is empty." << std::endl;
        return false;
    }
    if (_stations.size() == 1 && startId == destinationId) {
        // Special case: Route from a station to itself is valid if it only contains that station
        try {
            return graph.getStationIdByName(_stations.front().station.name) == startId;
        }
        catch (const std::runtime_error&) { return false; }
    }
    if (_stations.size() < 2 && startId != destinationId) {
        // Need at least two stations (start and end) if they are different
        // std::cerr << "Validation failed: Route too short for different start/end." << std::endl;
        return false;
    }

    try {
        // Check start station code matches startId
        if (_stations.front().station != graph.getStationById(startId)) {
            // std::cerr << "Validation failed: Start station mismatch (" << _stations.front().station.name << " vs ID " << startId << ")" << std::endl;
            return false;
        }
        // Check end station code matches destinationId
        if (_stations.back().station != graph.getStationById(destinationId)) {
            // std::cerr << "Validation failed: End station mismatch (" << _stations.back().station.name << " vs ID " << destinationId << ")" << std::endl;
            return false;
        }
    }
    catch (const std::runtime_error& e) {
        // std::cerr << "Validation failed: Station name lookup error - " << e.what() << std::endl;
        return false; // Station name not found in graph map
    }

    // Check transitions between stations
    for (size_t i = 1; i < _stations.size(); ++i) {
        const auto& prev_station_obj = _stations[i - 1].station;
        const auto& current_station_obj = _stations[i].station;
        const auto& line_taken = _stations[i].line; // Line used to arrive at current_station_obj

        int prev_station_code;
        int current_station_code;
        try {
            prev_station_code = graph.getStationIdByName(prev_station_obj.name);
            current_station_code = graph.getStationIdByName(current_station_obj.name);
        }
        catch (const std::runtime_error& e) {
            // std::cerr << "Validation failed: Station name lookup error during transition check - " << e.what() << std::endl;
            return false; // Station name not found
        }

        // Check 1: Does the 'to' field of the line match the station we arrived at?
        auto station = graph.getStationById(line_taken.to);
        if (line_taken.to != current_station_code && 
            calculateHaversineDistanceRoute(current_station_obj.latitude, current_station_obj.longitude,
                station.latitude, station.longitude) > MAX_WALKING_DISTANCE) {
            // std::cerr << "Validation failed: Line " << line_taken.id << " has 'to'=" << line_taken.to
            //          << " but arrived at station " << current_station_obj.name << " (code " << current_station_code << ")" << std::endl;
            return false;
        }

        // Check 2: Does a line matching 'line_taken' actually originate from 'prev_station_code' in the graph?
        // Skip this check for the dummy "Start" line associated with the first station
        if (line_taken.id == "Start") {
            if (i == 1) continue; // Expected for the very first step
            else {
                // std::cerr << "Validation failed: Unexpected 'Start' line after first step." << std::endl;
                return false; // "Start" line shouldn't appear later
            }
        }
        // Allow "Walk" segments without checking graph edges explicitly
        if (line_taken.id == "Walk") {
            continue;
        }

        //bool line_found_at_source = false;
        //try {
        //    const auto& lines_from_prev = graph.getLinesFrom(prev_station_code);
        //    for (const auto& available_line : lines_from_prev) {
        //        // Compare line ID and destination code for a match
        //        if (available_line.id == line_taken.id && available_line.to == line_taken.to) {
        //            line_found_at_source = true;
        //            break;
        //        }
        //    }
        //}
        //catch (const std::out_of_range&) {
        //    // This means prev_station_code wasn't found by getLinesFrom, but it should exist based on earlier checks. Graph inconsistency?
        //    // std::cerr << "Validation failed: Could not get lines from previous station code: " << prev_station_code << std::endl;
        //    return false;
        //}


        //if (!line_found_at_source) {
        //    // std::cerr << "Validation failed: Line " << line_taken.id << " to " << line_taken.to
        //    //          << " not found originating from station " << prev_station_obj.name << " (code " << prev_station_code << ")" << std::endl;
        //    return false;
        //}
    } // End transition check loop

    // If all checks passed
    return true;
}


// Calculate fitness score (higher is better)
double Route::getFitness(int startId, int destinationId, const Graph& graph) const {
    // Crucial first step: check validity
    if (!isValid(startId, destinationId, graph)) {
        return 0.0; // Invalid routes have zero fitness, prevents them from being selected
    }

    // --- Calculate Score Components (Lower is better for score) ---
    double totalTime = getTotalTime();
    double totalCost = getTotalCost();
    int transfers = getTransferCount();

    // Define weights/penalties
    const double time_weight = 1.0;        // Importance of time
    const double cost_weight = 0.5;        // Importance of cost (e.g., less important than time)
    const double penaltyPerTransfer = 20.0; // Penalty added to score per transfer (minutes equivalent?)
    const double walkPenaltyFactor = 3.0;   // Penalize walking time more than bus/train time

    // Calculate base score
    double score = (time_weight * totalTime) + (cost_weight * totalCost) + (transfers * penaltyPerTransfer);

    // Add penalty for walking time
    double walkPenalty = 0.0;
    for (size_t i = 1; i < _stations.size(); ++i) {
        if (_stations[i].line.id == "Walk") {
            // Penalize based on walking time relative to other factors
            walkPenalty += _stations[i].line.travelTime * walkPenaltyFactor;
        }
    }
    score += walkPenalty;

    // --- Convert Score to Fitness (Higher is better) ---
    // Use reciprocal: 1 / score. Handle score near zero.
    if (score <= std::numeric_limits<double>::epsilon()) {
        // Score is effectively zero (e.g., maybe start=end, no travel). Return max possible fitness.
        return std::numeric_limits<double>::max();
        // Alternative: return 1.0 / epsilon;
    }

    return 1.0 / score;
}


// Generate a path segment using guided random walk (helper for mutation)
// Tries to find a path from segmentStartId to segmentEndId
bool Route::generatePathSegment(int segmentStartId, int segmentEndId, const Graph& graph, std::mt19937& gen, std::vector<VisitedStation>& segment) {
    segment.clear(); // Ensure segment starts empty
    int currentCode = segmentStartId;
    const int maxSteps = 75; // Max steps for generating just a segment
    int steps = 0;
    const double epsilon = 1e-6; // For floating point weights
    std::unordered_set<int> visitedCodesSegment; // Prevent loops within this segment generation attempt

    try {
        // Get destination station info for distance calculation
        if (!graph.hasStation(segmentEndId)) return false; // Cannot reach non-existent station
        const Graph::Station& destStation = graph.getStationById(segmentEndId);
        double destLat = destStation.latitude;
        double destLon = destStation.longitude;

        visitedCodesSegment.insert(currentCode); // Mark starting point as visited for this attempt

        while (currentCode != segmentEndId && steps < maxSteps) {
            if (!graph.hasStation(currentCode)) return false; // Should not happen if graph is consistent
            const Graph::Station& currentStation = graph.getStationById(currentCode);

            // Check for short walk to the segment's end destination
            double distanceToSegmentEnd = calculateHaversineDistanceRoute(currentStation.latitude, currentStation.longitude, destLat, destLon);
            if (distanceToSegmentEnd < MAX_WALKING_DISTANCE) {
                Graph::TransportationLine walkingEdge("Walk", segmentEndId, (distanceToSegmentEnd / 5.0) * 60.0, 0, Graph::TransportMethod::Walk);
                // Add the final destination station and the walking line to the segment
                segment.push_back(VisitedStation(destStation, walkingEdge));
                currentCode = segmentEndId; // Mark as finished
                break; // Segment complete
            }

            // Get available lines from the current station
            const auto& availableLines = graph.getLinesFrom(currentCode);
            if (availableLines.empty()) return false; // Dead end

            // --- Guided Selection Logic ---
            std::vector<const Graph::TransportationLine*> validLines;
            std::vector<double> weights;

            // Evaluate valid, non-looping next steps
            for (const auto& line : availableLines) {
                int nextCode = line.to;
                // Use graph.hasStation() and check visited set
                if (graph.hasStation(nextCode) && visitedCodesSegment.find(nextCode) == visitedCodesSegment.end()) {
                    const Graph::Station& nextStation = graph.getStationById(nextCode); // Safe due to hasStation check
                    double distToDest = calculateHaversineDistanceRoute(nextStation.latitude, nextStation.longitude, destLat, destLon);
                    weights.push_back(distToDest + epsilon); // Add distance as weight (lower is better)
                    validLines.push_back(&line);
                }
            }

            if (validLines.empty()) return false; // No valid non-looping steps found

            // Weighted random choice based on inverse distance
            std::vector<double> probabilities;
            double sumInverseWeights = 0.0;
            for (double w : weights) { sumInverseWeights += 1.0 / std::max(w, epsilon); } // Use max to prevent division by zero

            const Graph::TransportationLine* chosenLinePtr = nullptr;
            if (sumInverseWeights <= epsilon) { // Fallback to uniform random if weights are problematic
                std::uniform_int_distribution<> uniform_dis(0, static_cast<int>(validLines.size() - 1));
                chosenLinePtr = validLines[uniform_dis(gen)];
            }
            else {
                // Calculate probabilities proportional to 1/distance
                for (double w : weights) { probabilities.push_back((1.0 / std::max(w, epsilon)) / sumInverseWeights); }
                std::discrete_distribution<> dist(probabilities.begin(), probabilities.end());
                chosenLinePtr = validLines[dist(gen)]; // Pick index based on probabilities
            }
            // --- End Guided Selection ---

            // Add the chosen step to the segment
            const Graph::Station& nextStation = graph.getStationById(chosenLinePtr->to); // Safe due to earlier check
            segment.push_back(VisitedStation(nextStation, *chosenLinePtr)); // Add station reached + line taken
            currentCode = chosenLinePtr->to; // Move to the next station
            visitedCodesSegment.insert(currentCode); // Mark new station as visited for this segment generation
            steps++;
        } // End while loop

        // Return true only if the segment destination was actually reached
        return (currentCode == segmentEndId);

    }
    catch (const std::out_of_range& e) {
        std::cerr << "Error during segment generation (getStationById): " << e.what() << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "Generic error during segment generation: " << e.what() << std::endl;
        return false;
    }
}


// Mutate the route by regenerating a segment
void Route::mutate(double mutationRate, std::mt19937& gen, int startId, int destinationId, const Graph& graph) {
    // Check mutation probability
    std::uniform_real_distribution<> prob_dis(0.0, 1.0);
    if (prob_dis(gen) >= mutationRate) {
        return; // No mutation this time
    }

    // Need at least 3 stations for meaningful segment replacement (start, intermediate, end)
    if (this->_stations.size() <= 2) {
        return;
    }

    // Choose a random intermediate station index to restart *from*
    // restart_index is the index of the first station in the segment being replaced.
    std::uniform_int_distribution<> index_dis(1, static_cast<int>(_stations.size() - 1)); // Can pick index 1 up to the last station index
    int restart_index = index_dis(gen);

    // Get the code of the station *before* the segment starts
    int segment_start_code;
    try {
        // Use the station at index restart_index - 1
        segment_start_code = graph.getStationIdByName(_stations[restart_index - 1].station.name);
    }
    catch (const std::runtime_error& e) {
        std::cerr << "Mutation aborted: Can't find ID for station " << _stations[restart_index - 1].station.name << " (" << e.what() << ")" << std::endl;
        return; // Cannot find start code, abort mutation
    }

    // Generate the new segment using the helper function
    std::vector<VisitedStation> new_segment;
    bool success = generatePathSegment(segment_start_code, destinationId, graph, gen, new_segment);

    // If successful, replace the old segment
    if (success && !new_segment.empty()) {
        // Resize the original route to keep only the part *before* the restart index
        _stations.resize(restart_index);
        // Append the new segment (which starts from the station *after* segment_start_code)
        _stations.insert(_stations.end(), new_segment.begin(), new_segment.end());
    }
    // If segment generation failed, the route remains unchanged.
}