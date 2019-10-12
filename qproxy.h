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

#include <atomic>
#include <memory>
#include <forward_list>
#include <deque>
#include <vector>
#include <algorithm>

#include "../t18/t18/utils/atomic_flags_set.h"
#include "../t18/t18/utils/spinlock.h"
#include "../t18/t18/utils/std.h"
#include "qsrv.h"

T18_COMP_SILENCE_COVERED_SWITCH;
T18_COMP_SILENCE_OLD_STYLE_CAST;
T18_COMP_SILENCE_SIGN_CONVERSION;
#include "../_extern/readerwriterqueue/readerwriterqueue.h"
T18_COMP_POP;T18_COMP_POP;T18_COMP_POP;

namespace t18 {
	namespace proxy {
		using namespace ::std::string_literals;

		namespace _impl {

			//the class registers whether something should be done with the given intTickerId as well as
			// tracks the usage count of a tickerId
			class Job4TickerId {
			public:
				typedef ::std::uint8_t usageCount_t;
				typedef ::utils::spinlock lock_t;
				typedef ::utils::spinlock_guard lockGuard_t;

			protected:
				//indexed by intTickerId and counts how many entities required the job for the given ticker
				::std::vector<usageCount_t> m_useCount;

				//indexed by intTickerId and contains whether some job must be done with the given intTickerId
				::std::vector<bool> m_hasJob;

			public:
				Job4TickerId(size_t reserv) {
					m_useCount.reserve(reserv);
					m_hasJob.reserve(reserv);
				}

				//call only from quiks' main() thread
				//MUST be called while lock is held
				void main_notifyNewTicker_locked() {
					m_useCount.emplace_back(0);
					m_hasJob.emplace_back(false);
				}

				//call only from quiks' main() thread
				void main_register(lock_t& l, intTickerId_t t) {
					T18_ASSERT(t < m_useCount.size() && m_useCount.size() == m_hasJob.size());

					lockGuard_t g(l);
					m_hasJob[t] = true;
					auto& uc = m_useCount[t];
					if (UNLIKELY(static_cast<size_t>(uc) >= ::std::numeric_limits<usageCount_t>::max()))
						throw ::std::runtime_error("WTF? Never expected that much uses!");
					++uc;
				}

				//call only from quiks' main() thread. returns whether there're no more registered jobs to do
				bool main_deregister(lock_t& l, intTickerId_t t) {
					T18_ASSERT(t < m_useCount.size() && m_useCount.size() == m_hasJob.size());

					lockGuard_t g(l);
					auto& uc = m_useCount[t];
					if (UNLIKELY(uc <= 0))
						throw ::std::runtime_error("WTF? There's nothing registered for ticker!");

					bool r = false;
					if (!(--uc)) {
						m_hasJob[t] = false;
						r = main_no_registered();
					}
					return r;
				}

				//call only from quiks' main() thread
				//Ok to not use the lock, as updates could only happens in the same current thread and therefore no
				// race conditions are expected
				bool main_no_registered()const noexcept {
					const size_t s = m_hasJob.size();
					for (size_t i = 0; i < s; ++i) {
						if (m_hasJob[i]) return false;
					}
					return true;
				}

				//the lock MUST already be acquired during the call to has_job() if called from on*() callbacks
				//(generally it's supposed to be called from quiks' on*() callbacks, with lock acquired.
				//The modification of m_hasJob happen only in the context of main() thread, so safe to query value from main() without lock
				bool has_job(const intTickerId_t t)noexcept {
					T18_ASSERT(t < m_hasJob.size() && m_hasJob.size()==m_useCount.size());
					return m_hasJob[t];
				}
				size_t use_count(const intTickerId_t t)noexcept {
					T18_ASSERT(t < m_hasJob.size() && m_hasJob.size() == m_useCount.size());
					return m_useCount[t];
				}
			};

		}


		class QProxy {
			typedef QProxy self_t;

		protected:
			typedef ::std::uint32_t flags_t;
			typedef ::utils::atomic_flags_set<flags_t> safe_flags_t;

			typedef qsrv_tcp_session<self_t> serv_session_t;

			typedef ::utils::spinlock_guard spinlock_guard_t;

