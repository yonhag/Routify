#define _USE_MATH_DEFINES // For M_PI
#include "Population.h"
#include <algorithm>
#include <random>
#include <stdexcept>
#include <cmath>
#include <iostream>
#include <vector>
#include <numeric>
#include <limits>
#include <unordered_set>
#include <queue>
#include <unordered_map>
#include <string> // Needed for string ID in BfsNode

// Haversine function...
static double calculateHaversineDistance(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371.0;
    if (abs(lat1 - lat2) < 1e-9 && abs(lon1 - lon2) < 1e-9) { return 0.0; }
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    lat1 = lat1 * M_PI / 180.0; lat2 = lat2 * M_PI / 180.0;
    double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1) * cos(lat2) * sin(dLon / 2) * sin(dLon / 2);
    if (a < 0.0) a = 0.0; if (a > 1.0) a = 1.0;
    double c = 2 * atan2(sqrt(a), sqrt(1.0 - a));
    return R * c;
}
double Population::haversineDistance(double lat1, double lon1, double lat2, double lon2) {
    return calculateHaversineDistance(lat1, lon1, lat2, lon2);
}


// --- BFS Pathfinding Implementation (Modified BfsNode and Reconstruction) ---
namespace {

    struct BfsNode {
        int stationCode;
        int parentCode;
        // Store only ID and destination code of the line from parent
        std::string lineIdFromParent;
        int lineToFromParent; // Store the 'to' code explicitly

        BfsNode(int code = -1, int parent = -1, std::string lineId = "", int lineTo = -1)
            : stationCode(code), parentCode(parent), lineIdFromParent(lineId), lineToFromParent(lineTo) {
        }
    };

    // Finds *the specific* TransportationLine object used in the graph
    // Returns a dummy line if not found (shouldn't happen if BFS data is valid)
    const Graph::TransportationLine& findLineInGraph(const Graph& graph, int fromCode, const std::string& lineId, int lineTo) {
        static Graph::TransportationLine dummyLine; // Reusable dummy
        try {
            const auto& lines = graph.getLinesFrom(fromCode);
            for (const auto& line : lines) {
                if (line.id == lineId && line.to == lineTo) {
                    return line; // Return reference to the line in the graph
                }
            }
        }
        catch (const std::out_of_range&) {
            // From code doesn't exist? Should have been caught earlier.
        }
        std::cerr << "Warning: findLineInGraph failed for from=" << fromCode
            << ", id='" << lineId << "', to=" << lineTo << std::endl;
        dummyLine.id = "Error"; // Mark dummy as error
        return dummyLine;
    }

