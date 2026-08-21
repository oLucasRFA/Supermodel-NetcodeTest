/**
 * * Supermodel
 ** A Sega Model 3 Arcade Emulator.
 **/

#include "TCPReceive.h"
#include "OSD/Logger.h"
#include "OSD/Thread.h"

#include <algorithm>
#include <chrono>
#include <limits>

#if defined(_DEBUG)
#include <cstdio>
#define DPRINTF DebugLog
#else
#define DPRINTF(a, ...)
#endif

namespace
{
	constexpr int kPollIntervalMS = 4;
	constexpr int kMaxPacketSize = 0x10000;
	constexpr size_t kMaxQueuedPackets = 256;
}

TCPReceive::TCPReceive(int port) :
m_listenSocket(nullptr),
m_receiveSocket(nullptr),
m_socketSet(nullptr),
m_running(false),
m_disconnected(false)
{
	SDLNet_Init();

	m_socketSet = SDLNet_AllocSocketSet(1);

	IPaddress ip;
	int result = SDLNet_ResolveHost(&ip, nullptr, port);

	if (result == 0)
	{
		m_listenSocket = SDLNet_TCP_Open(&ip);

		if (m_listenSocket)
		{
			m_running = true;
			m_listenThread = std::thread(&TCPReceive::ListenFunc, this);
			m_receiveThread = std::thread(&TCPReceive::ReceiveFunc, this);
		}
	}
}

TCPReceive::~TCPReceive()
{
	m_running = false;
	m_queueCV.notify_all();

	if (m_listenThread.joinable())
		m_listenThread.join();

	if (m_receiveThread.joinable())
		m_receiveThread.join();

	if (m_listenSocket)
	{
		SDLNet_TCP_Close(m_listenSocket);
		m_listenSocket = nullptr;
	}

	if (m_receiveSocket)
	{
		SDLNet_TCP_Close(m_receiveSocket);
		m_receiveSocket = nullptr;
	}

	if (m_socketSet)
	{
		SDLNet_FreeSocketSet(m_socketSet);
		m_socketSet = nullptr;
	}

	SDLNet_Quit();
}

bool TCPReceive::CheckDataAvailable(int timeoutMS)
{
	std::unique_lock<std::mutex> lock(m_queueMutex);

	if (!m_packets.empty())
		return true;

	if (timeoutMS == 0)
		return false;

	if (timeoutMS < 0)
	{
		m_queueCV.wait(lock, [this]() {
			return !m_running || !m_packets.empty() || m_disconnected;
		});
	}
	else
	{
		m_queueCV.wait_for(
			lock,
			std::chrono::milliseconds(timeoutMS),
						   [this]() {
							   return !m_running || !m_packets.empty() || m_disconnected;
						   }
		);
	}

	return !m_packets.empty();
}

std::vector<char>& TCPReceive::Receive()
{
	m_recBuffer.clear();

	if (!CheckDataAvailable(-1))
		return m_recBuffer;

	std::lock_guard<std::mutex> lock(m_queueMutex);

	if (m_packets.empty())
		return m_recBuffer;

	m_recBuffer = std::move(m_packets.front());
	m_packets.pop_front();

	return m_recBuffer;
}

bool TCPReceive::TryReceive(std::vector<char>& packet)
{
	std::lock_guard<std::mutex> lock(m_queueMutex);

	if (m_packets.empty())
		return false;

	packet = std::move(m_packets.front());
	m_packets.pop_front();

	return true;
}

void TCPReceive::ListenFunc()
{
	while (m_running)
	{
		CThread::Sleep(16);

		if (!m_listenSocket || m_receiveSocket)
			continue;

		TCPsocket socket = SDLNet_TCP_Accept(m_listenSocket);

		if (!socket)
			continue;

		if (m_receiveSocket)
			SDLNet_DelSocket(
				m_socketSet,
				reinterpret_cast<SDLNet_GenericSocket>(m_receiveSocket.load())
			);

		m_receiveSocket = socket;
		m_disconnected = false;

		SDLNet_AddSocket(
			m_socketSet,
			reinterpret_cast<SDLNet_GenericSocket>(socket)
		);

		DPRINTF("Accepted connection.\n");
	}
}

void TCPReceive::ReceiveFunc()
{
	while (m_running)
	{
		TCPsocket socket = m_receiveSocket.load();

		if (!socket)
		{
			CThread::Sleep(kPollIntervalMS);
			continue;
		}

		if (SDLNet_CheckSockets(m_socketSet, kPollIntervalMS) <= 0)
			continue;

		int packetSize = 0;
		int received = SDLNet_TCP_Recv(socket, &packetSize, sizeof(packetSize));

		if (received != sizeof(packetSize) ||
			packetSize < 0 ||
			packetSize > kMaxPacketSize)
		{
			if (m_receiveSocket == socket)
			{
				SDLNet_DelSocket(
					m_socketSet,
					 reinterpret_cast<SDLNet_GenericSocket>(socket)
				);

				SDLNet_TCP_Close(socket);
				m_receiveSocket = nullptr;
				m_disconnected = true;
				m_queueCV.notify_all();
			}

			continue;
		}

		std::vector<char> packet(packetSize);
		int remaining = packetSize;
		int offset = 0;
		bool failed = false;

		while (remaining > 0 && m_running)
		{
			received = SDLNet_TCP_Recv(
				socket,
				packet.data() + offset,
									   remaining
			);

			if (received <= 0)
			{
				failed = true;
				break;
			}

			offset += received;
			remaining -= received;
		}

		if (failed)
		{
			if (m_receiveSocket == socket)
			{
				SDLNet_DelSocket(
					m_socketSet,
					 reinterpret_cast<SDLNet_GenericSocket>(socket)
				);

				SDLNet_TCP_Close(socket);
				m_receiveSocket = nullptr;
				m_disconnected = true;
				m_queueCV.notify_all();
			}

			continue;
		}

		{
			std::lock_guard<std::mutex> lock(m_queueMutex);

			if (m_packets.size() >= kMaxQueuedPackets)
				m_packets.pop_front();

			m_packets.push_back(std::move(packet));
		}

		m_queueCV.notify_one();
	}
}

bool TCPReceive::Connected()
{
	return (m_receiveSocket != nullptr);
}
