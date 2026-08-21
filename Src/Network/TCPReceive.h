/**
 * * Supermodel
 ** A Sega Model 3 Arcade Emulator.
 **/

#ifndef INCLUDED_TCP_RECEIVE_H
#define INCLUDED_TCP_RECEIVE_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "SDLIncludes.h"

class TCPReceive
{
public:
	explicit TCPReceive(int port);
	~TCPReceive();

	bool CheckDataAvailable(int timeoutMS = 0);

	// Compatibilidade com o fluxo legado: bloqueia até obter um pacote.
	std::vector<char>& Receive();

	// Nova API: nunca bloqueia. Retorna false se não houver pacote completo.
	bool TryReceive(std::vector<char>& packet);

	bool Connected();

private:
	void ListenFunc();
	void ReceiveFunc();

	std::atomic<TCPsocket> m_listenSocket;
	std::atomic<TCPsocket> m_receiveSocket;
	SDLNet_SocketSet m_socketSet;

	std::thread m_listenThread;
	std::thread m_receiveThread;

	std::atomic<bool> m_running;
	std::atomic<bool> m_disconnected;

	std::mutex m_queueMutex;
	std::condition_variable m_queueCV;
	std::deque<std::vector<char>> m_packets;

	std::vector<char> m_recBuffer;
};

#endif  // INCLUDED_TCP_RECEIVE_H
