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
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#include "Supermodel.h"
#include "SimNetBoard.h"
#include "NetFrame.h"

#include <OSD/Thread.h>

// These make 16-bit read/writes much neater.
#define RAM16 *(uint16_t*)&RAM
#define CommRAM16 *(uint16_t*)&CommRAM

static const uint64_t netGUID = 0x5bf177da34872;

inline bool CSimNetBoard::IsGame(const char* gameName)
{
	return (m_gameInfo.name == gameName) || (m_gameInfo.parent == gameName);
}

CSimNetBoard::CSimNetBoard(const Util::Config::Node& config)
: m_config(config)
{
}

CSimNetBoard::~CSimNetBoard(void)
{
	m_quit = true;

	if (m_connectThread.joinable())
		m_connectThread.join();
}

void CSimNetBoard::SaveState(CBlockFile* SaveState)
{
	// Netplay state is intentionally not serialized yet.
	// Rollback snapshots will be integrated at the Model 3 level.
}

void CSimNetBoard::LoadState(CBlockFile* SaveState)
{
	// Netplay state is intentionally not restored yet.
	// Rollback snapshots will be integrated at the Model 3 level.
}

Result CSimNetBoard::Init(uint8_t* netRAMPtr, uint8_t* netBufferPtr)
{
	RAM = netRAMPtr;
	Buffer = netBufferPtr;

	CommRAM = Buffer;
	externalCommRAM = Buffer + 0x10000;

	m_attached =
	m_gameInfo.netboard_present &&
	m_config["Network"].ValueAs<bool>();

	if (!m_attached)
		return Result::OKAY;

	if (IsGame("daytona2") ||
		IsGame("harley") ||
		IsGame("scud") ||
		IsGame("srally2") ||
		IsGame("skichamp") ||
		IsGame("spikeout") ||
		IsGame("spikeofe"))
	{
		m_gameType = GameType::one;
	}
	else if (IsGame("lemans24") ||
		IsGame("von2") ||
		IsGame("dirtdvls"))
	{
		m_gameType = GameType::two;
	}
	else
	{
		return ErrorLog("Game not recognized or supported");
	}

	m_state = State::start;
	m_running = true;

	// Network sockets.
	port_in = m_config["PortIn"].ValueAs<unsigned>();
	port_out = m_config["PortOut"].ValueAs<unsigned>();
	addr_out = m_config["AddressOut"].ValueAs<std::string>();

	nets = std::make_unique<TCPSend>(addr_out, port_out);
	netr = std::make_unique<TCPReceive>(port_in);

	m_remoteInputBuffer.Reset();

	m_simulationFrame = 0;
	m_pendingNetworkFrame = 0;
	m_lastPredictedFrame = 0;

	m_latePacketCount = 0;
	m_predictedLastFrame = false;

	m_pendingSegment = 0;
	m_waitingForPacket = false;

	m_commbank = false;
	CommRAM = Buffer;
	externalCommRAM = Buffer + 0x10000;

	return Result::OKAY;
}

