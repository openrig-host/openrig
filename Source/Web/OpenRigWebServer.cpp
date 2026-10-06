#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#endif

#include "OpenRigWebServer.h"
#include "EmbeddedWebAssets.h"
#include "../FanfareEngine.h"
#include "../MainComponent.h"
#include "../SetlistManager.h"
#include "../YoutubeDownloadManager.h"
#include <sstream>

#if JUCE_WINDOWS
  typedef SOCKET SocketType;
  #define INVALID_SOCKET_VAL INVALID_SOCKET
  #define SOCKET_CLOSE(s) closesocket(s)
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  typedef int SocketType;
  #define INVALID_SOCKET_VAL -1
  #define SOCKET_CLOSE(s) close(s)
#endif

namespace OpenRig {

static const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

WebServer::WebServer(FanfareEngine& eng, MainComponent* mc)
    : juce::Thread("OpenRigWebServerThread"), engine(eng), mainComponent(mc) {
#if JUCE_WINDOWS
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

WebServer::~WebServer() {
    stopServer();
#if JUCE_WINDOWS
    WSACleanup();
#endif
}

bool WebServer::startServer(int port) {
    if (serverRunning.load())
        stopServer();

    serverPort = port;

    SocketType sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET_VAL) {
        return false;
    }

    int opt = 1;
#if JUCE_WINDOWS
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons((u_short)port);

    if (::bind(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        SOCKET_CLOSE(sock);
        return false;
    }

    if (::listen(sock, 16) < 0) {
        SOCKET_CLOSE(sock);
        return false;
    }

    listenSocket = (void*)(uintptr_t)sock;
    serverRunning.store(true);

    startThread(juce::Thread::Priority::normal);
    startTimerHz(30); // 30fps real-time VU and telemetry stream

    return true;
}

void WebServer::stopServer() {
    serverRunning.store(false);
    stopTimer();

    if (listenSocket != nullptr) {
        SocketType s = (SocketType)(uintptr_t)listenSocket;
        SOCKET_CLOSE(s);
        listenSocket = nullptr;
    }

    // Close all connected client sockets
    {
        std::lock_guard<std::mutex> lock(clientSocketsMutex);
        for (void* client : activeWsClients) {
            SocketType cs = (SocketType)(uintptr_t)client;
            SOCKET_CLOSE(cs);
        }
        activeWsClients.clear();
    }

    stopThread(2000);
}

juce::StringArray WebServer::getLocalIpAddresses() {
    juce::StringArray ips;
    auto addrs = juce::IPAddress::getAllAddresses();
    for (const auto& addr : addrs) {
        juce::String s = addr.toString();
        if (!addr.isNull() && s != "127.0.0.1" && s.contains(".")) {
            ips.add(s);
        }
    }
    if (ips.isEmpty()) {
        ips.add("127.0.0.1");
    }
    return ips;
}

juce::String WebServer::getPrimaryUrl(int port) {
    auto ips = getLocalIpAddresses();
    return "http://" + ips[0] + ":" + juce::String(port);
}

void WebServer::run() {
    SocketType lSock = (SocketType)(uintptr_t)listenSocket;

    while (!threadShouldExit() && serverRunning.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(lSock, &readSet);

        timeval tv{ 0, 100000 }; // 100ms timeout
        int ret = ::select((int)lSock + 1, &readSet, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(lSock, &readSet)) {
            sockaddr_in clientAddr{};
#if JUCE_WINDOWS
            int clientLen = sizeof(clientAddr);
#else
            socklen_t clientLen = sizeof(clientAddr);
#endif
            SocketType clientSock = ::accept(lSock, (struct sockaddr*)&clientAddr, &clientLen);
            if (clientSock != INVALID_SOCKET_VAL) {
                // Spawn a detached worker thread for each incoming connection
                std::thread([this, clientSock]() {
                    handleClientConnection((void*)(uintptr_t)clientSock);
                }).detach();
            }
        }
    }
}

void WebServer::handleClientConnection(void* socketHandle) {
    SocketType sock = (SocketType)(uintptr_t)socketHandle;
    std::vector<char> buffer(8192, 0);

    int bytesRead = ::recv(sock, buffer.data(), (int)buffer.size() - 1, 0);
    if (bytesRead <= 0) {
        SOCKET_CLOSE(sock);
        return;
    }

    buffer[bytesRead] = '\0';
    juce::String requestText(buffer.data());

    // Parse HTTP request line
    int firstLineEnd = requestText.indexOf("\r\n");
    if (firstLineEnd <= 0) firstLineEnd = requestText.indexOf("\n");
    if (firstLineEnd <= 0) {
        SOCKET_CLOSE(sock);
        return;
    }

    juce::String requestLine = requestText.substring(0, firstLineEnd);
    auto tokens = juce::StringArray::fromTokens(requestLine, " ", "");
    if (tokens.size() < 2) {
        SOCKET_CLOSE(sock);
        return;
    }

    juce::String method = tokens[0].toUpperCase();
    juce::String rawPath = tokens[1];
    int qIdx = rawPath.indexOf("?");
    juce::String path = (qIdx >= 0) ? rawPath.substring(0, qIdx) : rawPath;

    // Check for WebSocket Upgrade
    if (requestText.containsIgnoreCase("Upgrade: websocket")) {
        // Extract Sec-WebSocket-Key
        juce::String secKey = "";
        int keyPos = requestText.indexOfIgnoreCase("Sec-WebSocket-Key:");
        if (keyPos >= 0) {
            int valStart = keyPos + 18;
            int valEnd = requestText.indexOf(valStart, "\r\n");
            if (valEnd < 0) valEnd = requestText.indexOf(valStart, "\n");
            if (valEnd > valStart) {
                secKey = requestText.substring(valStart, valEnd).trim();
            }
        }

        if (secKey.isNotEmpty() && handleWebSocketHandshake(socketHandle, secKey)) {
            handleWebSocketSession(socketHandle);
            return;
        }
    }

    // Standard HTTP Request
    int headerEnd = requestText.indexOf("\r\n\r\n");
    juce::String body = "";
    if (headerEnd >= 0) {
        body = requestText.substring(headerEnd + 4);
    }

    handleHttpRequest(socketHandle, method, path, requestText, body);
    SOCKET_CLOSE(sock);
}

namespace Sha1Detail {
    inline uint32_t rol(uint32_t value, size_t bits) { return (value << bits) | (value >> (32 - bits)); }
    inline uint32_t blk(const uint32_t block[16], size_t i) {
        return rol(block[(i + 13) & 15] ^ block[(i + 8) & 15] ^ block[(i + 2) & 15] ^ block[i], 1);
    }
    inline void r0(const uint32_t block[16], uint32_t v, uint32_t &w, uint32_t x, uint32_t y, uint32_t &z, size_t i) {
        z += ((w & (x ^ y)) ^ y) + block[i] + 0x5a827999 + rol(v, 5);
        w = rol(w, 30);
    }
    inline void r1(uint32_t block[16], uint32_t v, uint32_t &w, uint32_t x, uint32_t y, uint32_t &z, size_t i) {
        block[i] = blk(block, i);
        z += ((w & (x ^ y)) ^ y) + block[i] + 0x5a827999 + rol(v, 5);
        w = rol(w, 30);
    }
    inline void r2(uint32_t block[16], uint32_t v, uint32_t &w, uint32_t x, uint32_t y, uint32_t &z, size_t i) {
        block[i] = blk(block, i);
        z += (w ^ x ^ y) + block[i] + 0x6ed9eba1 + rol(v, 5);
        w = rol(w, 30);
    }
    inline void r3(uint32_t block[16], uint32_t v, uint32_t &w, uint32_t x, uint32_t y, uint32_t &z, size_t i) {
        block[i] = blk(block, i);
        z += (((w | x) & y) | (w & x)) + block[i] + 0x8f1bbcdc + rol(v, 5);
        w = rol(w, 30);
    }
    inline void r4(uint32_t block[16], uint32_t v, uint32_t &w, uint32_t x, uint32_t y, uint32_t &z, size_t i) {
        block[i] = blk(block, i);
        z += (w ^ x ^ y) + block[i] + 0xca62c1d6 + rol(v, 5);
        w = rol(w, 30);
    }

    inline void transform(uint32_t digest[5], uint32_t block[16]) {
        uint32_t a = digest[0], b = digest[1], c = digest[2], d = digest[3], e = digest[4];
        r0(block, a, b, c, d, e, 0); r0(block, e, a, b, c, d, 1); r0(block, d, e, a, b, c, 2); r0(block, c, d, e, a, b, 3);
        r0(block, b, c, d, e, a, 4); r0(block, a, b, c, d, e, 5); r0(block, e, a, b, c, d, 6); r0(block, d, e, a, b, c, 7);
        r0(block, c, d, e, a, b, 8); r0(block, b, c, d, e, a, 9); r0(block, a, b, c, d, e, 10); r0(block, e, a, b, c, d, 11);
        r0(block, d, e, a, b, c, 12); r0(block, c, d, e, a, b, 13); r0(block, b, c, d, e, a, 14); r0(block, a, b, c, d, e, 15);
        r1(block, e, a, b, c, d, 0); r1(block, d, e, a, b, c, 1); r1(block, c, d, e, a, b, 2); r1(block, b, c, d, e, a, 3);
        r2(block, a, b, c, d, e, 4); r2(block, e, a, b, c, d, 5); r2(block, d, e, a, b, c, 6); r2(block, c, d, e, a, b, 7);
        r2(block, b, c, d, e, a, 8); r2(block, a, b, c, d, e, 9); r2(block, e, a, b, c, d, 10); r2(block, d, e, a, b, c, 11);
        r2(block, c, d, e, a, b, 12); r2(block, b, c, d, e, a, 13); r2(block, a, b, c, d, e, 14); r2(block, e, a, b, c, d, 15);
        r2(block, d, e, a, b, c, 0); r2(block, c, d, e, a, b, 1); r2(block, b, c, d, e, a, 2); r2(block, a, b, c, d, e, 3);
        r2(block, e, a, b, c, d, 4); r2(block, d, e, a, b, c, 5); r2(block, c, d, e, a, b, 6); r2(block, b, c, d, e, a, 7);
        r3(block, a, b, c, d, e, 8); r3(block, e, a, b, c, d, 9); r3(block, d, e, a, b, c, 10); r3(block, c, d, e, a, b, 11);
        r3(block, b, c, d, e, a, 12); r3(block, a, b, c, d, e, 13); r3(block, e, a, b, c, d, 14); r3(block, d, e, a, b, c, 15);
        r3(block, c, d, e, a, b, 0); r3(block, b, c, d, e, a, 1); r3(block, a, b, c, d, e, 2); r3(block, e, a, b, c, d, 3);
        r3(block, d, e, a, b, c, 4); r3(block, c, d, e, a, b, 5); r3(block, b, c, d, e, a, 6); r3(block, a, b, c, d, e, 7);
        r3(block, e, a, b, c, d, 8); r3(block, d, e, a, b, c, 9); r3(block, c, d, e, a, b, 10); r3(block, b, c, d, e, a, 11);
        r4(block, a, b, c, d, e, 12); r4(block, e, a, b, c, d, 13); r4(block, d, e, a, b, c, 14); r4(block, c, d, e, a, b, 15);
        r4(block, b, c, d, e, a, 0); r4(block, a, b, c, d, e, 1); r4(block, e, a, b, c, d, 2); r4(block, d, e, a, b, c, 3);
        r4(block, c, d, e, a, b, 4); r4(block, b, c, d, e, a, 5); r4(block, a, b, c, d, e, 6); r4(block, e, a, b, c, d, 7);
        r4(block, d, e, a, b, c, 8); r4(block, c, d, e, a, b, 9); r4(block, b, c, d, e, a, 10); r4(block, a, b, c, d, e, 11);
        r4(block, e, a, b, c, d, 12); r4(block, d, e, a, b, c, 13); r4(block, c, d, e, a, b, 14); r4(block, b, c, d, e, a, 15);
        digest[0] += a; digest[1] += b; digest[2] += c; digest[3] += d; digest[4] += e;
    }

    inline std::vector<uint8_t> compute(const std::string& input) {
        uint32_t digest[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };
        uint64_t bitLength = input.size() * 8;
        std::vector<uint8_t> data(input.begin(), input.end());
        data.push_back(0x80);
        while ((data.size() % 64) != 56) data.push_back(0x00);
        for (int i = 7; i >= 0; --i) data.push_back((uint8_t)((bitLength >> (i * 8)) & 0xFF));

        for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
            uint32_t block[16];
            for (size_t i = 0; i < 16; ++i) {
                block[i] = ((uint32_t)data[chunk + i * 4] << 24) |
                           ((uint32_t)data[chunk + i * 4 + 1] << 16) |
                           ((uint32_t)data[chunk + i * 4 + 2] << 8) |
                           ((uint32_t)data[chunk + i * 4 + 3]);
            }
            transform(digest, block);
        }

        std::vector<uint8_t> result(20);
        for (size_t i = 0; i < 5; ++i) {
            result[i * 4]     = (uint8_t)((digest[i] >> 24) & 0xFF);
            result[i * 4 + 1] = (uint8_t)((digest[i] >> 16) & 0xFF);
            result[i * 4 + 2] = (uint8_t)((digest[i] >> 8) & 0xFF);
            result[i * 4 + 3] = (uint8_t)(digest[i] & 0xFF);
        }
        return result;
    }
}

bool WebServer::handleWebSocketHandshake(void* socketHandle, const juce::String& secWebSocketKey) {
    SocketType sock = (SocketType)(uintptr_t)socketHandle;
    juce::String combined = secWebSocketKey + WS_GUID;

    auto rawSha1 = Sha1Detail::compute(combined.toStdString());
    juce::String acceptKey = juce::Base64::toBase64(rawSha1.data(), rawSha1.size());

    juce::String response = 
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + acceptKey + "\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";

    int sent = ::send(sock, response.toRawUTF8(), (int)response.getNumBytesAsUTF8(), 0);
    return sent > 0;
}

void WebServer::handleWebSocketSession(void* socketHandle) {
    SocketType sock = (SocketType)(uintptr_t)socketHandle;

    {
        std::lock_guard<std::mutex> lock(clientSocketsMutex);
        activeWsClients.insert(socketHandle);
    }

    // Send initial status packet upon connect
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("type", "status");
    root->setProperty("data", buildStatusJson());
    juce::String initMsg = juce::JSON::toString(juce::var(root.get()));
    broadcastWebSocket(initMsg);

    // Frame reader loop
    while (serverRunning.load()) {
        uint8_t header[2];
        int n = ::recv(sock, (char*)header, 2, 0);
        if (n <= 0) break;

        bool fin = (header[0] & 0x80) != 0;
        uint8_t opcode = header[0] & 0x0F;
        bool masked = (header[1] & 0x80) != 0;
        uint64_t payloadLen = header[1] & 0x7F;

        if (opcode == 0x8) { // Close frame
            break;
        } else if (opcode == 0x9) { // Ping
            // Send Pong
            uint8_t pongHeader[2] = { 0x8A, 0x00 };
            ::send(sock, (char*)pongHeader, 2, 0);
            continue;
        }

        if (payloadLen == 126) {
            uint8_t ext[2];
            if (::recv(sock, (char*)ext, 2, 0) <= 0) break;
            payloadLen = ((uint64_t)ext[0] << 8) | ext[1];
        } else if (payloadLen == 127) {
            uint8_t ext[8];
            if (::recv(sock, (char*)ext, 8, 0) <= 0) break;
            payloadLen = 0;
            for (int i = 0; i < 8; ++i)
                payloadLen = (payloadLen << 8) | ext[i];
        }

        uint8_t maskKey[4] = { 0 };
        if (masked) {
            if (::recv(sock, (char*)maskKey, 4, 0) <= 0) break;
        }

        std::vector<uint8_t> payload((size_t)payloadLen);
        size_t totalReceived = 0;
        while (totalReceived < payloadLen) {
            int r = ::recv(sock, (char*)payload.data() + totalReceived, (int)(payloadLen - totalReceived), 0);
            if (r <= 0) break;
            totalReceived += r;
        }
        if (totalReceived < payloadLen) break;

        if (masked) {
            for (size_t i = 0; i < payloadLen; ++i) {
                payload[i] ^= maskKey[i % 4];
            }
        }

        if (opcode == 0x1) { // Text frame
            juce::String textMsg = juce::String::fromUTF8((const char*)payload.data(), (int)payload.size());
            processIncomingWsMessage(textMsg);
        }
    }

    {
        std::lock_guard<std::mutex> lock(clientSocketsMutex);
        activeWsClients.erase(socketHandle);
    }
    SOCKET_CLOSE(sock);
}

void WebServer::broadcastWebSocket(const juce::String& jsonMessage) {
    std::lock_guard<std::mutex> lock(clientSocketsMutex);
    if (activeWsClients.empty()) return;

    size_t payloadLen = (size_t)jsonMessage.getNumBytesAsUTF8();
    std::vector<uint8_t> frame;
    frame.reserve(payloadLen + 10);

    frame.push_back(0x81); // FIN + Text opcode

    if (payloadLen <= 125) {
        frame.push_back((uint8_t)payloadLen);
    } else if (payloadLen <= 65535) {
        frame.push_back(126);
        frame.push_back((uint8_t)((payloadLen >> 8) & 0xFF));
        frame.push_back((uint8_t)(payloadLen & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i)
            frame.push_back((uint8_t)((payloadLen >> (i * 8)) & 0xFF));
    }

    const char* rawUtf8 = jsonMessage.toRawUTF8();
    frame.insert(frame.end(), rawUtf8, rawUtf8 + payloadLen);

    for (void* client : activeWsClients) {
        SocketType cs = (SocketType)(uintptr_t)client;
        ::send(cs, (const char*)frame.data(), (int)frame.size(), 0);
    }
}

void WebServer::processIncomingWsMessage(const juce::String& messageText) {
    auto json = juce::JSON::parse(messageText);
    if (!json.isObject()) return;

    juce::String type = json["type"].toString();

    if (type == "select_scene") {
        int idx = json["index"];
        juce::MessageManager::callAsync([this, idx]() {
            engine.saveCurrentStateToScene(engine.getCurrentSceneIndex());
            engine.loadScene(idx);
            notifySceneChanged(idx, engine.getSceneName(idx));
            if (mainComponent != nullptr)
                mainComponent->updateStripCollapseStates();
        });
    } else if (type == "prev_setlist") {
        juce::MessageManager::callAsync([this]() {
            auto& sm = Fanfare::SetlistManager::getInstance();
            if (sm.hasPrev() && mainComponent != nullptr) {
                int prevIdx = sm.getActiveIndex() - 1;
                auto file = sm.getSetups()[prevIdx];
                mainComponent->loadRigFromFile(file, prevIdx);
            }
        });
    } else if (type == "next_setlist") {
        juce::MessageManager::callAsync([this]() {
            auto& sm = Fanfare::SetlistManager::getInstance();
            if (sm.hasNext() && mainComponent != nullptr) {
                int nextIdx = sm.getActiveIndex() + 1;
                auto file = sm.getSetups()[nextIdx];
                mainComponent->loadRigFromFile(file, nextIdx);
            }
        });
    } else if (type == "select_setlist") {
        int targetIdx = json["index"];
        juce::MessageManager::callAsync([this, targetIdx]() {
            auto& sm = Fanfare::SetlistManager::getInstance();
            if (targetIdx >= 0 && targetIdx < sm.getSetups().size() && mainComponent != nullptr) {
                auto file = sm.getSetups()[targetIdx];
                mainComponent->loadRigFromFile(file, targetIdx);
            }
        });
    } else if (type == "load_song") {
        juce::String pathOrName = json["path"].toString();
        juce::MessageManager::callAsync([this, pathOrName]() {
            if (mainComponent == nullptr) return;
            auto songsDir = Fanfare::RigLibrary::getSongsDirectory();
            juce::File f(pathOrName);
            if (!f.existsAsFile()) {
                f = songsDir.getChildFile(pathOrName);
            }
            if (!f.existsAsFile()) {
                juce::Array<juce::File> found;
                songsDir.findChildFiles(found, juce::File::findFiles, true, pathOrName + ".json");
                if (!found.isEmpty()) f = found[0];
            }
            if (!f.existsAsFile()) {
                juce::Array<juce::File> found;
                songsDir.findChildFiles(found, juce::File::findFiles, true, pathOrName + ".orsong");
                if (!found.isEmpty()) f = found[0];
            }
            if (f.existsAsFile()) {
                mainComponent->loadRigFromFile(f);
            }
        });
    } else if (type == "queue_song") {
        juce::String pathOrName = json["path"].toString();
        juce::MessageManager::callAsync([this, pathOrName]() {
            auto songsDir = Fanfare::RigLibrary::getSongsDirectory();
            juce::File f(pathOrName);
            if (!f.existsAsFile()) {
                f = songsDir.getChildFile(pathOrName);
            }
            if (!f.existsAsFile()) {
                juce::Array<juce::File> found;
                songsDir.findChildFiles(found, juce::File::findFiles, true, pathOrName + ".json");
                if (!found.isEmpty()) f = found[0];
            }
            if (!f.existsAsFile()) {
                juce::Array<juce::File> found;
                songsDir.findChildFiles(found, juce::File::findFiles, true, pathOrName + ".orsong");
                if (!found.isEmpty()) f = found[0];
            }
            if (f.existsAsFile()) {
                auto& sm = Fanfare::SetlistManager::getInstance();
                sm.addSetup(f);
            }
        });
    } else if (type == "toggle_favorite") {
        juce::String pathOrName = json["path"].toString();
        juce::MessageManager::callAsync([this, pathOrName]() {
            auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
            for (const auto& appName : { "Fanfare", "OpenRig" }) {
                auto favFile = appData.getChildFile(appName).getChildFile("library_favorites.txt");
                std::set<juce::String> favs;
                if (favFile.existsAsFile()) {
                    juce::StringArray lines;
                    favFile.readLines(lines);
                    for (const auto& l : lines) if (l.trim().isNotEmpty()) favs.insert(l.trim());
                }
                if (favs.count(pathOrName) > 0) favs.erase(pathOrName);
                else favs.insert(pathOrName);

                juce::String outText;
                for (const auto& favPath : favs) outText += favPath + "\n";
                favFile.replaceWithText(outText);
            }
        });
    } else if (type == "panic") {
        engine.triggerPanic();
    } else if (type == "set_fader") {
        juce::String fader = json["fader"].toString();
        float val = (float)json["value"];
        if (fader == "masterFoh") engine.setFohMasterLevel(val);
        else if (fader == "masterIem") engine.setIemMasterLevel(val);
    } else if (type == "set_slot_fader") {
        int slotIdx = json["slot"];
        juce::String ch = json["channel"].toString();
        float val = (float)json["value"];
        if (slotIdx >= 0 && slotIdx < engine.getNumSlots()) {
            if (ch == "foh") engine.getSlot(slotIdx)->setFohLevel(val);
            else if (ch == "iem") engine.getSlot(slotIdx)->setIemLevel(val);
        }
    } else if (type == "set_slot_mute") {
        int slotIdx = json["slot"];
        juce::String ch = json["channel"].toString();
        bool enabled = json["enabled"];
        if (slotIdx >= 0 && slotIdx < engine.getNumSlots()) {
            if (ch == "foh") engine.getSlot(slotIdx)->setFohEnabled(enabled);
            else if (ch == "iem") engine.getSlot(slotIdx)->setIemEnabled(enabled);
        }
    } else if (type == "set_notepad_tab") {
        int idx = json["index"];
        engine.setActiveNoteTabIndex(idx);
    } else if (type == "set_notepad_font_size") {
        float sz = (float)json["size"];
        engine.setNoteFontSize(sz);
    } else if (type == "set_notepad_monospace") {
        bool mono = json["isMonospace"];
        engine.setNoteIsMonospace(mono);
    } else if (type == "update_notepad_content") {
        int idx = json["index"];
        juce::String content = json["content"].toString();
        auto notes = engine.getNotes();
        if (idx >= 0 && idx < (int)notes.size()) {
            notes[idx].content = content;
            engine.setNotes(notes);
            if (engine.onNotesChanged) engine.onNotesChanged();
        }
    } else if (type == "get_status") {
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty("type", "status");
        root->setProperty("data", buildStatusJson());
        broadcastWebSocket(juce::JSON::toString(juce::var(root.get())));
    } else if (type == "get_notepad") {
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty("type", "notepad");
        root->setProperty("data", buildNotepadJson());
        broadcastWebSocket(juce::JSON::toString(juce::var(root.get())));
    } else if (type == "get_mixer") {
        juce::DynamicObject::Ptr root = new juce::DynamicObject();
        root->setProperty("type", "mixer");
        root->setProperty("data", buildMixerJson());
        broadcastWebSocket(juce::JSON::toString(juce::var(root.get())));
    }
}

void WebServer::timerCallback() {
    // 30Hz high-frequency VU and telemetry broadcast (calibrated logarithmic dB scale)
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("type", "meter");
    root->setProperty("fohL", FanfareLog::amplitudeToLogScale(engine.getFohPeakL()));
    root->setProperty("fohR", FanfareLog::amplitudeToLogScale(engine.getFohPeakR()));
    root->setProperty("iemL", FanfareLog::amplitudeToLogScale(engine.getIemPeakL()));
    root->setProperty("iemR", FanfareLog::amplitudeToLogScale(engine.getIemPeakR()));
    root->setProperty("cpu", mainComponent ? (float)mainComponent->getCpuUsage() : 0.0f);

    broadcastWebSocket(juce::JSON::toString(juce::var(root.get())));
}

void WebServer::notifySceneChanged(int sceneIndex, const juce::String& sceneName) {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("type", "scene_changed");
    root->setProperty("sceneIndex", sceneIndex);
    root->setProperty("sceneName", sceneName);
    broadcastWebSocket(juce::JSON::toString(juce::var(root.get())));
}

void WebServer::notifySetlistChanged() {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("type", "setlist_changed");
    root->setProperty("status", buildStatusJson());
    broadcastWebSocket(juce::JSON::toString(juce::var(root.get())));
}

void WebServer::notifyNotesChanged() {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("type", "notepad");
    root->setProperty("data", buildNotepadJson());
    broadcastWebSocket(juce::JSON::toString(juce::var(root.get())));
}

void WebServer::notifyMixerChanged() {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("type", "mixer");
    root->setProperty("data", buildMixerJson());
    broadcastWebSocket(juce::JSON::toString(juce::var(root.get())));
}

void WebServer::notifyMp3Changed() {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("type", "mp3");
    root->setProperty("data", buildMp3Json());
    broadcastWebSocket(juce::JSON::toString(juce::var(root.get())));
}

// REST API and Static Request Router
bool WebServer::handleHttpRequest(void* socketHandle, const juce::String& method, const juce::String& path, const juce::String& headers, const juce::String& body) {
    if (method == "OPTIONS") {
        sendHttpResponse(socketHandle, 200, "text/plain", "");
        return true;
    }

    if (path.startsWith("/api/")) {
        if (method == "GET") {
            if (path == "/api/status") {
                sendHttpResponse(socketHandle, 200, "application/json", juce::JSON::toString(buildStatusJson()));
                return true;
            } else if (path == "/api/scenes") {
                sendHttpResponse(socketHandle, 200, "application/json", juce::JSON::toString(buildScenesJson()));
                return true;
            } else if (path == "/api/setlist") {
                sendHttpResponse(socketHandle, 200, "application/json", juce::JSON::toString(buildSetlistJson()));
                return true;
            } else if (path == "/api/notepad") {
                sendHttpResponse(socketHandle, 200, "application/json", juce::JSON::toString(buildNotepadJson()));
                return true;
            } else if (path == "/api/mixer") {
                sendHttpResponse(socketHandle, 200, "application/json", juce::JSON::toString(buildMixerJson()));
                return true;
            } else if (path == "/api/mp3") {
                sendHttpResponse(socketHandle, 200, "application/json", juce::JSON::toString(buildMp3Json()));
                return true;
            }
        } else if (method == "POST") {
            auto payload = juce::JSON::parse(body);
            juce::String res = handleApiPost(path, payload);
            sendHttpResponse(socketHandle, 200, "application/json", res);
            return true;
        }
    }

    // Static File Serving
    return serveStaticFile(socketHandle, path);
}

juce::String WebServer::handleApiPost(const juce::String& path, const juce::var& jsonPayload) {
    if (path == "/api/scenes/select") {
        int idx = jsonPayload["index"];
        juce::MessageManager::callAsync([this, idx]() {
            engine.saveCurrentStateToScene(engine.getCurrentSceneIndex());
            engine.loadScene(idx);
            notifySceneChanged(idx, engine.getSceneName(idx));
            if (mainComponent != nullptr)
                mainComponent->updateStripCollapseStates();
        });
        return "{\"ok\":true}";
    } else if (path == "/api/panic") {
        engine.triggerPanic();
        return "{\"ok\":true}";
    } else if (path == "/api/mp3/play") {
        if (auto* mp3 = engine.getMp3Player()) {
            mp3->play();
            notifyMp3Changed();
        }
        return "{\"ok\":true}";
    } else if (path == "/api/mp3/pause") {
        if (auto* mp3 = engine.getMp3Player()) {
            mp3->pause();
            notifyMp3Changed();
        }
        return "{\"ok\":true}";
    } else if (path == "/api/mp3/next") {
        if (auto* mp3 = engine.getMp3Player()) {
            mp3->nextTrack(true);
            notifyMp3Changed();
        }
        return "{\"ok\":true}";
    } else if (path == "/api/mp3/prev") {
        if (auto* mp3 = engine.getMp3Player()) {
            mp3->prevTrack();
            notifyMp3Changed();
        }
        return "{\"ok\":true}";
    } else if (path == "/api/mp3/bank") {
        int bankIdx = jsonPayload["index"];
        if (auto* mp3 = engine.getMp3Player()) {
            mp3->loadBank(bankIdx, true);
            notifyMp3Changed();
        }
        return "{\"ok\":true}";
    } else if (path == "/api/mp3/track") {
        int trkIdx = jsonPayload["index"];
        if (auto* mp3 = engine.getMp3Player()) {
            mp3->playTrack(trkIdx, true);
            notifyMp3Changed();
        }
        return "{\"ok\":true}";
    } else if (path == "/api/mp3/gain") {
        float g = (float)jsonPayload["gain"];
        if (auto* mp3 = engine.getMp3Player()) {
            mp3->setGain(g);
            notifyMp3Changed();
        }
        return "{\"ok\":true}";
    } else if (path == "/api/mp3/autodj") {
        bool a = jsonPayload["enabled"];
        if (auto* mp3 = engine.getMp3Player()) {
            mp3->setAutoDjEnabled(a);
            notifyMp3Changed();
        }
        return "{\"ok\":true}";
    } else if (path == "/api/mp3/download") {
        juce::String url = jsonPayload["url"].toString().trim();
        bool playNow = (bool)jsonPayload.getProperty("playNow", false);
        if (url.isNotEmpty()) {
            YoutubeDownloadManager::getInstance().queueDownload(url, playNow,
                [this, playNow](bool success, const juce::File& file, const juce::String& /*title*/, const juce::String& /*err*/) {
                    if (success && file.existsAsFile()) {
                        if (auto* mp3 = engine.getMp3Player()) {
                            mp3->addFile(file);
                            if (playNow) {
                                int newIdx = (int)mp3->getPlaylist().size() - 1;
                                mp3->playTrack(newIdx, true);
                            }
                            notifyMp3Changed();
                        }
                    }
                },
                [this](const juce::String& status, float /*progress*/) {
                    juce::DynamicObject::Ptr root = new juce::DynamicObject();
                    root->setProperty("type", "mp3_download_status");
                    root->setProperty("status", status);
                    broadcastWebSocket(juce::JSON::toString(juce::var(root.get())));
                }
            );
            return "{\"ok\":true,\"message\":\"Download queued\"}";
        }
        return "{\"ok\":false,\"error\":\"Empty URL\"}";
    }
    return "{\"ok\":false,\"error\":\"endpoint not found\"}";
}

juce::var WebServer::buildStatusJson() {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("ok", true);
    
    auto& sm = Fanfare::SetlistManager::getInstance();
    juce::String activeName = sm.getActiveFile().existsAsFile() ? sm.getActiveFile().getFileNameWithoutExtension() : "DEFAULT RIG";
    obj->setProperty("activePreset", activeName);
    obj->setProperty("activeScene", engine.getCurrentSceneIndex());
    obj->setProperty("numScenes", engine.getNumScenes());
    
    juce::Array<juce::var> sceneArr;
    for (int i = 0; i < engine.getNumScenes(); ++i) {
        juce::DynamicObject::Ptr scObj = new juce::DynamicObject();
        scObj->setProperty("index", i);
        scObj->setProperty("name", engine.getSceneName(i));
        sceneArr.add(juce::var(scObj.get()));
    }
    obj->setProperty("scenes", sceneArr);

    juce::Array<juce::var> setlistArr;
    for (const auto& f : sm.getSetups()) {
        setlistArr.add(f.getFileNameWithoutExtension());
    }
    obj->setProperty("setlist", setlistArr);
    obj->setProperty("activeSetlistIndex", sm.getActiveIndex());
    obj->setProperty("isPreloaded", sm.isPreloaded());
    obj->setProperty("isPreloading", sm.isPreloading());
    obj->setProperty("preloadName", sm.getPreloadSetupName());

    int preloadIdx = -1;
    juce::File nextFile = sm.getNextPreloadTarget();
    for (int i = 0; i < sm.getSetups().size(); ++i) {
        if (sm.getSetups()[i] == nextFile) {
            preloadIdx = i;
            break;
        }
    }
    obj->setProperty("preloadIndex", preloadIdx);

    // Slots array for the active song/setup
    juce::Array<juce::var> slotsArr;
    for (int i = 0; i < engine.getNumSlots(); ++i) {
        auto* slot = engine.getSlot(i);
        juce::DynamicObject::Ptr slObj = new juce::DynamicObject();
        slObj->setProperty("index", i);
        slObj->setProperty("name", slot->getName());
        slObj->setProperty("fohLevel", slot->getFohLevel());
        slObj->setProperty("iemLevel", slot->getIemLevel());
        slObj->setProperty("fohEnabled", slot->isFohEnabled());
        slObj->setProperty("iemEnabled", slot->isIemEnabled());
        slObj->setProperty("bypassed", slot->isBypassed());
        slObj->setProperty("lowNote", slot->getLowNote());
        slObj->setProperty("highNote", slot->getHighNote());
        slotsArr.add(juce::var(slObj.get()));
    }
    obj->setProperty("slots", slotsArr);

    // Quick notes preview for active song
    auto notes = engine.getNotes();
    if (!notes.empty()) {
        obj->setProperty("quickNotes", notes[0].content);
        obj->setProperty("quickNotesTitle", notes[0].title);
    } else {
        obj->setProperty("quickNotes", "");
        obj->setProperty("quickNotesTitle", "Notes");
    }

    obj->setProperty("cpuLoad", mainComponent ? mainComponent->getCpuUsage() : 0.0);
    obj->setProperty("ramMb", (int)(FanfareLog::getMemoryStats().workingSetBytes / (1024 * 1024)));
    obj->setProperty("underruns", mainComponent ? mainComponent->getAudioUnderruns() : 0);
    obj->setProperty("librarySongs", buildSongsLibraryJson());

    return juce::var(obj.get());
}

juce::var WebServer::buildScenesJson() {
    return buildStatusJson()["scenes"];
}

juce::var WebServer::buildSongsLibraryJson() {
    juce::Array<juce::var> songsArr;
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    
    std::vector<juce::File> searchDirs;
    searchDirs.push_back(Fanfare::RigLibrary::getSongsDirectory());
    auto altDir = appData.getChildFile("OpenRig").getChildFile("songs");
    if (altDir.isDirectory() && altDir != Fanfare::RigLibrary::getSongsDirectory()) {
        searchDirs.push_back(altDir);
    }

    // Read favorites from both Fanfare and OpenRig
    std::set<juce::String> favorites;
    for (const auto& appName : { "Fanfare", "OpenRig" }) {
        auto favFile = appData.getChildFile(appName).getChildFile("library_favorites.txt");
        if (favFile.existsAsFile()) {
            juce::StringArray lines;
            favFile.readLines(lines);
            for (const auto& l : lines) {
                if (l.trim().isNotEmpty()) {
                    favorites.insert(l.trim());
                    favorites.insert(juce::File(l.trim()).getFileNameWithoutExtension());
                }
            }
        }
    }

    std::set<juce::String> seenNames;
    juce::Array<juce::File> found;

    for (const auto& songsDir : searchDirs) {
        if (!songsDir.isDirectory()) continue;
        juce::Array<juce::File> dirFound;
        songsDir.findChildFiles(dirFound, juce::File::findFiles, true, "*.json");
        juce::Array<juce::File> dirFoundOrsong;
        songsDir.findChildFiles(dirFoundOrsong, juce::File::findFiles, true, "*.orsong");
        for (const auto& f : dirFoundOrsong) dirFound.addIfNotAlreadyThere(f);

        for (const auto& f : dirFound) {
            juce::String fn = f.getFileNameWithoutExtension().toLowerCase();
            if (seenNames.count(fn) == 0) {
                seenNames.insert(fn);
                found.add(f);
            }
        }
    }

    std::sort(found.begin(), found.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareIgnoreCase(b.getFileName()) < 0;
    });

    for (const auto& f : found) {
        juce::DynamicObject::Ptr sObj = new juce::DynamicObject();
        juce::String fileName = f.getFileNameWithoutExtension();
        juce::String fullPath = f.getFullPathName();

        juce::String catName = "GENERAL";
        auto parent = f.getParentDirectory();
        if (parent.getFileName().equalsIgnoreCase("Core Setups")) {
            catName = "CORE SETUPS";
        } else if (!parent.getFileName().equalsIgnoreCase("songs")) {
            catName = parent.getFileName().toUpperCase();
        }

        bool isFav = (favorites.count(fullPath) > 0 || favorites.count(fileName) > 0);

        juce::Array<juce::var> tagsArr;
        juce::String lowerName = fileName.toLowerCase();

        if (isFav) tagsArr.add("FAVORITES");
        if (catName != "GENERAL") tagsArr.add(catName);

        if (lowerName.contains("piano") || lowerName.contains("keys") || lowerName.contains("grand") || lowerName.contains("giant") || lowerName.contains("gentleman") || lowerName.contains("maverick") || lowerName.contains("noire") || lowerName.contains("wurli") || lowerName.contains("rhodes") || lowerName.contains("pianoverse"))
            tagsArr.add("PIANO");

        if (lowerName.contains("b3") || lowerName.contains("organ") || lowerName.contains("blue3") || lowerName.contains("leslie"))
            tagsArr.add("B3 ORGAN");

        if (lowerName.contains("pad") || lowerName.contains("synth") || lowerName.contains("lead") || lowerName.contains("zenology") || lowerName.contains("juno") || lowerName.contains("5080") || lowerName.contains("wolf") || lowerName.contains("beds") || lowerName.contains("rebell"))
            tagsArr.add("SYNTH / PAD");

        if (lowerName.contains("brass") || lowerName.contains("horn") || lowerName.contains("sax") || lowerName.contains("trumpet") || lowerName.contains("centerfold"))
            tagsArr.add("BRASS / HORNS");

        if (lowerName.contains("solo"))
            tagsArr.add("SOLO");

        if (lowerName.contains("cello") || lowerName.contains("string") || lowerName.contains("violin"))
            tagsArr.add("STRINGS");

        if (lowerName.contains("snap") || lowerName.contains("clap") || lowerName.contains("drum") || lowerName.contains("perc") || lowerName.contains("marimba"))
            tagsArr.add("PERCUSSION");

        sObj->setProperty("name", fileName);
        sObj->setProperty("category", catName);
        sObj->setProperty("isFavorite", isFav);
        sObj->setProperty("tags", tagsArr);
        sObj->setProperty("path", fullPath);
        sObj->setProperty("fullPath", fullPath);
        songsArr.add(juce::var(sObj.get()));
    }

    return juce::var(songsArr);
}

juce::var WebServer::buildSetlistJson() {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    auto& sm = Fanfare::SetlistManager::getInstance();
    obj->setProperty("activeIndex", sm.getActiveIndex());
    juce::Array<juce::var> setups;
    for (const auto& f : sm.getSetups()) setups.add(f.getFileNameWithoutExtension());
    obj->setProperty("setups", setups);
    obj->setProperty("isPreloaded", sm.isPreloaded());
    obj->setProperty("preloadName", sm.getPreloadSetupName());
    return juce::var(obj.get());
}

juce::var WebServer::buildNotepadJson() {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    juce::Array<juce::var> tabsArr;
    for (const auto& t : engine.getNotes()) {
        juce::DynamicObject::Ptr tabObj = new juce::DynamicObject();
        tabObj->setProperty("title", t.title);
        tabObj->setProperty("content", t.content);
        tabsArr.add(juce::var(tabObj.get()));
    }
    obj->setProperty("tabs", tabsArr);
    obj->setProperty("activeTabIndex", engine.getActiveNoteTabIndex());
    obj->setProperty("fontSize", engine.getNoteFontSize());
    obj->setProperty("isMonospace", engine.getNoteIsMonospace());
    return juce::var(obj.get());
}

juce::var WebServer::buildMixerJson() {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("masterFoh", engine.getFohMasterLevel());
    obj->setProperty("masterIem", engine.getIemMasterLevel());

    juce::Array<juce::var> slotsArr;
    for (int i = 0; i < engine.getNumSlots(); ++i) {
        auto* slot = engine.getSlot(i);
        juce::DynamicObject::Ptr slObj = new juce::DynamicObject();
        slObj->setProperty("index", i);
        slObj->setProperty("name", slot->getName());
        slObj->setProperty("fohLevel", slot->getFohLevel());
        slObj->setProperty("iemLevel", slot->getIemLevel());
        slObj->setProperty("fohEnabled", slot->isFohEnabled());
        slObj->setProperty("iemEnabled", slot->isIemEnabled());
        slObj->setProperty("bypassed", slot->isBypassed());
        slObj->setProperty("lowNote", slot->getLowNote());
        slObj->setProperty("highNote", slot->getHighNote());
        slotsArr.add(juce::var(slObj.get()));
    }
    obj->setProperty("slots", slotsArr);
    return juce::var(obj.get());
}

juce::var WebServer::buildMp3Json() {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    auto* mp3 = engine.getMp3Player();
    if (!mp3) {
        obj->setProperty("available", false);
        return juce::var(obj.get());
    }

    obj->setProperty("available", true);
    obj->setProperty("isPlaying", mp3->isPlaying());
    obj->setProperty("gain", (double)mp3->getGain());
    obj->setProperty("position", mp3->getPosition());
    obj->setProperty("length", mp3->getLength());
    obj->setProperty("activeDeck", mp3->getActiveDeckIndex());
    obj->setProperty("isCrossfading", mp3->getIsCrossfading());
    obj->setProperty("autoDj", mp3->isAutoDjEnabled());
    obj->setProperty("crossfadeSec", (double)mp3->getCrossfadeDuration());
    obj->setProperty("activeBank", mp3->getActiveBankIndex());
    obj->setProperty("currentTrackIndex", mp3->getCurrentTrackIndex());

    int curIdx = mp3->getCurrentTrackIndex();
    const auto& playlist = mp3->getPlaylist();
    if (curIdx >= 0 && curIdx < (int)playlist.size()) {
        obj->setProperty("currentTitle", playlist[curIdx].title);
    } else {
        obj->setProperty("currentTitle", "No track loaded");
    }

    juce::Array<juce::var> banksArr;
    for (const auto& b : mp3->getBanks()) {
        juce::DynamicObject::Ptr bo = new juce::DynamicObject();
        bo->setProperty("index", b.index);
        bo->setProperty("name", b.name);
        bo->setProperty("numTracks", (int)b.tracks.size());
        banksArr.add(juce::var(bo.get()));
    }
    obj->setProperty("banks", banksArr);

    juce::Array<juce::var> tracksArr;
    for (int i = 0; i < (int)playlist.size(); ++i) {
        juce::DynamicObject::Ptr to = new juce::DynamicObject();
        to->setProperty("index", i);
        to->setProperty("title", playlist[i].title);
        to->setProperty("duration", playlist[i].durationSeconds);
        tracksArr.add(juce::var(to.get()));
    }
    obj->setProperty("tracks", tracksArr);

    return juce::var(obj.get());
}

bool WebServer::serveStaticFile(void* socketHandle, const juce::String& relativePath) {
    juce::String cleanPath = relativePath.trim();
    if (cleanPath == "/" || cleanPath.isEmpty()) {
        cleanPath = "/index.html";
    }

    // Try multiple possible disk locations for development and installed modes
    juce::Array<juce::File> candidateDirs;
    auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    candidateDirs.add(exeDir.getChildFile("Web/static"));
    candidateDirs.add(exeDir.getChildFile("static"));
    candidateDirs.add(exeDir.getChildFile("Source/Web/static"));
    candidateDirs.add(exeDir.getParentDirectory().getChildFile("Web/static"));
    candidateDirs.add(exeDir.getParentDirectory().getParentDirectory().getChildFile("Web/static"));
    candidateDirs.add(exeDir.getParentDirectory().getParentDirectory().getParentDirectory().getParentDirectory().getChildFile("Source/Web/static"));
    candidateDirs.add(juce::File::getCurrentWorkingDirectory().getChildFile("Source/Web/static"));
    candidateDirs.add(juce::File("C:/davecore/Source/Web/static"));
    candidateDirs.add(juce::File("Z:/davecore/Source/Web/static"));

    for (const auto& dir : candidateDirs) {
        if (dir.isDirectory()) {
            juce::File targetFile = dir.getChildFile(cleanPath.startsWithChar('/') ? cleanPath.substring(1) : cleanPath);
            if (targetFile.existsAsFile()) {
                juce::MemoryBlock block;
                if (targetFile.loadFileAsData(block)) {
                    sendHttpDataResponse(socketHandle, 200, getMimeType(cleanPath), block.getData(), block.getSize());
                    return true;
                }
            }
        }
    }

    // Compiled-in Production Fallback (guaranteed to work regardless of disk environment)
    if (cleanPath == "/index.html" || cleanPath == "/") {
        sendHttpDataResponse(socketHandle, 200, "text/html; charset=utf-8", EmbeddedWebAssets::INDEX_HTML_DATA, EmbeddedWebAssets::INDEX_HTML_DATA_LEN);
        return true;
    } else if (cleanPath == "/style.css") {
        sendHttpDataResponse(socketHandle, 200, "text/css; charset=utf-8", EmbeddedWebAssets::STYLE_CSS_DATA, EmbeddedWebAssets::STYLE_CSS_DATA_LEN);
        return true;
    } else if (cleanPath == "/app.js") {
        sendHttpDataResponse(socketHandle, 200, "application/javascript; charset=utf-8", EmbeddedWebAssets::APP_JS_DATA, EmbeddedWebAssets::APP_JS_DATA_LEN);
        return true;
    }

    sendHttpResponse(socketHandle, 404, "text/plain", "404 Not Found");
    return false;
}

juce::String WebServer::getMimeType(const juce::String& filePath) {
    if (filePath.endsWithIgnoreCase(".html")) return "text/html; charset=utf-8";
    if (filePath.endsWithIgnoreCase(".css")) return "text/css; charset=utf-8";
    if (filePath.endsWithIgnoreCase(".js")) return "application/javascript; charset=utf-8";
    if (filePath.endsWithIgnoreCase(".json")) return "application/json; charset=utf-8";
    if (filePath.endsWithIgnoreCase(".png")) return "image/png";
    if (filePath.endsWithIgnoreCase(".svg")) return "image/svg+xml";
    return "text/plain";
}

void WebServer::sendHttpResponse(void* socketHandle, int statusCode, const juce::String& contentType, const juce::String& body) {
    sendHttpDataResponse(socketHandle, statusCode, contentType, body.toRawUTF8(), (size_t)body.getNumBytesAsUTF8());
}

void WebServer::sendHttpDataResponse(void* socketHandle, int statusCode, const juce::String& contentType, const void* data, size_t dataSize) {
    SocketType sock = (SocketType)(uintptr_t)socketHandle;
    juce::String statusText = (statusCode == 200) ? "OK" : (statusCode == 404) ? "Not Found" : "Error";

    std::ostringstream ss;
    ss << "HTTP/1.1 " << statusCode << " " << statusText.toStdString() << "\r\n";
    ss << "Content-Type: " << contentType.toStdString() << "\r\n";
    ss << "Content-Length: " << dataSize << "\r\n";
    ss << "Access-Control-Allow-Origin: *\r\n";
    ss << "Connection: close\r\n\r\n";

    std::string headerStr = ss.str();
    ::send(sock, headerStr.data(), (int)headerStr.size(), 0);
    if (dataSize > 0 && data != nullptr) {
        ::send(sock, (const char*)data, (int)dataSize, 0);
    }
}

juce::String WebServer::getEmbeddedHtml() {
    return juce::String::createStringFromData(EmbeddedWebAssets::INDEX_HTML_DATA, (int)EmbeddedWebAssets::INDEX_HTML_DATA_LEN);
}

juce::String WebServer::getEmbeddedCss() {
    return juce::String::createStringFromData(EmbeddedWebAssets::STYLE_CSS_DATA, (int)EmbeddedWebAssets::STYLE_CSS_DATA_LEN);
}

juce::String WebServer::getEmbeddedJs() {
    return juce::String::createStringFromData(EmbeddedWebAssets::APP_JS_DATA, (int)EmbeddedWebAssets::APP_JS_DATA_LEN);
}

} // namespace OpenRig
