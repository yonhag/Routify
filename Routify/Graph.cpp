#include "Graph.h"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <locale>

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
