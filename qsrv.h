/*
    This file is a part of t18qsrv project (proxy-server plugin for QUIK terminal)
    Copyright (C) 2019, Arech (aradvert@gmail.com; https://github.com/Arech)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once

#include <chrono>
#include <memory>
#include <deque>
//#include <forward_list>
#include <list>
#include <cstring>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
//#include <boost/asio/ip/udp.hpp>
//#include <boost/asio/read_until.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>

#include "..\t18/t18\proxy\protocol.h"

#if defined(_DEBUG) || defined(DEBUG)
#define DBGQMESSAGE(txt) m_handler.hndMessage((txt))
#else//defined(_DEBUG) || defined(DEBUG)
#ifdef T18_RELEASE_WITH_DEBUG
#define DBGQMESSAGE(txt) m_handler.hndMessage((txt))
#else//def T18_RELEASE_WITH_DEBUG
#define DBGQMESSAGE(a) ((void)(0))
#endif//def T18_RELEASE_WITH_DEBUG
#endif//defined(_DEBUG) || defined(DEBUG)

namespace t18 {
	namespace proxy {
		namespace asio = ::boost::asio;
		using asio::ip::tcp;
		//using asio::ip::udp;
		using namespace ::std::string_literals;

		namespace _impl_qsrv {

			//valid until finalize is called
			template<typename SessT>
			class AllTrades_Sender : private asio::noncopyable {
				SessT* pSess{nullptr};
				char* pPacket{nullptr};
				const ::std::uint16_t maxTradesPerPacket;
				::std::uint16_t totTrades{ 0 };

			public:
				AllTrades_Sender(SessT& s, const size_t maxpp) : pSess(&s)
					, maxTradesPerPacket(static_cast<decltype(maxTradesPerPacket)>(maxpp))
				{
					if (UNLIKELY(::std::numeric_limits<decltype(maxTradesPerPacket)>::max() <= maxpp || maxpp == 0)) {
						throw ::std::runtime_error("Invalid count of trades/deals chosen!");
					}
				}
				AllTrades_Sender(AllTrades_Sender&& o)noexcept : maxTradesPerPacket(o.maxTradesPerPacket) {
					pSess = o.pSess;
					o.pSess = nullptr;
					pPacket = o.pPacket;
					o.pPacket = nullptr;
					totTrades = o.totTrades;
				}
				~AllTrades_Sender() {
					exceptionCaught();
				}

				auto& getSession()noexcept { return *pSess; }

				prxyTsDeal* newAllTrades() {
					prxyTsDeal* r;
					if (totTrades >= maxTradesPerPacket) pPacket = nullptr;

					if (pPacket) {
						r = reinterpret_cast<prxyTsDeal*>(pPacket + totTrades * prxyTsDeal_Size);
						++totTrades;
					} else {
						pPacket = pSess->_alloc_packet_AllTrades(maxTradesPerPacket);
						T18_ASSERT(pPacket);
						totTrades = 1;
						r = reinterpret_cast<prxyTsDeal*>(pPacket);
					}
					return r;
				}

				void exceptionCaught() {
					if (pPacket) {
						//either finalize was not called, or it's stack unwinding during exception handling
						//but we can't findout what is it, so presumable it's stack unwinding
						if (totTrades > 1) {
							T18_ASSERT(pPacket);
							--totTrades;
							//sending as much as possible, just dropping current unfinished entry
							finalize();
						} else {
							pSess->_dismiss_packet();
							pPacket = nullptr;
						}
					}
				}

				void finalize() {
					if (pPacket && totTrades<maxTradesPerPacket) {
						T18_ASSERT(totTrades > 0);
						pSess->_fix_packet_AllTrades(totTrades);
					}
					pPacket = nullptr;
				}

			};

		}

		template<typename HandlerT>
		class qsrv_tcp_session : public ::std::enable_shared_from_this<qsrv_tcp_session<HandlerT>> {
			typedef ::std::enable_shared_from_this<qsrv_tcp_session<HandlerT>> base_class_t;
			using base_class_t::shared_from_this;

			typedef qsrv_tcp_session<HandlerT> self_t;

		protected:
			//typedef ::std::vector<::std::uint8_t> data_packet_raw_t;
			typedef ::std::vector<char> data_packet_raw_t;

		public:
			typedef typename HandlerT::SessionData SessionData_t;

			typedef _impl_qsrv::AllTrades_Sender<self_t> AllTrades_Sender_t;
			friend AllTrades_Sender_t;

		public:
			static constexpr long timeoutReadMs = HandlerT::timeoutReadMs;
			static constexpr long timeoutWriteMs = HandlerT::timeoutWriteMs;
			
		protected:
			tcp::socket m_socket;
			asio::steady_timer m_readDeadline{ m_socket.get_executor() };
			asio::steady_timer m_writeDeadline{ m_socket.get_executor() };			

			Cli2Srv_PacketHdr m_cliHdr;
						
			::std::deque<data_packet_raw_t> m_writeQueue;
			data_packet_raw_t m_readBuf;

			HandlerT& m_handler;

			bool m_bWriteInProgress{ false };
			bool m_bReadInProgress{ false };

			SessionData_t m_SessionData;//just storage in session object. Used by external entities
		public:
			SessionData_t& getSessionData()noexcept { return m_SessionData; }

			//////////////////////////////////////////////////////////////////////////

		public:
			~qsrv_tcp_session() {
				m_SessionData._onDestroy(m_handler);
			}

			qsrv_tcp_session(HandlerT& h, tcp::socket&& socket) : m_socket(std::move(socket)), m_handler(h) {
				m_readDeadline.expires_at(asio::steady_timer::time_point::max());
				m_writeDeadline.expires_at(asio::steady_timer::time_point::max());

				// The non_empty_output_queue_ asio::steady_timer is set to the maximum time
				// point whenever the output queue is empty. This ensures that the output
				// actor stays asleep until a message is put into the queue.
				//m_non_empty_output_queue.expires_at(asio::steady_timer::time_point::max());
			}

			// Called by the server object to initiate the four actors.
			void start() {
				_read_request();
				_check_deadline(m_readDeadline, "read");

				//_await_output();
				_check_deadline(m_writeDeadline, "write");

				m_handler.hndMessage("session started!");
			}

			void stop(bool bSay = true) {
				if (! stopped()) {
					m_socket.shutdown(tcp::socket::shutdown_both);

					::boost::system::error_code ignored_error;
					m_socket.close(ignored_error);
					if (ignored_error) {
						//#todo there was some error during closing, #log it here
					}
				}

				m_readDeadline.cancel();
				m_writeDeadline.cancel();

				if(bSay) m_handler.hndMessage("session stopped!");
			}

			bool stopped() const { return !m_socket.is_open(); }

			void enqueue_packet(const ProtoSrv2Cli pid, const char*const str, const size_t slength, const Srv2Cli_PacketHdr_flags_t flags7b = 0) {
				char* pData = _make_packet(pid, slength, flags7b);
				T18_ASSERT(pData);
				if (str) {
					T18_ASSERT(!packetHaveOnlyHeader(pid));
					::std::memcpy(pData, str, slength);
				} else {
					T18_ASSERT(packetHaveOnlyHeader(pid));
					T18_ASSERT(slength == 0);
				}
			}
			void enqueue_packet(const ProtoSrv2Cli pid, const ::std::string& str, const Srv2Cli_PacketHdr_flags_t flags7b = 0) {
				enqueue_packet(pid, str.data(), str.length(), flags7b);
			}
			template<size_t N>
			void enqueue_packet(const ProtoSrv2Cli pid, char (&str)[N], const Srv2Cli_PacketHdr_flags_t flags7b = 0) {
				enqueue_packet(pid, str, N, flags7b);
			}
			void enqueue_packet(const ProtoSrv2Cli pid, const char*const str, const Srv2Cli_PacketHdr_flags_t flags7b = 0) {
				enqueue_packet(pid, str, str ? ::std::strlen(str) : 0, flags7b);
			}

			AllTrades_Sender_t make_AllTrades_sender(const size_t maxTradesPerPacket) {
				return AllTrades_Sender_t(*this, maxTradesPerPacket);
			}

			char* _make_packet(const ProtoSrv2Cli pid, const size_t payloadLen, const Srv2Cli_PacketHdr_flags_t flags7b = 0) {
				//same as _alloc, just syntactic wrap, because _alloc assumes subsequent _fix, that updates the length of data
				return _alloc_packet(pid, payloadLen, flags7b);
			}

			void write_responce() {
				//MUST do this check because communicating party may induce new write requests while some older
				//requests are in process
				if (m_writeQueue.empty() || m_bWriteInProgress) return;
				m_bWriteInProgress = true;

				// Set a deadline for the write operation.
				m_writeDeadline.expires_after(::std::chrono::milliseconds(timeoutWriteMs));

				// Start an asynchronous operation to send a message.
				auto self(shared_from_this());
				//IMPORTANT: NO other write operations MUST be issued before completion handler fires
				//that is why we need the write queue
				asio::async_write(m_socket, asio::buffer(m_writeQueue.front()),
					[this, self](const ::boost::system::error_code& error, ::std::size_t /*n*/)
				{
					// Check if the session was stopped while the operation was pending.
					if (UNLIKELY(stopped())) return;

					m_bWriteInProgress = false;

					if (UNLIKELY(error)) {
						//#log
						m_handler.hndMessage(::std::string("write failed, reason: ") + error.message());
						stop(false);
					} else {
						m_writeQueue.pop_front();

						if (m_writeQueue.empty()) {
							//no more data to send. Resetting deadline
							m_writeDeadline.expires_at(asio::steady_timer::time_point::max());
							//and going asleep until new item will be added to the queue
							//_asleep_till_output();
						} else {
							//sending next message
							write_responce();
						}
					}
				});
			}

		protected:
			//WARNING! alloc/fix/dismiss_packet MUST be used from the very same thread, that runs io_context and 
			//the whole alloc/fix/dismiss_packet cycle MUST ends within a single io_context job context.
			// Also NO additional allocs permitted before either fix or dismiss is called, or the prev packet will be finalized
			char* _alloc_packet(const ProtoSrv2Cli pid, const size_t payloadLen, const Srv2Cli_PacketHdr_flags_t flags7b = 0) {
				T18_ASSERT(flags7b < 0x80);
				const size_t TotalLen = Srv2Cli_PacketHdr_Size + payloadLen;
				T18_ASSERT(::std::numeric_limits<decltype(Srv2Cli_PacketHdr().wTotalPacketLen)>::max() >= TotalLen);

				m_writeQueue.emplace_back(TotalLen);
				auto& s = m_writeQueue.back();
				T18_ASSERT(s.size() >= TotalLen);

				Srv2Cli_PacketHdr* pHdr = reinterpret_cast<Srv2Cli_PacketHdr*>(s.data());
				pHdr->wTotalPacketLen = static_cast<decltype(Srv2Cli_PacketHdr().wTotalPacketLen)>(TotalLen);
				pHdr->bPacketType = static_cast<decltype(pHdr->bPacketType)>(pid);
				pHdr->bFlags = flags7b | (m_handler.hndIsQuikConnected()
					? static_cast<Srv2Cli_PacketHdr_flags_t>(0x80)
					: static_cast<Srv2Cli_PacketHdr_flags_t>(0));
				return s.data() + Srv2Cli_PacketHdr_Size;
			}
			//fix may be skipped if realPayloadLen==payloadLen passed to _alloc_packet
			void _fix_packet(const size_t realPayloadLen) {
				T18_ASSERT(realPayloadLen > 0 || !"Dont fix empty packets, dismiss them!");
				const size_t TotalLen = Srv2Cli_PacketHdr_Size + realPayloadLen;
				if (UNLIKELY(m_writeQueue.size() == 0)) throw ::std::runtime_error("_fix_packet - Invalid queue state");
				auto& s = m_writeQueue.back();
				if (UNLIKELY(s.size() < TotalLen)) throw ::std::runtime_error("_fix_packet - invalid cur packet");
				s.resize(TotalLen);
				Srv2Cli_PacketHdr* pHdr = reinterpret_cast<Srv2Cli_PacketHdr*>(s.data());
				pHdr->wTotalPacketLen = static_cast<decltype(Srv2Cli_PacketHdr().wTotalPacketLen)>(TotalLen);
			}
			void _dismiss_packet() {
				if (UNLIKELY(m_writeQueue.size() == 0)) throw ::std::runtime_error("_dismiss_packet - Invalid queue state");
				m_writeQueue.pop_back();
			}

			char* _alloc_packet_AllTrades(const size_t maxTradesInPacket) {
				return _alloc_packet(ProtoSrv2Cli::AllTrades, maxTradesInPacket*prxyTsDeal_Size);
			}
			void _fix_packet_AllTrades(const size_t tradesInPacket) {
				return _fix_packet(tradesInPacket*prxyTsDeal_Size);
			}

		protected:
			decltype(auto) _read_hdr_buffer()noexcept {
				return asio::buffer(&m_cliHdr, Cli2Srv_PacketHdr_Size);
			}
			
			void _check_deadline(asio::steady_timer& deadline, const char*const dname) {
				auto self(shared_from_this());
				deadline.async_wait([this, self, &deadline, dname](const ::boost::system::error_code& /*error*/) {
					// Check if the session was stopped while the operation was pending.
					if (UNLIKELY(stopped())) return;

					// Check whether the deadline has passed. We compare the deadline
					// against the current time since a new asynchronous operation may
					// have moved the deadline before this actor had a chance to run.
					if (UNLIKELY(deadline.expiry() <= asio::steady_timer::clock_type::now())) {
						// The deadline has passed. Stop the session. The other actors will
						// terminate as soon as possible.
						//#log
						m_handler.hndMessage(::std::string(dname) + " deadline failed, stopping session");
						stop(false);
					} else {
						// Put the actor back to sleep.
						_check_deadline(deadline, dname);
					}
				});
			}

			void _enqueue_heartbeat() {
				// We received a heartbeat message from the client. If there's
				// nothing else being sent or ready to be sent, send a heartbeat
				// right back.
				if (m_writeQueue.empty()) {
					_make_packet(ProtoSrv2Cli::heartbeat, 0);
				}
			}

			//////////////////////////////////////////////////////////////////////////
			void _log_read_failed(const ::boost::system::error_code& error, const ::std::size_t n
				, const ::std::size_t expected, const char* const nme)const
			{
				//#log

				::std::string s;
				s.reserve(256);
				s += "read ";
				s += nme;
				s += " failed, reason: ";
				if (error) {
					s += error.message();
				} else {
					s += "incoherent read count (";
					s += ::std::to_string(n); s += " instead of "; s += ::std::to_string(expected);
					s += ")";
				}
				m_handler.hndMessage(s);
			}

			void _read_request() {
				//no additional read operations could be induced by a communicating party, so just asserting it
				T18_ASSERT(!m_bReadInProgress);
				m_bReadInProgress = true;

				// Set a deadline for the read operation.
				m_readDeadline.expires_after(::std::chrono::milliseconds(timeoutReadMs));
				auto self(shared_from_this());

				//#TODO IMPORTANT: NO other read operation MUST be issued before completion handler fires
				asio::async_read(m_socket, _read_hdr_buffer()
					, [this, self](const ::boost::system::error_code& error, const ::std::size_t n)
				{
					// Check if the session was stopped while the operation was pending.
					if (UNLIKELY(stopped())) return;
					
					if (UNLIKELY(error || (n != Cli2Srv_PacketHdr_Size))) {
						_log_read_failed(error, n, Cli2Srv_PacketHdr_Size, "hdr");
						stop(false);
					} else {
						T18_ASSERT(m_cliHdr.wTotalPacketLen >= Cli2Srv_PacketHdr_Size);
						const auto packetType = static_cast<ProtoCli2Srv>(m_cliHdr.bPacketType);

						if (packetHaveOnlyHeader(packetType)) {
							//ProtoCli2Srv::heartbeat == packetType) {
							T18_ASSERT(m_cliHdr.wTotalPacketLen == Cli2Srv_PacketHdr_Size || !"Invalid hdr only packet size!");
							m_bReadInProgress = false;
							m_readDeadline.expires_at(asio::steady_timer::time_point::max());

							_process_header_only_packet(packetType);

							//repeat wait for incoming data
							_read_request();
						} else {
							// we must read the packet entirely and parse it
							m_readDeadline.expires_after(::std::chrono::milliseconds(timeoutReadMs));
							const auto expectedLen = (static_cast<::std::size_t>(m_cliHdr.wTotalPacketLen) - Cli2Srv_PacketHdr_Size);
							m_readBuf.clear();
							m_readBuf.reserve(expectedLen + 1);//we may need +1 to parse strings
							asio::async_read(m_socket, asio::dynamic_buffer(m_readBuf, expectedLen)
								, [this, self, expectedLen, packetType]
								(const ::boost::system::error_code& error2, const ::std::size_t n2)
							{
								// Check if the session was stopped while the operation was pending.
								if (UNLIKELY(stopped())) return;

								if (UNLIKELY(error2 || (n2 != expectedLen))) {
									_log_read_failed(error2, n2, expectedLen, ("body("s 
										+ ::std::to_string(static_cast<int>(packetType)) + ")").c_str());
									stop(false);
								} else {
									//#todo msg processing
									m_bReadInProgress = false;
									//stopping the deadline timer to prevent false positive during possibly long
									//processing of the packet
									m_readDeadline.expires_at(asio::steady_timer::time_point::max());
									_process_packet(packetType, n2);

									//repeat wait for incoming data
									_read_request();
								}
							});
						}
					}
				});
			}

			void _process_header_only_packet(const ProtoCli2Srv packetType) {
				T18_ASSERT(packetHaveOnlyHeader(packetType));//leave here to ensure coherent handling

				switch (packetType) {
				case ProtoCli2Srv::heartbeat:
					_enqueue_heartbeat();
					break;

				case ProtoCli2Srv::getClassesList:
					m_handler.hndGetClassesList(*this);
					break;

				case ProtoCli2Srv::listAllTickers:
					m_handler.hndListAllTickers(*this);
					break;

					//////////////////////////////////////////////////////////////////////////
					//enumerate all non-header only packets here to make compiler happy
				case ProtoCli2Srv::subscribeAllTrades:
				case ProtoCli2Srv::queryTickerInfo:
					T18_ASSERT(!"Mustn't be here!");
					break;
				}

				if (!m_writeQueue.empty()) write_responce();
			}

			//////////////////////////////////////////////////////////////////////////
			//packet processing MUST NOT issue any read commands. Write commands is OK
			void _process_packet(const ProtoCli2Srv packetType, const size_t packetLen) {
				T18_ASSERT(!packetHaveOnlyHeader(packetType));//leave here to ensure coherent handling

				switch (packetType) {
				case ProtoCli2Srv::subscribeAllTrades:
					_do_subscribeAllTrades(packetLen);
					break;

				case ProtoCli2Srv::queryTickerInfo:
					_do_queryTickerInfo(packetLen);
					break;

				/*case ProtoCli2Srv::cancelAllTrades:
					_do_cancelAllTrades();
					break;*/

					//enumerate all header only packets here to make compiler happy
				case ProtoCli2Srv::heartbeat:
				case ProtoCli2Srv::getClassesList:
				case ProtoCli2Srv::listAllTickers:
					T18_ASSERT(!"Mustn't be here!");
					break;
				}
				if (!m_writeQueue.empty()) write_responce();
			}

			void _do_queryTickerInfo(const size_t packetLen) {
				//the packet contents is a string, so putting terminating null char at the end
				m_readBuf.emplace_back(0);

				char* pReq = static_cast<char*>(m_readBuf.data());
				if (UNLIKELY(::std::strlen(pReq) != packetLen || m_readBuf.size() != packetLen + 1)) {
					//#log
					DBGQMESSAGE("_do_queryTickerInfo: unexpected length");
					return;
				}
				// For example: "TQBR(SBER,GAZP)" requests GAZP and SBER tickers info
				do {
					const char*const pClassCode = pReq;
					auto pTicker = ::std::strchr(pReq, '(');
					if (UNLIKELY(!pTicker)) {
						DBGQMESSAGE("_do_queryTickerInfo: failed to find end of class code (");
						return;
					}
					*pTicker++ = 0;

					bool bNextIsATicker;
					do {
						pReq = ::std::strpbrk(pTicker, ",)");
						if (UNLIKELY(!pReq)) {
							DBGQMESSAGE("_do_queryTickerInfo: failed to find end of time field");
							return;
						}
						bNextIsATicker = *pReq == ',';
						*pReq++ = 0;

						m_handler.hndQueryTickerInfo(*this, pClassCode, pTicker);

						pTicker = pReq;
					} while (bNextIsATicker);
				} while (*pReq != 0);
			}

			void _do_subscribeAllTrades(const size_t packetLen) {
				//the packet contents is a string, so putting terminating null char at the end
				m_readBuf.emplace_back(0);
				
				char* pReq = static_cast<char*>(m_readBuf.data());
				if (UNLIKELY(::std::strlen(pReq) != packetLen || m_readBuf.size() != packetLen + 1)) {
					//#log
					DBGQMESSAGE("_do_subscribeAllTrades: unexpected length of request='"s + pReq + "'("
						+ ::std::to_string(::std::strlen(pReq)) + ") while packet len=" + ::std::to_string(packetLen)
						+ " and buf size=" + ::std::to_string(m_readBuf.size()));
					return;
				}
				auto curDay = m_handler.hndGetTradeDate();
				// For example: "TQBR(SBER|,GAZP|100511.324123)" requests GAZP ticker since 10:05:11.324123 and all deals
				//		for SBER since session start.
				//searching for (
				do {
					const char*const pClassCode = pReq;
					auto pTicker = ::std::strchr(pReq, '(');
					if (UNLIKELY(!pTicker)) {
						DBGQMESSAGE("_do_subscribeAllTrades: failed to find end of class code (");
						return;
					}
					*pTicker++ = 0;

					bool bNextIsATicker;

					do {
						auto pTime = ::std::strchr(pTicker, '|');
						if (UNLIKELY(!pTime)) {
							DBGQMESSAGE("_do_subscribeAllTrades: failed to find end of ticker name");
							return;
						}
						*pTime++ = 0;

						pReq = ::std::strpbrk(pTime, ",)");
						if (UNLIKELY(!pReq)) {
							DBGQMESSAGE("_do_subscribeAllTrades: failed to find end of time field");
							return;
						}
						bNextIsATicker = *pReq == ',';
						*pReq++ = 0;

						auto thisTime = curDay;
						//if (UNLIKELY(!thisTime.sscanf_time(pTime))) {
						if (UNLIKELY(!thisTime.sscanf_full(pTime))) {
							DBGQMESSAGE("_do_subscribeAllTrades: failed to parse time");
							return;
						}

						m_handler.hndSubscribeAllTrades(*this, pClassCode, pTicker, thisTime);

						pTicker = pReq;
					} while (bNextIsATicker);
				} while (*pReq != 0);
			}

			void _do_cancelAllTrades() {
				//#todo
				T18_ASSERT(!"_do_cancelAllTrades not implemented yet");
			}
			
		};

		//----------------------------------------------------------------------

		template<typename HandlerT>
		class qsrv_tcp_server : public ::std::enable_shared_from_this<qsrv_tcp_server<HandlerT>> {
			typedef ::std::enable_shared_from_this<qsrv_tcp_server<HandlerT>> base_class_t;
			using base_class_t::shared_from_this;

		public:
			typedef qsrv_tcp_session<HandlerT> session_t;
			static constexpr long timeoutPruneSessionsMs = HandlerT::timeoutPruneSessionsMs;

			typedef typename session_t::AllTrades_Sender_t AllTrades_Sender_t;

		protected:
			typedef ::std::weak_ptr<session_t> session_ptr_t;

		protected:
			asio::io_context& m_io_context;
			tcp::acceptor m_acceptor;
			//::std::forward_list<session_ptr_t> m_sessions;
			::std::list<session_ptr_t> m_sessions;
			asio::steady_timer m_pruner{ m_io_context };
			HandlerT& m_Handler;

		public:
			qsrv_tcp_server(HandlerT& h, asio::io_context& io_context, const tcp::endpoint& listen_endpoint/*,
				const udp::endpoint& broadcast_endpoint*/)
				: m_io_context(io_context), m_acceptor(io_context, listen_endpoint), m_Handler(h)
			{
				/*channel_.join(std::make_shared<udp_broadcaster>(m_io_context, broadcast_endpoint));*/
				m_pruner.expires_at(asio::steady_timer::time_point::max());
			}

			//MUST be called after object creation
			void start() {
				_accept();
				m_pruner.expires_after(::std::chrono::milliseconds(timeoutPruneSessionsMs));
				_check_prune();
			}

			void pruneObsolette() {
				m_sessions.remove_if([](const auto& weakPtr) {
					return weakPtr.expired();
				});
			}

			void stop() {
				if (!stopped()) {
					::boost::system::error_code ec;
					m_acceptor.close(ec);
					if (ec) {
						//#log An error occurred.
					}
				}
				
				m_pruner.cancel();
				m_sessions.remove_if([](auto& weakPtr) {
					if (auto ptr = weakPtr.lock()) {
						ptr->stop();
					}
					return true;
				});
			}

			bool stopped()const { return !m_acceptor.is_open(); }

			template<typename F>
			void for_each_session(F&& f) {
				for (auto& weakPtr : m_sessions) {
					if (auto ptr = weakPtr.lock()) {
						::std::forward<F>(f)(*ptr.get());
					}
				}
			}

			size_t sessionCount()const noexcept { return m_sessions.size(); }

		protected:
			void _accept() {
				auto self(shared_from_this());
				m_acceptor.async_accept([this, self](const ::boost::system::error_code& error, tcp::socket&& socket){
					if (UNLIKELY(stopped())) return;

					pruneObsolette();

					if (UNLIKELY(error)) {
						//#log
					}else{
						auto pShrd = ::std::make_shared<session_t>(m_Handler, ::std::move(socket));
						pShrd->start();
						session_ptr_t weakPtr = pShrd;
						m_sessions.push_front(::std::move(weakPtr));
					}
					_accept();
				});
			}
			void _check_prune() {
				auto self(shared_from_this());
				m_pruner.async_wait([this, self](const ::boost::system::error_code& /*error*/) {
					// Check if the acceptor was stopped while the operation was pending.
					if (UNLIKELY(stopped())) return;

					// Check whether the deadline has passed. We compare the deadline
					// against the current time since a new asynchronous operation may
					// have moved the deadline before this actor had a chance to run.
					if (m_pruner.expiry() <= asio::steady_timer::clock_type::now()) {
						//pruning obsolette sessions
						pruneObsolette();
						m_pruner.expires_after(::std::chrono::milliseconds(timeoutPruneSessionsMs));
					}
					// Put the actor back to sleep.
					_check_prune();
				});
			}
		};

		//----------------------------------------------------------------------

		// QSrv class defines a network server that accepts clients connections/orders and responds to them.
		template<typename HandlerT>
		class QSrv {
		public:
			typedef qsrv_tcp_server<HandlerT> server_t;
			struct tcpV4_tag {};
			struct tcpV6_tag {};

			typedef typename server_t::AllTrades_Sender_t AllTrades_Sender_t;

		protected:
			asio::io_context m_io_context;
			tcp::endpoint m_listen_endpoint;
			::std::shared_ptr<server_t> m_srv;//MUST be created as shared_ptr, else it won't work
			//this will make it possible to destory QSrv object during server_t object stopping process

		protected:
			template<typename EpT>
			QSrv(HandlerT& hndlr, ::std::uint16_t tcpPort, EpT&& ep) : m_listen_endpoint(::std::move(ep), tcpPort)
				, m_srv{ ::std::make_shared<server_t>(hndlr, m_io_context, m_listen_endpoint) }
			{
				/*udp::endpoint broadcast_endpoint(
					boost::asio::ip::make_address(argv[2]), atoi(argv[3]));*/
				m_srv->start();
			}

		public:
			~QSrv() { stop(); }

			QSrv(HandlerT& hndlr, ::std::uint16_t tcpV4Port) : QSrv(hndlr, tcpV4Port, tcp::v4()) {}
			QSrv(HandlerT& hndlr, ::std::uint16_t tcpV4Port, const tcpV4_tag&) : QSrv(hndlr, tcpV4Port, tcp::v4()){}
			QSrv(HandlerT& hndlr, ::std::uint16_t tcpV6Port, const tcpV6_tag&) : QSrv(hndlr, tcpV4Port, tcp::v6()){}

			template<typename Rep, typename Period>
			::std::size_t run_for(const ::std::chrono::duration< Rep, Period > & rel_time){
				return m_io_context.run_for(rel_time);
			}

			bool stopped()const { return m_io_context.stopped(); }
			void stop() {
				if (!stopped()) {
					m_io_context.stop();
				}
				m_srv->stop();
			}

			template<typename F>
			void for_each_session(F&& f) {
				m_srv->for_each_session(::std::forward<F>(f));
			}

			size_t sessionCount()const noexcept { return m_srv->sessionCount(); }
		};


		//----------------------------------------------------------------------

		/*class udp_broadcaster
		: public subscriber
		{
		public:
		udp_broadcaster(boost::asio::io_context& io_context,
		const udp::endpoint& broadcast_endpoint)
		: socket_(io_context)
		{
		socket_.connect(broadcast_endpoint);
		socket_.set_option(udp::socket::broadcast(true));
		}

		private:
		void deliver(const std::string& msg)
		{
		boost::system::error_code ignored_error;
		socket_.send(boost::asio::buffer(msg), 0, ignored_error);
		}

		udp::socket socket_;
		};*/

		/*
		 *https://stackoverflow.com/questions/20188718/configuring-tcp-keep-alive-with-boostasio
		 * it is possible to set timeout value for the socket itself
			unsigned int timeout_milli = 10000;

		// platform-specific switch
	#if defined _WIN32 || defined WIN32 || defined OS_WIN64 || defined _WIN64 || defined WIN64 || defined WINNT
		// use windows-specific time
		int32_t timeout = timeout_milli;
		setsockopt(tcpsocket->native_handle(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
		setsockopt(tcpsocket->native_handle(), SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
	#else
		// assume everything else is posix
		struct timeval tv;
		tv.tv_sec = timeout_milli / 1000;
		tv.tv_usec = (timeout_milli % 1000) * 1000;
		setsockopt(tcpsocket->native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		setsockopt(tcpsocket->native_handle(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	#endif
		 *
		 *see also https://docs.microsoft.com/en-us/windows/desktop/winsock/so-keepalive
		 *https://docs.microsoft.com/en-us/previous-versions/windows/desktop/legacy/dd877220(v=vs.85)
		 *http://read.pudn.com/downloads79/ebook/301417/Chapter09/SIO_KEEPALIVE_VALS/alive.c__.htm
		 *
		 struct tcp_keepalive   alive;  
		 // Set the keepalive values
	//
	alive.onoff = TRUE;
	alive.keepalivetime = 7200000;
	alive.keepaliveinterval = 6000;

	if (WSAIoctl(s, SIO_KEEPALIVE_VALS, &alive, sizeof(alive),
			NULL, 0, &dwBytesRet, NULL, NULL) == SOCKET_ERROR)
	{
		printf("WSAIotcl(SIO_KEEPALIVE_VALS) failed; %d\n",
			WSAGetLastError());
		return -1;
	}
		 *
		 *
		 **/

	}
}
