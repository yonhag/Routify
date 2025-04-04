//#include <vector>
//#include <queue>
//#include <unordered_map>
//#include <unordered_set>
//#include <algorithm>
//#include <cmath>
//#include <limits>
//#include "Graph.h"   // Provides Graph, TransportationLine, Station, etc.
//#include "Route.h"   // Assumes a Route class with a constructor that takes a vector of TransportationLine
//
//// Helper: Hash function for pair<int,int> for banned edge storage.
//struct PairHash {
//    std::size_t operator()(const std::pair<int, int>& p) const {
//        return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
//    }
//};
//
//// Helper: Compute Euclidean distance between two stations based on their coordinates.
//double euclideanDistance(const Graph& graph, int station1, int station2) {
//    const Graph::Station* s1 = graph.getStationById(station1);
//    const Graph::Station* s2 = graph.getStationById(station2);
//    if (!s1 || !s2)
//        return 0.0;
//    double dx = s1->latitude - s2->latitude;
//    double dy = s1->longitude - s2->longitude;
//    return std::sqrt(dx * dx + dy * dy);
//}
//
//// A* search algorithm that optionally ignores banned edges.
//// Returns a vector of station IDs from 'start' to 'goal'.
//std::vector<int> aStarPathModified(const Graph& graph, int start, int goal,
//    const std::unordered_set<std::pair<int, int>, PairHash>& bannedEdges)
//{
//    struct NodeRecord {
//        int station;
//        int parent;           // To reconstruct the path
//        double costSoFar;
//        double estimatedTotal;
//        bool operator>(const NodeRecord& other) const {
//            return estimatedTotal > other.estimatedTotal;
//        }
//    };
//
//    auto heuristic = [&graph, goal](int station) -> double {
//        return euclideanDistance(graph, station, goal);
//        };
//
//    std::priority_queue<NodeRecord, std::vector<NodeRecord>, std::greater<NodeRecord>> openList;
//    std::unordered_map<int, double> costSoFar;
//    std::unordered_map<int, int> cameFrom;
//
//    openList.push({ start, -1, 0.0, heuristic(start) });
//    costSoFar[start] = 0.0;
//
//    while (!openList.empty()) {
//        NodeRecord current = openList.top();
//        openList.pop();
//
//        if (current.station == goal) {
//            // Reconstruct path from goal to start.
//            std::vector<int> path;
//            int cur = goal;
//            while (cur != -1) {
//                path.push_back(cur);
//                cur = cameFrom[cur];
//            }
//            std::reverse(path.begin(), path.end());
//            return path;
//        }
//
//        const auto& neighbors = graph.getLinesFrom(current.station);
//        for (const auto& edge : neighbors) {
//            // Skip edge if it is banned.
//            if (bannedEdges.find({ current.station, edge.to }) != bannedEdges.end())
//                continue;
//
//            int next = edge.to;
//            double newCost = current.costSoFar + edge.weight;
//            if (costSoFar.find(next) == costSoFar.end() || newCost < costSoFar[next]) {
//                costSoFar[next] = newCost;
//                double priority = newCost + heuristic(next);
//                openList.push({ next, current.station, newCost, priority });
//                cameFrom[next] = current.station;
//            }
//        }
//    }
//    return {}; // Return empty if no path is found.
//}
//
//// Regular A* search (no banned edges).
//std::vector<int> aStarPath(const Graph& graph, int start, int goal) {
//    std::unordered_set<std::pair<int, int>, PairHash> emptyBanned;
//    return aStarPathModified(graph, start, goal, emptyBanned);
//}
//
//// Computes the total cost of a given path (vector of station IDs) by summing the weights of connecting edges.
//double pathCost(const Graph& graph, const std::vector<int>& path) {
//    double cost = 0.0;
//    for (size_t i = 0; i < path.size() - 1; i++) {
//        int from = path[i], to = path[i + 1];
//        const auto& neighbors = graph.getLinesFrom(from);
//        for (const auto& edge : neighbors) {
//            if (edge.to == to) {
//                cost += edge.weight;
//                break;
//            }
//        }
//    }
//    return cost;
//}
//
//// Implementation of a simplified Yen’s algorithm for k-shortest paths.
//std::vector<std::vector<int>> findKShortestPaths(const Graph& graph, int start, int goal, int k) {
//    std::vector<std::vector<int>> shortestPaths;
//    // Get the first (shortest) path using A*.
//    std::vector<int> firstPath = aStarPath(graph, start, goal);
//    if (firstPath.empty())
//        return shortestPaths;
//    shortestPaths.push_back(firstPath);
//
//    // Container for candidate paths.
//    std::vector<std::vector<int>> candidates;
//
//    for (int kth = 1; kth < k; kth++) {
//        // Iterate over nodes in the previous shortest path (except the last node).
//        const std::vector<int>& previousPath = shortestPaths[kth - 1];
//        for (size_t i = 0; i < previousPath.size() - 1; i++) {
//            int spurNode = previousPath[i];
//            // The root path is the segment from start to spur node.
//            std::vector<int> rootPath(previousPath.begin(), previousPath.begin() + i + 1);
//
//            // Build a set of banned edges that have already been used by paths sharing this root.
//            std::unordered_set<std::pair<int, int>, PairHash> bannedEdges;
//            for (const auto& path : shortestPaths) {
//                if (path.size() > i && std::equal(rootPath.begin(), rootPath.end(), path.begin())) {
//                    bannedEdges.insert({ path[i], path[i + 1] });
//                }
//            }
//            // Find the spur path from the spur node to the goal while ignoring banned edges.
//            std::vector<int> spurPath = aStarPathModified(graph, spurNode, goal, bannedEdges);
//            if (spurPath.empty())
//                continue;
//            // Combine root and spur paths (avoiding duplicate spur node).
//            std::vector<int> totalPath = rootPath;
//            totalPath.insert(totalPath.end(), spurPath.begin() + 1, spurPath.end());
//            // Add candidate if it is not already present.
//            if (std::find(candidates.begin(), candidates.end(), totalPath) == candidates.end())
//                candidates.push_back(totalPath);
//        }
//        if (candidates.empty())
//            break;
//        // Select the candidate with the lowest cost.
//        auto bestCandidate = std::min_element(candidates.begin(), candidates.end(),
//            [&graph](const std::vector<int>& p1, const std::vector<int>& p2) {
//                return pathCost(graph, p1) < pathCost(graph, p2);
//            });
//        shortestPaths.push_back(*bestCandidate);
//        candidates.erase(bestCandidate);
//    }
//    return shortestPaths;
//}
//
//// Population class that generates the starting population using the coordinate-based k-shortest paths.
//class Population {
//public:
//    // Constructs the Population by generating 'populationSize' unique routes from start to destination.
//    Population(int populationSize, int startStation, int destinationStation, const Graph& graph)
//        : populationSize(populationSize), startStation(startStation), destinationStation(destinationStation)
//    {
//        generateInitialPopulation(graph);
//    }
//
//    const std::vector<Route>& getRoutes() const { return routes; }
//
//private:
//    int populationSize;
//    int startStation;
//    int destinationStation;
//    std::vector<Route> routes;
//
//    // Converts a vector of station IDs (the path) into a Route object.
//    // It retrieves the corresponding TransportationLine between each consecutive station.
//    Route createRouteFromStops(const Graph& graph, const std::vector<int>& stops) {
//        std::vector<Graph::TransportationLine> lines;
//        for (size_t i = 0; i < stops.size() - 1; i++) {
//            int current = stops[i];
//            int next = stops[i + 1];
//            const auto& neighbors = graph.getLinesFrom(current);
//            auto it = std::find_if(neighbors.begin(), neighbors.end(), [next](const Graph::TransportationLine& line) {
//                return line.to == next;
//                });
//            if (it != neighbors.end()) {
//                lines.push_back(*it);
//            }
//        }
//        return Route(lines);
//    }
//
//    // Generates the initial population of routes by computing up to 'populationSize' distinct paths.
//    void generateInitialPopulation(const Graph& graph) {
//        std::vector<std::vector<int>> paths = findKShortestPaths(graph, startStation, destinationStation, populationSize);
//        for (const auto& path : paths) {
//            Route route = createRouteFromStops(graph, path);
//            routes.push_back(route);
//        }
//    }
//};
