#pragma once

#include <vector>
#include "Graph.h"

class Route {
public:
    struct VisitedStation {
        Graph::Station station;
        Graph::TransportationLine line;
    };
    
    Route();
    explicit Route(const std::vector<Graph::Station>& stations, const std::vector<Graph::TransportationLine>& path);

    void addVisitedStation(const VisitedStation& vs);
    double getTotalTime() const;
    double getTotalCost() const;
    int getTransferCount() const;
    const std::vector<VisitedStation> getVisitedStations() const;
    double getFitness() const;

    void mutate(double mutationRate);

private:
    std::vector<VisitedStation> _stations;
};
