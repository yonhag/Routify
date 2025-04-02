#include "Graph.h"
#include "Server.h"
#include <iostream>

int main() {
	Server server(8200);
	server.start();
}