#include "Route.h"
#include "Graph.h"
#include "Utilities.hpp"
#include <iostream>
#include <ctime>
#include <algorithm>
#include <stdexcept>
#include <limits>
#include <unordered_set>

namespace {
    bool isPublicTransport(Graph::TransportMethod method) {
        return method == Graph::TransportMethod::Bus ||
            method == Graph::TransportMethod::Train ||
            method == Graph::TransportMethod::LightTrain;
    }

    double calculateEstimatedSegmentTime(
        const Graph::Station* prevStationPtr,
        const Route::VisitedStation& currentVs,
        double walkSpeedKph,
        double publicTransportSpeedKph)
    {
        if (!prevStationPtr) {
            return 0.0;
        }

        const Utilities::Coordinates& startCoords = prevStationPtr->coordinates;
        const Utilities::Coordinates& endCoords = currentVs.station.coordinates;
        const auto& lineTaken = currentVs.line;

        double distance = Utilities::calculateHaversineDistance(startCoords, endCoords);
        double segmentTime = 0.0;

        // Merged conditions
        if (lineTaken.id == "Walk" && walkSpeedKph > 0) {
            segmentTime = (distance / walkSpeedKph) * 60.0;
        }
        else if (isPublicTransport(lineTaken.type) && distance > 0 && publicTransportSpeedKph > 0) {
            segmentTime = (distance / publicTransportSpeedKph) * 60.0;
        }

        if (std::isnan(segmentTime) || segmentTime < 0) {
            return 0.0;
        }

        return segmentTime;
    }
}

// Add a visited station step
void Route::addVisitedStation(const VisitedStation& vs) {
    this->_stations.push_back(vs);
}

// Calculate total travel time from line segments
double Route::getTotalTime(const Graph& graph, int routeStartId) const {
    if (_stations.empty()) {
        return 0.0;
    }

    double totalEstimatedTime = 0.0;
    const Graph::Station* prevStationPtr = nullptr;

    try {
        prevStationPtr = &graph.getStationByCode(routeStartId);
    }
    catch (const std::exception& e) {
        std::cout << e.what() << std::endl;
        return 0.0; 
    }

    // Iterate through each step in the recorded path
    for (const auto& i : _stations) {
        totalEstimatedTime += calculateEstimatedSegmentTime(
            prevStationPtr,
            i,
            Utilities::WALK_SPEED_KPH,
            Utilities::ASSUMED_PUBLIC_TRANSPORT_SPEED_KPH
        );
        prevStationPtr = &i.station;
    }

    return totalEstimatedTime;
}

/*
* Calculates an estimated cost for the route.
* Uses aerial distance between the start and end station of each public transport segment.
* This is faster but less accurate than tracing the full path.
*/
double Route::getTotalCost(const Graph& graph) const {
    if (_stations.empty()) {
        return 0.0;
    }

    double totalPublicTransportAerialDistance = 0.0;
    bool usedPublicTransport = false;

    int firstStationId = -1;
    if (!_stations.empty()) {
        firstStationId = _stations[0].station.code;
        if (firstStationId == -1) {
            std::cerr << "Critical Error [getTotalCost]: First station in route has invalid code (-1)." << std::endl;
            return 0.0;
        }
    }
    else {
        std::cerr << "Critical Error [getTotalCost]: Route is empty, cannot determine first station." << std::endl;
        return 0.0;
    }


    // Iterate through the route segments defined by VisitedStation entries
    for (size_t i = 1; i < _stations.size(); ++i) {
        const auto& currentVs = _stations[i];
        const auto& lineTaken = currentVs.line;

        if (isPublicTransport(lineTaken.type)) {
            usedPublicTransport = true;

            // Determine the start and end station IDs for this segment
            int segmentStartId = currentVs.prevStationCode;
            int segmentEndId = lineTaken.to; // The station ID where this line segment ends

            // If it's the first segment, the 'previous' station is the overall start station
            if (segmentStartId == -1 && i == 1) {
                segmentStartId = firstStationId; // Use the ID we determined earlier
            }
            else if (segmentStartId == -1) {
                std::cerr << "Warning [getTotalCost]: Invalid prevStationCode (-1) for non-first segment index " << i << "." << std::endl;
                continue;
            }

            // Check if segment is valid (start and end are different and valid IDs)
            if (segmentStartId != segmentEndId && segmentStartId != -1 && segmentEndId != -1) {
                try {
                    // Get the Station objects for start and end of the segment
                    const Graph::Station& startSegStation = graph.getStationByCode(segmentStartId);
                    const Graph::Station& endSegStation = graph.getStationByCode(segmentEndId);

                    // Calculate direct aerial distance between the start and end stations of this segment
                    totalPublicTransportAerialDistance += Utilities::calculateHaversineDistance(
                        startSegStation.coordinates,
                        endSegStation.coordinates);

                }
                catch (const std::out_of_range& oor) {
                    std::cerr << "Warning [getTotalCost]: Failed station lookup for segment "
                        << segmentStartId << " -> " << segmentEndId << ". " << oor.what() << std::endl;
                    continue;
                }
                catch (const std::exception& e) {
                    std::cerr << "Error [getTotalCost]: Exception calculating distance for segment "
                        << segmentStartId << " -> " << segmentEndId << ": " << e.what() << std::endl;
                    continue;
                }
            }
        }
    }

    // --- Fare Calculation ---
    if (!usedPublicTransport) { return 0.0; }
    if (totalPublicTransportAerialDistance <= 15.0) return 6.0;
    else if (totalPublicTransportAerialDistance <= 40.0) return 12.5;
    else if (totalPublicTransportAerialDistance <= 120.0) return 17.0;
    else if (totalPublicTransportAerialDistance <= 225.0) return 28.5;
    else return 84.24;
}

