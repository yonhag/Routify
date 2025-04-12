#include "Socket.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>  // For memset
#include <iostream>

#pragma comment(lib, "Ws2_32.lib")

Socket::Socket() : sockfd(INVALID_SOCKET), ownsSocket(true) {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed with error: " << result << std::endl;
    }
    // Create a TCP socket.
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == INVALID_SOCKET) {
        std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
    }
}

Socket::Socket(SOCKET socketDescriptor) : sockfd(socketDescriptor), ownsSocket(false) {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed with error: " << result << std::endl;
    }
}

Socket::~Socket() {
    if (sockfd != INVALID_SOCKET && ownsSocket) {
        closeSocket();
    }
}

bool Socket::isValid() const {
    return sockfd != INVALID_SOCKET;
}

bool Socket::sendMessage(const std::string& message) const {
    if (!isValid())
        return false;
    int sent = send(sockfd, message.c_str(), static_cast<int>(message.size()), 0);
    return sent != SOCKET_ERROR;
}

std::string Socket::receiveMessage(const int bufferSize) const {
    if (!isValid())
        return "";
    char* buffer = new char[bufferSize + 1];
    std::memset(buffer, 0, bufferSize + 1);
    int bytesReceived = recv(sockfd, buffer, bufferSize, 0);
    std::string message;
    if (bytesReceived > 0) {
        message = std::string(buffer, bytesReceived);
    }
    delete[] buffer;
    return message;
}

SOCKET Socket::getSocketDescriptor() const {
    return sockfd;
}

void Socket::closeSocket() {
    if (sockfd == INVALID_SOCKET)
        return;
    closesocket(sockfd);
    sockfd = INVALID_SOCKET;
}
