#pragma once
#include "Socket.h"
#include "RequestHandler.h"
#include <thread>

class Server {
public:
    explicit Server(const int port);

    virtual ~Server();

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