			T18_COMP_SILENCE_OLD_STYLE_CAST;
			typedef ::std::decay_t<decltype(((::qlua::api*)(nullptr))->getNumberOf( (const char*)(nullptr)))> qlua_table_elms_count_t;
			T18_COMP_POP;

			struct TickerInfo {
				const ::std::string tickerClassCode;
				const ::std::string tickerCode;

				const real_t minStepSize;
				const int precision;
				const volume_lots_t lotSize;

				const intTickerId_t tid;

				TickerInfo(const ::std::string& classc, const ::std::string& tc, real_t ss, int p
					, volume_lots_t ls, intTickerId_t _tid) noexcept
					: tickerClassCode(classc), tickerCode(tc), minStepSize(ss), precision(p), lotSize(ls), tid(_tid)
				{}

				::std::string to_string() const {
					char b[256];
					sprintf_s(b, "%s@%s(%u) lots=%u, prec=%d, stepsize=%.6f", tickerClassCode.c_str()
						, tickerCode.c_str(), static_cast<unsigned>(tid), lotSize, precision, minStepSize);
					return ::std::string(b);
				}

				void makePrxyTickerInfo(char* pPkt, const size_t payloadSize)const noexcept {
					T18_UNREF(payloadSize);

					prxyTickerInfo* pPTI = reinterpret_cast<prxyTickerInfo*>(pPkt);
					pPTI->minStepSize = minStepSize;
					pPTI->precision = precision;
					pPTI->lotSize = lotSize;
					pPTI->tid = tid;

					T18_ASSERT(payloadSize >= offsetof(prxyTickerInfo, tid) + 1 + tickerCode.length() + 1 + tickerClassCode.length());
					pPkt = pPTI->_ofsPastEOS();
					::std::memcpy(pPkt, tickerCode.data(), tickerCode.length());

					pPkt += tickerCode.length();
					*pPkt = '@';
					++pPkt;

					::std::memcpy(pPkt, tickerClassCode.data(), tickerClassCode.length());
				}
			};

		public:
			//this structure is allocated for each session in a session object. MUST be default constructible
			//MUST be accessed ONLY from main() thread
			struct SessionData {
			protected:
				//vector of intTickerId_t for which the alltrades data must be send
				::std::vector<intTickerId_t> m_Tickers4AllTrades;

				//vector indexed by intTickerId_t of elements, that defines how many elements of ::qlua::table::all_trades
				//were processes before onAllTrades handler was started
				::std::vector<qlua_table_elms_count_t> m_Tickers2AllTrades_LastInitialNum;

			public:
				~SessionData() {
					T18_ASSERT(0 == m_Tickers4AllTrades.size() || !"Session object MUST call _onDestroy!");
				}
				//the only function that MUST be called from session object during destruction
				void _onDestroy(QProxy& prxy) {
					for (auto t : m_Tickers4AllTrades) {
						prxy._cancelAllTradesFor(t);
					}
					m_Tickers4AllTrades.clear();
				}
				//////////////////////////////////////////////////////////////////////////

				//returns true if had to add new and false if it's already added
				bool tickerTrades_setListen(const intTickerId_t t, const qlua_table_elms_count_t initiallyProcessed) {
					const auto r = !tickerTrades_shouldListen(t);
					if (r) m_Tickers4AllTrades.emplace_back(t);

					if (t >= m_Tickers2AllTrades_LastInitialNum.size()) {
						m_Tickers2AllTrades_LastInitialNum.resize(t + 1, qlua_table_elms_count_t(0));
					}
					m_Tickers2AllTrades_LastInitialNum[t] = initiallyProcessed;
					return r;
				}
				bool tickerTrades_shouldListen(const intTickerId_t t)const noexcept {
					const size_t l = m_Tickers4AllTrades.size();
					for (size_t i = 0; i < l; ++i) {
						if (m_Tickers4AllTrades[i] == t) return true;
					}
					return false;
				}
				void tickerTrades_stopListen(const intTickerId_t t) noexcept{
					auto it = ::std::find(m_Tickers4AllTrades.begin(), m_Tickers4AllTrades.end(), t);
					if (it != m_Tickers4AllTrades.end()) {
						m_Tickers4AllTrades.erase(it);
					}
				}
				auto& tickerTrades_lastInitProcessed(const intTickerId_t t) noexcept {
					T18_ASSERT(m_Tickers2AllTrades_LastInitialNum.size() > t);
					return m_Tickers2AllTrades_LastInitialNum[t];
				}


			};


