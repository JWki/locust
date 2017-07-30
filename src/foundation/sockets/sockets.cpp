#include "sockets.h"

#ifdef _MSC_VER
#include <WinSock2.h>
#pragma comment(lib, "wsock32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#endif

//
//  @TODO: Pull implementation into common functions shared between types for socket handling
//  @TODO: properly shutdown() sockets before closing

namespace fnd
{
    namespace sockets
    {
        Address::Address(uint8_t a, uint8_t b, uint8_t c, uint8_t d, Port port)
        {
            m_address = (a << 24) |
                (b << 16) |
                (c << 8) |
                d;
            m_port = port;
        }

        Address::Address(uint32_t address, Port port)
            :   m_address(address), m_port(port) {}

        uint8_t Address::GetA() const
        {
            return (uint8_t)(m_address >> 24);
        }

        uint8_t Address::GetB() const
        {
            uint32_t mask = 0xff << 16;
            return (uint8_t)((m_address & mask) >> 16);
        }

        uint8_t Address::GetC() const
        {
            uint32_t mask = 0xff << 8;
            return (uint8_t)((m_address & mask) >> 8);
        }

        uint8_t Address::GetD() const
        {
            uint32_t mask = 0xff;
            return (uint8_t)((m_address & mask));
        }

 
        bool InitializeSocketLayer()
        {
#if _MSC_VER
            WSADATA WsaData;
            return WSAStartup(MAKEWORD(2, 2), &WsaData) == NO_ERROR;
#else
            return true;
#endif
        }
            
        void ShutdownSocketLayer()
        {
#if _MSC_VER
            WSACleanup();
#endif
        }
        
#ifndef _MSC_VER
#define INVALID_SOCKET -1
#endif

        UDPSocket::UDPSocket()
            :   m_handle(INVALID_SOCKET), m_port(0)
        {

        }

        bool UDPSocket::Open(Port port)
        {
            m_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifndef _MSC_VER
            if(m_handle <= 0) 
#else
            if (m_handle == INVALID_SOCKET) 
#endif
            {
                return false;
            }
            sockaddr_in address;
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons((unsigned short)port);

            if (bind(m_handle, (const sockaddr*)&address, sizeof(sockaddr_in)) < 0) {
                Close();
                return false;
            }

#ifndef _MSC_VER

            int nonBlocking = 1;
            if (fcntl(handle, F_SETFL, O_NONBLOCK, nonBlocking) == -1) {
                Close()
                return false;
            }

#else
            DWORD nonBlocking = 1;
            if (ioctlsocket(m_handle, FIONBIO, &nonBlocking) != 0) {
                Close();
                return false;
            }
#endif
            m_port = port;
            return true;
        }

        void UDPSocket::Close()
        {
            if (!IsOpen()) return;
#ifdef _MSC_VER
            closesocket(m_handle);
#else
            close(m_handle);
#endif
            m_handle = INVALID_SOCKET;
            m_port = 0;
        }

        bool UDPSocket::IsOpen()
        {
            Port port;
            return IsOpen(&port);
        }



        bool UDPSocket::IsOpen(Port* port)
        {
            *port = m_port;

            if (m_handle == INVALID_SOCKET) { return false; }
            
            int error;
            size_t errorSize = sizeof(error);
            if (getsockopt(m_handle, SOL_SOCKET, SO_ERROR, (char*)&error, reinterpret_cast<int*>(&errorSize)) < 0) {
                return false;
            }
            if (error == 0)
            {
                return true;
            }
            return false;
        }


        bool UDPSocket::Send(Address* address, void* data, size_t numBytes)
        {
            sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(address->GetAddress());
            addr.sin_port = htons(address->GetPort());
            int bytesWritten = sendto(m_handle, static_cast<const char*>(data), (int)numBytes, 0, reinterpret_cast<sockaddr*>(&addr), sizeof(sockaddr_in));
            if (bytesWritten != numBytes) {
                return false;
            }
            return true;
        }

        size_t UDPSocket::Receive(Address* address, void* buffer, size_t bufferSize)
        {
#ifdef _MSC_VER
            typedef int socklen_t;
#endif
            sockaddr_in from;
            socklen_t fromLength = sizeof(from);

            int bytesRead = recvfrom(m_handle, reinterpret_cast<char*>(buffer), (int)bufferSize, 0, (sockaddr*)&from, &fromLength);

            if (bytesRead > 0) {
                uint32_t from_address = ntohl(from.sin_addr.s_addr);
                uint32_t from_port = ntohs(from.sin_port);

                *address = Address(from_address, from_port);
            }
            return bytesRead >= 0 ? bytesRead : 0;
        }


        TCPListenSocket::TCPListenSocket()
            :   m_handle(INVALID_SOCKET), m_port(0) {}

        bool TCPListenSocket::Listen(Port port, size_t maxConnections)
        {
            m_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifndef _MSC_VER
            if (m_handle <= 0)
#else
            if (m_handle == INVALID_SOCKET)
#endif
            {
                return false;
            }
            sockaddr_in address;
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons((unsigned short)port);

            if (bind(m_handle, (const sockaddr*)&address, sizeof(sockaddr_in)) < 0) {
                StopListen();
                return false;
            }

#ifndef _MSC_VER

            int nonBlocking = 1;
            if (fcntl(handle, F_SETFL, O_NONBLOCK, nonBlocking) == -1) {
                Close()
                    return false;
            }

#else
            DWORD nonBlocking = 1;
            if (ioctlsocket(m_handle, FIONBIO, &nonBlocking) != 0) {
                StopListen();
                return false;
            }
#endif
            if (listen(m_handle, (int)maxConnections) < 0) {
                StopListen();
                return false;
            }

            m_port = port;
            return true;
        }