    std::vector<Route::VisitedStation> findPathBFS(
        const Graph& graph,
        int startCode,
        int endCode)
    {
        std::vector<Route::VisitedStation> path;
        std::queue<int> q;
        std::unordered_map<int, BfsNode> visitedInfo;

        if (!graph.hasStation(startCode) || !graph.hasStation(endCode)) {
            std::cerr << "BFS Error: Start or end code not found in graph." << std::endl;
            return path;
        }

        q.push(startCode);
        // For start node, use special "Start" ID and its own code as 'to' marker?
        visitedInfo[startCode] = BfsNode(startCode, -1, "Start", startCode);

        int currentCode = -1;
        bool found = false;
        while (!q.empty()) {
            currentCode = q.front();
            q.pop();

            if (currentCode == endCode) {
                found = true;
                break;
            }

            try {
                const auto& outgoingLines = graph.getLinesFrom(currentCode);
                for (const auto& line : outgoingLines) {
                    int nextCode = line.to;
                    if (graph.hasStation(nextCode) && visitedInfo.find(nextCode) == visitedInfo.end()) {
                        // Store parent, line ID, and line's 'to' code
                        visitedInfo[nextCode] = BfsNode(nextCode, currentCode, line.id, line.to);
                        q.push(nextCode);
                    }
                }
            }
            catch (const std::out_of_range& e) {
                std::cerr << "BFS Warning: Error getting lines from code " << currentCode << ": " << e.what() << std::endl;
                continue;
            }
        }

        // --- Reconstruct Path (Modified) ---
        if (found) {
            std::vector<Route::VisitedStation> reversed_path;
            int traceCode = endCode;

            while (traceCode != -1) {
                auto it = visitedInfo.find(traceCode);
                if (it == visitedInfo.end()) { /* Error handling */ return {}; }
                const BfsNode& nodeInfo = it->second;

                try {
                    const Graph::Station& station = graph.getStationById(traceCode); // Get station object

                    // Find the actual line object used to reach this station from its parent
                    const Graph::TransportationLine& lineUsed =
                        (nodeInfo.parentCode == -1) // Is this the start node?
                        ? Graph::TransportationLine("Start", startCode, 0, 0, Graph::TransportMethod::Walk) // Create dummy Start line
                        : findLineInGraph(graph, nodeInfo.parentCode, nodeInfo.lineIdFromParent, nodeInfo.lineToFromParent);

                    if (lineUsed.id == "Error" && nodeInfo.parentCode != -1) {
                        std::cerr << "BFS Error: Path reconstruction failed, line not found in graph." << std::endl;
                        return {}; // Line lookup failed
                    }

                    // Add the VisitedStation using the retrieved station and line objects
                    reversed_path.push_back(Route::VisitedStation(station, lineUsed));
                    traceCode = nodeInfo.parentCode; // Move to the parent

                }
                catch (const std::out_of_range& e) { /* Error handling */ return {}; }
                // Safety break...
                if (reversed_path.size() > graph.getStationCount() * 2) { /* Error handling */ return {}; }
            }
            std::reverse(reversed_path.begin(), reversed_path.end());
            path = std::move(reversed_path);
        }
        else { /* No path found */ }

        return path;
    }

} // end anonymous namespace

// --- Population Constructor (Uses the modified BFS) ---
Population::Population(int size, int startId, int destinationId, const Graph& graph)
    : _startId(startId), _destinationId(destinationId), _graph(graph)
{
    // ... (validation, seeding, reserve) ...
    if (size <= 0) { throw std::invalid_argument("Population size must be positive."); }
    std::random_device rd;
    _gen.seed(rd());
    _routes.reserve(size);
    if (!graph.hasStation(startId) || !graph.hasStation(destinationId)) {
        throw std::runtime_error("Population initialization failed: Invalid start/destination ID provided.");
    }
    std::cout << "Generating initial population (" << size << " routes) using BFS + Mutation..." << std::endl;

    // --- Step 1: Generate Baseline Route using NEW BFS ---
    std::vector<Route::VisitedStation> bfsPathStations = findPathBFS(_graph, _startId, _destinationId);

    if (bfsPathStations.empty()) {
        std::cerr << "Error: BFS could not find any path between start (" << startId << ") and destination (" << destinationId << ")." << std::endl;
        throw std::runtime_error("Population initialization failed: No path exists between stations.");
    }

    Route baseRoute;
    for (const auto& vs : bfsPathStations) { baseRoute.addVisitedStation(vs); }

    // --- DEBUG Print for constructed Base Route ---
    // (Keep the debug printing block from the previous response here if needed)
    // --- END DEBUG ---

    // Verify the base route - THIS SHOULD NOW PASS
    if (!baseRoute.isValid(_startId, _destinationId, _graph)) {
        std::cerr << "Error: BFS generated path FAILED validation even after BFS fix! (BFS Path Size: " << bfsPathStations.size() << ")" << std::endl;
        // If this still fails, the issue might be deeper in isValid's logic or graph data itself.
        throw std::runtime_error("Population initialization failed: BFS path invalid.");
    }
    _routes.push_back(baseRoute);
    std::cout << "Generated baseline route via BFS (Length: " << bfsPathStations.size() << " steps)." << std::endl;

    // --- Step 2: Generate Remaining Population by Mutating ---
    // ... (Mutation loop remains the same as previous response) ...
    size_t routesNeeded = static_cast<size_t>(size);
    size_t safetyCounter = 0;
    const size_t maxAttempts = routesNeeded * 10;
    const int minMutationSteps = 5;
    const int maxMutationSteps = 20;

    while (_routes.size() < routesNeeded && safetyCounter < maxAttempts) {
        safetyCounter++;
        Route mutatedRoute = baseRoute; // Start with a copy
        std::uniform_int_distribution<> numMutationsDist(minMutationSteps, maxMutationSteps);
        int mutationsToApply = numMutationsDist(_gen);
        for (int m = 0; m < mutationsToApply; ++m) {
            mutatedRoute.mutate(1.0, _gen, _startId, _destinationId, _graph);
        }
        if (mutatedRoute.isValid(_startId, _destinationId, _graph)) {
            _routes.push_back(mutatedRoute);
        }
    }
    std::cout << "Generated " << _routes.size() << " initial routes total (using " << safetyCounter << " mutation attempts)." << std::endl;
    if (_routes.empty()) { throw std::runtime_error("Population became empty after mutation phase."); }
    if (_routes.size() < routesNeeded) { std::cerr << "Warning: Could only generate " << _routes.size() << "/" << size << " valid routes via BFS+Mutation." << std::endl; }
}