			//////////////////////////////////////////////////////////////////////////
			//////////////////////////////////////////////////////////////////////////
		protected:
			static constexpr size_t maxDealsToSendOnAllTrades = defaultMaxDealsToSendOnAllTrades;

			static constexpr size_t allTradesQueueInitialCapacity = 1024;

			static constexpr int mainSleepMs = 1000;
			static constexpr int waitOnStopMs = 5 * mainSleepMs;

		public:
			static constexpr long timeoutReadMs = 90 * mainSleepMs;
			static constexpr long timeoutWriteMs = 60 * mainSleepMs;
			static constexpr long timeoutPruneSessionsMs = 60 * mainSleepMs;

			static constexpr ::std::uint16_t serverTcpPort = defaultServerTcpPort;


		protected:
			static constexpr flags_t _flagsQSrv_Run = 1;
			static constexpr flags_t _flagsQSrv_ListenAllTrades = (_flagsQSrv_Run << 1);

			static constexpr flags_t _flagsQSrv__lastUsedBit = _flagsQSrv_ListenAllTrades;

		protected:

			//::std::forward_list<TickerInfo> m_knownTickers;//MUST be accessed/modified under lock only
			//vector is a better option, because of memory locality
			::std::vector<TickerInfo> m_knownTickers;
			
			_impl::Job4TickerId m_gatherAllTrades;

			::std::vector<const TickerInfo*> m_tickerId2Info;

			::qlua::extended* m_mainQ{ nullptr };//qlua object passed to main().
			::utils::spinlock* m_pMainSpinlock{ nullptr };

			::moodycamel::ReaderWriterQueue<prxyTsDeal> m_allTradesQueue;

			safe_flags_t m_flags;

		public:
			QProxy(size_t tickersExpected) : m_gatherAllTrades(tickersExpected)
				, m_allTradesQueue(allTradesQueueInitialCapacity), m_flags(_flagsQSrv_Run)
			{
				m_knownTickers.reserve(tickersExpected);
				m_tickerId2Info.reserve(tickersExpected);
			}

		protected:
			//MUST be called from main() thread only
			intTickerId_t _nextTickerId()const{
				//auto r = m_tickerId2Info.size();
				auto r = m_knownTickers.size();
				//T18_ASSERT(::std::forward_list_size_slow(m_knownTickers) == r);
				if (UNLIKELY(::std::numeric_limits<intTickerId_t>::max() < r))
					throw ::std::runtime_error("WTF? Never expected that much tickers!");

				return static_cast<intTickerId_t>(r);
			}

			//whether the ticker is in the known list
			//MUST be accessed under lock held from any thread, other than the main()
			const TickerInfo* _getKnownTickerInfo(const char* pClassCode, const char* pTickerCode)noexcept {
				for (const auto& e : m_knownTickers) {
					if (e.tickerCode == pTickerCode && e.tickerClassCode == pClassCode) {
						return &e;
					}
				}
				return nullptr;
			}

