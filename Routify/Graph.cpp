#include "Graph.h"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <locale>
#include <unordered_set>

static std::string trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(static_cast<unsigned char>(*start)))
        start++;
    if (start == s.end())
        return "";
    auto end = s.end() - 1;
    while (end != start && std::isspace(static_cast<unsigned char>(*end)))
        end--;
    return std::string(start, end + 1);
}

static std::vector<std::string> splitCSV(const std::string& line, char delimiter = ',') {
    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        if (!token.empty() && token.front() == '"') token.erase(0, 1);
        if (!token.empty() && token.back() == '"') token.pop_back();
        tokens.push_back(token);
    }
    return tokens;
}

static int convertTimeToMinutes(const std::string& timeStr) {
    int h, m, s;
    char colon;
    std::istringstream iss(timeStr);
    iss >> h >> colon >> m >> colon >> s;
    return h * 60 + m;
}

Graph::Graph() {
    fetchAPIData();
}

Graph::~Graph() = default;

void Graph::addStation(const int code, const std::string& name, const Utilities::Coordinates& coords) {
    if (!coords.isValid()) {
		std::cout << "Invalid coords for " << code << ": " << coords.latitude << " " << coords.longitude << std::endl;
    }

    this->_map.emplace(code, Station(code, name, coords));
}

const std::vector<Graph::TransportationLine>& Graph::getLinesFrom(const int nodeId) const {
    auto it = this->_map.find(nodeId);
    if (it != this->_map.end())
        return it->second.lines;
    static std::vector<TransportationLine> empty;
    return empty;
}

const Graph::Station& Graph::getStationByCode(const int id) const
{
    auto it = _map.find(id);
    if (it == _map.end()) {
        throw std::out_of_range("Station with the given ID not found: " + id);
    }
    return it->second;
}

bool Graph::hasStation(const int id) const
{
    return _map.contains(id);
}

size_t Graph::getStationCount() const
{
    return this->_map.size();
}

/*
* Returns the nearby stations, sorted by distance
*/
std::vector<std::pair<int, Graph::Station>> Graph::getNearbyStations(const Utilities::Coordinates& userCoords) const {
    // Find nearby stations
    std::vector<std::pair<int, Station>> nearbyStations;
    for (const auto& [id, station] : _map) {
        double distance = Utilities::calculateHaversineDistance(station.coordinates, userCoords);
        if (distance <= this->maxNearbyDistance) {
            nearbyStations.push_back({ id, station });
        }
    }

    // Sort by distance
    std::sort(nearbyStations.begin(), nearbyStations.end(),
        [&userCoords](const auto& a, const auto& b) {
            double distA = Utilities::calculateHaversineDistance(a.second.coordinates, userCoords);
            double distB = Utilities::calculateHaversineDistance(b.second.coordinates, userCoords);
            return distA < distB;
        });

    return nearbyStations;
}

/*
* Simplified function to find stations between two points along a specific line.
* Assumes a reasonably direct path exists in the graph for the given lineId
* between the start and end points. Includes basic safeguards.
*/
std::vector<Graph::Station> Graph::getStationsAlongLineSegment(
    const std::string& lineId,
    int segmentStartStationId,
    int segmentEndStationId) const
{
    std::vector<Graph::Station> pathStations;
    const int MAX_STEPS = 150; // Safety limit against potential cycles or errors
    int stepCount = 0;

    // 1. Get Start Station & Add to Path
    try {
        const Station& startStation = getStationByCode(segmentStartStationId);
        pathStations.push_back(startStation);

        // If start is already the end, we're done
        if (segmentStartStationId == segmentEndStationId) {
            return pathStations;
        }
    }
    catch (const std::out_of_range& oor) {
        std::cerr << "Error [getStationsAlongLineSegment Simple]: Start station ID "
            << segmentStartStationId << " not found. " << oor.what() << std::endl;
        return {}; // Return empty vector if start doesn't exist
    }
    catch (const std::exception& e) {
        std::cerr << "Error [getStationsAlongLineSegment Simple]: Exception getting start station " << segmentStartStationId << ": " << e.what() << std::endl;
        return {};
    }


    // 2. Trace the Path Step-by-Step
    int currentStationId = segmentStartStationId;
    int previousStationId = -1; // Track just the immediate previous to avoid simple A->B->A loops

    while (currentStationId != segmentEndStationId && stepCount < MAX_STEPS) {
        stepCount++;
        const Graph::TransportationLine* chosenLine = nullptr;

        // Find *one* appropriate outgoing edge for the lineId
        try {
            // Ensure current station is valid before proceeding
            if (!hasStation(currentStationId)) {
                std::cerr << "Error [getStationsAlongLineSegment Simple]: Current station " << currentStationId << " became invalid during trace." << std::endl;
                break;
            }
            const std::vector<TransportationLine>& linesFromCurrent = getLinesFrom(currentStationId);

            // Search strategy:
            // 1. Look for the line going directly to the target end station.
            // 2. If not found, look for *any* line with the matching ID that doesn't immediately go back.
            const Graph::TransportationLine* fallbackLine = nullptr;

            for (const auto& line : linesFromCurrent) {
                if (line.id == lineId) {
                    if (line.to == segmentEndStationId) {
                        chosenLine = &line; // Found direct path to end
                        break;
                    }
                    // Check if it's a valid fallback (doesn't go back to immediate previous)
                    if (line.to != previousStationId && fallbackLine == nullptr) {
                        fallbackLine = &line;
                        // Continue checking in case the direct target edge is later
                    }
                }
            }

            // If direct target wasn't found, use the fallback if one exists
            if (chosenLine == nullptr && fallbackLine != nullptr) {
                chosenLine = fallbackLine;
            }

        }
        catch (const std::out_of_range& oor) {
            std::cerr << "Error [getStationsAlongLineSegment Simple]: Cannot get lines from station " << currentStationId << ". " << oor.what() << std::endl;
            break; // Stop if we can't get lines
        }
        catch (const std::exception& e) {
            std::cerr << "Error [getStationsAlongLineSegment Simple]: Exception getting lines from " << currentStationId << ": " << e.what() << std::endl;
            break;
        }


        // 3. Process the chosen edge (or lack thereof)
        if (chosenLine != nullptr) {
            int nextStationId = chosenLine->to;
            try {
                // Get the station object for the next stop
                const Station& nextStation = getStationByCode(nextStationId);
                // Add it to our path result
                pathStations.push_back(nextStation);

                // Update state for the next iteration
                previousStationId = currentStationId;
                currentStationId = nextStationId;

            }
            catch (const std::out_of_range& oor) {
                std::cerr << "Error [getStationsAlongLineSegment Simple]: Next station ID " << nextStationId
                    << " (on line " << lineId << ") not found. " << oor.what() << std::endl;
                break; // Stop if the graph data points to a non-existent station
            }
            catch (const std::exception& e) {
                std::cerr << "Error [getStationsAlongLineSegment Simple]: Exception getting next station " << nextStationId << ": " << e.what() << std::endl;
                break;
            }
        }
        else {
            // Dead end: No valid outgoing edge found for this line from the current station
            std::cerr << "Warning [getStationsAlongLineSegment Simple]: No onward path found for line " << lineId
                << " from station " << currentStationId << " towards " << segmentEndStationId << ". Stopping trace." << std::endl;
            break; // Stop tracing
        }
    } // End while loop

    // 4. Log warnings if trace didn't complete as expected
    if (stepCount >= MAX_STEPS) {
        std::cerr << "Warning [getStationsAlongLineSegment Simple]: Exceeded MAX_STEPS (" << MAX_STEPS << ") for "
            << segmentStartStationId << " -> " << segmentEndStationId << " on line " << lineId << "." << std::endl;
    }
    // Check if loop finished but didn't reach the end (implies dead end was hit)
    if (currentStationId != segmentEndStationId && stepCount < MAX_STEPS) {
        std::cerr << "Warning [getStationsAlongLineSegment Simple]: Trace did not reach target " << segmentEndStationId
            << " from " << segmentStartStationId << " on line " << lineId << ". Returning partial path." << std::endl;
    }

    // 5. Return the collected path (complete or partial)
    return pathStations;
}

