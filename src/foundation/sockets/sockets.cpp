#include "sockets.h"

#ifdef _MSC_VER
#include <WinSock2.h>
#pragma comment(lib, "wsock32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#endif

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
#ifdef _MSC_VER
            return m_handle != INVALID_SOCKET;
#else
            return m_handle > 0;
#endif
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
    }
}