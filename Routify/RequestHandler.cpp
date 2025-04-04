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
    case 1:
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

    std::cout << response << std::endl;

    clientSocket.sendMessage(response);
}
