#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct lua_State;

namespace Actors
{
	// Owns data, never a TValue, registry reference or pointer into a source VM.
	class Transfer
	{
	  public:
		explicit Transfer( lua_State* L, int First );
		void Push( lua_State* L ) const;
		[[nodiscard]] int Count( ) const;

	  private:
		struct Value
		{
			int Type = 0;
			double Number = 0;
			float Vector[ 3 ]{ };
			std::string Bytes;
			size_t Table = 0;
			std::shared_ptr<void> Native;
		};
		using Table = std::vector<std::pair<Value, Value>>;
		Value Read( lua_State* L, int Index, size_t Depth );
		void PushValue( lua_State* L, const Value& Item, int Tables ) const;
		void Account( size_t Bytes );
		std::vector<Value> m_Arguments;
		std::vector<Table> m_Tables;
		std::unordered_map<const void*, size_t> m_Seen;
		size_t m_Bytes = 0;
		size_t m_Values = 0;
	};
}