void CSimNetBoard::RunFrame(void)
{
	if (!IsRunning())
		return;

	switch (m_state)
	{
		case State::start:
		{
			if (!m_connected && !m_connectThread.joinable())
				m_connectThread =
				std::thread(&CSimNetBoard::ConnectProc, this);

			m_status0 = 0;
			m_status1 = IsGame("dirtdvls") ? 0x4004 : 0xe000;

			m_state = State::init;
			break;
		}

		case State::init:
		{
			memset(CommRAM, 0, 0x20000);

			if (m_gameType == GameType::one)
			{
				if (m_status0 & 0x8000)
				{
					m_IRQ2ack |= 0x01;

					if (m_status0 == 0xf000)
					{
						m_status1 = 0;
						CommRAM16[0x72] = FLIPENDIAN16(0x1);
						m_state = State::testing;
					}

					m_status0 = 0;
				}
			}
			else
			{
				m_status1 = 0;
				m_state = State::testing;
				m_counter = 0;
			}

			break;
		}

		case State::testing:
		{
			if (m_gameType == GameType::one)
			{
				m_status0 += 1;

				if (!m_connected)
					break;

				uint8_t numMachines = 0;
				uint8_t machineIndex = 0;

				if (RAM16[0x400] == 0)
				{
					// Flush receive buffer.
					while (netr->CheckDataAvailable())
						netr->Receive();

					// Check all linked instances have the same GUID.
					nets->Send(&netGUID, sizeof(netGUID));

					auto& recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					uint64_t testGUID;

					memcpy(
						&testGUID,
			recv_data.data(),
						   std::min(
							   recv_data.size(),
									sizeof(testGUID))
					);

					if (testGUID != netGUID)
						testGUID = 0;

					// Send the GUID for one more loop.
					nets->Send(&testGUID, sizeof(testGUID));
					netr->Receive();

					if (testGUID != netGUID)
					{
						ErrorLog(
							"unable to verify connection. "
							"Make sure all machines are using same build!"
						);

						m_state = State::error;
						break;
					}

					// Master has an index of zero.
					machineIndex = 0;

					nets->Send(
						&machineIndex,
				sizeof(machineIndex)
					);

					// Receive back the number of linked machines.
					recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					numMachines = recv_data[0];

					// Forward the number of linked machines.
					nets->Send(
						&numMachines,
				sizeof(numMachines)
					);

					netr->Receive();
				}
				else
				{
					// Receive GUID from previous machine.
					auto& recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					uint64_t testGUID;

					memcpy(
						&testGUID,
			recv_data.data(),
						   std::min(
							   recv_data.size(),
									sizeof(testGUID))
					);

					if (testGUID != netGUID)
						testGUID = 0;

					nets->Send(
						&testGUID,
				sizeof(testGUID)
					);

					// One more time in case a later machine has a GUID mismatch.
					recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					memcpy(
						&testGUID,
			recv_data.data(),
						   std::min(
							   recv_data.size(),
									sizeof(testGUID))
					);

					if (testGUID != netGUID)
						testGUID = 0;

					nets->Send(
						&testGUID,
				sizeof(testGUID)
					);

					if (testGUID != netGUID)
					{
						ErrorLog(
							"unable to verify connection. "
							"Make sure all machines are using same build!"
						);

						m_state = State::error;
						break;
					}

					// Receive previous machine index and increment it.
					recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					machineIndex = recv_data[0] + 1;

					nets->Send(
						&machineIndex,
				sizeof(machineIndex)
					);

					// Receive number of linked machines and forward it.
					recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					numMachines = recv_data[0];

					nets->Send(
						&numMachines,
				sizeof(numMachines)
					);
				}

				// If there are no linked machines, only continue when
				// explicitly connected to ourselves.
				if (
					(numMachines == 0) &&
					(
						(port_in != port_out) ||
						(addr_out != "127.0.0.1")
					)
				)
				{
					ErrorLog(
						"no slave machines detected. "
						"Make sure only one machine is set to master!"
					);

					m_state = State::error;
					break;
				}

				m_numMachines = numMachines + 1;

				m_status0 = 0;
				m_status1 =
				0x2021 +
				(numMachines * 0x20) +
				machineIndex;

				CommRAM16[0x0] = RAM16[0x400];
				CommRAM16[0x2] = numMachines;
				CommRAM16[0x4] = machineIndex;

				m_counter = 0;
				CommRAM16[0x6] = 0;

				m_segmentSize = RAM16[0x404];

				CommRAM16[0x8] =
				FLIPENDIAN16(0x100 + m_segmentSize);

				CommRAM16[0xa] =
				FLIPENDIAN16(
					RAM16[0x402] -
					m_segmentSize -
					1
				);

				CommRAM16[0xc] =
				FLIPENDIAN16(0x100);

				CommRAM16[0xe] =
				FLIPENDIAN16(
					RAM16[0x402] -
					m_segmentSize +
					0x200
				);

				m_pendingSegment = 0;
				m_waitingForPacket = false;

				m_remoteInputBuffer.Reset();

				m_simulationFrame = 0;
				m_pendingNetworkFrame = 0;
				m_lastPredictedFrame = 0;

				m_latePacketCount = 0;
				m_predictedLastFrame = false;

				m_commbank = false;
				CommRAM = Buffer;
				externalCommRAM = Buffer + 0x10000;

				m_state = State::ready;
			}
			else
			{
				if (!m_connected)
					break;

				struct
				{
					uint8_t total;
					uint8_t playable;
				} numMachines, machineIndex;

				if (RAM16[0x200] == 0)
				{
					// Master.
					while (netr->CheckDataAvailable())
						netr->Receive();

					nets->Send(
						&netGUID,
				sizeof(netGUID)
					);

					auto& recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					uint64_t testGUID;

					memcpy(
						&testGUID,
			recv_data.data(),
						   std::min(
							   recv_data.size(),
									sizeof(testGUID))
					);

					if (testGUID != netGUID)
						testGUID = 0;

					nets->Send(
						&testGUID,
				sizeof(testGUID)
					);

					netr->Receive();

					if (testGUID != netGUID)
					{
						ErrorLog(
							"unable to verify connection. "
							"Make sure all machines are using same build!"
						);

						m_state = State::error;
						break;
					}

					machineIndex.total = 0;
					machineIndex.playable = 0;

					nets->Send(
						&machineIndex,
				sizeof(machineIndex)
					);

					recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					memcpy(
						&numMachines,
			recv_data.data(),
						   std::min(
							   recv_data.size(),
									sizeof(numMachines))
					);

					nets->Send(
						&numMachines,
				sizeof(numMachines)
					);

					netr->Receive();
				}
				else if (RAM16[0x200] < 0x8000)
				{
					// Slave.
					auto& recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					uint64_t testGUID;

					memcpy(
						&testGUID,
			recv_data.data(),
						   std::min(
							   recv_data.size(),
									sizeof(testGUID))
					);

					if (testGUID != netGUID)
						testGUID = 0;

					nets->Send(
						&testGUID,
				sizeof(testGUID)
					);

					recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					memcpy(
						&testGUID,
			recv_data.data(),
						   std::min(
							   recv_data.size(),
									sizeof(testGUID))
					);

					if (testGUID != netGUID)
						testGUID = 0;

					nets->Send(
						&testGUID,
				sizeof(testGUID)
					);

					if (testGUID != netGUID)
					{
						ErrorLog(
							"unable to verify connection. "
							"Make sure all machines are using same build!"
						);

						m_state = State::error;
						break;
					}

					recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					memcpy(
						&machineIndex,
			recv_data.data(),
						   std::min(
							   recv_data.size(),
									sizeof(machineIndex))
					);

					machineIndex.total++;
					machineIndex.playable++;

					nets->Send(
						&machineIndex,
				sizeof(machineIndex)
					);

					recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					memcpy(
						&numMachines,
			recv_data.data(),
						   std::min(
							   recv_data.size(),
									sizeof(numMachines))
					);

					nets->Send(
						&numMachines,
				sizeof(numMachines)
					);
				}
				else
				{
					// Relay/satellite.
					auto& recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					uint64_t testGUID;

					memcpy(
						&testGUID,
			recv_data.data(),
						   std::min(
							   recv_data.size(),
									sizeof(testGUID))
					);

					if (testGUID != netGUID)
						testGUID = 0;

					nets->Send(
						&testGUID,
				sizeof(testGUID)
					);

					recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					memcpy(
						&testGUID,
			recv_data.data(),
						   std::min(
							   recv_data.size(),
									sizeof(testGUID))
					);

					if (testGUID != netGUID)
						testGUID = 0;

					nets->Send(
						&testGUID,
				sizeof(testGUID)
					);

					if (testGUID != netGUID)
					{
						ErrorLog(
							"unable to verify connection. "
							"Make sure all machines are using same build!"
						);

						m_state = State::error;
						break;
					}

					recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					memcpy(
						&machineIndex,
			recv_data.data(),
						   std::min(
							   recv_data.size(),
									sizeof(machineIndex))
					);

					machineIndex.total++;

					nets->Send(
						&machineIndex,
				sizeof(machineIndex)
					);

					recv_data = netr->Receive();

					if (recv_data.empty())
						break;

					memcpy(
						&numMachines,
			recv_data.data(),
						   std::min(
							   recv_data.size(),
									sizeof(numMachines))
					);

					nets->Send(
						&numMachines,
				sizeof(numMachines)
					);

					if (!IsGame("dirtdvls"))
						machineIndex.playable |= 0x80;
				}

				if (
					(numMachines.total == 0) &&
					(
						(port_in != port_out) ||
						(addr_out != "127.0.0.1")
					)
				)
				{
					ErrorLog(
						"no slave machines detected. "
						"Make sure only one machine is set to master!"
					);

					if (IsGame("dirtdvls"))
					{
						m_status1 = 0x8085;
					}

					m_state = State::error;
					break;
				}

				m_numMachines = numMachines.total + 1;

				m_status0 = 5;

				if (IsGame("dirtdvls"))
				{
					m_status1 =
					(numMachines.playable << 4) |
					machineIndex.playable |
					0x7400;
				}
				else
				{
					m_status1 =
					(numMachines.playable << 8) |
					machineIndex.playable;
				}

				CommRAM16[0x0] = RAM16[0x200];

				CommRAM16[0x2] =
				(numMachines.playable << 8) |
				numMachines.total;

				CommRAM16[0x4] =
				(machineIndex.playable << 8) |
				machineIndex.total;

				m_counter = 0;
				CommRAM16[0x6] = 0;

				m_segmentSize = RAM16[0x204];

				CommRAM16[0x8] =
				FLIPENDIAN16(
					0x100 + m_segmentSize
				);

				CommRAM16[0xa] =
				FLIPENDIAN16(RAM16[0x206]);

				CommRAM16[0xc] =
				FLIPENDIAN16(0x100);

				CommRAM16[0xe] =
				FLIPENDIAN16(
					RAM16[0x206] + 0x80
				);

				m_pendingSegment = 0;
				m_waitingForPacket = false;

				m_remoteInputBuffer.Reset();

				m_simulationFrame = 0;
				m_pendingNetworkFrame = 0;
				m_lastPredictedFrame = 0;

				m_latePacketCount = 0;
				m_predictedLastFrame = false;

				m_commbank = false;
				CommRAM = Buffer;
				externalCommRAM = Buffer + 0x10000;

				m_state = State::ready;
			}

			break;
		}

		case State::ready:
		{
			m_simulationFrame++;
			m_counter++;

			/*
			 * A network exchange can span multiple emulation frames.
			 * Therefore the network frame is assigned only when starting
			 * a new complete segment exchange.
			 */
			if (
				m_pendingSegment == 0 &&
				!m_waitingForPacket
			)
			{
				m_pendingNetworkFrame = m_simulationFrame;
			}

			const int kMaxSegmentsPerFrame = 3;
			int segmentsProcessed = 0;

			/*
			 * Send the local segment and process packets without blocking.
			 *
			 * State::init/testing continues to use the original protocol.
			 * NetFrame is used only for gameplay traffic here.
			 */
			while (
				m_pendingSegment < m_numMachines &&
				segmentsProcessed < kMaxSegmentsPerFrame
			)
			{
				if (!m_waitingForPacket)
				{
					std::vector<uint8_t> packet;

					const uint16_t payloadSize =
					m_segmentSize;

					if (
						!NetFrame::Encode(
							m_pendingNetworkFrame,
						NetFrame::FLAG_INPUT,
						CommRAM +
						0x100 +
						m_pendingSegment * m_segmentSize,
						payloadSize,
						packet
						)
					)
					{
						ErrorLog(
							"failed to encode net frame %u.",
			   m_pendingNetworkFrame
						);

						m_state = State::error;
						break;
					}

					nets->Send(
						packet.data(),
							   packet.size()
					);

					m_waitingForPacket = true;
				}

				/*
				 * Receive the response corresponding to the segment
				 * currently waiting.
				 */
				std::vector<char> recvData;

				if (!netr->TryReceive(recvData))
				{
					m_predictedLastFrame = true;
					m_latePacketCount++;
					break;
				}

				/*
				 * Empty packets are still used as disconnect/error markers
				 * by the existing netboard protocol.
				 */
				if (recvData.empty())
				{
					nets->Send(nullptr, 0);

					m_state = State::error;
					m_waitingForPacket = false;

					if (m_gameType == GameType::one)
						m_status1 = 0x40;

					break;
				}

				NetFrame::Header header{};
				const uint8_t* payload = nullptr;

				if (
					!NetFrame::Decode(
						recvData.data(),
									  recvData.size(),
									  header,
					   payload
					)
				)
				{
					ErrorLog(
						"invalid net frame received "
						"(packet size=%u).",
							 static_cast<unsigned>(
								 recvData.size()
							 )
					);

					m_state = State::error;
					m_waitingForPacket = false;
					break;
				}

				if (
					(header.flags & NetFrame::FLAG_INPUT) == 0
				)
				{
					ErrorLog(
						"unexpected net frame flags: 0x%04x.",
			  header.flags
					);

					m_state = State::error;
					m_waitingForPacket = false;
					break;
				}

				if (header.payloadSize != m_segmentSize)
				{
					ErrorLog(
						"invalid net frame payload size: "
						"expected=%u received=%u.",
			  static_cast<unsigned>(m_segmentSize),
							 static_cast<unsigned>(header.payloadSize)
					);

					m_state = State::error;
					m_waitingForPacket = false;
					break;
				}

				/*
				 * TCP preserves ordering, but our simulation may have
				 * advanced while waiting for the packet.
				 *
				 * Packets belonging to an older outstanding exchange
				 * are therefore valid and must NOT be discarded merely
				 * because m_simulationFrame has advanced.
				 */
				if (header.frame < m_pendingNetworkFrame)
				{
					/*
					 * This can only happen if an old packet survived in
					 * the receive queue. Discard it and continue looking
					 * for the packet belonging to the outstanding exchange.
					 */
					continue;
				}

				if (header.frame > m_pendingNetworkFrame)
				{
					ErrorLog(
						"future net frame received: "
						"expected=%u received=%u.",
			  m_pendingNetworkFrame,
			  header.frame
					);

					m_predictedLastFrame = true;
					m_latePacketCount++;
					break;
				}

				/*
				 * The payload belongs to the current pending segment.
				 */
				memcpy(
					CommRAM +
					0x100 +
					(m_pendingSegment + 1) *
					m_segmentSize,
		   payload,
		   header.payloadSize
				);

				m_predictedLastFrame = false;
				m_waitingForPacket = false;

				m_pendingSegment++;
				segmentsProcessed++;
			}

			/*
			 * A complete network exchange has arrived.
			 * Store only the currently active CommRAM bank as the frame
			 * snapshot. This is enough for the network state used by
			 * ReadCommRAM/WriteCommRAM and avoids copying both banks.
			 */
			if (m_pendingSegment >= m_numMachines)
			{
				std::vector<uint8_t> frameState(
					0x10000
				);

				memcpy(
					frameState.data(),
					   CommRAM,
		   frameState.size()
				);

				m_remoteInputBuffer.Push(
					m_pendingNetworkFrame,
					frameState.data(),
										 frameState.size()
				);

				/*
				 * The old implementation modified the buffer delay once
				 * per second according to observed late packets.
				 *
				 * We preserve that behavior through NetInputBuffer's
				 * adaptive delay interface while keeping all frame history
				 * inside NetInputBuffer.
				 */
				if (m_simulationFrame % 60 == 0)
				{
					const uint32_t currentDelay =
					m_remoteInputBuffer.GetDelayFrames();

					if (m_latePacketCount > 5)
					{
						/*
						 * Simulate a shallow effective buffer so
						 * UpdateTargetDelay increases the target delay.
						 */
						m_remoteInputBuffer.UpdateTargetDelay(
							m_simulationFrame
						);
					}
					else if (
						m_latePacketCount == 0 &&
						currentDelay >
						m_remoteInputBuffer.GetMinDelayFrames()
					)
					{
						const uint32_t highestFrame =
						m_remoteInputBuffer.GetHighestReceivedFrame();

						const uint32_t referenceFrame =
						highestFrame >
						(kTargetBufferFrames + 2)
						? highestFrame -
						(kTargetBufferFrames + 3)
						: 0;

						m_remoteInputBuffer.UpdateTargetDelay(
							referenceFrame
						);
					}
					else
					{
						const uint32_t highestFrame =
						m_remoteInputBuffer.GetHighestReceivedFrame();

						const uint32_t referenceFrame =
						highestFrame >
						kTargetBufferFrames
						? highestFrame -
						kTargetBufferFrames
						: 0;

						m_remoteInputBuffer.UpdateTargetDelay(
							referenceFrame
						);
					}

					m_latePacketCount = 0;
				}

				/*
				 * Select the delayed frame.
				 *
				 * The minimum delay starts at one frame. If the desired
				 * frame does not exist, NetInputBuffer returns the last
				 * confirmed snapshot, which is our current prediction.
				 */
				const uint32_t delay =
				m_remoteInputBuffer.GetDelayFrames();

				const uint32_t targetFrame =
				m_pendingNetworkFrame > delay
				? m_pendingNetworkFrame - delay
				: 0;

				std::vector<uint8_t> delayedState;

				bool predicted = false;

				if (
					!GetRemoteInputForFrame(
						targetFrame,
						delayedState,
						predicted
					)
				)
				{
					/*
					 * No confirmed or predicted snapshot is available
					 * yet. Keep the current CommRAM intact.
					 */
					m_predictedLastFrame = true;
				}
				else
				{
					m_predictedLastFrame = predicted;

					if (predicted)
						m_lastPredictedFrame = targetFrame;

					ApplyRemoteInput(
						delayedState
					);
				}

				/*
				 * Start the next network exchange.
				 */
				m_pendingSegment = 0;
				m_waitingForPacket = false;
			}
			else
			{
				/*
				 * The complete exchange has not arrived yet.
				 *
				 * Use the most recent confirmed frame as prediction and
				 * leave the outstanding network request alive.
				 */
				m_predictedLastFrame = true;

				const uint32_t delay =
				m_remoteInputBuffer.GetDelayFrames();

				const uint32_t targetFrame =
				m_pendingNetworkFrame > delay
				? m_pendingNetworkFrame - delay
				: 0;

				std::vector<uint8_t> predictedState;

				bool predicted = false;

				if (
					GetRemoteInputForFrame(
						targetFrame,
						predictedState,
						predicted
					)
				)
				{
					m_predictedLastFrame = true;
					m_lastPredictedFrame = targetFrame;

					ApplyRemoteInput(
						predictedState
					);
				}
			}

			/*
			 * Network metrics.
			 */
			static uint32_t logCounter = 0;

			logCounter++;

			if (logCounter % 60 == 0)
			{
				const double pingMs =
				m_lastPingUs / 1000.0;

				const double avgPingMs =
				m_avgPingUs / 1000.0;

				const uint32_t delay =
				m_remoteInputBuffer.GetDelayFrames();

				const uint32_t highestFrame =
				m_remoteInputBuffer.GetHighestReceivedFrame();

				printf(
					"[NetMetrics] "
					"ping_ms=%.1f "
					"avg_ms=%.1f "
					"buffer_delay=%u "
					"late_packets=%u "
					"predicted=%d "
					"frame=%u "
					"network_frame=%u "
					"highest_received=%u\n",
		   pingMs,
		   avgPingMs,
		   delay,
		   m_latePacketCount,
		   m_predictedLastFrame ? 1 : 0,
		   m_simulationFrame,
		   m_pendingNetworkFrame,
		   highestFrame
				);

				ErrorLog(
					"[NetMetrics] "
					"ping_ms=%.1f "
					"avg_ms=%.1f "
					"buffer_delay=%u "
					"late_packets=%u "
					"predicted=%d "
					"frame=%u "
					"network_frame=%u "
					"highest_received=%u",
			 pingMs,
			 avgPingMs,
			 delay,
			 m_latePacketCount,
			 m_predictedLastFrame ? 1 : 0,
			 m_simulationFrame,
			 m_pendingNetworkFrame,
			 highestFrame
				);
			}

			break;
		}

		case State::error:
		{
			// Do nothing.
			break;
		}
	}
}

