#include "Population.h"
#include <algorithm>
#include <random>
#include <stdexcept>
#include <iostream>
#include <numeric>
#include <limits>
#include <unordered_set>
#include <queue>


// --- BFS Pathfinding Implementation (Modified Reconstruction) ---
namespace {

    struct BfsNode { // Keep BfsNode as is (stores parent code)
        int stationCode;
        int parentCode;
        std::string lineIdFromParent;
        int lineToFromParent;
        BfsNode(int c = -1, int p = -1, const std::string& lId = "", int lTo = -1) : stationCode(c), parentCode(p), lineIdFromParent(lId), lineToFromParent(lTo) {}
    };

    const Graph::TransportationLine& findLineInGraph(const Graph& graph, int fromCode, const std::string& lineId, int lineTo) {
        static Graph::TransportationLine dummyLine;
        try {
            const auto& lines = graph.getLinesFrom(fromCode);
            for (const auto& line : lines) { if (line.id == lineId && line.to == lineTo) return line; }
        }
        catch (...) {}
        dummyLine.id = "Error"; return dummyLine;
    }

    std::vector<Route::VisitedStation> findPathBFS(
        const Graph& graph,
        int startCode,
        int endCode)
    {
        std::vector<Route::VisitedStation> path;
        std::queue<int> q;
        std::unordered_map<int, BfsNode> visitedInfo;

        if (!graph.hasStation(startCode) || !graph.hasStation(endCode)) return path;

        q.push(startCode);
        visitedInfo[startCode] = BfsNode(startCode, -1, "Start", startCode); // Parent code is -1 for start

        int currentCode = -1;
        bool found = false;
        while (!q.empty()) { // (BFS Exploration loop remains the same)
            currentCode = q.front(); q.pop();
            if (currentCode == endCode) { found = true; break; }
            try {
                const auto& lines = graph.getLinesFrom(currentCode);
                for (const auto& line : lines) {
                    int nextCode = line.to;
                    if (graph.hasStation(nextCode) && !visitedInfo.contains(nextCode)) {
                        visitedInfo[nextCode] = BfsNode(nextCode, currentCode, line.id, line.to);
                        q.push(nextCode);
                    }
                }
            }
            catch (...) { continue; }
        }

        // --- Reconstruct Path ---
        if (found) {
            std::vector<Route::VisitedStation> reversed_path;
            int traceCode = endCode;

            while (traceCode != -1) {
                auto it = visitedInfo.find(traceCode);
                if (it == visitedInfo.end()) return {};
                const BfsNode& nodeInfo = it->second; // Info for the current traceCode station

                try {
                    const Graph::Station& station = graph.getStationByCode(traceCode);

                    // Find the actual line object used
                    const Graph::TransportationLine& lineUsed =
                        (nodeInfo.parentCode == -1)
                        ? Graph::TransportationLine("Start", startCode, 0, Graph::TransportMethod::Walk) // Dummy start line
                        : findLineInGraph(graph, nodeInfo.parentCode, nodeInfo.lineIdFromParent, nodeInfo.lineToFromParent);

                    if (lineUsed.id == "Error" && nodeInfo.parentCode != -1) return {}; // Line lookup failed

                    // *** ADDED: Pass nodeInfo.parentCode to VisitedStation constructor ***
                    reversed_path.push_back(Route::VisitedStation(station, lineUsed, nodeInfo.parentCode));
                    traceCode = nodeInfo.parentCode; // Move to the parent

                }
                catch (const std::out_of_range&) { return {}; }
                catch (const std::exception& e_rec) { std::cerr << "BFS Rec Err: " << e_rec.what() << std::endl; return {}; }

                // Safety break
                if (reversed_path.size() > graph.getStationCount() + 5) { // Adjusted safety limit
                    std::cerr << "BFS Error: Path reconstruction loop or excessive length." << std::endl;
                    return {};
                }
            }
            std::reverse(reversed_path.begin(), reversed_path.end());
            path = std::move(reversed_path);
        }
        else { /* No path found */ }

        return path;
    }

} // end anonymous namespace