			//fetches ticker data into known list
			const TickerInfo* _getTickerInfo(const char* pClassCode, const char* pTickerCode) {
				T18_ASSERT(m_mainQ && m_pMainSpinlock);
				const TickerInfo* pTI = _getKnownTickerInfo(pClassCode, pTickerCode);
				if (!pTI) {
					T18_ASSERT(m_mainQ);
					try {
						m_mainQ->getSecurityInfo(pClassCode, pTickerCode, [this, pClassCode, pTickerCode, &pTI](const auto& sec_info) {
							const auto& t = sec_info();
							T18_ASSERT(t.class_code() == pClassCode && t.code() == pTickerCode);

							const auto lotSz = t.lot_size();
							const auto rLotSz = static_cast<decltype(TickerInfo::lotSize)>(lotSz);
							//checks
							T18_COMP_SILENCE_FLOAT_CMP_UNSAFE;
							if (LIKELY(rLotSz == lotSz)) {
								//to safely modify m_knownTickers and m_gatherAllTrades we MUST acquire the lock
								const auto tid = _nextTickerId();
								const auto& cc = t.class_code();
								const auto& c = t.code();
								const auto mps = static_cast<decltype(TickerInfo::minStepSize)>(t.min_price_step());
								const auto sc = static_cast<decltype(TickerInfo::precision)>(t.scale());
								{
									spinlock_guard_t g(*m_pMainSpinlock);
									//#WARNING it may locks here for a long time because of memory allocation, however, not expecting it
									//to be a real problem
									//m_knownTickers.emplace_front(cc, c, mps, sc, rLotSz, tid);
									m_knownTickers.emplace_back(cc, c, mps, sc, rLotSz, tid);
									m_gatherAllTrades.main_notifyNewTicker_locked();
								}
								//pTI = &m_knownTickers.front();
								pTI = &m_knownTickers.back();
								
								//we use m_tickerId2Info only from the main() thread, so there's no need to acquire lock
								T18_ASSERT(m_tickerId2Info.size() == tid);
								m_tickerId2Info.emplace_back(pTI);

							} else {
								//#log
								m_mainQ->message(::std::string("ERROR, ticker ") + pClassCode + "@" + pTickerCode + " has non integer lot size = "
									+ ::std::to_string(lotSz) + ". Ignoring ticker!");
							}
							T18_COMP_POP;
							return 1;//MUST
						});
					} catch (const ::std::exception& e) {
						m_mainQ->message(::std::string("Exception in _fetchTicker@getSecurityInfo for") + pClassCode + "@" + pTickerCode
							+ "\ntext: " + e.what());
					}
				}
				return pTI;
			}

			//#TODO may be not fully implemented yet
			void _cancelAllTradesFor(intTickerId_t t) {
				//T18_ASSERT(m_mainQ && m_pMainSpinlock);
				if (m_pMainSpinlock) {//MUST check, because this routine might be called from destructors
					if (m_gatherAllTrades.main_deregister(*m_pMainSpinlock, t)) {
						m_flags.clear<_flagsQSrv_ListenAllTrades>();
					}
				}
			}

			bool _continueRun()const noexcept { return m_flags.isSet<_flagsQSrv_Run>(); }

		public:
			mxTimestamp hndGetTradeDate() {
				T18_ASSERT(m_mainQ);
				int y, m, d;
				m_mainQ->getTradeDate([&y, &m, &d](const auto& __d) {
					const auto& _d = __d();
					y = static_cast<int>(_d.year());
					m = static_cast<int>(_d.month());
					d = static_cast<int>(_d.day());
					return 1;
				});
				return mxTimestamp(y, m, d, 0, 0, 0);
			}

			void hndMessage(const char* pt) {
				T18_ASSERT(pt);
				//need nullcheck only if hndMessage() is called from QSrv destructor, which is still possible while handling stop() call
				if (m_mainQ) m_mainQ->message(pt);
			}
			void hndMessage(const ::std::string& s) { hndMessage(s.c_str()); }

			bool hndIsQuikConnected()const noexcept {
				T18_ASSERT(m_mainQ);
				return m_mainQ->isConnected();
			}

			void hndGetClassesList(serv_session_t& sess)const noexcept {
				T18_ASSERT(m_mainQ);
				const char* classlist = m_mainQ->getClassesList();
				if (classlist) {
					sess.enqueue_packet(ProtoSrv2Cli::getClassesListResult, classlist);
				} else {
					//#log
					sess.enqueue_packet(ProtoSrv2Cli::requestFailed, "Failed to get getClassesList");
					m_mainQ->message("Failed to get getClassesList");
				}
			}

			void hndListAllTickers(serv_session_t& sess)const noexcept {
				T18_ASSERT(m_mainQ);
				const char* classlist = m_mainQ->getClassesList();
				if (classlist) {
					//parsing classlist to obtain individual class code and querning sec list
					const auto tlen = ::std::strlen(classlist) + 1;
					auto ptr = ::std::make_unique<char[]>(tlen);
					char* pCl = ptr.get();

					::std::memcpy(pCl, classlist, tlen);

					::std::string ret;
					ret.reserve(1024 * 1024);

					do {
						auto pCur = pCl;
						pCl = ::std::strchr(pCur, ',');
						*pCl++ = 0;

						ret += pCur;
						ret += "(";

						ret += m_mainQ->getClassSecurities(pCur);

						ret += ")";
					} while (*pCl);

					sess.enqueue_packet(ProtoSrv2Cli::listAllTickersResult, ret);

				} else {
					//#log
					sess.enqueue_packet(ProtoSrv2Cli::requestFailed, "Failed to get getClassesList");
					m_mainQ->message("Failed to get getClassesList");
				}
			}