Graph::Station& Graph::getStationRefById(const int id) {
    auto it = _map.find(id);
    if (it == _map.end()) {
        throw std::out_of_range("Station with the given ID not found");
    }
    return it->second;
}

void Graph::fetchAPIData() {
    fetchGTFSStops();
    fetchGTFSTransportationLines();
}

void Graph::fetchGTFSStops() {
    std::ifstream file(GTFSStopsFile);
    if (!file.is_open()) {
        std::cerr << "Failed to open GTFS stops.txt file.\n";
        return;
    }

    std::string header;
    std::getline(file, header); // Skipping the format header

    int i = 0;
    std::string line;
    while (std::getline(file, line)) {
        auto tokens = splitCSV(line);
        if (tokens.size() < 6) continue;
        int stop_code = std::stoi(tokens[1]);
        std::string stop_name = tokens[2];
        double stop_lat = std::stod(tokens[4]);
        double stop_lon = std::stod(tokens[5]);
        addStation(stop_code, stop_name, Utilities::Coordinates(stop_lat, stop_lon));
        if (i % 10000 == 0)
            std::cout << "Added " << i << " stations out of 34.5K" << std::endl;
        i++;
    }
    std::cout << "Done!" << std::endl;
    file.close();
}

void Graph::fetchGTFSTransportationLines() {
    std::ifstream stopTimesFile(GTFSLinesFile);
    if (!stopTimesFile.is_open()) {
        std::cerr << "Failed to open " << GTFSLinesFile << " file." << std::endl;
        return;
    }

    std::string header;
    std::getline(stopTimesFile, header);
    std::string line;
    int i = 0;
    int lastId = -1;                        // Initialize to an invalid value
    TransportationLine* lastLine = nullptr; // Pointer to the last line in the vector

    while (std::getline(stopTimesFile, line)) {
        auto tokens = splitCSV(line);
        if (tokens.size() < 4) continue;    // Ensure we have enough tokens

        std::string line_code = tokens[0];
        int id = std::stoi(tokens[1]);
        int time = convertTimeToMinutes(tokens[2]);
        int stationCode = std::stoi(tokens[3]);

        // If this row continues the previous transportation line, update its 'to' field.
        if (id == lastId && lastLine != nullptr) {
            lastLine->to = stationCode;
        }

        auto& station = getStationRefById(stationCode);
        // Use a temporary TransportationLine with the same id to find an existing one.
        TransportationLine tempLine;
        tempLine.id = line_code;

        auto it = std::find(station.lines.begin(), station.lines.end(), tempLine);
        if (it != station.lines.end()) {
            // Found an existing line, add the arrival time.
            it->arrivalTimes.push_back(time);
            lastLine = std::to_address(it);  // Update lastLine to point to the actual element in the vector.
        }
        else {
            // Create a new transportation line.
            TransportationLine newLine;
            newLine.id = line_code;
            newLine.arrivalTimes.push_back(time);
            station.lines.push_back(newLine);
            lastLine = &station.lines.back();  // lastLine references the last element inserted
        }

        lastId = id;  // Update lastId to the current id.

        if (i % 1000000 == 0)
            std::cout << "Processed " << i / 1000000 << "B lines, out of 20.6B" << std::endl;
        i++;
    }
    std::cout << "Done!" << std::endl;
    stopTimesFile.close();
}