void CSimNetBoard::Reset(void)
{
	/*
	 * Notify the other machine that the current exchange ended.
	 * Unlike the previous implementation, do not block waiting for a
	 * response here.
	 */
	if (
		m_state == State::ready &&
		nets &&
		netr
	)
	{
		nets->Send(nullptr, 0);

		std::vector<char> recvData;

		while (netr->TryReceive(recvData))
		{
			// Flush pending network packets.
		}
	}

	m_remoteInputBuffer.Reset();

	m_simulationFrame = 0;
	m_pendingNetworkFrame = 0;
	m_lastPredictedFrame = 0;

	m_pendingSegment = 0;
	m_waitingForPacket = false;

	m_latePacketCount = 0;
	m_predictedLastFrame = false;

	m_counter = 0;

	/*
	 * Keep the TCP connection alive, as the previous implementation did.
	 */
	m_running = false;
	m_state = State::testing;
}

bool CSimNetBoard::IsAttached(void)
{
	return m_attached;
}

bool CSimNetBoard::IsRunning(void)
{
	return m_attached && m_running;
}

void CSimNetBoard::GetGame(const Game& gameInfo)
{
	m_gameInfo = gameInfo;
}

void CSimNetBoard::ConnectProc(void)
{
	if (m_connected)
		return;

	printf(
		"Connecting to %s:%i ..\n",
		addr_out.c_str(),
		   port_out
	);

	// Wait until TCPSend connects to the next machine.
	while (!nets->Connect())
	{
		if (m_quit)
			return;
	}

	// Wait until TCPReceive accepts a connection from the previous machine.
	while (!netr->Connected())
	{
		if (m_quit)
			return;

		CThread::Sleep(1);
	}

	printf("Successfully connected.\n");

	m_connected = true;
}