		protected:
			static void _postSubscribeAllTradesSuccess(serv_session_t& sess, const TickerInfo* pTI) {
				const auto payloadSize = prxyTickerInfo_Size + pTI->tickerClassCode.length() + pTI->tickerCode.length() + 1;
				char* pPkt = sess._make_packet(ProtoSrv2Cli::subscribeAllTradesResult, payloadSize, true);
				pTI->makePrxyTickerInfo(pPkt, payloadSize);
			}

		public:
			void hndQueryTickerInfo(serv_session_t& sess, const char* pClass, const char* pTicker) {
				T18_ASSERT(m_mainQ && m_pMainSpinlock);
				auto pTI = _getTickerInfo(pClass, pTicker);
				if (LIKELY(pTI)) {
					const auto payloadSize = prxyTickerInfo_Size + pTI->tickerClassCode.length() + pTI->tickerCode.length() + 1;
					char* pPkt = sess._make_packet(ProtoSrv2Cli::queryTickerInfoResult, payloadSize, true);
					pTI->makePrxyTickerInfo(pPkt, payloadSize);
				} else {
					m_mainQ->message(::std::string("Ticker ") + pTicker + "@" + pClass + " NOT FOUND!");
					sess.enqueue_packet(ProtoSrv2Cli::queryTickerInfoResult, ::std::string(pTicker) + "@" + pClass, false);
				}
			}

