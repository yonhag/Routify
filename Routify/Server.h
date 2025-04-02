#pragma once
#include <thread>
#include <vector>
#include "RequestHandler.h"
#include "Socket.h"  // Include the Socket class

class Server {
public:
    // Constructor takes the port number.
    Server(int port);

    // Destructor cleans up resources.
    virtual ~Server();

    // Start the server.
    void start();

private:
    // Our server socket wrapped in the Socket class.
    Socket serverSocket;
    int port;
    bool running;
    std::vector<std::thread> threads;
    RequestHandler handler;

    // Internal functions to structure the server setup.
    bool initSocket();
    bool bindSocket();
    bool listenSocket();
    void acceptConnections();
};