        void TCPListenSocket::StopListen()
        {
            if (!IsListening()) return;
#ifdef _MSC_VER
            closesocket(m_handle);
#else
            close(m_handle);
#endif
            m_handle = INVALID_SOCKET;
            m_port = 0;
        }

        bool TCPListenSocket::IsListening()
        {
            Port port;
            return IsListening(&port);
        }

        bool TCPListenSocket::IsListening(Port* port)
        {
            *port = m_port;
            if (m_handle == INVALID_SOCKET) { return false; }

            int error;
            size_t errorSize = sizeof(error);
            if (getsockopt(m_handle, SOL_SOCKET, SO_ACCEPTCONN, (char*)&error, reinterpret_cast<int*>(&errorSize)) < 0) {
                return false;
            }
            if (error)
            {
                return true;
            }
            return false;
        }

        bool TCPListenSocket::HasConnection(Address* address, TCPConnectionSocket* socket)
        {
            sockaddr_in addr;
            size_t addrlen = sizeof(addr);
            SOCKET sock = accept(m_handle, (sockaddr*)(&addr), (int*)&addrlen);
            if (sock == INVALID_SOCKET) {
                return false;
            }
            *address = Address(ntohl(addr.sin_addr.s_addr), ntohs(addr.sin_port));
            return socket->Open(sock);
        }


        TCPConnectionSocket::TCPConnectionSocket()
            :   m_handle(INVALID_SOCKET), m_remoteAddress() {}


        bool TCPConnectionSocket::Connect(Address* remoteAddress)
        {
            m_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
            if (m_handle == INVALID_SOCKET) { return false; }

            sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(remoteAddress->GetAddress());
            addr.sin_port = htons(remoteAddress->GetPort());

            if (connect(m_handle, (sockaddr*)(&addr), (int)(sizeof(addr))) < 0) {
                Close();
                return false;
            }

#ifndef _MSC_VER

            int nonBlocking = 1;
            if (fcntl(handle, F_SETFL, O_NONBLOCK, nonBlocking) == -1) {
                Close()
                    return false;
            }

#else
            DWORD nonBlocking = 1;
            if (ioctlsocket(m_handle, FIONBIO, &nonBlocking) != 0) {
                Close();
                return false;
            }
#endif
            return true;
            return IsConnected();
        }

        bool TCPConnectionSocket::Open(SocketHandle socket)
        {
            if (IsConnected()) { return false; }

            m_handle = socket;

            // get the address
            sockaddr_in addr;
            size_t addrlen = sizeof(addr);
            getpeername(m_handle, (sockaddr*)&addr, (int*)&addrlen);
            m_remoteAddress = Address(ntohl(addr.sin_addr.s_addr), ntohs(addr.sin_port));

#ifndef _MSC_VER

            int nonBlocking = 1;
            if (fcntl(handle, F_SETFL, O_NONBLOCK, nonBlocking) == -1) {
                Close()
                    return false;
            }

#else
            /*DWORD nonBlocking = 1;
            if (ioctlsocket(m_handle, FIONBIO, &nonBlocking) != 0) {
                Close();
                return false;
            }*/
#endif


            return IsConnected();
        }

        void TCPConnectionSocket::Close()
        {
            if (!IsConnected()) return;
#ifdef _MSC_VER
            closesocket(m_handle);
#else
            close(m_handle);
#endif
            m_handle = INVALID_SOCKET;
            m_remoteAddress = Address(0, 0);
        }


        bool TCPConnectionSocket::IsConnected()
        {
            Address addr;
            return IsConnected(&addr);
        }
#include <stdio.h>
#pragma warning(disable: 4996)
        bool TCPConnectionSocket::IsConnected(Address* address)
        {
            *address = m_remoteAddress;
            if (m_handle == INVALID_SOCKET) { return false; }

            int error;
            size_t errorSize = sizeof(error);
            int res = 0;
            if ((res = getsockopt(m_handle, SOL_SOCKET, SO_ERROR, (char*)&error, reinterpret_cast<int*>(&errorSize))) < 0) {
                return false;
            }
            if (error == 0)
            {
                return true;
            }
            return false;
        }

        bool TCPConnectionSocket::Send(void* data, size_t numBytes)
        {
            /*
            sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(m_remoteAddress.GetAddress());
            addr.sin_port = htons(m_remoteAddress.GetPort());*/
            int bytesWritten = send(m_handle, static_cast<const char*>(data), (int)numBytes, 0);
            if (bytesWritten != numBytes) {
                return false;
            }
            return true;
        }

        size_t TCPConnectionSocket::Receive(void* buffer, size_t bufferSize)
        {
#ifdef _MSC_VER
            typedef int socklen_t;
#endif
            sockaddr_in from;
            socklen_t fromLength = sizeof(from);

            int bytesRead = recvfrom(m_handle, reinterpret_cast<char*>(buffer), (int)bufferSize, 0, (sockaddr*)&from, &fromLength);
            return bytesRead >= 0 ? bytesRead : 0;
        }
    }
}