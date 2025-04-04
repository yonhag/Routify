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
    static Route crossover(const Route& parent1, const Route& parent2);
    void performSelection();

private:
    std::vector<Route> _routes;
    int _startId;
    int _destinationId;
    const Graph& _graph;

    Route generateRandomRoute() const;
};
