//#include "Route.h"
//#include <cstdlib>
//#include <ctime>
//#include <algorithm>
//
//Route::Route(const std::vector<Graph::Station>& stations, const std::vector<Graph::TransportationLine>& lines)
//    : _stations(stations), _lines(lines)
//{
//}
//
//double Route::getTotalTime() const {
//    double totalTime = 0.0;
//    for (const auto& edge : _lines) {
//        totalTime += edge.travelTime;
//    }
//    return totalTime;
//}
//
//double Route::getTotalCost() const {
//    double totalCost = 0.0;
//    for (const auto& edge : _lines)
//        totalCost += edge.price;
//    return totalCost;
//}
//
//int Route::getTransferCount() const {
//    if (_lines.empty()) return 0;
//    int transfers = 0;
//    for (size_t i = 1; i < _lines.size(); ++i) {
//        if (_lines[i].type != _lines[i - 1].type)
//            transfers++;
//    }
//    return transfers;
//}
//
//const std::vector<Graph::Station>& Route::getStations() const {
//    return this->_stations;
//}
//
//double Route::getFitness() const {
//    double totalTime = getTotalTime();
//    double totalCost = getTotalCost();
//    int transfers = getTransferCount();
//    double penaltyPerTransfer = 10.0;
//    double score = totalTime + totalCost + transfers * penaltyPerTransfer;
//    return (score > 0) ? 1.0 / score : 0.0;
//}
//
//void Route::mutate(double mutationRate) {
//    std::srand(static_cast<unsigned>(std::time(nullptr)));
//
//    if (_stops.size() <= 2) return;
//
//    double r = static_cast<double>(std::rand()) / RAND_MAX;
//    if (r < mutationRate) {
//        int minIndex = 1;
//        int maxIndex = static_cast<int>(_stops.size()) - 2;
//        int i = minIndex + std::rand() % (maxIndex - minIndex + 1);
//        int j = minIndex + std::rand() % (maxIndex - minIndex + 1);
//        while (j == i) {
//            j = minIndex + std::rand() % (maxIndex - minIndex + 1);
//        }
//        std::swap(_stops[i], _stops[j]);
//    }
//}
