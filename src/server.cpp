// To learn more about winsock programming you can visit
// https://learn.microsoft.com/en-us/windows/win32/winsock/complete-server-code
// https://learn.microsoft.com/en-us/windows/win32/winsock/complete-client-code
// https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/WinSock/creating-a-basic-winsock-application.md
// https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/WinSock/binding-a-socket.md
#include <iostream>
#include <string>
#include <cstring>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

bool sendAll(SOCKET clientSocket, const std::string &data)
{
    size_t totalSent = 0;

    while (totalSent < data.size())
    {
        int sent = send(
            clientSocket,
            data.c_str() + totalSent,
            static_cast<int>(data.size() - totalSent),
            0);

        if (sent == SOCKET_ERROR || sent == 0)
        {
            return false;
        }

        totalSent += sent;
    }

    return true;
}

void handleClient(SOCKET clientSocket)
{
    char buffer[4096];

    int bytesReceived = recv(
        clientSocket,
        buffer,
        sizeof(buffer),
        0);

    if (bytesReceived <= 0)
    {
        std::cerr << "Failed to receive request.\n";
        closesocket(clientSocket);
        return;
    }

    std::string request(buffer, bytesReceived);

    std::cout << "\n========== Incoming Request ==========\n";
    std::cout << request;
    std::cout << "\n======================================\n";

    // Simple HTTP response
    const std::string body =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<title>My C++ Server</title>"
        "</head>"
        "<body>"
        "<h1>Hello from my C++ server!</h1>"
        "<p>Your request reached the server successfully.</p>"
        "</body>"
        "</html>";

    const std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: " +
        std::to_string(body.size()) + "\r\n"
                                      "Connection: close\r\n"
                                      "\r\n" +
        body;

    if (sendAll(clientSocket, response))
    {
        std::cout << "HTTP response sent successfully.\n";
    }
    else
    {
        std::cerr << "Failed to send complete response.\n";
    }

    shutdown(clientSocket, SD_SEND);
    closesocket(clientSocket);
}

int main(int argc, char* argv[])
{   
    //N server creation
    if (argc != 3)
    {
        std::cerr << "Usage: server.exe <port> <name>\n";
        return 1;
    }

    int port = std::stoi(argv[1]); //hold's the port
    std::string serverName = argv[2]; //hold's the servername


    // 1. Initialize Winsock
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    // 2. Create TCP socket
    SOCKET serverSocket = socket(
        AF_INET,     // AF_INET is used to specify the IPv4 address family
        SOCK_STREAM, // SOCK_STREAM is used to specify a stream socket
        IPPROTO_TCP  // IPPROTO_TCP is used to specify the TCP protocol
    );

    if (serverSocket == INVALID_SOCKET)
    {
        std::cerr << "Socket creation failed: "
                  << WSAGetLastError() << '\n';
        WSACleanup();
        return 1;
    }
    /*
     sockaddr_in is a storage for the address of a socket. It is an
     IPv4-specific structure that contain an IP address and a port number and other information about the socket.
     At present we are actually using sockaddr_in to store socket address into it.

    struct sockaddr {
         ushort  sa_family;
         char    sa_data[14];
    };

    struct sockaddr_in {                    #We are using this one 
         short   sin_family;
         u_short sin_port;
         struct  in_addr sin_addr;
         char    sin_zero[8];
    };
    */
    // 3. Configure server address
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port); //hton is host to network short which convert the host byte order to network byte order (little endian to big endian)
    serverAddress.sin_addr.s_addr =
        htonl(INADDR_LOOPBACK);
 

    
    // 4. Bind socket to localhost:8080
    if (bind(
            serverSocket,
            reinterpret_cast<sockaddr *>(&serverAddress),
            sizeof(serverAddress)) == SOCKET_ERROR)
    {
        std::cerr << "Bind failed: "
                  << WSAGetLastError() << '\n';

        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }


    // 5. Listen for connections
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "Listen failed: "
                  << WSAGetLastError() << '\n';

        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "=====================================\n";
    std::cout << "C++ HTTP server is running!\n";
    std::cout << "Open: http://localhost:" <<port<< "\n";
    std::cout << "Press Ctrl+C to stop the server.\n";
    std::cout << "=====================================\n";

    // 6. Accept multiple successive connections
    while (true)
    {
        SOCKET clientSocket = accept(
            serverSocket,
            nullptr,
            nullptr);

        if (clientSocket == INVALID_SOCKET)
        {
            std::cerr << "Accept failed: "
                      << WSAGetLastError() << '\n';
            continue;
        }

        std::cout << "\nClient connected!\n";
        handleClient(clientSocket);
    }

    // Normally unreachable unless the loop is changed
    closesocket(serverSocket);
    WSACleanup();

    return 0;
}
