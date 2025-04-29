#include "Graph.h"  
#include "Server.h"  
#include <iostream>  
#include <chrono>  

int main() {  
	SetConsoleOutputCP(CP_UTF8);  
	auto now = std::chrono::system_clock::now().time_since_epoch();  
	auto seed = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::seconds>(now).count());  
	std::mt19937 randomGenerator(seed);
	Server server(8200);  
	server.start();  
}