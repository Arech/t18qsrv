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
// T18QSrv.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "qproxy.h"

T18_COMP_SILENCE_ZERO_AS_NULLPTR;

#ifdef LUA53
static struct luaL_Reg ls_lib[] = {
	{ NULL, NULL }
};
#else
static struct luaL_reg ls_lib[] = {
	{ NULL, NULL }
};
#endif

T18_COMP_POP;

//////////////////////////////////////////////////////////////////////////


//pointer created&initialized during library load and destroyed AFTER my_main() ends.
//Generally, it's unsafe, because library loading+callbacks and my_main() work in different threads, but
// Quik architecture guarantees that no callback will be called after onStop() and therefore my_main() end should be
// the last one, who sees gPtr. However, should the main() crash, or shutdown by intent, the pointer may be deleted
// while still in use. So generally, every callback handler MUST acquire spinlock during using QProxy and
// that very same spinlock must also be acquired before object deletion starts

static ::t18::proxy::QProxy* gPtr{ nullptr };
static constexpr size_t expectedTickers = 4u;



T18_COMP_SILENCE_REQ_GLOBAL_CONSTR;
static ::utils::spinlock gPtrLock;
typedef ::utils::spinlock_guard spinlock_guard_t;

//only for debugging during main() active
static ::std::atomic<::qlua::extended*> gpQ{ nullptr };

//main() function runs inside dedicated thread by Quik
//every other callback function runs inside Quiks' UI thread and should be as fast to complete as possible.
void my_main(::lua::state& l) {
	::qlua::extended q(l);
	gpQ = &q;
	//q.message("T18 - Starting main handler");
	
	if (LIKELY(gPtr)) {
		try {
			gPtr->main(q, gPtrLock);
		} catch (const ::std::exception& e) {
			//#log
			q.message(::std::string("t18 main() exception: ") + e.what());
		} catch (...) {
			//#log
			q.message("t18 - non ::std exception was thrown from main()!");
		}
		
		//once main finishes, no callback function should ever be called,
		// so it seems that we generally don't have to worry about memory synchronization between threads here.
		// However, if we shutdown the main() ourselves or exception is thrown, then after `delete gPtr`
		// and setting gPtr to null being visible
		// in callbacks thread, some callbacks may fire and produce crash. So, we must enter the same critical section
		// that guards QProxy usage and delete it from there.

		//we must acquire spinlock here to make sure no callback is running during object deletion
		//while (gPtrLock.test_and_set(::std::memory_order_acquire));
		spinlock_guard_t _grd(gPtrLock);
		if (LIKELY(gPtr)) {
			auto p = gPtr;
			gPtr = nullptr;
			delete p;
		} else {
			//#log
			q.message("T18 - gPtr not exists after main completion!");
		}
		//gPtrLock.clear(::std::memory_order_release);               // release spinlock
	} else {
		//#log
		q.message("T18 - gPtr not exists in main!");
	}
	gpQ = nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Функция вызывается терминалом QUIK при остановке скрипта из диалога управления и при
// закрытии терминала QUIK.
// [NUMBER time_out] OnStop(NUMBER flag)
// Функция возвращает количество миллисекунд, которое дается скрипту на завершение работы.
// При остановке или удалении работающего скрипта Lua из диалога управления «Доступные
// скрипты» параметр вызова flag принимает значение «1».При закрытии терминала QUIK –
// значение «2».
::std::tuple<int> OnStop(const ::lua::state& l, ::lua::entity<::lua::type_policy<int>> signal) {
	T18_UNREF(signal);

	qlua::extended q(l);
	int st;

	//while (gPtrLock.test_and_set(::std::memory_order_acquire));  // acquire spinlock, that prevents gPtr premature deletion
	spinlock_guard_t _grd(gPtrLock);
	if (LIKELY(gPtr)) {
		st = gPtr->onStop(q);
	} else {
		//#log
		q.message("T18 - gPtr not exists onStop");
		st = 1;
	}
	//gPtrLock.clear(::std::memory_order_release);               // release spinlock
	return ::std::make_tuple(st);
}

//////////////////////////////////////////////////////////////////////////
void OnAllTrade(const ::lua::state& l, ::lua::entity<::lua::type_policy<::qlua::table::all_trades>> data) {
	//while (gPtrLock.test_and_set(::std::memory_order_acquire));  // acquire spinlock, that prevents gPtr premature deletion
	spinlock_guard_t _grd(gPtrLock);
	if (LIKELY(gPtr)) {
		if (gPtr->listenAllTrade()) {
			//qlua::extended q(l);
			try {
				gPtr->onAllTrade(/*q,*/ data);
			}catch(const ::std::exception& e){
				//#log
				qlua::extended q(l);
				q.message(::std::string("t18 OnAllTrade() exception: ") + e.what());
			} catch (...) {
				//#log
				qlua::api q(l);
				q.message("t18 - non ::std exception was thrown from onAllTrade!");
			}			
		}
	} else {
		//#log
		qlua::api q(l);
		q.message("T18 - gPtr not exists OnAllTrade");
	}
	//gPtrLock.clear(::std::memory_order_release);               // release spinlock
}


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////// 

LUACPP_STATIC_FUNCTION2(main, my_main)
LUACPP_STATIC_FUNCTION3(OnAllTrade, OnAllTrade, ::qlua::table::all_trades)
LUACPP_STATIC_FUNCTION3(OnStop, OnStop, int)

extern "C" {
	LUALIB_API int luaopen_t18qsrv(lua_State *L) {
		::lua::state l(L);

		if (UNLIKELY(gPtr)) {
			//#todo log
			qlua::api q(l);
			q.message("T18 - gPtr exists during loading, WTF?");
		} else {
			gPtr = new ::t18::proxy::QProxy(expectedTickers);
			T18_ASSERT(gPtr);

			::lua::function::main().register_in_lua(l, my_main);
			::lua::function::OnStop().register_in_lua(l, OnStop);
			::lua::function::OnAllTrade().register_in_lua(l, OnAllTrade);

		#ifdef LUA53
			::lua_newtable(L);
			::luaL_setfuncs(L, ls_lib, 0);
			::lua_setglobal(L, "t18qsrv");
		#else
			::luaL_openlib(L, "t18qsrv", ls_lib, 0);
		#endif
		}
		return 0;
	}
}

#ifdef BOOST_ENABLE_ASSERT_HANDLER

namespace boost {
	T18_COMP_SILENCE_MISSING_NORETURN;
	void assertion_failed_msg(char const * expr, char const * msg, char const * function, char const * file, long line) {
		if (gpQ.load()) {
			static constexpr int bl = 4096;
			auto buf = ::std::make_unique<char[]>(bl);
			sprintf_s(buf.get(), bl, "*** Boost assertion failed @%s\nfunc=%s@d\nexpr=(%s)\n%s", file, function, line, expr, msg ? msg : "");
			//#warning that is very bad idea, but for the last try ok.
			if (gpQ.load()) gpQ.load()->message(buf.get());
		}
		T18_ASSERT(!"Boost assertion failed!");
		::std::terminate();
	}

	void assertion_failed(char const * expr, char const * function, char const * file, long line) {
		assertion_failed_msg(expr, nullptr, function, file, line);
	}
	T18_COMP_POP;

} // namespace boost

#endif
