
#pragma once

#include <vector>
#include <random>
#include <string> // Include string
#include "Graph.h" // Ensure Graph is included

class Route {
public:
    struct VisitedStation {
        Graph::Station station;         // The station object itself
        Graph::TransportationLine line; // Line taken TO REACH this station
        int prevStationCode;            // *** ADDED: Code of the station *before* this one ***

        // Constructor updated
        VisitedStation(Graph::Station s, Graph::TransportationLine l, int prevCode)
            : station(s), line(l), prevStationCode(prevCode) {
        }

        // Default constructor needed if vector operations require it
        // Note: Initializes prevStationCode to -1 (or another invalid code)
        VisitedStation() : station(), line(), prevStationCode(-1) {}
    };

    // Default constructor
    Route() = default;

    // --- Rule of 5: Explicitly default special members ---
    Route(const Route&) = default;
    Route(Route&&) = default;
    Route& operator=(const Route&) = default;
    Route& operator=(Route&&) = default;
    ~Route() = default;
    // --- End of Rule of 5 ---

    void addVisitedStation(const VisitedStation& vs);
    double getTotalTime() const;
    double getTotalCost(const Graph& graph) const;
    int getTransferCount() const;
    const std::vector<VisitedStation> getVisitedStations() const;

    double getFitness(int startId, int destinationId, const Graph& graph,
        const Utilities::Coordinates& userCoords, // User's actual location
        const Utilities::Coordinates& destCoords) const; // Clicked destination

    bool isValid(int startId, int destinationId, const Graph& graph) const;

    // Static const for walking distance check in validation (if needed)
    // static const double MAX_WALKING_DISTANCE; // Example: 1.0 km

    // Mutation needs graph access and random generator
    void mutate(double mutationRate, std::mt19937& gen, int startId, int destinationId, const Graph& graph);

    // Helper for mutation (consider making private or moving logic)
    static bool generatePathSegment(int segmentStartId, int segmentEndId, const Graph& graph, std::mt19937& gen, std::vector<VisitedStation>& segment);

private:
    double calculateWalkTime(const Utilities::Coordinates& c1, const Utilities::Coordinates& c2) const;

    const static double WALK_SPEED_KPH;
    std::vector<VisitedStation> _stations;
};