void CSimNetBoard::ProcessNetworkPackets(void)
{
	/*
	 * Gameplay packets are intentionally processed from RunFrame().
	 *
	 * A generic drain here would be incorrect because TCP preserves
	 * packet ordering and an outstanding segment may be waiting for a
	 * specific frame-tagged response.
	 */
}

bool CSimNetBoard::GetRemoteInputForFrame(
	uint32_t frame,
	std::vector<uint8_t>& input,
	bool& predicted
)
{
	predicted = false;

	if (m_remoteInputBuffer.Has(frame))
	{
		if (
			m_remoteInputBuffer.GetPredicted(
				frame,
				input
			)
		)
		{
			return true;
		}
	}

	if (
		m_remoteInputBuffer.GetPredicted(
			frame,
			input
		)
	)
	{
		predicted = true;
		return true;
	}

	return false;
}

void CSimNetBoard::ApplyRemoteInput(
	const std::vector<uint8_t>& input
)
{
	if (input.empty())
		return;

	constexpr size_t kCommBankSize = 0x10000;

	if (input.size() < kCommBankSize)
	{
		ErrorLog(
			"remote snapshot too small: expected at least %u bytes, got %u.",
		   static_cast<unsigned>(kCommBankSize),
				 static_cast<unsigned>(input.size())
		);

		return;
	}

	/*
	 * The inactive bank becomes the next active CommRAM bank.
	 */
	memcpy(
		externalCommRAM,
		input.data(),
		   kCommBankSize
	);

	if (m_commbank)
	{
		m_commbank = false;
		CommRAM = Buffer;
		externalCommRAM = Buffer + 0x10000;
	}
	else
	{
		m_commbank = true;
		CommRAM = Buffer + 0x10000;
		externalCommRAM = Buffer;
	}
}

