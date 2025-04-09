#pragma once

#include <vector>
#include <random> // Added for mutation generator
#include "Graph.h"

class Route {
public:
    struct VisitedStation {
        Graph::Station station;
        Graph::TransportationLine line; // Line taken TO REACH this station

        VisitedStation(const Graph::Station& s, const Graph::TransportationLine& l) : station(s), line(l) {}
        VisitedStation() = default;
    };
    // --- Rule of 5: Explicitly default special members ---
    // Ensure Route is copyable and movable using default compiler logic
    Route() : _stations() {}
    Route(const Route&) = default;
    Route(Route&&) = default;
    Route& operator=(const Route&) = default;
    Route& operator=(Route&&) = default;
    ~Route() = default; // Default destructor is sufficient
    // --- End of Rule of 5 ---


    void addVisitedStation(const VisitedStation& vs);
    double getTotalTime() const;
    double getTotalCost() const;
    int getTransferCount() const;
    const std::vector<VisitedStation> getVisitedStations() const;

    // Fitness now takes arguments for validation
    double getFitness(int startId, int destinationId, const Graph& graph) const;
    // Added validation method
    bool isValid(int startId, int destinationId, const Graph& graph) const;

    // Mutate needs graph access and random generator for better implementation
    void mutate(double mutationRate, std::mt19937& gen, int startId, int destinationId, const Graph& graph);

    // Helper function to generate a path segment (used by mutate)
    // Returns true on success, false on failure (e.g., max steps exceeded)
    // segment parameter is an out-parameter, filled on success.
    static bool generatePathSegment(int segmentStartId, int segmentEndId, const Graph& graph, std::mt19937& gen, std::vector<VisitedStation>& segment);


private:
    static const double MAX_WALKING_DISTANCE; // KM
    std::vector<VisitedStation> _stations; // Stores the sequence of visited stations and the lines taken
};