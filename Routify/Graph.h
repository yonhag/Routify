#pragma once
#include "Utilities.hpp"
#include <string>
#include <vector>
#include <unordered_map>

class Graph {
public:
    enum class TransportMethod { Bus, Train, LightTrain, Walk };

    // Represents an edge to a destination station.
    struct TransportationLine {
        std::string id;                 // Bus number. Is a string for cases where letters indicate different routes.
        int to;                         // Destination station id.
        double travelTime;              // Travel time (in minutes).
        double price;                   // Cost (if available; otherwise 0).
        TransportMethod type;           // Type of the connection.
        std::vector<int> arrivalTimes;  // Times of the day the bus arrives, in minutes since midnight. 0 is midnight, 90 is 1:30, etc.


        TransportationLine(const std::string& id, int to, double travelTime, double price, TransportMethod type)
            : id(id), to(to), travelTime(travelTime), price(price), type(type) {
        }
        TransportationLine() : id(""), to(0), travelTime(0), price(0), type(TransportMethod::Bus) {}

        bool operator==(const TransportationLine& other) const {
            return this->id == other.id;
        }
    };

    // Represents a node (station) in the graph.
    struct Station {
        std::string name;                       // Station name.
        Utilities::Coordinates coordinates;     // Station location.
        std::vector<TransportationLine> lines;  // Lines going through the station.

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

    // Returns the edges from a given station id.
    const std::vector<TransportationLine>& getLinesFrom(const int nodeId) const;

    // Returns a station from the graph.
    const Station& getStationById(const int id) const;

    // Finds a station id based on station data.
    int getStationIdByName(const std::string& name) const;

    // Checks if a certain station exists.
    bool hasStation(const int id) const;

    // Returns the size of the map.
    size_t getStationCount() const;

    // Finds stations within maxNearbyDistance from given coords.
    std::vector<std::pair<int, Graph::Station>> getNearbyStations(const Utilities::Coordinates& userCoords) const;

    /*
    * Finds stations between two stations that a certain line visits.
    * Used after the GA - that only outputs action stations (stations where an action has to be done, like start, end and line transfers) -
    * To show the route in a better way on the frontend.
    */
    std::vector<Graph::Station> getStationsAlongLineSegment( 
        const std::string& lineId,
        int segmentStartStationId,
        int segmentEndStationId) const;

private:
    // Data parsers
    void fetchAPIData();
    void fetchGTFSStops();                   // Parses stops.txt to extract station code, name, and coordinates.
    void fetchGTFSTransportationLines();     // Parses stop_files_filtered.txt to extract line stations and timings.

    // Grants a muteable reference to a station object from the map.
    Station& getStationRefById(const int id);

	const double maxNearbyDistance = 0.6; // km

    std::unordered_map<int, Station> _map;
};
