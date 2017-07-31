#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <stdio.h>
#include <malloc.h>

#include <foundation/logging/logging.h>
#include <foundation/memory/allocators.h>
#include <foundation/math/math.h>
#include <foundation/sockets/sockets.h>

#define GT_TOOL_SERVER_PORT 8080

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

class ConsoleWriter
{
public:
    void Write(const char* msg)
    {
        printf("%s\n", msg);
    }
};

typedef fnd::logging::Logger<SimpleFilterPolicy, SimpleFormatPolicy, ConsoleWriter> ConsoleLogger;

int main(int argc, char* argv[])
{
    using namespace fnd;

    sockets::InitializeSocketLayer();

    ConsoleLogger consoleLogger;

    GT_LOG_INFO("Editor", "Initialized logging...");

    sockets::TCPConnectionSocket tcpSocket;
    sockets::Address remoteAddress(127, 0, 0, 1, GT_TOOL_SERVER_PORT);
    do {
        GT_LOG_INFO("Editor", "Trying to connect to tools server @ { %d.%d.%d.%d:%d }", remoteAddress.GetA(), remoteAddress.GetB(), remoteAddress.GetC(), remoteAddress.GetD(), remoteAddress.GetPort());
        tcpSocket.Connect(&remoteAddress);
    } while (!tcpSocket.IsConnected());
    GT_LOG_INFO("Editor", "Successfully connected to tools server @ { %d.%d.%d.%d:%d }", remoteAddress.GetA(), remoteAddress.GetB(), remoteAddress.GetC(), remoteAddress.GetD(), remoteAddress.GetPort());

    tcpSocket.Send("Handshake", strlen("Handshake") + 1);

    const size_t RECEIVE_BUFFER_SIZE = 1024 * 1024;
    char* receiveBuffer = (char*)malloc(RECEIVE_BUFFER_SIZE);

    do {
        size_t bytesReceived = tcpSocket.Receive(receiveBuffer, RECEIVE_BUFFER_SIZE);
        if (bytesReceived > 0) {
            GT_LOG_INFO("Editor", "Received msg: %s", receiveBuffer);
        }
    } while (true);
    

    GT_LOG_INFO("Editor", "Shutting down...");
    
    tcpSocket.Close();
    sockets::ShutdownSocketLayer();

    return 0;
}