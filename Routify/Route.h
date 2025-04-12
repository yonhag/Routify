#pragma once
#include "Graph.h"
#include <random>

class Route {
public:
    struct VisitedStation {
        Graph::Station station;         // The station object itself
        Graph::TransportationLine line; // Line taken to reach this station
        int prevStationCode;            // The code of the station before this one

        VisitedStation(const Graph::Station& s, const Graph::TransportationLine& l, const int prevCode)
            : station(s), line(l), prevStationCode(prevCode) {
        }

        VisitedStation() : station(), line(), prevStationCode(-1) {}
    };

    // Default constructor.
    Route() = default;

    // --- Rule of 5: Explicitly default special members ---
    Route(const Route&) = default;
    Route(Route&&) = default;
    Route& operator=(const Route&) = default;
    Route& operator=(Route&&) = default;
    ~Route() = default;
    // --- End of Rule of 5 ---

    // Adds a visited station to the object.
    void addVisitedStation(const VisitedStation& vs);

    // Calculates the total time the trip should take.
    double getTotalTime() const;

    // Estimates the total price the trip should cost.
    double getTotalCost(const Graph& graph) const;

    // Returns the transportation.
    int getTransferCount() const;
    
    // Returns the visited stations vector.
    const std::vector<VisitedStation> getVisitedStations() const;

    /*
    * Checks if the route is valid -
    * If all the stations appear on the graph, and are connected by the same lines mentioned here.
    */
    bool isValid(int startId, int destinationId, const Graph& graph) const;

    /*
    * --- Genetic Algorithm Methods ---
    */

    // Gives the route a fitness score, describing how good it is at solving the requested problem.
    double getFitness(const int startId, const int destinationId, const Graph& graph,
        const Utilities::Coordinates& userCoords, // User's actual location
        const Utilities::Coordinates& destCoords) const; // Clicked destination

	// Mutates the route by regenerating a segment or replacing it with a walk.
    void mutate(double mutationRate, std::mt19937& gen, int startId, int destinationId, const Graph& graph);

    // Combines two routes together to create a better one
    static Route crossover(const Route& parent1, const Route& parent2, std::mt19937& gen);

    /*
	*  --- End of Genetic Algorithm Methods ---
    */

    // Helper for mutation (consider making private or moving logic)
    static bool generatePathSegment(int segmentStartId, int segmentEndId, const Graph& graph, std::mt19937& gen, std::vector<VisitedStation>& segment);

private:
	// Calculates walk time between two coordinates based on WALK_SPEED_KPH
    double calculateWalkTime(const Utilities::Coordinates& c1, const Utilities::Coordinates& c2) const;

	const static double WALK_SPEED_KPH; // defined in Route.cpp
    std::vector<VisitedStation> _stations;
};
