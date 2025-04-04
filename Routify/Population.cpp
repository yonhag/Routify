#include "Population.h"
#include <algorithm>
#include <random>
#include <stdexcept>

// Constructor: initialize population with random routes.
Population::Population(int size, int startId, int destinationId, const Graph& graph)
    : _startId(startId), _destinationId(destinationId), _graph(graph)
{
    for (int i = 0; i < size; ++i) {
        _routes.push_back(generateRandomRoute());
    }
}

// Generates a random route from the start to the destination.
// It begins at _startId and, at each step, randomly selects one of the available lines from the current station.
// It stops if the destination is reached or after a fixed maximum number of steps.
Route Population::generateRandomRoute() const {
    std::vector<Graph::Station> stations;
    std::vector<Graph::TransportationLine> lines;

    // Get the starting station from the graph.
    Graph::Station startStation = _graph.getStationById(_startId);
    stations.push_back(startStation);

    int currentId = _startId;
    const int maxSteps = 100;
    int steps = 0;

    // Set up random number generation.
    std::random_device rd;
    std::mt19937 gen(rd());

    while (currentId != _destinationId && steps < maxSteps) {
        const auto& availableLines = _graph.getLinesFrom(currentId);
        if (availableLines.empty()) {
            // Dead-end reached; break out.
            break;
        }
        std::uniform_int_distribution<> dis(0, availableLines.size() - 1);
        int idx = dis(gen);
        const auto& chosenLine = availableLines[idx];
        lines.push_back(chosenLine);
        try {
            Graph::Station nextStation = _graph.getStationById(chosenLine.to);
            stations.push_back(nextStation);
            currentId = chosenLine.to;
        }
        catch (std::exception& e) {
            break;
        }
        steps++;
    }
    // If destination not reached within maxSteps, try to force completion by adding the destination station.
    if (currentId != _destinationId) {
        try {
            Graph::Station destStation = _graph.getStationById(_destinationId);
            stations.push_back(destStation);
        }
        catch (...) {
            // If the destination cannot be found, the route remains incomplete.
        }
    }
    return Route(stations, lines);
}

// Evolves the population for the given number of generations using mutation and crossover.
void Population::evolve(int generations, double mutationRate) {
    std::random_device rd;
    std::mt19937 gen(rd());

    for (int genIndex = 0; genIndex < generations; ++genIndex) {
        performSelection();

        std::vector<Route> newRoutes;
        // Ensure at least two individuals exist for crossover.
        if (_routes.size() < 2) {
            while (_routes.size() < 2) {
                _routes.push_back(generateRandomRoute());
            }
        }
        std::uniform_int_distribution<> dis(0, _routes.size() - 1);
        // Generate offspring until we restore the population to (approximately) its previous size.
        while (newRoutes.size() < _routes.size() * 2) {
            int idx1 = dis(gen);
            int idx2 = dis(gen);
            if (idx1 == idx2)
                continue;
            Route child = crossover(_routes[idx1], _routes[idx2]);
            child.mutate(mutationRate);
            newRoutes.push_back(child);
        }
        _routes = newRoutes;
    }
}

// Returns the best route (with the highest fitness) in the current population.
const Route& Population::getBestSolution() const {
    return *std::max_element(_routes.begin(), _routes.end(),
        [](const Route& a, const Route& b) {
            return a.getFitness() < b.getFitness();
        });
}

// A very simple crossover implementation.
// (For a more advanced algorithm one would combine segments from both parents
// in order to produce a new route. Here we simply return the first parent.)
Route Population::crossover(const Route& parent1, const Route& parent2) {
    return parent1;
}

// Performs selection by sorting the current population in descending order of fitness,
// and keeping only the top half.
void Population::performSelection() {
    std::sort(_routes.begin(), _routes.end(),
        [](const Route& a, const Route& b) {
            return a.getFitness() > b.getFitness();
        });
    _routes.resize(_routes.size() / 2);
}
