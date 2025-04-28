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
        TransportMethod type;           // Type of the connection.
        std::vector<int> arrivalTimes;  // Times of the day the bus arrives, in minutes since midnight. 0 is midnight, 90 is 1:30, etc.


        TransportationLine(const std::string& id, int to, double travelTime, TransportMethod type)
            : id(id), to(to), travelTime(travelTime), type(type) {
        }
        TransportationLine() : id(""), to(0), travelTime(0), type(TransportMethod::Bus) {}

        bool operator==(const TransportationLine& other) const {
            return this->id == other.id;
        }
    };

    // Represents a node (station) in the graph.
    struct Station {
		int code;                               // Station code. Also the map key.
        std::string name;                       // Station name.
        Utilities::Coordinates coordinates;     // Station location.
        std::vector<TransportationLine> lines;  // Lines going through the station.

        Station(const int code, const std::string& name, const Utilities::Coordinates& coords)
            : code(code), name(name), coordinates(coords) {
        }

        Station() : code(-1), name(""), coordinates() {}

        bool operator==(const Station& other) const {
            return this->code == other.code;
        }
    };

    Graph();
    ~Graph();

    // Adds a station to the graph.
    void addStation(const int code, const std::string& name, const Utilities::Coordinates& coords);

    // Returns the edges from a given station id.
    const std::vector<TransportationLine>& getLinesFrom(const int stationCode) const;

    // Returns a station from the graph.
    const Station& getStationByCode(const int code) const;

    // Checks if a certain station exists.
    bool hasStation(const int code) const;

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

    const std::string GTFSPath = "../GTFS/";
	const std::string GTFSStopsFile = GTFSPath + "stops.txt";
	const std::string GTFSLinesFile = GTFSPath + "stop_times_filtered.txt";

	const double maxNearbyDistance = 0.6; // km

    std::unordered_map<int, Station> _map;
};
