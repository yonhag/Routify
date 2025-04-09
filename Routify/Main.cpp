#include "Graph.h"
#include "Server.h"
#include <iostream>

int main() {
	SetConsoleOutputCP(CP_UTF8);
	std::srand(static_cast<unsigned>(std::time(nullptr)));
	Server server(8200);
	server.start();
}