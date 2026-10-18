#pragma once
#include <lua.h>
#include <memory>

class Environment
{
  public:
	Environment( ) = default;
	explicit Environment( lua_State* State );
	~Environment( );

	Environment( const Environment& ) = delete;
	Environment& operator=( const Environment& ) = delete;

	bool Create( lua_State* State );
	void Destroy( );

	bool Valid( ) const;

	lua_State* State( ) const;

	void Push( lua_State* Target ) const;
	void PushLocal( lua_State* Target ) const;
	void Register( const char* Name, lua_CFunction Function );

  private:
	lua_State* m_State = nullptr;
};

using EnvironmentRef = std::shared_ptr<Environment>;

extern EnvironmentRef g_Alpen;
extern EnvironmentRef g_Roblox;

namespace Env
{
	void Initialize( lua_State* RobloxState );
	void Shutdown( );

	void RegisterGlobal( const char* Name, lua_CFunction Function );
	void RegisterMethod( const char* Name, lua_CFunction Function );
}