// Calculate number of transfers (line changes)
int Route::getTransferCount() const {
    if (_stations.size() < 2) return 0;
    int vehicleBoardings = 0;
    for (size_t i = 1; i < _stations.size(); ++i) {
        const auto& currentVs = _stations[i];
        const auto& prevVs = _stations[i - 1];
        Graph::TransportMethod currentMethod = currentVs.line.type;
        const std::string& currentLineId = currentVs.line.id;
        Graph::TransportMethod prevMethod = prevVs.line.type;
        const std::string& prevLineId = prevVs.line.id;
        if (isPublicTransport(currentMethod) && 
            (!isPublicTransport(prevMethod) || (currentLineId != prevLineId))) {
            vehicleBoardings++;
        }
    }
    return std::max(0, vehicleBoardings - 1);
}

// Get a copy of the visited stations vector
const std::vector<Route::VisitedStation>& Route::getVisitedStations() const {
    return this->_stations;
}

bool Route::isValid(const int startId, const int destinationId, const Graph& graph) const {
    if (_stations.empty()) return false;

    // Check Start Station
    try {
        // Ensure first VisitedStation's internal station matches the graph's station for startId
        if (_stations.front().station != graph.getStationByCode(startId)) {
            return false;
        }
        // Also check the stored previous code for the start station (should be -1 or similar)
        if (_stations.front().prevStationCode != -1) {
            return false;
        }
    }
    catch (const std::runtime_error&) { return false; }
    catch (const std::out_of_range&) { return false; }

    // Handle special case: start == end
    if (_stations.size() == 1) { return (startId == destinationId); }

    // Check End Station (using line.to of last step)
    if (_stations.back().line.to != destinationId) {
        return false;
    }
    // Also check that the last station object matches the graph's station for destinationId
    try {
        if (_stations.back().station != graph.getStationByCode(destinationId)) {
            return false;
        }
    }
    catch (...) { return false; }


    // --- Check Transitions ---
    for (size_t i = 1; i < _stations.size(); ++i) {
        const auto& vs = _stations[i];
        const auto& line_taken = vs.line;
        int current_station_code = line_taken.to;
        int prev_station_code = vs.prevStationCode;

        if (prev_station_code == -1 && i > 0) {
            return false;
        }

        try {
            if (graph.getStationByCode(current_station_code) != vs.station) {
                return false;
            }
        }
        catch (...) { return false; }

        if (line_taken.id == "Start") { continue; }
        if (line_taken.id == "Walk") { continue; }

        bool line_found_at_source = false;
        try {
            if (!graph.hasStation(prev_station_code)) {
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
        catch (const std::out_of_range&) { return false; }

        if (!line_found_at_source) {
            return false;
        }
    }

    return true;
}


// --- getFitness - Higher is better ---
double Route::getFitness(int startId, int destinationId, const Graph& graph,
    const Utilities::Coordinates& userCoords,
    const Utilities::Coordinates& destCoords) const
{
    // --- Initial Checks ---
    if (_stations.empty() || !isValid(startId, destinationId, graph)) {
        return 0.0;
    }

    // --- Calculate Initial & Final Walk Times ---
    double initialWalkTime = 0.0;
    double finalWalkTime = 0.0;
    double totalWalkTime = 0.0;

    try { initialWalkTime = calculateWalkTime(userCoords, _stations.front().station.coordinates); }
    catch (...) {  }
    try {
        const Graph::Station& lastSt = _stations.back().station;
        if (lastSt.code == destinationId) finalWalkTime = calculateWalkTime(lastSt.coordinates, destCoords);
        else finalWalkTime = calculateWalkTime(graph.getStationByCode(destinationId).coordinates, destCoords);
    }
    catch (...) {  }

    // --- Get Estimated Station-to-Station Time ---
    double totalStationToStationTime = getTotalTime(graph, startId);

    // --- Calculate Total Raw Walk Time ---
    totalWalkTime = initialWalkTime + finalWalkTime;
    for (size_t i = 0; i < _stations.size(); ++i) {
        if (_stations[i].line.id == "Walk") {
            try {
                int prevCode = _stations[i].prevStationCode;
                const auto& prevSt = (i == 0 || prevCode == -1) ? graph.getStationByCode(startId) : graph.getStationByCode(prevCode);
                totalWalkTime += calculateWalkTime(prevSt.coordinates, _stations[i].station.coordinates);
            }
            catch (...) {  }
        }
    }

    // --- Calculate Other Components ---
    double totalCost = getTotalCost(graph);
    int transfers = getTransferCount();

    // --- Define weights/penalties ---
    const double time_weight = 1.0;
    const double cost_weight = 0.1;
    const double transfer_penalty = 45.0;
    const double walk_penalty_factor = 2.0;

    // --- Calculate Score ---
    double baseTime = initialWalkTime + totalStationToStationTime + finalWalkTime;
    double score = (time_weight * baseTime) +
        (walk_penalty_factor - 1.0) * totalWalkTime +
        (cost_weight * totalCost) + (transfers * transfer_penalty);

    // --- Convert Score to Fitness ---
    if (score <= std::numeric_limits<double>::epsilon()) return std::numeric_limits<double>::max();
    return 1.0 / score;
}

// --- generatePathSegment ---
bool Route::generatePathSegment(const int segmentStartId, const int segmentEndId, const Graph& graph, std::mt19937& gen, std::vector<VisitedStation>& segment) {
    segment.clear();
    int currentCode = segmentStartId;
    const int maxSteps = 75;
    int steps = 0;
    const double epsilon = 1e-6;
    std::unordered_set<int> visitedCodesSegment;

    try {
        if (!graph.hasStation(segmentStartId) || !graph.hasStation(segmentEndId)) return false;
        const Graph::Station& destStation = graph.getStationByCode(segmentEndId);
        double destLat = destStation.coordinates.latitude; double destLon = destStation.coordinates.longitude;
        visitedCodesSegment.insert(currentCode);

        while (currentCode != segmentEndId && steps < maxSteps) {
            if (!graph.hasStation(currentCode)) return false;
            const Graph::Station& currentStation = graph.getStationByCode(currentCode);
			double curLat = currentStation.coordinates.latitude; double curLon = currentStation.coordinates.longitude;
            double distanceToSegmentEnd = Utilities::calculateHaversineDistance(
                Utilities::Coordinates(curLat, curLon),
                Utilities::Coordinates(destLat, destLon)
            );
            if (const double MAX_WALKING_DISTANCE_SEGMENT = 0.5;
                distanceToSegmentEnd < MAX_WALKING_DISTANCE_SEGMENT) {
                Graph::TransportationLine walkingEdge("Walk", segmentEndId, (distanceToSegmentEnd / 5.0) * 60.0, Graph::TransportMethod::Walk);
                segment.push_back(VisitedStation(destStation, walkingEdge, currentCode));
                currentCode = segmentEndId;
                break;
            }

            const auto& availableLines = graph.getLinesFrom(currentCode);
            if (availableLines.empty()) return false;

            std::vector<const Graph::TransportationLine*> validLines; std::vector<double> weights;
            for (const auto& line : availableLines) {
                int nextCode = line.to;
                if (graph.hasStation(nextCode) && !visitedCodesSegment.contains(nextCode)) {
                    const Graph::Station& nextStation = graph.getStationByCode(nextCode);
					double nextLat = nextStation.coordinates.latitude; double nextLon = nextStation.coordinates.longitude;
                    double distToDest = Utilities::calculateHaversineDistance(
                        Utilities::Coordinates(nextLat, nextLon),
                        Utilities::Coordinates(destLat, destLon)
                    );
                    weights.push_back(distToDest + epsilon); validLines.push_back(&line);
                }
            }
            if (validLines.empty()) return false;
            std::vector<double> probabilities; double sumInverseWeights = 0.0;
            for (double w : weights) { sumInverseWeights += 1.0 / std::max(w, epsilon); }
            const Graph::TransportationLine* chosenLinePtr = nullptr;
            if (sumInverseWeights <= epsilon) {
                std::uniform_int_distribution<> uniform_dis(0, static_cast<int>(validLines.size() - 1)); chosenLinePtr = validLines[uniform_dis(gen)];
            }
            else { // Weighted Choice
                for (double w : weights) { probabilities.push_back((1.0 / std::max(w, epsilon)) / sumInverseWeights); }
                std::discrete_distribution<> dist(probabilities.begin(), probabilities.end()); chosenLinePtr = validLines[dist(gen)];
            }

            // Add the chosen step to the segment
            const Graph::Station& nextStation = graph.getStationByCode(chosenLinePtr->to);
            segment.push_back(VisitedStation(nextStation, *chosenLinePtr, currentCode));
            currentCode = chosenLinePtr->to; // Move to the next station
            visitedCodesSegment.insert(currentCode);
            steps++;
        }
        return (currentCode == segmentEndId);

    }
    catch (...) { return false; }
}

// --- Mutate ---
void Route::mutate(const double mutationRate, std::mt19937& gen, const int startId, const int destinationId, const Graph& graph) {
    std::uniform_real_distribution<> prob_dis(0.0, 1.0);
    if (prob_dis(gen) >= mutationRate) {
        return; 
    }

    // --- Choose Mutation Type ---
    // e.g., 80% chance regenerate segment, 20% chance try walk replacement
    std::uniform_real_distribution<> type_dis(0.0, 1.0);
    double mutationTypeRoll = type_dis(gen);

    // --- Mutation Type 1: Regenerate Tail Segment ---
    if (mutationTypeRoll < 0.8 || _stations.size() <= 3) { // Also fallback if route too short for walk replacement
        if (this->_stations.size() <= 2) return; // Need 3+ for segment regen

        std::uniform_int_distribution<> index_dis(1, static_cast<int>(_stations.size() - 1));
        int restart_index = index_dis(gen);
        int segment_start_code;
        try { segment_start_code = _stations[restart_index - 1].station.code; }
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
            try { before_segment_code = before_segment_vs.station.code; }
            catch (...) { return; }
        }
        else {
            before_segment_code = _stations[idx1].prevStationCode;
        }

        int segment_end_code = segment_end_vs.line.to; // The 'to' code of the line that reached the segment end station

		auto before_station_coords = before_segment_vs.station.coordinates;
		auto end_station_coords = segment_end_vs.station.coordinates;

        // Calculate direct walking distance
        double walk_dist = Utilities::calculateHaversineDistance(
            Utilities::Coordinates(before_station_coords.latitude, before_station_coords.longitude),
            Utilities::Coordinates(end_station_coords.latitude, end_station_coords.longitude)
        );

        if (walk_dist < MAX_WALK_REPLACE_DISTANCE) {
            // Walking is feasible, create the walk step
            double walk_time = (walk_dist / Utilities::WALK_SPEED_KPH) * 60.0; // time in minutes

            Graph::TransportationLine walkLine("Walk", segment_end_code, walk_time, Graph::TransportMethod::Walk);
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
            }
        }
    }
}


