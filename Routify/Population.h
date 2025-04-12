#pragma once

#include "Route.h"
#include "Graph.h"
#include <vector>
#include <random>

class Population {
public:
    Population(int size, int startId, int destinationId, const Graph& graph,
        const Utilities::Coordinates& userCoords,
        const Utilities::Coordinates& destCoords);
    void evolve(int generations, double mutationRate);
    const Route& getBestSolution() const;

    static Route crossover(const Route& parent1, const Route& parent2, std::mt19937& gen);

    void performSelection();

    std::vector<Route> getRoutes() const;

private:
    const Graph& _graph;
    std::vector<Route> _routes;

    int _startId;
    int _destinationId;
    Utilities::Coordinates _userCoords;
    Utilities::Coordinates _destCoords;

    std::mt19937 _gen;

    // Renamed and logic changed
    Route generateGuidedRandomRoute(std::mt19937& gen) const;
};