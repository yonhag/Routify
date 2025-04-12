#pragma once
#include "Socket.h"
#include "RequestHandler.h"
#include <thread>

class Server {
public:
    // Constructor takes the port number.
    Server(const int port);

    // Destructor cleans up resources.
    virtual ~Server();

    // Start the server.
    void start();

private:
    // Server socket wrapped in the Socket class.
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