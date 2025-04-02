#include "RequestHandler.h"
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

    json j = json::parse(received);
    int type = j["type"];
    switch (type) {
    case 0:
        for (const Graph::TransportationLine& line : this->_graph.getLinesFrom(int(j["stationId"]))) {
            response.append("ID: " + (line.id) + ", To: " + std::to_string(line.to) + ", " + _graph.getStationById(line.to)->name + "\n");
        }
        break;
    case 1:
        const Graph::Station* st = this->_graph.getStationById(j["stationId"]);
        json k;
        if (!st)
        {
            k = "Error";
            break;
        }
        k = *st;
        response.append(k.dump());
        break;
    }

    clientSocket.sendMessage(response);
}