			// must send subscribeAllTradesResult packet and if successfull a set of AllTrades packets with trades since lastKnownDeal.
			// If already subscribed for the ticker, then must resend trades/deals since lastKnownDeal
			void hndSubscribeAllTrades(serv_session_t& sess, const char* pClass, const char* pTicker, mxTimestamp lastKnownDeal) {
				T18_ASSERT(m_mainQ && m_pMainSpinlock);
				auto pTI = _getTickerInfo(pClass, pTicker);
				if (LIKELY(pTI)) {
					m_mainQ->message(::std::string("Subscribing to ") + pTI->to_string()
						+ ", lastKnownDeal=" + lastKnownDeal.to_string());
					// main() (and therefore all hnd*() functions) works in a separate thread from quik's UI and on*() functions. Also we
					// know nothing about which thread populate all_trades table. Therefore we cannot make any assumptions about
					// how to fetch all the trades/deals from all_trades table and make sure we won't skip anything between the fetching
					// and reading new trades/deal using onAllTrades() callback, besides adopting the following logic:
					// 1. we'd fix some arbitrary, but current n = getNumberOf<::qlua::table::all_trades>() here.
					// 2. we then allow the collection of new trades using onAllTrades() callback.
					// 3. we the fetch all necessary trades using q.getItem<::qlua::table::all_trades>(i, [](const auto& /*allTrdEnt*/) {}));
					//		where i spans between 0 and n (not counting n).
					// 4. when we'll be processing for the first time the queue of all_trades, populated from onAllTrades() callback, we will
					//		at first fetch all the trades between n and the first trade in the queue. Then we'll process the queue.
					// That mechanism will (should?) guarantee (provided that QUIK will not shrink the ::qlua::table::all_trades table)
					// that we will be able to fetch all trades without duplicating or skipping due to multithreading issues.

					const auto nDeals = m_mainQ->getNumberOf<::qlua::table::all_trades>();
					const auto tid = pTI->tid;

					//#note uncomment the next line to induce a delay for testing "invisible" trades (those that added
					//# after nDeals fixation and before enabling alltrades gathering)
					//::std::this_thread::sleep_for(::std::chrono::seconds(15));

					//preparing session object to gather alltrades data
					auto& SD = sess.getSessionData();
					if (SD.tickerTrades_setListen(tid, nDeals)) {
						//allowing the proxy to gather alltrades data 
						m_gatherAllTrades.main_register(*m_pMainSpinlock, tid);
						m_flags.set<_flagsQSrv_ListenAllTrades>();
					} else {
						T18_ASSERT(m_gatherAllTrades.has_job(tid) && m_gatherAllTrades.use_count(tid) > 0);
						T18_ASSERT(m_flags.isSet<_flagsQSrv_ListenAllTrades>());
					}
					
					//sending success
					_postSubscribeAllTradesSuccess(sess, pTI);

					//finally we must send to session object all trades since lastKnownDeal to a trade nDeals
					auto sendr{ sess.make_AllTrades_sender(maxDealsToSendOnAllTrades) };
					mxTimestamp prevTs((tag_mxTimestamp()));//sanity check
					qlua_table_elms_count_t i = 0;// , nFetched = 0;
					try {
						bool bNeverSaidBadTime{ true };
						auto f = [&sendr, pClass, pTicker, lastKnownDeal, tid/*, &nFetched*/, &bNeverSaidBadTime, &prevTs, pQ = m_mainQ]
						(const auto& allTrdEnt)
						{
							const auto& d = allTrdEnt();
							const auto& tickr = d.sec_code();
							if (tickr == pTicker) {
								const auto& cls = d.class_code();
								if (cls == pClass) {
									const ::qlua::c_date_time dt{ d.datetime() };

									//#WARNING it seems, that .mcs at least at QUIK v7.20.1.4 (contrary to QUIK Junior v7.19...) spans 0 to 999999 !
									//mxTimestamp thisTs(dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec, dt.ms * 1000 + dt.mcs);
									T18_ASSERT(dt.ms == dt.mcs / 1000);
									mxTimestamp thisTs(dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec, dt.mcs);

									if (prevTs <= thisTs) {//must be non strict inequality
										if (thisTs > lastKnownDeal) {
											auto* ptr = sendr.newAllTrades();
											//++nFetched;

											const auto flgs = d.flags();
											const bool bShrt = static_cast<bool>(flgs & 1);
											const bool bLng = static_cast<bool>(flgs & 2);
											if (UNLIKELY(!(bLng ^ bShrt))) {
												throw ::std::runtime_error("Invalid flags for alltrade entry");
											}

											ptr->ts = thisTs;
											ptr->pr = static_cast<decltype(ptr->pr)>(d.price());
											ptr->volLots = static_cast<decltype(ptr->volLots)>(d.qty());
											ptr->dealNum = static_cast<decltype(ptr->dealNum)>(d.trade_num());
											ptr->bLong = static_cast<decltype(ptr->bLong)>(bLng);
											ptr->tid = tid;
										}
									} else {
										//#log
										if (bNeverSaidBadTime) {
											bNeverSaidBadTime = false;
											pQ->message("WARNING. This generally not expected to happen at all, but found at least "
												"one trade with incoherent time! It was|will be skipped...");
										}
									}
									prevTs = thisTs;
								}
							}
						};
						for (; i < nDeals; ++i) {
							m_mainQ->getItem<::qlua::table::all_trades>(i, f);
						}
					} catch (const ::std::exception& ex) {
						sendr.exceptionCaught();
						const auto er = ::std::string("Failed to getItem(") + ::std::to_string(i) + ") for "
							+ pTI->to_string() + ", lastKnownDeal=" + lastKnownDeal.to_string()
							+ "\nexception = " + ex.what();
						sess.enqueue_packet(ProtoSrv2Cli::requestFailed, er);
						m_mainQ->message(er);
					}
					sendr.finalize();

// 					m_mainQ->message("Fetched "s + ::std::to_string(nFetched) + " trades for " + pTI->to_string()
// 						+"\nThere're "+::std::to_string(nDeals) + " total");
				} else {
				    m_mainQ->message(::std::string("Ticker ") + pTicker + "@" + pClass + " NOT FOUND!");
					sess.enqueue_packet(ProtoSrv2Cli::subscribeAllTradesResult, ::std::string(pTicker) + "@" + pClass, false);
				}
			}