uint8_t CSimNetBoard::ReadCommRAM8(unsigned addr)
{
	return CommRAM[addr];
}

uint16_t CSimNetBoard::ReadCommRAM16(unsigned addr)
{
	return *(uint16_t*)&CommRAM[addr];
}

uint32_t CSimNetBoard::ReadCommRAM32(unsigned addr)
{
	return *(uint32_t*)&CommRAM[addr];
}

void CSimNetBoard::WriteCommRAM8(
	unsigned addr,
	uint8_t data
)
{
	CommRAM[addr] = data;
}

void CSimNetBoard::WriteCommRAM16(
	unsigned addr,
	uint16_t data
)
{
	*(uint16_t*)&CommRAM[addr] = data;
}

void CSimNetBoard::WriteCommRAM32(
	unsigned addr,
	uint32_t data
)
{
	*(uint32_t*)&CommRAM[addr] = data;
}

uint16_t CSimNetBoard::ReadIORegister(unsigned reg)
{
	if (!IsRunning())
		return 0;

	switch (reg)
	{
		case 0x00:
			return m_IRQ2ack;

		case 0x88:
			return m_status0;

		case 0x8a:
			return m_status1;

		default:
			ErrorLog(
				"read from unknown IO register 0x%02x",
			reg
			);

			return 0;
	}
}

void CSimNetBoard::WriteIORegister(
	unsigned reg,
	uint16_t data
)
{
	switch (reg)
	{
		case 0x00:
			m_IRQ2ack = data;
			break;

		case 0x88:
			m_status0 = data;
			break;

		case 0x8a:
			m_status1 = data;
			break;

		case 0xc0:
		{
			if (data == 0)
			{
				Reset();
			}

			m_running = (data != 0);
			break;
		}

		default:
			ErrorLog(
				"write to unknown IO register 0x%02x",
			reg
			);
			break;
	}
}
