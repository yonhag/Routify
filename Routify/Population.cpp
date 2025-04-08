#define _USE_MATH_DEFINES
#include "Population.h"
#include <algorithm>
#include <random>
#include <stdexcept>
#include <cmath>

static double haversineDistance(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371.0; // Earth radius in kilometers
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dLat / 2) * sin(dLat / 2) +
        cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
        sin(dLon / 2) * sin(dLon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return R * c;
}


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
// Modified generateRandomRoute function in Population.cpp
Route Population::generateRandomRoute() const {
    std::vector<Graph::Station> stations;
    std::vector<Graph::TransportationLine> lines;

    // Get the starting station from the graph.
    Graph::Station startStation = _graph.getStationById(_startId);
    stations.push_back(startStation);

    int currentId = _startId;
    const int maxSteps = 100;
    int steps = 0;

    // Pre-fetch destination station.
    Graph::Station destStation = _graph.getStationById(_destinationId);

    // Set up random number generation.
    std::random_device rd;
    std::mt19937 gen(rd());

    while (currentId != _destinationId && steps < maxSteps) {
        Graph::Station currentStation = _graph.getStationById(currentId);

        // Check if destination is within walking distance (less than 0.2 km)
        double distanceToDestination = haversineDistance(
            currentStation.latitude, currentStation.longitude,
            destStation.latitude, destStation.longitude
        );

        if (distanceToDestination < 0.2) {
            // Create a walking edge: assume walking speed is 5 km/h, so time in minutes is (distance/5)*60.
            Graph::TransportationLine walkingEdge;
            walkingEdge.id = "Walk";
            walkingEdge.to = _destinationId;
            walkingEdge.travelTime = (distanceToDestination / 5.0) * 60.0;
            walkingEdge.price = 0;
            walkingEdge.type = Graph::TransportMethod::Walk;

            lines.push_back(walkingEdge);
            stations.push_back(destStation);
            currentId = _destinationId;
            break;
        }

        // Otherwise, choose a random available transportation line.
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

    // If destination not reached within maxSteps, force a connection via a walking edge.
    if (currentId != _destinationId) {
        Graph::Station lastStation = stations.back();
        double distance = haversineDistance(lastStation.latitude, lastStation.longitude,
            destStation.latitude, destStation.longitude);
        Graph::TransportationLine walkingEdge;
        walkingEdge.id = "Walk";
        walkingEdge.to = _destinationId;
        walkingEdge.travelTime = (distance / 5.0) * 60.0; // Walking speed assumed to be 5 km/h.
        walkingEdge.price = 0;
        walkingEdge.type = Graph::TransportMethod::Walk;

        lines.push_back(walkingEdge);
        stations.push_back(destStation);
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
    auto visited1 = parent1.getVisitedStations();
    auto visited2 = parent2.getVisitedStations();

    // Find common stations (ignoring the first and last station)
    std::vector<std::pair<size_t, size_t>> commonIndices;
    for (size_t i = 1; i < visited1.size() - 1; ++i) {
        for (size_t j = 1; j < visited2.size() - 1; ++j) {
            if (visited1[i].station == visited2[j].station) {
                commonIndices.push_back({ i, j });
            }
        }
    }

    // If a common station is found, perform a crossover using it
    if (!commonIndices.empty()) {
        // Choose a common station, for example at random
        auto [i, j] = commonIndices[rand() % commonIndices.size()];

        // Build the child route:
        //  - First segment from parent1 (start to common station)
        //  - Second segment from parent2 (common station to destination)
        std::vector<Graph::Station> childStations;
        std::vector<Graph::TransportationLine> childLines;

        // Copy the route from parent1 until the common station (inclusive)
        for (size_t k = 0; k <= i; ++k) {
            childStations.push_back(visited1[k].station);
            if (k < i) {
                childLines.push_back(visited1[k].line);
            }
        }
        // Append the remaining segment from parent2 starting after the common station
        for (size_t k = j + 1; k < visited2.size(); ++k) {
            childStations.push_back(visited2[k].station);
            if (k < visited2.size() - 1) {
                childLines.push_back(visited2[k].line);
            }
        }
        // Construct and return the new Route
        return Route(childStations, childLines);
    }
    else {
        // Fallback: if no common station exists, return one parent or apply a different strategy.
        return parent1;
    }
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

std::vector<Route> Population::getRoutes()
{
    return this->_routes;
}
