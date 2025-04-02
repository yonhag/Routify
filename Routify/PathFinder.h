#pragma once

#include "Graph.h"
#include "Route.h"
#include "Population.h"
#include <array>

class PathFinder {
public:
    PathFinder(int populationSize, int generations, double mutationRate, const Graph& graph);

    std::array<Route, 1> findBestRoute(int startId, int destinationId);

private:
    int _populationSize;
    int _generations;
    double _mutationRate;
    const Graph& _graph;
};