// --- Population Evolution Methods ---

// Evolves the population
void Population::evolve(int generations, double mutationRate) {
    if (_routes.empty()) {
        std::cerr << "Cannot evolve initial empty population." << std::endl;
        return;
    }
    std::cout << "Starting evolution..." << std::endl;
    const size_t targetSize = _routes.size(); // Maintain original size if possible
    // Calculate elitism count based on the target size
    const size_t elitismCount = std::max(static_cast<size_t>(1), static_cast<size_t>(targetSize * 0.1));

    for (int genIndex = 0; genIndex < generations; ++genIndex) {
        // --- Selection ---
        performSelection(); // Sorts and potentially shrinks _routes

        if (_routes.empty()) {
            std::cerr << "Population extinct after selection in generation " << genIndex + 1 << std::endl;
            break; // Stop evolution
        }

        // --- Reproduction ---
        std::vector<Route> newGeneration;
        newGeneration.reserve(targetSize); // Reserve target size

        // Elitism: Copy the best survivors directly
        size_t current_pop_size = _routes.size(); // Size *after* selection
        size_t actualElitismCount = std::min(elitismCount, current_pop_size);
        for (size_t i = 0; i < actualElitismCount; ++i) {
            newGeneration.push_back(_routes[i]);
        }

        // Breeding: Fill the rest using crossover and mutation
        if (current_pop_size > 0) { // Check if parents exist
            std::uniform_int_distribution<> parent_dis(0, static_cast<int>(current_pop_size - 1));
            // Loop until the new generation reaches the target size
            while (newGeneration.size() < targetSize) {
                int idx1 = parent_dis(_gen);
                int idx2 = parent_dis(_gen);
                if (current_pop_size > 1 && idx1 == idx2) {
                    idx2 = (idx1 + 1) % current_pop_size;
                }

                Route child = crossover(_routes[idx1], _routes[idx2], _gen);
                child.mutate(mutationRate, _gen, _startId, _destinationId, _graph);

                // Add the new child regardless of validity (selection will handle it)
                newGeneration.push_back(child);

                // Safety break if loop runs too long (shouldn't happen if targetSize is reached)
                if (newGeneration.size() > targetSize * 2) {
                    std::cerr << "Error: Breeding loop exceeded expected size. Breaking." << std::endl;
                    if (newGeneration.size() > targetSize) newGeneration.resize(targetSize); // Trim excess
                    break;
                }
            }
        }
        else {
            // If selection killed all parents, new generation will only contain elites (if any)
        }

        // Replace old population with the new one
        _routes = std::move(newGeneration);

        // --- Reporting ---
        if (!_routes.empty()) {
            // Find best fitness in the *current* new generation
            const Route& best_gen_route = *std::max_element(_routes.begin(), _routes.end(),
                [this](const Route& a, const Route& b) {
                    return a.getFitness(_startId, _destinationId, _graph) < b.getFitness(_startId, _destinationId, _graph);
                });
            double best_fitness = best_gen_route.getFitness(_startId, _destinationId, _graph);

            // Print periodically
            if (genIndex == 0 || (genIndex + 1) % 50 == 0 || genIndex == generations - 1) {
                std::cout << "Generation " << (genIndex + 1) << "/" << generations
                    << " - Pop Size: " << _routes.size()
                    << " - Best Fitness: " << best_fitness << std::endl;
            }
        }
        else {
            std::cout << "Generation " << (genIndex + 1) << " - Population empty after reproduction." << std::endl;
        }
    } // End generation loop
    std::cout << "Evolution finished." << std::endl;
}


