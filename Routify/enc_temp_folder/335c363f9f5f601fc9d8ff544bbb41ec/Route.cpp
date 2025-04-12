#include "Route.h"
#include "Graph.h" // Required for Graph types and access
#include "Utilities.hpp"
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

// --- Route Class Implementation ---

namespace {
    bool isPublicTransport(Graph::TransportMethod method) {
        return method == Graph::TransportMethod::Bus ||
            method == Graph::TransportMethod::Train ||
            method == Graph::TransportMethod::LightTrain;
    }
}

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
    // Need at least two segments (start -> vehicle) to have any boardings.
    if (_stations.size() < 2) {
        return 0;
    }

    int vehicleBoardings = 0; // Count total times a vehicle is boarded

    // Iterate through the segments (from the second station onwards)
    for (size_t i = 1; i < _stations.size(); ++i) {
        const auto& currentVs = _stations[i];
        const auto& prevVs = _stations[i - 1]; // Station arrived at before current

        Graph::TransportMethod currentMethod = currentVs.line.type;
        const std::string& currentLineId = currentVs.line.id;

        // Get previous method/ID. Handle the "Start" case (index i=1).
        // The line associated with prevVs (at index i-1) is the one taken *to reach it*.
        Graph::TransportMethod prevMethod = prevVs.line.type;
        const std::string& prevLineId = prevVs.line.id;

        // Check if the CURRENT segment involves boarding a public transport vehicle
        if (isPublicTransport(currentMethod)) {
            // Now, determine if this boarding is a "new" boarding event
            // compared to the previous segment.

            // It's a new boarding if:
            // 1. The previous segment was NOT public transport (i.e., Walk or Start)
            // OR
            // 2. The previous segment WAS public transport, BUT the line ID changed.
            if (!isPublicTransport(prevMethod) ||
                (isPublicTransport(prevMethod) && currentLineId != prevLineId))
            {
                vehicleBoardings++;
            }
            // Else (previous was public transport AND same line ID), it's not a new boarding.
        }
    }

    // The number of transfers is the number of boardings MINUS ONE
    // (because the first boarding doesn't count as a transfer).
    // Ensure the result is not negative if there were 0 or 1 boardings.
    return std::max(0, vehicleBoardings - 1);
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


// --- getFitness - Higher is better ---
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

            double distanceToSegmentEnd = Utilities::calculateHaversineDistance(currentStation.latitude, currentStation.longitude, destLat, destLon);
            const double MAX_WALKING_DISTANCE_SEGMENT = 0.5;
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
                    double distToDest = Utilities::calculateHaversineDistance(nextStation.latitude, nextStation.longitude, destLat, destLon);
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





