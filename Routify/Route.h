#pragma once
#include "Graph.h"
#include <random>

class Route {
public:
    struct VisitedStation {
        Graph::Station station;
        Graph::TransportationLine line;
        int prevStationCode;

        VisitedStation(const Graph::Station& s, const Graph::TransportationLine& l, const int prevCode)
            : station(s), line(l), prevStationCode(prevCode) {
        }

        VisitedStation() : prevStationCode(-1) {}
    };

    Route() = default;

    Route(const Route&) = default;
    Route(Route&&) = default;
    Route& operator=(const Route&) = default;
    Route& operator=(Route&&) = default;
    ~Route() = default;

    // Adds a visited station to the object.
    void addVisitedStation(const VisitedStation& vs);

    // Calculates the total time the trip should take. 
    double getTotalTime(const Graph& graph, const int routeStartId) const;

    // Estimates the total price the trip should cost.
    double getTotalCost(const Graph& graph) const;

    // Returns the transportation.
    int getTransferCount() const;
    
    // Returns the visited stations vector.
    const std::vector<VisitedStation>& getVisitedStations() const;
    std::vector<VisitedStation>& getMutableVisitedStations() { return _stations; }

    // Checks if the route is valid
    bool isValid(const int startId, const int destinationId, const Graph& graph) const;

    /*
    * --- Genetic Algorithm Methods ---
    */

    // Gives the route a fitness score, describing how good it is at solving the requested problem.
    double getFitness(const int startId, const int destinationId, const Graph& graph,
        const Utilities::Coordinates& userCoords, // User's actual location
        const Utilities::Coordinates& destCoords) const; // Clicked destination

	// Mutates the route by regenerating a segment or replacing it with a walk.
    void mutate(const double mutationRate, std::mt19937& gen, const int startId, const int destinationId, const Graph& graph);

    // Combines two routes together to create a better one
    static Route crossover(const Route& parent1, const Route& parent2, std::mt19937& gen);

    /*
	*  --- End of Genetic Algorithm Methods ---
    */

    // Calculates walk time between two coordinates based on WALK_SPEED_KPH
    double calculateFullJourneyTime(
        const Graph& graph,
        int routeStartId,
        int routeEndId,
        const Utilities::Coordinates& userCoords,
        const Utilities::Coordinates& destCoords) const;

private:
    // Helper for mutation 
    static bool generatePathSegment(const int segmentStartId, const int segmentEndId, const Graph& graph, std::mt19937& gen, std::vector<VisitedStation>& segment);

    // Calculates the approximate time it would take to walk between two coordinates
    static double calculateWalkTime(const Utilities::Coordinates& c1, const Utilities::Coordinates& c2);

    std::vector<VisitedStation> _stations;
};