// Returns the best route found
const Route& Population::getBestSolution() const {
    if (_routes.empty()) {
        // This should only happen if initialization failed or population went extinct
        throw std::runtime_error("Error: Attempted to get best solution from an empty or extinct population.");
    }
    // Find the element with the maximum fitness
    auto best_it = std::max_element(_routes.begin(), _routes.end(),
        [this](const Route& a, const Route& b) {
            return a.getFitness(_startId, _destinationId, _graph) < b.getFitness(_startId, _destinationId, _graph);
        });
    // Check if max_element somehow failed (e.g., all NaN fitness - very unlikely)
    if (best_it == _routes.end()) {
        throw std::runtime_error("Error: Could not determine best solution (max_element failed).");
    }
    return *best_it;
}


// Crossover function (single-point based on common station)
Route Population::crossover(const Route& parent1, const Route& parent2, std::mt19937& gen) {
    const auto& visited1 = parent1.getVisitedStations();
    const auto& visited2 = parent2.getVisitedStations();

    // Basic checks for valid crossover
    if (visited1.size() <= 2 || visited2.size() <= 2) {
        // Not enough intermediate points, return one parent (e.g., the first one)
        return parent1;
    }

    // Find common intermediate stations
    std::vector<std::pair<size_t, size_t>> commonIndices;
    for (size_t i = 1; i < visited1.size() - 1; ++i) { // Exclude start/end
        for (size_t j = 1; j < visited2.size() - 1; ++j) { // Exclude start/end
            // Use Station's operator==
            if (visited1[i].station == visited2[j].station) {
                commonIndices.push_back({ i, j });
            }
        }
    }

    if (!commonIndices.empty()) {
        // Choose a random common station pair
        std::uniform_int_distribution<> common_dis(0, static_cast<int>(commonIndices.size() - 1));
        auto [idx1, idx2] = commonIndices[common_dis(gen)]; // Indices in parent1 and parent2

        // Create child route by combining segments
        Route childRoute;
        // Segment from parent1 up to (and including) the common station
        for (size_t k = 0; k <= idx1; ++k) {
            childRoute.addVisitedStation(visited1[k]);
        }
        // Segment from parent2 *after* the common station to the end
        for (size_t k = idx2 + 1; k < visited2.size(); ++k) {
            childRoute.addVisitedStation(visited2[k]);
        }
        // Note: Validity of the child is not guaranteed here, depends on connections at the crossover point.
        return childRoute;
    }
    else {
        // Fallback: No common intermediate station found. Return one parent randomly.
        std::uniform_int_distribution<> parent_choice(0, 1);
        return (parent_choice(gen) == 0) ? parent1 : parent2;
    }
}


// Selection: Sorts by fitness (descending) and keeps the top half (at least 1)
void Population::performSelection() {
    if (_routes.empty()) return; // Nothing to select from

    // Sort routes by fitness, highest first. Handle potential NaNs.
    std::sort(_routes.begin(), _routes.end(),
        [this](const Route& a, const Route& b) {
            double fitnessA = a.getFitness(_startId, _destinationId, _graph);
            double fitnessB = b.getFitness(_startId, _destinationId, _graph);
            // Sort NaNs to the end (equivalent to lowest fitness)
            if (std::isnan(fitnessB)) return true;  // B is NaN, A comes first
            if (std::isnan(fitnessA)) return false; // A is NaN, B comes first
            return fitnessA > fitnessB; // Normal descending sort for valid fitness values
        });

    // Calculate the number to keep: Half rounded up, but minimum 1.
    size_t current_size = _routes.size();
    size_t keepCount = std::max(static_cast<size_t>(1), (current_size + 1) / 2);

    // Resize if necessary
    if (keepCount < current_size) {
        _routes.resize(keepCount);
    }
}


// Getter for current routes
std::vector<Route> Population::getRoutes() const
{
    // Return by value creates a copy
    return this->_routes;
}
