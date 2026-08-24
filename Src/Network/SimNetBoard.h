/**
 * * Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2020 Bart Trzynadlowski, Nik Henson, Ian Curtis,
 **                     Harry Tuttle, and Spindizzi
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

#ifndef INCLUDED_SIMNETBOARD_H
#define INCLUDED_SIMNETBOARD_H

#include "NetInputBuffer.h"
#include "TCPSend.h"
#include "TCPReceive.h"
#include "INetBoard.h"

#include <cstdint>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

enum class State
{
	start,
	init,
	testing,
	ready,
	error
};

enum class GameType
{
	unknown,
	one,		// all games except those listed below
	two			// Le Mans 24, Virtual-On, Dirt Devils
};

class CSimNetBoard : public INetBoard
{
public:
	CSimNetBoard(const Util::Config::Node& config);
	~CSimNetBoard(void);

	void SaveState(CBlockFile* SaveState);
	void LoadState(CBlockFile* SaveState);

	Result Init(uint8_t* netRAMPtr, uint8_t* netBufferPtr);
	void RunFrame(void);
	void Reset(void);

	bool IsAttached(void);
	bool IsRunning(void);

	void GetGame(const Game& gameInfo);

	uint8_t ReadCommRAM8(unsigned addr);
	uint16_t ReadCommRAM16(unsigned addr);
	uint32_t ReadCommRAM32(unsigned addr);

	void WriteCommRAM8(unsigned addr, uint8_t data);
	void WriteCommRAM16(unsigned addr, uint16_t data);
	void WriteCommRAM32(unsigned addr, uint32_t data);

	uint16_t ReadIORegister(unsigned reg);
	void WriteIORegister(unsigned reg, uint16_t data);

private:
	// Config
	const Util::Config::Node& m_config;

	// Model 3 RAM
	uint8_t* RAM = nullptr;
	uint8_t* Buffer = nullptr;
	uint8_t* CommRAM = nullptr;
	uint8_t* externalCommRAM = nullptr;

	// Network
	uint16_t port_in = 0;
	uint16_t port_out = 0;
	std::string addr_out;

	std::thread m_connectThread;
	std::atomic_bool m_quit = false;
	std::atomic_bool m_connected = false;

	std::unique_ptr<TCPSend> nets = nullptr;
	std::unique_ptr<TCPReceive> netr = nullptr;

	// Game / state
	Game m_gameInfo;
	GameType m_gameType = GameType::unknown;
	State m_state = State::start;

	uint8_t m_numMachines = 0;
	uint16_t m_counter = 0;

		// Métricas para overlay
	uint32_t m_lastPingUs = 0;
	uint32_t m_avgPingUs = 0;
	uint64_t m_segmentSendUs = 0;   // timestamp do envio do segmento (p/ RTT)

	// CommRAM
	uint16_t m_segmentSize = 0;
	uint8_t m_pendingSegment = 0;
	bool m_waitingForPacket = false;

	// ---------------------------------------------------------------------
	// Netcode
	//
	// O NetInputBuffer substitui o buffer circular manual anterior.
	// Ele mantém os inputs recebidos indexados por frame e permite usar
	// o último input confirmado como predição.
	// ---------------------------------------------------------------------

	static constexpr uint32_t kMinBufferDelay = 1;
	static constexpr uint32_t kMaxBufferDelay = 8;
	static constexpr uint32_t kTargetBufferFrames = 2;

	NetInputBuffer m_remoteInputBuffer{
		kMinBufferDelay,
		kMaxBufferDelay,
		kTargetBufferFrames
	};

	uint32_t m_pendingNetworkFrame = 0;

	// Frame atualmente processado pelo netboard.
	uint32_t m_simulationFrame = 0;

	// Último frame que precisou utilizar predição.
	uint32_t m_lastPredictedFrame = 0;

	// Número de pacotes que chegaram atrasados.
	uint32_t m_latePacketCount = 0;

	// Indica se o frame atual utilizou input previsto.
	bool m_predictedLastFrame = false;

	// ---------------------------------------------------------------------
	// Estado de execução
	// ---------------------------------------------------------------------

	bool m_attached = false;
	bool m_running = false;

	// ---------------------------------------------------------------------
	// I/O / CommBoard
	// ---------------------------------------------------------------------

	uint16_t m_IRQ2ack = 0;
	uint16_t m_status0 = 0;	// ioreg 0x88
	uint16_t m_status1 = 0;	// ioreg 0x8a
	bool m_commbank = false;

	// ---------------------------------------------------------------------
	// Helpers
	// ---------------------------------------------------------------------

	inline bool IsGame(const char* gameName);

	void ConnectProc(void);

	// Processa todos os pacotes disponíveis sem bloquear a simulação.
	void ProcessNetworkPackets(void);

	// Obtém o input remoto correspondente ao frame atual.
	// Retorna true quando havia input confirmado.
	// Retorna false quando foi necessário utilizar predição.
	bool GetRemoteInputForFrame(
		uint32_t frame,
		std::vector<uint8_t>& input,
		bool& predicted
	);

	// Copia o input remoto para a área de CommRAM utilizada pelo jogo.
	void ApplyRemoteInput(
		const std::vector<uint8_t>& input
	);
};

#endif