// --- Mutate (MODIFIED) ---
void Route::mutate(double mutationRate, std::mt19937& gen, int startId, int destinationId, const Graph& graph) {
    // Check overall mutation probability
    std::uniform_real_distribution<> prob_dis(0.0, 1.0);
    if (prob_dis(gen) >= mutationRate) {
        return; // No mutation this time
    }

    // --- Choose Mutation Type ---
    // e.g., 80% chance regenerate segment, 20% chance try walk replacement
    std::uniform_real_distribution<> type_dis(0.0, 1.0);
    double mutationTypeRoll = type_dis(gen);

    // --- Mutation Type 1: Regenerate Tail Segment (Original Logic) ---
    if (mutationTypeRoll < 0.8 || _stations.size() <= 3) { // Also fallback if route too short for walk replacement
        if (this->_stations.size() <= 2) return; // Need 3+ for segment regen

        std::uniform_int_distribution<> index_dis(1, static_cast<int>(_stations.size() - 1));
        int restart_index = index_dis(gen);
        int segment_start_code;
        try { segment_start_code = graph.getStationIdByName(_stations[restart_index - 1].station.name); }
        catch (...) { return; } // Abort if prev station lookup fails

        std::vector<VisitedStation> new_segment;
        bool success = generatePathSegment(segment_start_code, destinationId, graph, gen, new_segment);

        if (success && !new_segment.empty()) {
            _stations.resize(restart_index);
            _stations.insert(_stations.end(), new_segment.begin(), new_segment.end());
        }
    }
    // --- Mutation Type 2: Try Walking Replacement ---
    else if (_stations.size() > 3) { // Need at least 4 stations for a 2-leg segment (start, s1, s2, end...)
        // Define max walk distance for replacement and max segment length to check
        const double MAX_WALK_REPLACE_DISTANCE = 1.5; // km - Allow slightly longer walks than end-of-segment checks
        const size_t MAX_SEGMENT_LEGS = 2;            // Check replacing 1 or 2 transit legs

        // Randomly choose start index of segment (must be at least 1, leaving start node)
        // Ensure end index doesn't exceed bounds and allows for MAX_SEGMENT_LEGS
        size_t max_start_idx = _stations.size() - 1 - MAX_SEGMENT_LEGS;
        if (max_start_idx <= 0) return; // Cannot select a valid segment start
        std::uniform_int_distribution<> seg_start_dis(1, static_cast<int>(max_start_idx));
        size_t idx1 = seg_start_dis(gen); // Index of the station *before* the segment to potentially replace

        // Randomly choose segment length (1 or 2 legs)
        std::uniform_int_distribution<> seg_len_dis(1, static_cast<int>(MAX_SEGMENT_LEGS));
        size_t legs_to_replace = seg_len_dis(gen);
        size_t idx2 = idx1 + legs_to_replace; // Index of the station *at the end* of the segment

        // Get relevant stations and codes
        const VisitedStation& before_segment_vs = _stations[idx1];
        const VisitedStation& segment_end_vs = _stations[idx2];
        int before_segment_code = before_segment_vs.prevStationCode; // Code before station idx1
        if (idx1 == 0) { // Special case if idx1 is the first station (shouldn't happen with distribution starting at 1, but safety)
            try { before_segment_code = graph.getStationIdByName(before_segment_vs.station.name); }
            catch (...) { return; }
        }
        else {
            before_segment_code = _stations[idx1].prevStationCode; // Use the reliable code
        }


        // Need code for the end station of the segment to create the walk line
        int segment_end_code = segment_end_vs.line.to; // The 'to' code of the line that reached the segment end station

        // Calculate direct walking distance
        double walk_dist = Utilities::calculateHaversineDistance(
            before_segment_vs.station.latitude, before_segment_vs.station.longitude,
            segment_end_vs.station.latitude, segment_end_vs.station.longitude
        );

        if (walk_dist < MAX_WALK_REPLACE_DISTANCE) {
            // Walking is feasible, create the walk step
            const double WALK_SPEED_KPH = 5.0;
            double walk_time = (walk_dist / WALK_SPEED_KPH) * 60.0; // time in minutes

            Graph::TransportationLine walkLine("Walk", segment_end_code, walk_time, 0, Graph::TransportMethod::Walk);
            VisitedStation walkStep(segment_end_vs.station, walkLine, before_segment_code); // Walk ends at station idx2, came from station idx1

            // --- Replace the segment ---
            // Erase the stations within the segment being replaced (from idx1+1 up to idx2)
            // Example: Route A-B-C-D. idx1=A, idx2=C. Erase B and C (indices 1, 2).
            // Indices to erase: idx1+1 to idx2+1 (exclusive end iterator)
            auto erase_start = _stations.begin() + idx1 + 1;
            // Handle potential off-by-one if idx2 is last element?
            auto erase_end = _stations.begin() + idx2 + 1;
            // Adjust erase_end if it goes past the actual end iterator
            if (erase_end > _stations.end()) erase_end = _stations.end();


            // Check if erase range is valid before erasing
            if (erase_start < erase_end) {
                _stations.erase(erase_start, erase_end);
                // Insert the single walk step *after* idx1
                _stations.insert(_stations.begin() + idx1 + 1, walkStep);
                // Optional: std::cout << "DEBUG: Replaced segment with walk!" << std::endl;
            }
            else {
                // Optional: std::cerr << "DEBUG: Walk replacement erase range invalid." << std::endl;
            }


        }
        // else: Walking distance too long, do nothing for this mutation type
    }
    // else: No other mutation types defined for now
}