// Crossover function (single-point based on common station)
Route Route::crossover(const Route& parent1, const Route& parent2, std::mt19937& gen) {
    const auto& visited1 = parent1.getVisitedStations();
    const auto& visited2 = parent2.getVisitedStations();

    // Basic checks for valid crossover
    if (visited1.size() <= 2 || visited2.size() <= 2) {
        // Not enough intermediate points, return one parent
        return parent1;
    }

    // Find common intermediate stations
    std::vector<std::pair<size_t, size_t>> commonIndices;
    for (size_t i = 1; i < visited1.size() - 1; ++i) { // Exclude start/end
        for (size_t j = 1; j < visited2.size() - 1; ++j) { // Exclude start/end
            // Use Station's operator==
            if (visited1[i].station == visited2[j].station) {
                commonIndices.push_back({ i, j });
            }
        }
    }

    if (!commonIndices.empty()) {
        // Choose a random common station pair
        std::uniform_int_distribution<> common_dis(0, static_cast<int>(commonIndices.size() - 1));
        const auto& [idx1, idx2] = commonIndices[common_dis(gen)];

        // Create child route by combining segments
        Route childRoute;
        // Segment from parent1 up to (and including) the common station
        for (size_t k = 0; k <= idx1; ++k) {
            childRoute.addVisitedStation(visited1[k]);
        }
        // Segment from parent2 *after* the common station to the end
        for (size_t k = idx2 + 1; k < visited2.size(); ++k) {
            childRoute.addVisitedStation(visited2[k]);
        }
        return childRoute;
    }
    else {
        std::uniform_int_distribution<> parent_choice(0, 1);
        return (parent_choice(gen) == 0) ? parent1 : parent2;
    }
}

