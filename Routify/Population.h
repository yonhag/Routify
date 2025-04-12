#pragma once
#include "Route.h"
#include "Graph.h"
#include <random>

class Population {
public:
    Population(const int size, const int startId, const int destinationId, const Graph& graph,
        const Utilities::Coordinates& userCoords,
        const Utilities::Coordinates& destCoords);
    void evolve(const int generations, const double mutationRate);
    const Route& getBestSolution() const;

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
};