// --- Population Constructor (No changes needed here, uses the fixed BFS) ---
Population::Population(const int size, const int startId, const int destinationId, const Graph& graph,
    const Utilities::Coordinates& userCoords,
    const Utilities::Coordinates& destCoords)
    : _graph(graph), _startId(startId), _destinationId(destinationId),
    _userCoords(userCoords), _destCoords(destCoords) // Initialize members
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

    // --- Step 1: Generate Baseline Route using BFS ---
    std::vector<Route::VisitedStation> bfsPathStations = findPathBFS(_graph, _startId, _destinationId);

    if (bfsPathStations.empty()) {
        std::cerr << "Error: BFS could not find any path between start (" << startId << ") and destination (" << destinationId << ")." << std::endl;
        throw std::runtime_error("Population initialization failed: No path exists between stations.");
    }

    Route baseRoute;
    for (const auto& vs : bfsPathStations) { baseRoute.addVisitedStation(vs); }

    // --- Re-enable Validation ---
    if (!baseRoute.isValid(_startId, _destinationId, _graph)) {
        std::cerr << "Error: BFS generated path FAILED validation AFTER FIX! (BFS Path Size: " << bfsPathStations.size() << ")" << std::endl;
        // Print detailed debug info for the baseRoute if this happens
        throw std::runtime_error("Population initialization failed: BFS path invalid despite fixes.");
    }
    _routes.push_back(baseRoute);
    std::cout << "Generated baseline route via BFS (Length: " << bfsPathStations.size() << " steps)." << std::endl;

    // --- Step 2: Generate Remaining Population by Mutating ---
    auto routesNeeded = static_cast<size_t>(size);
    size_t safetyCounter = 0; 
    const size_t maxAttempts = routesNeeded * 10;
    const int minMutationSteps = 5; 
    const int maxMutationSteps = 20;

    while (_routes.size() < routesNeeded && safetyCounter < maxAttempts) {
        safetyCounter++; Route mutatedRoute = baseRoute;
        std::uniform_int_distribution<> numMutationsDist(minMutationSteps, maxMutationSteps);
        int mutationsToApply = numMutationsDist(_gen);
        for (int m = 0; m < mutationsToApply; ++m) { mutatedRoute.mutate(1.0, _gen, _startId, _destinationId, _graph); }
        if (mutatedRoute.isValid(_startId, _destinationId, _graph)) { _routes.push_back(mutatedRoute); }
    }
    
    std::cout << "Generated " << _routes.size() << " initial routes total (using " << safetyCounter << " mutation attempts)." << std::endl;
    
    if (_routes.empty()) { throw std::runtime_error("Population became empty after mutation phase."); }
    if (_routes.size() < routesNeeded) { std::cerr << "Warning: Could only generate " << _routes.size() << "/" << size << " valid routes via BFS+Mutation." << std::endl; }
}


// --- Population Evolution Methods ---

// Evolves the population
void Population::evolve(const int generations, const double mutationRate) {
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

                Route child = Route::crossover(_routes[idx1], _routes[idx2], _gen);
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
                    // Pass member coordinates stored in the Population object
                    return a.getFitness(_startId, _destinationId, _graph, _userCoords, _destCoords) <
                        b.getFitness(_startId, _destinationId, _graph, _userCoords, _destCoords);
                });
            double best_fitness = best_gen_route.getFitness(_startId, _destinationId, _graph, _userCoords, _destCoords);
    
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
        throw std::runtime_error("Error: Attempted to get best solution from an empty or extinct population.");
    }
    // Find the element with the maximum fitness
    auto best_it = std::max_element(_routes.begin(), _routes.end(),
        [this](const Route& a, const Route& b) {
            return a.getFitness(_startId, _destinationId, _graph, _userCoords, _destCoords) <
                b.getFitness(_startId, _destinationId, _graph, _userCoords, _destCoords);
        });
    // Check if max_element somehow failed (e.g., all NaN fitness - very unlikely)
    if (best_it == _routes.end()) {
        throw std::runtime_error("Error: Could not determine best solution (max_element failed).");
    }
    return *best_it;
}


// Selection: Sorts by fitness (descending) and keeps the top half (at least 1)
void Population::performSelection() {
    if (_routes.empty()) return; // Nothing to select from

    // Sort routes by fitness, highest first. Handle potential NaNs.
    std::sort(_routes.begin(), _routes.end(),
        [this](const Route& a, const Route& b) {
            // Pass member coordinates
            double fitnessA = a.getFitness(_startId, _destinationId, _graph, _userCoords, _destCoords);
            double fitnessB = b.getFitness(_startId, _destinationId, _graph, _userCoords, _destCoords);
            if (std::isnan(fitnessB)) return true;
            if (std::isnan(fitnessA)) return false;
            return fitnessA > fitnessB; // Higher fitness first
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
    return this->_routes;
}
