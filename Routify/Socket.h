#pragma once

#include <WinSock2.h>
#include <string>

class Socket {
public:
    Socket();

    Socket(SOCKET socketDescriptor);

    ~Socket();

    // Check if the socket is valid.
    bool isValid() const;

    // Send a message over the socket.
    bool sendMessage(const std::string& message);

    // Receive a message from the socket.
    // The bufferSize parameter sets the maximum number of bytes to receive.
    std::string receiveMessage(int bufferSize = 1024);

    // Retrieve the underlying socket descriptor.
    SOCKET getSocketDescriptor() const;

    // Close the socket.
    void closeSocket();

private:
    int sockfd;
    bool ownsSocket;
};
