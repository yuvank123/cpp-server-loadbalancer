
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <map>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <limits>

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

struct Backend
{
    const char *ip;
    int port;
    const char *name;
};

vector<Backend> servers = {
    {"127.0.0.1", 8081, "Server-1"},
    {"127.0.0.1", 8082, "Server-2"}
};

size_t currentServer = 0;
mutex mtx;

const size_t MAX_HEADER_SIZE = 64 * 1024;
const size_t MAX_BODY_SIZE = 10 * 1024 * 1024;

// Convert text to lowercase.
string toLower(string text)
{
    transform(text.begin(), text.end(), text.begin(),
              [](unsigned char c)
              {
                  return static_cast<char>(tolower(c));
              });

    return text;
}

// Parse the HTTP request line and headers.
// Header names are stored in lowercase.
// headerEnd includes the terminating CRLFCRLF.
bool parse_header(
    const string &request,
    map<string, string> &http_request,
    size_t &headerEnd)
{
    size_t separator = request.find("\r\n\r\n");

    if (separator == string::npos)
        return false;

    size_t lineEnd = request.find("\r\n");

    if (lineEnd == string::npos || lineEnd > separator)
        return false;

    string requestLine = request.substr(0, lineEnd);
    istringstream line(requestLine);

    if (!(line >> http_request["Type"]
               >> http_request["Path"]
               >> http_request["Version"]))
    {
        return false;
    }

    string extra;

    if (line >> extra)
        return false;

    if (http_request["Version"].rfind("HTTP/", 0) != 0)
        return false;

    size_t pos = lineEnd + 2;

    while (pos < separator)
    {
        lineEnd = request.find("\r\n", pos);

        if (lineEnd == string::npos || lineEnd > separator)
            return false;

        string header = request.substr(pos, lineEnd - pos);

        size_t colon = header.find(':');

        if (colon == string::npos || colon == 0)
            return false;

        string key = toLower(header.substr(0, colon));

        // Reject invalid whitespace in header names.
        if (key.find_first_of(" \t") != string::npos)
            return false;

        size_t valueStart = header.find_first_not_of(" \t", colon + 1);

        string value;

        if (valueStart != string::npos)
            value = header.substr(valueStart);

        // Reject duplicate headers in this basic implementation.
        if (http_request.count(key))
            return false;

        http_request[key] = value;

        pos = lineEnd + 2;
    }

    headerEnd = separator + 4;

    return true;
}

// Send all bytes, handling partial sends.
bool sendAll(SOCKET sock, const char *data, size_t length)
{
    size_t total = 0;

    while (total < length)
    {
        int amount = static_cast<int>(
            min(length - total,
                static_cast<size_t>(numeric_limits<int>::max())));

        int sent = send(sock, data + total, amount, 0);

        if (sent == SOCKET_ERROR || sent == 0)
            return false;

        total += static_cast<size_t>(sent);
    }

    return true;
}

