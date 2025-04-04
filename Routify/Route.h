//#pragma once
//
//#include <vector>
//#include "Graph.h"
//
//class Route {
//public:
//    explicit Route(const std::vector<Graph::Station>& stations, const std::vector<Graph::TransportationLine>& path);
//
//    double getTotalTime() const;
//    double getTotalCost() const;
//    int getTransferCount() const;
//    const std::vector<int>& getStations() const;
//    double getFitness() const;
//
//    void mutate(double mutationRate);
//
//private:
//    std::vector<Graph::TransportationLine> _lines;
//    std::vector<Graph::Station> _stations;
//};
