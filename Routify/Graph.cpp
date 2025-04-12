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

Graph::~Graph() {
}

void Graph::addStation(int code, const std::string& name, const Utilities::Coordinates& coords) {
    if (!coords.isValid()) {
		std::cout << "Invalid coords for " << code << ": " << coords.latitude << " " << coords.longitude << std::endl;
    }

    // Use the Station constructor that takes Coordinates
    this->_map.emplace(code, Station(name, coords));
}

int Graph::calculateWeight(double travelTime, double price) const {
    return static_cast<int>(travelTime + (price * 10));
}

const std::vector<Graph::TransportationLine>& Graph::getLinesFrom(int nodeId) const {
    auto it = this->_map.find(nodeId);
    if (it != this->_map.end())
        return it->second.lines;
    static std::vector<TransportationLine> empty;
    return empty;
}

const Graph::Station& Graph::getStationById(int id) const
{
    auto it = _map.find(id);
    if (it == _map.end()) {
        throw std::out_of_range("Station with the given ID not found: " + id);
    }
    return it->second;
}

int Graph::getStationIdByName(const std::string& name) const
{
    for (const auto& [id, station] : _map) {
        if (station.name == name) {
            return id;
        }
    }
    throw std::runtime_error("Station not found " + name);
}

bool Graph::hasStation(const int id) const
{
    return _map.count(id) > 0;
}

size_t Graph::getStationCount() const
{
    return this->_map.size();
}

std::vector<std::pair<int, Graph::Station>> Graph::getNearbyStations(const Utilities::Coordinates& userCoords) const {
    std::vector<std::pair<int, Station>> nearbyStations;
    for (const auto& [id, station] : _map) {
        // Use the updated Haversine function and the coordinates struct from Station
        double distance = Utilities::calculateHaversineDistance(station.coordinates, userCoords);
        if (distance <= this->maxNearbyDistance) {
            nearbyStations.push_back({ id, station });
        }
    }
    // Optional: Sort nearbyStations by distance
    std::sort(nearbyStations.begin(), nearbyStations.end(),
        [&userCoords](const auto& a, const auto& b) {
            double distA = Utilities::calculateHaversineDistance(a.second.coordinates, userCoords);
            double distB = Utilities::calculateHaversineDistance(b.second.coordinates, userCoords);
            return distA < distB;
        });
    return nearbyStations;
}

std::vector<Graph::Station> Graph::getStationsAlongLineSegment( // Changed return type
    const std::string& lineId,
    int segmentStartStationId,
    int segmentEndStationId) const
{
    std::vector<Graph::Station> pathStations; // Store Station objects
    std::unordered_set<int> visitedOnPathIds; // Still track IDs to detect cycles

    // 1. Basic Validation & Get Start Station Object
    const Station* startStationPtr = nullptr;
    const Station* endStationPtr = nullptr; // Not strictly needed for trace, but good practice
    try {
        startStationPtr = &getStationById(segmentStartStationId);
        endStationPtr = &getStationById(segmentEndStationId); // Verify end exists too
    }
    catch (const std::out_of_range& oor) {
        std::cerr << "Warning [getStationsAlongLineSegment]: Start (" << segmentStartStationId
            << ") or End (" << segmentEndStationId << ") station ID not found. " << oor.what() << std::endl;
        return pathStations; // Return empty vector
    }

    if (segmentStartStationId == segmentEndStationId) {
        pathStations.push_back(*startStationPtr); // Add the single station object
        return pathStations;
    }

    int currentStationId = segmentStartStationId;
    pathStations.push_back(*startStationPtr); // Add start station object
    visitedOnPathIds.insert(currentStationId);

    const int MAX_STEPS = 150;
    int stepCount = 0;

    // 2. Trace the path
    while (currentStationId != segmentEndStationId && stepCount < MAX_STEPS) {
        stepCount++;
        bool foundNextHop = false;
        const TransportationLine* nextHopLine = nullptr; // To store the chosen line

        try {
            const std::vector<TransportationLine>& linesFromCurrent = getLinesFrom(currentStationId);

            // Find the next hop (same logic as before, finding the line)
            for (const auto& line : linesFromCurrent) {
                if (line.id == lineId) {
                    nextHopLine = &line;
                    break;
                }
            }

            if (nextHopLine != nullptr) {
                int nextStationId = nextHopLine->to;

                // Cycle detection using IDs
                if (visitedOnPathIds.count(nextStationId)) {
                    std::cerr << "Warning [getStationsAlongLineSegment]: Cycle detected..." << std::endl;
                    pathStations.clear(); return pathStations;
                }

                // Get the next Station object
                const Station& nextStation = getStationById(nextStationId); // Can throw std::out_of_range

                // Add the *next* station object to the path
                pathStations.push_back(nextStation);
                visitedOnPathIds.insert(nextStationId);
                currentStationId = nextStationId; // Update the current ID for the next loop iteration
                foundNextHop = true;

            }
            else {
                std::cerr << "Warning [getStationsAlongLineSegment]: No outgoing edge found..." << std::endl;
                pathStations.clear(); return pathStations;
            }

        }
        catch (const std::out_of_range& oor_err) {
            std::cerr << "Error [getStationsAlongLineSegment]: Station lookup failed for ID "
                << (nextHopLine ? nextHopLine->to : currentStationId) // ID that failed
                << ". " << oor_err.what() << std::endl;
            pathStations.clear(); return pathStations;
        }
        catch (const std::exception& e) {
            std::cerr << "Error [getStationsAlongLineSegment]: Exception during trace: " << e.what() << std::endl;
            pathStations.clear(); return pathStations;
        }
        // No need for explicit !foundNextHop check here as the inner logic handles it
    } // End while loop

    // 3. Final Checks (same as before)
    if (stepCount >= MAX_STEPS) { /* ... warning, clear, return ... */ }
    if (currentStationId != segmentEndStationId) { /* ... warning, clear, return ... */ }

    // 4. Success
    return pathStations; // Return vector of Station objects
}

Graph::Station& Graph::getStationRefById(int id) {
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
    std::ifstream file("GTFS/stops.txt");
    if (!file.is_open()) {
        std::cerr << "Failed to open GTFS stops.txt file.\n";
        return;
    }

    std::string header;
    std::getline(file, header); // skip header

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
    std::ifstream stopTimesFile("GTFS/stop_times_filtered.txt");
    if (!stopTimesFile.is_open()) {
        std::cerr << "Failed to open stop_times_filtered.txt file." << std::endl;
        return;
    }

    std::string header;
    std::getline(stopTimesFile, header);
    std::string line;
    int i = 0;
    int lastId = -1;              // Initialize to an invalid value
    TransportationLine* lastLine = nullptr; // Pointer to the last line in the vector

    while (std::getline(stopTimesFile, line)) {
        auto tokens = splitCSV(line);
        if (tokens.size() < 4) continue;  // Ensure we have enough tokens

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
            lastLine = &(*it);  // Update lastLine to point to the actual element in the vector.
        }
        else {
            // Create a new transportation line.
            TransportationLine newLine;
            newLine.id = line_code;
            newLine.arrivalTimes.push_back(time);
            newLine.price = 0;
            station.lines.push_back(newLine);
            lastLine = &station.lines.back();  // lastLine references the last element inserted
        }

        lastId = id;  // Update lastId to the current id.

        if (i % 1000000 == 0)
            std::cout << "Processed " << i / 1000000 << "B lines, out of 20.6B" << std::endl;
        i++;          // Increment the line counter
    }
    std::cout << "Done!" << std::endl;
    stopTimesFile.close();
}
