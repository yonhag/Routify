#pragma once
#include "Utilities.hpp"
#include <string>
#include <vector>
#include <unordered_map>

class Graph {
public:
    enum class TransportMethod { Bus, Train, LightTrain, Walk };

    // Represents a weighted edge to a destination station.
    struct TransportationLine {
        std::string id;                 // Bus number. Is a string for cases where letters indicate different routes.
        int to;                         // Destination station id.
        double travelTime;              // Travel time (in minutes).
        double price;                   // Cost (if available; otherwise 0).
        int weight;                     // Computed weight of the edge.
        TransportMethod type;           // Type of the connection.
        std::vector<int> arrivalTimes;  // Times of the day the bus arrives, in minutes since midnight. 0 is midnight, 90 is 1:30, etc.


        TransportationLine(const std::string& id, int to, double travelTime, double price, TransportMethod type)
            : id(id), to(to), travelTime(travelTime), price(price), weight(0), type(type) {
        }
        TransportationLine() : id(""), to(0), travelTime(0), price(0), weight(0), type(TransportMethod::Bus) {}

        bool operator==(const TransportationLine& other) const {
            return this->id == other.id;
        }
    };

    // Represents a node (station) in the graph.
    struct Station {
        std::string name;
        Utilities::Coordinates coordinates;
        std::vector<TransportationLine> lines;

        Station(const std::string& name, const Utilities::Coordinates& coords)
            : name(name), coordinates(coords) {
        }

        Station() : name(""), coordinates() {}


        bool operator==(const Station& other) const {
            return this->name == other.name && this->coordinates == other.coordinates;
        }
    };


    struct RouteInfo {
        std::string id;
        int route_type;
    };

    Graph();
    ~Graph();

    // Adds a station to the graph.
    void addStation(const int code, const std::string& name, const Utilities::Coordinates& coords);

    // Computes the weight for an edge.
    int calculateWeight(double travelTime, double price) const;

    // Returns the edges from a given station id.
    const std::vector<TransportationLine>& getLinesFrom(int nodeId) const;

    const Station& getStationById(int id) const;

    int getStationIdByName(const std::string& name) const;

    bool hasStation(const int id) const;

    size_t getStationCount() const;

    std::vector<std::pair<int, Graph::Station>> getNearbyStations(const Utilities::Coordinates& userCoords) const;

private:
    void fetchAPIData();
    void fetchGTFSStops();                   // Parses stops.txt to extract station code, name, and coordinates.
    void fetchGTFSTransportationLines();     // Parses routes.txt, trips.txt, and stop_times.txt to add edges.

    Station& getStationRefById(int id);

	const double maxNearbyDistance = 0.6; // km

    std::unordered_map<int, Station> _map;
};
