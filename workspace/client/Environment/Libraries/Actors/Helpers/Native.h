#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct lua_State;

namespace Actors::Native
{
	using Instance = std::shared_ptr<uintptr_t>;
	struct Thread
	{
		virtual ~Thread( ) = default;
		[[nodiscard]] virtual std::shared_ptr<lua_State> Lock( ) const = 0;
		Instance Actor;
	};
	using ThreadRef = std::shared_ptr<Thread>;
	struct Snapshot
	{
		std::vector<Instance> Actors;
		std::vector<ThreadRef> Threads;
	};

	[[nodiscard]] uint64_t Generation( );
	[[nodiscard]] bool Parallel( lua_State* L );
	[[nodiscard]] Snapshot Enumerate( lua_State* L );
	[[nodiscard]] ThreadRef CreateTarget( lua_State* L, const Instance& Actor );
	[[nodiscard]] bool Deleted( const Instance& Value );
	[[nodiscard]] Instance CheckInstance( lua_State* L, int Index, const char* Class );
	void PushInstance( lua_State* L, const Instance& Value );
	[[nodiscard]] std::shared_ptr<void> ReadValue( lua_State* L, int Index );
	void PushValue( lua_State* L, const std::shared_ptr<void>& Value );
	[[nodiscard]] std::string Compile( const std::string& Source );
	void Prepare( lua_State* Thread, const Instance& Actor );
	void Load( lua_State* Thread, const std::string& Bytecode );
	void Enqueue( lua_State* Thread, int Arguments );
}
