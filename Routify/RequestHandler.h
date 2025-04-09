#pragma once
#include "Graph.h"
#include "Socket.h"
#include "json.hpp"
using json = nlohmann::json;


class RequestHandler {
public:
	RequestHandler();
	void handleRequest(Socket clientSocket);
	std::string findBestRoutes(const json& request);
private:
	Graph _graph;
};