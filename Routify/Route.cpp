#include "Route.h"
#include <cstdlib>
#include <ctime>
#include <algorithm>

Route::Route() : _stations()
{
}

Route::Route(const std::vector<Graph::Station>& stations, const std::vector<Graph::TransportationLine>& lines)
{
    for (int i = 0; i < stations.size() && i < lines.size(); i++) {
        this->_stations.push_back(VisitedStation(stations[i], lines[i]));
    }
}

void Route::addVisitedStation(const VisitedStation& vs)
{
    this->_stations.push_back(vs);
}

double Route::getTotalTime() const {
    double totalTime = 0.0;
    for (const auto& edge : this->_stations) {
        totalTime += edge.line.travelTime;
    }
    return totalTime;
}

double Route::getTotalCost() const {
    double totalCost = 0.0;
    for (const auto& edge : this->_stations)
        totalCost += edge.line.price;
    return totalCost;
}

int Route::getTransferCount() const {
    if (this->_stations.empty()) return 0;

    int transfers = 0;
    for (size_t i = 1; i < this->_stations.size(); ++i)
        if (this->_stations[i].line.id != this->_stations[i - 1].line.id)
            transfers++;

    return transfers;
}

const std::vector<Route::VisitedStation> Route::getVisitedStations() const
{
    return this->_stations;
}

double Route::getFitness() const {
    double totalTime = getTotalTime();
    double totalCost = getTotalCost();
    int transfers = getTransferCount();
    double penaltyPerTransfer = 10.0;
    double score = totalTime + totalCost + transfers * penaltyPerTransfer;
    return (score > 0) ? 1.0 / score : 0.0;
}

void Route::mutate(double mutationRate) {
    // Ensure route has at least three stations: source, at least one intermediate, destination.
    if (this->_stations.size() <= 2) return;

    // Generate a random probability.
    double r = static_cast<double>(std::rand()) / RAND_MAX;
    if (r < mutationRate) {
        int minIndex = 1;
        int maxIndex = this->_stations.size() - 2; // leave the last station intact
        int i = minIndex + std::rand() % (maxIndex - minIndex + 1);
        int j = minIndex + std::rand() % (maxIndex - minIndex + 1);
        while (j == i) {
            j = minIndex + std::rand() % (maxIndex - minIndex + 1);
        }
        std::swap(this->_stations[i], this->_stations[j]);
    }
}

