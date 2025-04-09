#pragma once
#include "Graph.h"
#include "Socket.h"
#include "json.hpp"
using json = nlohmann::json;


class RequestHandler {
public:
	RequestHandler();
	void handleRequest(Socket clientSocket);

private:
	json handleGetLines(const json& request_json);
	json handleFindRoute(const json& request_json);
	json handleGetStationInfo(const json& request_json);


	Graph _graph;
};