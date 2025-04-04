#pragma once

#include <vector>
#include "Graph.h"

class Route {
public:
    Route();
    explicit Route(const std::vector<Graph::Station>& stations, const std::vector<Graph::TransportationLine>& path);

    double getTotalTime() const;
    double getTotalCost() const;
    int getTransferCount() const;
    const std::vector<Graph::Station>& getStations() const;
    double getFitness() const;

    void mutate(double mutationRate);

private:
    std::vector<Graph::TransportationLine> _lines;
    std::vector<Graph::Station> _stations;
};
