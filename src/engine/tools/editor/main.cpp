#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <stdio.h>
#include <malloc.h>
#include <tchar.h>

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

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
   
    switch (msg)
    {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);


        FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));

        EndPaint(hWnd, &ps);
        return 0;
    }   break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720


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

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, LoadCursor(NULL, IDC_ARROW), NULL, NULL, _T("void"), NULL };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindowEx(WS_EX_OVERLAPPEDWINDOW, _T("void"), _T("GT Editor"), WS_OVERLAPPEDWINDOW, 100, 100, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, wc.hInstance, NULL);
    GT_LOG_DEBUG("Editor", "created window with handle %llu", hwnd);


    if (!hwnd) {
        GT_LOG_ERROR("Editor", "failed to create a window\n");
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    GT_LOG_INFO("Editor", "Created application window");
    Sleep(500);
    char buf[512] = "";
    snprintf(buf, 512, "WindowHandle = %llu", (uintptr_t)hwnd);
    tcpSocket.Send(buf, strlen(buf) + 1);
    
    const size_t RECEIVE_BUFFER_SIZE = 1024 * 1024;
    char* receiveBuffer = (char*)malloc(RECEIVE_BUFFER_SIZE);

    MSG msg = {};
    bool exitFlag = false;
    do {
        size_t bytesReceived = tcpSocket.Receive(receiveBuffer, RECEIVE_BUFFER_SIZE);
        if (bytesReceived > 0) {
            GT_LOG_INFO("Editor", "Received msg: %s", receiveBuffer);
        }


        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) {
                exitFlag = true;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        if (msg.message == WM_QUIT) { exitFlag = true; }

    } while (!exitFlag);
    

    GT_LOG_INFO("Editor", "Shutting down...");
    
    tcpSocket.Close();
    sockets::ShutdownSocketLayer();

    return 0;
}