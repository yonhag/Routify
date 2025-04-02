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
    if (retrieveExistingData())
        return;
    fetchAPIData();
}

Graph::~Graph() {
}

void Graph::addStation(const int code, const std::string& name, double latitude, double longitude) {
    this->_map.emplace(code, Station(name, latitude, longitude));
}

void Graph::addLine(int from, int to, double travelTime, double price, TransportMethod type) {
    auto it = this->_map.find(from);
    if (it == this->_map.end())
        throw std::runtime_error("Source station not found");
    TransportationLine line("", to, travelTime, price, type);
    line.weight = calculateWeight(travelTime, price);
    it->second.lines.push_back(line);
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

const Graph::Station* Graph::getStationById(int id) const {
    auto it = this->_map.find(id);
    return (it != this->_map.end()) ? &it->second : nullptr;
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

    std::string line;
    while (std::getline(file, line)) {
        auto tokens = splitCSV(line);
        if (tokens.size() < 6) continue;
        int stop_code = std::stoi(tokens[1]);
        std::string stop_name = tokens[2];
        double stop_lat = std::stod(tokens[4]);
        double stop_lon = std::stod(tokens[5]);
        addStation(stop_code, stop_name, stop_lat, stop_lon);
        std::cout << "Added station: " << stop_code << " with name " << stop_name << std::endl;
    }
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

        auto stationIt = this->_map.find(stationCode);
        if (stationIt == this->_map.end()) {
            std::cout << "STATION #" << stationCode << " NOT FOUND" << std::endl;
            continue;
        }

        Station* station = &stationIt->second;
        // Use a temporary TransportationLine with the same id to find an existing one.
        TransportationLine tempLine;
        tempLine.id = line_code;

        auto it = std::find(station->lines.begin(), station->lines.end(), tempLine);
        if (it != station->lines.end()) {
            // Found an existing line, add the arrival time.
            it->arrivalTimes.push_back(time);
            lastLine = &(*it);  // Update lastLine to point to the actual element in the vector.
            // std::cout << "Added time " << time << " for line " << line_code << std::endl;
        }
        else {
            // Create a new transportation line.
            TransportationLine newLine;
            newLine.id = line_code;
            newLine.arrivalTimes.push_back(time);
            newLine.price = 0;
            station->lines.push_back(newLine);
            lastLine = &station->lines.back();  // Now lastLine references the new element.
            // std::cout << "Added line " << line_code << " to station " << stationCode << std::endl;
        }

        lastId = id;  // Update lastId to the current id.

        if (i % 1000000 == 0)
            std::cout << "Processed " << i % 1000000 << "M lines, out of 20.6M" << std::endl;
        i++;          // Increment the line counter
    }
    stopTimesFile.close();
}


bool Graph::retrieveExistingData() {
    return false;
}