		protected:
			void _handleNewAllTrades(QSrv<self_t>& srv) {
				T18_ASSERT(m_mainQ);
				const auto nSess = srv.sessionCount();
				if (nSess && m_allTradesQueue.peek()) {
					//1. for each session create an AllTrades_sender object for faster data packaging
					typedef ::std::decay_t<decltype(srv)>::AllTrades_Sender_t AllTrades_Sender_t;
					::std::vector<AllTrades_Sender_t> senders;
					senders.reserve(nSess);

					srv.for_each_session([&senders](serv_session_t& sess) {
						//sess is guaranteed to be valid during the callback duration, however, because of we
						//work in the same thread as the one that communicates with the network, sess object should be
						// valid at least until next io_context.run() call.
						senders.emplace_back(sess.make_AllTrades_sender(maxDealsToSendOnAllTrades));
					});

					//session might be present in the list of server's sessions, however, it might already be expired and it
					//won't be used to call for_each_session()'s callback.
					if (LIKELY(!senders.empty())) {
						//2. dequeueing alltrades and passing them to corresponding sender 
						prxyTsDeal e;
						while (m_allTradesQueue.try_dequeue(e)) {
							for (auto& sendr : senders) {
								auto& SD = sendr.getSession().getSessionData();
								const auto tid = e.tid;
								if (SD.tickerTrades_shouldListen(tid)) {
									//if it is the first run for the ticker&session, we must check whether there were trades
									// added before onAllTrade() callback was fired and after we processed last deal 
									// in hndSubscribeAllTrades()
									auto& refnDeals = SD.tickerTrades_lastInitProcessed(tid);
									if (UNLIKELY(refnDeals)) {//rare case
										auto lastNDeals = refnDeals;
										refnDeals = 0;//the check must only be done once

										unsigned nUpd = 0;
										decltype(lastNDeals) tot = 0;

										try {
											auto f = [&sendr, &nUpd, tid, dealNum = e.dealNum, this](const auto& allTrdEnt)
											{
												const auto& d = allTrdEnt();
												const dealnum_t thisDealNum = static_cast<dealnum_t>(d.trade_num());
												if (thisDealNum < dealNum) {
													const auto& tickr = d.sec_code();
													const auto& cls = d.class_code();
													const auto pTI = _getKnownTickerInfo(cls.c_str(), tickr.c_str());
													if (pTI && tid == pTI->tid) {
														//ticker suits, deal happened before the one found in queue,
														// so sending the alltrade to the session recepient
														++nUpd;

														const ::qlua::c_date_time dt{ d.datetime() };
														//#WARNING it seems, that .mcs at least at QUIK v7.20.1.4 (contrary to QUIK Junior v7.19...) spans 0 to 999999 !
														//mxTimestamp thisTs(dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec, dt.ms * 1000 + dt.mcs);
														T18_ASSERT(dt.ms == dt.mcs / 1000);
														mxTimestamp thisTs(dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec, dt.mcs);

														auto* ptr = sendr.newAllTrades();
														const auto flgs = d.flags();
														const bool bShrt = static_cast<bool>(flgs & 1);
														const bool bLng = static_cast<bool>(flgs & 2);
														if (UNLIKELY(!(bLng ^ bShrt))) {
															throw ::std::runtime_error("Invalid flags for alltrade entry");
														}

														ptr->ts = thisTs;
														ptr->pr = static_cast<decltype(ptr->pr)>(d.price());
														ptr->volLots = static_cast<decltype(ptr->volLots)>(d.qty());
														ptr->dealNum = static_cast<decltype(ptr->dealNum)>(d.trade_num());
														ptr->bLong = static_cast<decltype(ptr->bLong)>(bLng);
														ptr->tid = tid;
													}
												}
											};

											//note that we must NOT fix nDeals, declaring it before m_allTradesQueue.try_dequeue() happen
											const auto nDeals = m_mainQ->getNumberOf<::qlua::table::all_trades>();
											tot = nDeals - lastNDeals;
											//checking the alltrades betweeen lastNDeals and current nDeals
											for (; lastNDeals < nDeals; ++lastNDeals) {
												m_mainQ->getItem<::qlua::table::all_trades>(lastNDeals, f);
											}
										} catch (const ::std::exception& ex) {
											sendr.exceptionCaught();
											const auto er = ::std::string("Failed to getItem(") + ::std::to_string(lastNDeals) 
												+ ") during processing lagged trades in _handleNewAllTrades\nexception = " + ex.what();
											sendr.getSession().enqueue_packet(ProtoSrv2Cli::requestFailed, er);
											m_mainQ->message(er);
										}
									
										m_mainQ->message("Checked "s + ::std::to_string(tot) + " deals, found "
											+ ::std::to_string(nUpd) + " invisible trades to send");
									}

									auto* ptr = sendr.newAllTrades();
									//#todo it is possible to avoid additional struct prxyTsData data copying entirely
									//if one to devide m_allTradesQueue into two separate queues, this first one of which
									//holds tid only. Then dequeueing the second directly into *ptr.
									// The possible issue however is in possible desynchronization of queues (may or may not
									// happen at all - should investigate it)
									*ptr = e;
								}
							}
						}

						//finalizing senders
						for (auto& sendr : senders) {
							sendr.finalize();
							sendr.getSession().write_responce();
						}
					}
				}
			}

