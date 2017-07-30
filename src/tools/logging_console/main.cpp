#include <stdio.h>
#include <foundation/sockets/sockets.h>
#include <foundation/logging/logging.h>

#define REMOTE_LOGGING_PORT 8090

class SimpleFilterPolicy
{
public:
    bool Filter(fnd::logging::LogCriteria criteria)
    {
        return true;
    }
};
class SimpleFormatPolicy
{
public:
    void Format(char* buf, size_t bufSize, fnd::logging::LogCriteria criteria, const char* format, va_list args)
    {
        size_t offset = snprintf(buf, bufSize, "[%s]    ", criteria.channel.str);
        vsnprintf(buf + offset, bufSize - offset, format, args);
    }
};
class PrintfWriter
{
public:
    void Write(const char* msg)
    {
        printf("%s\n", msg);
    }
};

typedef fnd::logging::Logger<SimpleFilterPolicy, SimpleFormatPolicy, PrintfWriter> ConsoleLogger;

int main(int argc, char* argv)
{
    ConsoleLogger consoleLog;

    fnd::sockets::InitializeSocketLayer();

    const size_t MAX_CONNECTIONS = 8;
    size_t numConnections = 0;
    bool isFree[MAX_CONNECTIONS]; 
    for (size_t i = 0; i < MAX_CONNECTIONS; ++i) { isFree[i] = true; }
    fnd::sockets::TCPConnectionSocket connections[MAX_CONNECTIONS];
    fnd::sockets::TCPListenSocket listenSocket;

    listenSocket.Listen(REMOTE_LOGGING_PORT, MAX_CONNECTIONS);
    Port port;
    if (!listenSocket.IsListening(&port)) {
        GT_LOG_ERROR("TCP endpoint", "Failed to setup listen socket");
        return -1;
    }
    GT_LOG_INFO("TCP endpoint", "Listening on port %d", port);

    char buf[512];
    do {
        fnd::sockets::Address addr;
        fnd::sockets::TCPConnectionSocket incoming;
        while (listenSocket.HasConnection(&addr, &incoming)) {
            GT_LOG_INFO("TCP endpoint", "Incoming connection from %d.%d.%d.%d:%d", addr.GetA(), addr.GetB(), addr.GetC(), addr.GetD(), addr.GetPort());
            if (numConnections == MAX_CONNECTIONS) { 
                GT_LOG_WARNING("TCP endpoint", "Connection slots exhausted");
                break; 
            }
            size_t index = MAX_CONNECTIONS + 1;
            for (size_t i = 0; i < MAX_CONNECTIONS; ++i) {
                if (isFree[i]) { index = i;  break; }
            }
            if (index <= MAX_CONNECTIONS) {
                isFree[index] = false;
                connections[index] = incoming;
                numConnections++;
            }
            
        }
        for (size_t i = 0; i < MAX_CONNECTIONS; ++i) {
            fnd::sockets::Address remoteAddress;
            auto& socket = connections[i];
            auto stillConnected = socket.IsConnected(&remoteAddress);
            if (!stillConnected) {
                if(!isFree[i]) {
                    GT_LOG_INFO("TCP endpoint", "Connection from { %d.%d.%d.%d:%d } has timed out, closing", remoteAddress.GetA(), remoteAddress.GetB(), remoteAddress.GetC(), remoteAddress.GetD(), remoteAddress.GetPort(), buf);
                    socket.Close();
                }
                isFree[i] = true;
                numConnections--;
            }
            if (isFree[i]) { continue; }
            size_t bytesRead = socket.Receive(buf, 512);
            if (bytesRead > 0) {
                GT_LOG_INFO("Remote Logging", "Received log from  { %d.%d.%d.%d:%d } : %s", remoteAddress.GetA(), remoteAddress.GetB(), remoteAddress.GetC(), remoteAddress.GetD(), remoteAddress.GetPort(), buf);
            }
        }

    } while(true);
    for (size_t i = 0; i < numConnections; ++i) {
        connections[i].Close();
    }
    listenSocket.StopListen();
    fnd::sockets::ShutdownSocketLayer();

    return 0;
}