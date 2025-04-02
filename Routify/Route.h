#pragma once

#include <vector>
#include "Graph.h"

class Route {
public:
    explicit Route(int start, const std::vector<Graph::TransportationLine>& path);

    double getTotalTime() const;
    double getTotalCost() const;
    int getTransferCount() const;
    const std::vector<int>& getStops() const;
    double getFitness() const;

    void mutate(double mutationRate);

private:
    int _start;
    std::vector<Graph::TransportationLine> _lines;
    std::vector<int> _stops;
};
