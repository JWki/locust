#pragma once

#include "../int_types.h"

typedef size_t      SocketHandle;
typedef uint16_t    Port;

namespace fnd
{
    namespace sockets
    {
        class Address
        {
            uint32_t    m_address = 0;
            Port        m_port = 0;
        public:
            Address() = default;
            Address(uint8_t a, uint8_t b, uint8_t c, uint8_t d, Port port);
            Address(uint32_t address, Port port);

            uint32_t    GetAddress() const { return m_address; };
            uint8_t     GetA() const;
            uint8_t     GetB() const;
            uint8_t     GetC() const;
            uint8_t     GetD() const;

            Port        GetPort() const { return m_port; }
        };

        bool InitializeSocketLayer();
        void ShutdownSocketLayer();

        class UDPSocket
        {
            SocketHandle    m_handle;
            Port            m_port;
        public:
            UDPSocket();
            bool Open(Port port);
            void Close();

            bool IsOpen();
            bool IsOpen(Port* port);

            bool Send(Address* address, void* data, size_t numBytes);

            size_t Receive(Address* sender, void* buffer, size_t bufferSize);
        };

        class TCPConnectionSocket;

        class TCPListenSocket
        {
            SocketHandle    m_handle;
            Port            m_port;
        public:
            TCPListenSocket();
            bool Listen(Port port, size_t maxConnections);
            void StopListen();

            bool IsListening();
            bool IsListening(Port* port);

            bool HasConnection(Address* address, TCPConnectionSocket* socket);
        };


        class TCPConnectionSocket
        {
            SocketHandle    m_handle;
            Address         m_remoteAddress;
        public:
            TCPConnectionSocket();
            
            bool Connect(Address* remoteAddress);
            bool Open(SocketHandle socket);
            void Close();

            bool IsConnected();
            bool IsConnected(Address* address);

            bool Send(void* data, size_t numBytes);
            size_t Receive(void* buffer, size_t bufferSize);
        };
    }
}