double Route::calculateFullJourneyTime(
    const Graph& graph,
    int routeStartId,
    int routeEndId,
    const Utilities::Coordinates& userCoords,
    const Utilities::Coordinates& destCoords) const
{
    double initialWalkTime = 0.0;
    double finalWalkTime = 0.0;

    // Calculate Initial Walk
    try {
        const Graph::Station& startStation = graph.getStationByCode(routeStartId);
        initialWalkTime = calculateWalkTime(userCoords, startStation.coordinates);
    }
    catch (const std::exception& e) {
        std::cerr << "Warning: Failed to get start station " << routeStartId << " for initial walk time. " << e.what() << std::endl;
    }

    // Calculate Station-to-Station Time (using existing method)
    double stationToStationTime = getTotalTime(graph, routeStartId);

    // Calculate Final Walk
    try {
        const Graph::Station& endStation = graph.getStationByCode(routeEndId);
        finalWalkTime = calculateWalkTime(endStation.coordinates, destCoords);
    }
    catch (const std::exception& e) {
        std::cerr << "Warning: Failed to get end station " << routeEndId << " for final walk time. " << e.what() << std::endl;
    }

    // Basic checks for NaN or negative values from components
    if (std::isnan(initialWalkTime) || initialWalkTime < 0) initialWalkTime = 0;
    if (std::isnan(stationToStationTime) || stationToStationTime < 0) stationToStationTime = 0;
    if (std::isnan(finalWalkTime) || finalWalkTime < 0) finalWalkTime = 0;

    return initialWalkTime + stationToStationTime + finalWalkTime;
}

