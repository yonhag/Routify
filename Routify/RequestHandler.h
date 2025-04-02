#pragma once
#include "Graph.h"
#include "Socket.h"

class RequestHandler {
public:
	RequestHandler();
	void handleRequest(Socket clientSocket);
private:
	Graph _graph;
};