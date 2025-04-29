#include "Server.h"
#include <iostream>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h>


Server::Server(const int port)
    : serverSocket(),
    port(port),
    running(false)
{
}

Server::~Server() {
    running = false;
    // Close the server socket to unblock any waiting accept call.
    serverSocket.closeSocket();
    // Wait for all threads to finish.
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    WSACleanup();
}

bool Server::initSocket() const {
    if (!serverSocket.isValid()) {
        std::cerr << "Failed to create server socket." << std::endl;
        return false;
    }
    int opt = 1;
    if (setsockopt(serverSocket.getSocketDescriptor(), SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0) {
        std::cerr << "setsockopt failed" << std::endl;
        return false;
    }
    return true;
}

bool Server::bindSocket() const {
    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket.getSocketDescriptor(), reinterpret_cast<sockaddr*>(&serverAddr),
        sizeof(serverAddr)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        return false;
    }
    return true;
}

bool Server::listenSocket() const {
    if (listen(serverSocket.getSocketDescriptor(), 10) < 0) {
        std::cerr << "Listen failed" << std::endl;
        return false;
    }
    std::cout << "Server is listening on port " << port << "..." << std::endl;
    return true;
}

void Server::acceptConnections() {
    while (running) {
        sockaddr_in clientAddr;
        int clientAddrSize = sizeof(clientAddr);
        // Accept an incoming connection
        SOCKET clientDescriptor = accept(serverSocket.getSocketDescriptor(),
            reinterpret_cast<sockaddr*>(&clientAddr),
            &clientAddrSize);
        if (clientDescriptor == INVALID_SOCKET) {
            int error = WSAGetLastError();
            if (!running) {
                break;
            }
            // If no connection is pending, sleep briefly to avoid a tight loop.
            if (error == WSAEWOULDBLOCK) {
                continue;
            }
            std::cerr << "Accept failed with error: " << error << std::endl;
            Sleep(10);
            continue;
        }
        std::cout << "Accepted connection from " << inet_ntoa(clientAddr.sin_addr) << std::endl;

        // Wrap the accepted socket in our Socket class.
        Socket clientSocket(clientDescriptor);

        // Spawn a new thread to handle the client.
        threads.emplace_back(std::thread(&RequestHandler::handleRequest, handler, clientSocket));
    }
}

void Server::start() {
    if (!initSocket())
        return;
    if (!bindSocket())
        return;
    if (!listenSocket())
        return;

    running = true;
    acceptConnections();
}