		public:
			//////////////////////////////////////////////////////////////////////////
			////////////////////////////////////////////////////////////////////////// 
			//main() as well as hnd*() runs in same separate thread context from on*() callbacks (whick runs in Quik's UI thread)
			void main(::qlua::extended& q, ::utils::spinlock& sl) {
				m_mainQ = &q;
				m_pMainSpinlock = &sl;

				QSrv<self_t> qsrv(*this, serverTcpPort);

				do {
					_handleNewAllTrades(qsrv);

					qsrv.run_for(::std::chrono::milliseconds(mainSleepMs));

					//if (_continueRun()) ::std::this_thread::sleep_until(processingBeginTime + ::std::chrono::milliseconds(mainSleepMs));
				} while (_continueRun());
				qsrv.stop();

				m_pMainSpinlock = nullptr;
				m_mainQ = nullptr;
			}

			//////////////////////////////////////////////////////////////////////////
			// All on*() callbacks are run inside Quik's UI thread, so they must return as fast as possible

			int onStop(const ::qlua::extended& q/*, int signal*/)noexcept {
				T18_UNREF(q);
				//q.message("in onstop()");
				m_flags.clear<_flagsQSrv_Run>();
				return waitOnStopMs;
			}

			bool listenAllTrade()const noexcept { return m_flags.isSet<_flagsQSrv_ListenAllTrades>(); }
			void onAllTrade(/*::qlua::extended& q,*/ ::lua::entity<::lua::type_policy<::qlua::table::all_trades>> data) {
				T18_ASSERT(listenAllTrade());

				const auto& d = data();
				const auto& tickerCode = d.sec_code();
				const auto& classCode = d.class_code();

				const auto pTI = _getKnownTickerInfo(classCode.c_str(), tickerCode.c_str());
				if (pTI) {
					const auto tid = pTI->tid;
					//the lock is already acquired
					if (m_gatherAllTrades.has_job(tid)) {
						//enqueing for processing
						const ::qlua::c_date_time dt{ d.datetime() };

						//#WARNING it seems, that .mcs at least at QUIK v7.20.1.4 (contrary to QUIK Junior v7.19...) spans 0 to 999999 !
						//mxTimestamp thisTs(dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec, dt.ms * 1000 + dt.mcs);
						T18_ASSERT(dt.ms == dt.mcs / 1000);
						mxTimestamp thisTs(dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec, dt.mcs);

						const auto flgs = d.flags();
						const bool bShrt = static_cast<bool>(flgs & 1);
						const bool bLng = static_cast<bool>(flgs & 2);
						if (UNLIKELY(!(bLng ^ bShrt))) {
							throw ::std::runtime_error("Invalid flags for alltrade entry");
						}

						m_allTradesQueue.emplace(thisTs, static_cast<decltype(::std::declval<prxyTsDeal>().pr)>(d.price())
							, static_cast<decltype(::std::declval<prxyTsDeal>().volLots)>(d.qty())
							, static_cast<decltype(::std::declval<prxyTsDeal>().dealNum)>(d.trade_num())
							, static_cast<decltype(::std::declval<prxyTsDeal>().bLong)>(bLng), tid
						);
					}
				}
			}
		};


	}
}

