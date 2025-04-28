#pragma once
#include "Socket.h"
#include "RequestHandler.h"
#include <thread>

class Server {
public:
    // Constructor takes the port number.
    explicit Server(const int port);

    // Destructor cleans up resources.
    virtual ~Server();

    // Start the server.
    void start();

private:
    bool initSocket() const;
    bool bindSocket() const;
    bool listenSocket() const;
    void acceptConnections();

    Socket serverSocket;
    int port;
    bool running;
    std::vector<std::thread> threads;
    RequestHandler handler;
};