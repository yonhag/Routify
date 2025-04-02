/*
#include "Population.h"
#include <algorithm>
#include <ctime>
#include <cstdlib>

Population::Population(int size, int startId, int destinationId, const Graph& graph)
    : _startId(startId), _destinationId(destinationId), _graph(graph)
{
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    for (int i = 0; i < size; ++i) {
        _routes.push_back(generateRandomRoute());
    }
}

void Population::evolve(int generations, double mutationRate) {
    for (int i = 0; i < generations; ++i) {
        performSelection();
        std::vector<Route> newGeneration;
        while (newGeneration.size() < _routes.size()) {
            const Route& parent1 = _routes[rand() % _routes.size()];
            const Route& parent2 = _routes[rand() % _routes.size()];
            Route child = crossover(parent1, parent2);
            child.mutate(mutationRate);
            newGeneration.push_back(child);
        }
        _routes = newGeneration;
    }
}

const Route& Population::getBestSolution() const {
    return *std::max_element(_routes.begin(), _routes.end(), [](const Route& a, const Route& b) {
        return a.getFitness() < b.getFitness();
        });
}

Route Population::crossover(const Route& parent1, const Route& parent2) {
    // Simple crossover: take half from parent1 and the rest from parent2
    const std::vector<int>& stops1 = parent1.getStops();
    const std::vector<int>& stops2 = parent2.getStops();

    std::vector<int> newStops;
    size_t half = stops1.size() / 2;
    newStops.insert(newStops.end(), stops1.begin(), stops1.begin() + half);

    for (int stop : stops2) {
        if (std::find(newStops.begin(), newStops.end(), stop) == newStops.end()) {
            newStops.push_back(stop);
        }
    }

    // Note: in practice, you'd regenerate the edges from stops using Graph here.
    // This is a simplified placeholder.
    std::vector<Graph::TransportationLine> dummyEdges; // should reconstruct valid edges
    return Route(newStops.front(), dummyEdges);
}

void Population::performSelection() {
    std::sort(_routes.begin(), _routes.end(), [](const Route& a, const Route& b) {
        return a.getFitness() > b.getFitness();
        });
    _routes.resize(_routes.size() / 2); // Keep top 50%
}

Route Population::generateRandomRoute() const {
    // Placeholder: should be replaced with actual random walk from start to destination
    std::vector<Graph::TransportationLine> path;
    const auto& lines = _graph.getLinesFrom(_startId);
    if (!lines.empty()) {
        path.push_back(lines[rand() % lines.size()]);
    }
    return Route(_startId, path);
}
*/