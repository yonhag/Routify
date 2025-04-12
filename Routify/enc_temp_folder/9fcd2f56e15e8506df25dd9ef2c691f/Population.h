#pragma once

#include "Route.h"
#include "Graph.h"
#include <vector>
#include <random>

class Population {
public:
    explicit Population(int size, int startId, int destinationId, const Graph& graph);

    void evolve(int generations, double mutationRate);
    const Route& getBestSolution() const;

    static Route crossover(const Route& parent1, const Route& parent2, std::mt19937& gen);

    void performSelection();

    std::vector<Route> getRoutes() const;

private:
    std::vector<Route> _routes;
    int _startId;
    int _destinationId;
    const Graph& _graph;

    std::mt19937 _gen;

    // Renamed and logic changed
    Route generateGuidedRandomRoute(std::mt19937& gen) const;

    // Helper for distance calculation (can be static or free function)
    static double haversineDistance(double lat1, double lon1, double lat2, double lon2);
};