// Connect to a backend server.
SOCKET connectToServer(const Backend &server)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (sock == INVALID_SOCKET)
        return INVALID_SOCKET;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(server.port));

    if (inet_pton(AF_INET, server.ip, &addr.sin_addr) != 1)
    {
        closesocket(sock);
        return INVALID_SOCKET;
    }

    if (connect(sock, reinterpret_cast<sockaddr *>(&addr),
                sizeof(addr)) == SOCKET_ERROR)
    {
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

// Send a simple HTTP error response.
void sendError(SOCKET client, int status, const string &message)
{
    string body = to_string(status) + " " + message + "\n";

    string response =
        "HTTP/1.1 " + to_string(status) + " " + message + "\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: " + to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body;

    sendAll(client, response.data(), response.size());
}

// Handle one browser connection.
void handleClient(SOCKET client)
{
    // Select a backend using round-robin.
    Backend server;

    {
        lock_guard<mutex> lock(mtx);

        server = servers[currentServer];
        currentServer = (currentServer + 1) % servers.size();
    }

    cout << "\nSelected " << server.name
         << " on port " << server.port << '\n';

    string request;
    char buffer[8192];

    // STEP 1: Receive complete HTTP headers.
    // Do not assume one recv() gets the whole header.

    size_t headerEnd = string::npos;

    while (true)
    {
        headerEnd = request.find("\r\n\r\n");

        if (headerEnd != string::npos)
        {
            headerEnd += 4;
            break;
        }

        if (request.size() >= MAX_HEADER_SIZE)
        {
            sendError(client, 431,
                      "Request Header Fields Too Large");

            closesocket(client);
            return;
        }

        size_t remaining = MAX_HEADER_SIZE - request.size();

        int amount = static_cast<int>(
            min(remaining, sizeof(buffer)));

        int received = recv(client, buffer, amount, 0);

        if (received <= 0)
        {
            closesocket(client);
            return;
        }

        request.append(buffer, static_cast<size_t>(received));
    }

    // STEP 2: Parse the request line and headers.

    map<string, string> http_request;
    size_t parsedHeaderEnd = 0;

    if (!parse_header(request, http_request, parsedHeaderEnd))
    {
        sendError(client, 400, "Bad Request");

        closesocket(client);
        return;
    }

    headerEnd = parsedHeaderEnd;

    cout << "Method: " << http_request["Type"] << '\n';
    cout << "Path: " << http_request["Path"] << '\n';
    cout << "Version: " << http_request["Version"] << '\n';

    for (const auto &header : http_request)
    {
        cout << header.first << ": "
             << header.second << '\n';
    }

    // STEP 3: Reject unsupported transfer encoding.
    // This implementation does not support chunked
    // request bodies.

    string transferEncoding =
        toLower(http_request["transfer-encoding"]);

    if (!transferEncoding.empty())
    {
        sendError(client, 501,
                  "Chunked Requests Not Supported");

        closesocket(client);
        return;
    }

 
    // STEP 4: Validate Content-Length.

    size_t contentLength = 0;

    if (http_request.count("content-length"))
    {
        const string &value = http_request["content-length"];

        if (value.empty() ||
            !all_of(value.begin(), value.end(),
                    [](unsigned char c)
                    {
                        return c >= '0' && c <= '9';
                    }))
        {
            sendError(client, 400, "Bad Request");

            closesocket(client);
            return;
        }

        for (char c : value)
        {
            size_t digit = static_cast<size_t>(c - '0');

            if (contentLength > (MAX_BODY_SIZE - digit) / 10)
            {
                sendError(client, 413, "Content Too Large");

                closesocket(client);
                return;
            }

            contentLength = contentLength * 10 + digit;
        }
    }

    // STEP 5: Receive the complete request body.
    // The first recv() may already have received some
    // or all of the body along with the headers.


    size_t expectedSize = headerEnd + contentLength;

    while (request.size() < expectedSize)
    {
        size_t remaining = expectedSize - request.size();

        int amount = static_cast<int>(
            min(remaining, sizeof(buffer)));

        int received = recv(client, buffer, amount, 0);

        if (received <= 0)
        {
            closesocket(client);
            return;
        }

        request.append(buffer, static_cast<size_t>(received));
    }

    // STEP 6: Connect to the selected backend.


    SOCKET backend = connectToServer(server);

    if (backend == INVALID_SOCKET)
    {
        sendError(client, 502, "Bad Gateway");

        closesocket(client);
        return;
    }

   
    // STEP 7: Forward the complete request.
    // sendAll() handles partial sends.
  
    if (!sendAll(backend, request.data(), expectedSize))
    {
        closesocket(backend);
        closesocket(client);
        return;
    }

  
    // STEP 8: Relay the backend response to the client.
    // This basic version reads until the backend closes.

    while (true)
    {
        int received = recv(backend, buffer, sizeof(buffer), 0);

        if (received <= 0)
            break;

        if (!sendAll(client, buffer,
                     static_cast<size_t>(received)))
        {
            break;
        }
    }

    closesocket(backend);
    closesocket(client);

    cout << "Completed via " << server.name << '\n';
}

int main()
{
    WSADATA wsa{};

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (listener == INVALID_SOCKET)
    {
        cerr << "Socket creation failed: "
             << WSAGetLastError() << '\n';

        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listener, reinterpret_cast<sockaddr *>(&addr),
             sizeof(addr)) == SOCKET_ERROR)
    {
        cerr << "Bind failed: "
             << WSAGetLastError() << '\n';

        closesocket(listener);
        WSACleanup();
        return 1;
    }

    if (listen(listener, SOMAXCONN) == SOCKET_ERROR)
    {
        cerr << "Listen failed: "
             << WSAGetLastError() << '\n';

        closesocket(listener);
        WSACleanup();
        return 1;
    }

    cout << "Load balancer running at http://localhost:8080\n";

    while (true)
    {
        SOCKET client = accept(listener, nullptr, nullptr);

        if (client == INVALID_SOCKET)
        {
            cerr << "Accept failed: "
                 << WSAGetLastError() << '\n';

            continue;
        }

        thread(handleClient, client).detach();
    }

    closesocket(listener);
    WSACleanup();

    return 0;
}