double Route::calculateWalkTime(const Utilities::Coordinates& c1, const Utilities::Coordinates& c2) {
    if (!c1.isValid() || !c2.isValid()) {
        std::cerr << "Warning: Invalid coordinates passed to calculateWalkTime." << std::endl;
        return 0.0; // Cannot calculate time for invalid coords
    }
    double dist = Utilities::calculateHaversineDistance(c1, c2);

    // Handle potential issues with distance or speed
    if (dist < 0 || std::isnan(dist)) {
        std::cerr << "Warning: Invalid distance (" << dist << ") in calculateWalkTime." << std::endl;
        return 0.0; // Distance cannot be negative
    }
    if (Utilities::WALK_SPEED_KPH <= 0 || std::isnan(Utilities::WALK_SPEED_KPH)) {
        std::cerr << "Error: Invalid WALK_SPEED_KPH (" << Utilities::WALK_SPEED_KPH << ")." << std::endl;
        // Return a large value to penalize? Or 0? Let's return 0 for now.
        return 0.0;
    }
    if (dist == 0) {
        return 0.0; // No time if no distance
    }

    double time = (dist / Utilities::WALK_SPEED_KPH) * 60.0; // minutes
    if (std::isnan(time) || time < 0) {
        std::cerr << "Warning: Calculated walk time is invalid (" << time << ")." << std::endl;
        return 0.0;
    }
    return time;
}
