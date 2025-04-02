/*
#include "PathFinder.h"

PathFinder::PathFinder(int populationSize, int generations, double mutationRate, const Graph& graph)
    : _populationSize(populationSize), _generations(generations), _mutationRate(mutationRate), _graph(graph)
{
}

std::array<Route, 1> PathFinder::findBestRoute(int startId, int destinationId) {
    Population population(_populationSize, startId, destinationId, _graph);
    population.evolve(_generations, _mutationRate);
    return { population.getBestSolution() };
}
*/