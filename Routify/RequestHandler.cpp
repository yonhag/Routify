#include "RequestHandler.h"
#include "Population.h"
#include <iostream>
#include "json.hpp"

using json = nlohmann::json;

static void to_json(json& j, const Graph::Station& p) {
    j = json{ {"lat", p.latitude}, {"long", p.longitude}, { "name", p.name } };
}

RequestHandler::RequestHandler() : _graph()
{
}

void RequestHandler::handleRequest(Socket clientSocket)
{
    std::string response;
    std::string received = clientSocket.receiveMessage();
    std::cout << "Received: " << received << std::endl;

    if (received.empty())
    {
        clientSocket.sendMessage("Error");
        return;
    }

    json j = json::parse(received);
    int type = j["type"];
    switch (type) {
    case 0:
        for (const Graph::TransportationLine& line : this->_graph.getLinesFrom(int(j["stationId"]))) {
            response.append("ID: " + (line.id) + ", To: " + std::to_string(line.to) + ", " + _graph.getStationById(line.to).name + "\n");
        }
        break;
    case 1: {
        json k;
        try {
            const Graph::Station& st = this->_graph.getStationById(j["stationId"]);
            k = st;
        }
        catch (std::exception) {
            response = "Error";
        }
        response.append(k.dump());
        break;
    }
    case 2: {
        // Create a population with 5 routes from startStationId to destStationId.
        Population pop(5, int(j["startStationId"]), int(j["destStationId"]), this->_graph);

        auto routes = pop.getRoutes();
        for (const auto& i : routes) {

            std::cout << _graph.getStationIdByName(i.getVisitedStations()[0].station.name) << " To " << _graph.getStationIdByName(i.getVisitedStations()[i.getVisitedStations().size() - 1].station.name) << std::endl;
        }

        pop.evolve(int(j["gen"]), int(j["mut"]));

        response.append("From: " + this->_graph.getStationById(int(j["startStationId"])).name + "\nTo: " + this->_graph.getStationById(int(j["destStationId"])).name + "\n");

        // Get the best route found
        const Route& bestRoute = pop.getBestSolution();
        auto visitedStations = bestRoute.getVisitedStations();

        if (visitedStations.empty()) {
            response = "No route found.";
            break;
        }

        size_t startIndex = 0;
        for (size_t i = 1; i <= visitedStations.size(); i++) {
            // When reaching the end of the vector or when the line changes, finalize the current segment.
            if (i == visitedStations.size() || visitedStations[i].line.id != visitedStations[i - 1].line.id) {
                std::string lineId = visitedStations[startIndex].line.id;
                std::string departureStationName = visitedStations[startIndex].station.name;
                // Get the arrival station using the destination station ID from the last leg in the segment.
                int destId = visitedStations[i - 1].line.to;
                std::string arrivalStationName = this->_graph.getStationById(destId).name;

                response.append("Take line " + lineId + " from " + departureStationName + " until " + arrivalStationName + ".\n");
                // If there is another segment, indicate that a transfer occurs.
                if (i < visitedStations.size()) {
                    response.append("Transfer at " + arrivalStationName + ".\n");
                }
                startIndex = i;
            }
        }
        break;
    }
    }


    std::cout << response << std::endl;

    clientSocket.sendMessage(response);
}
