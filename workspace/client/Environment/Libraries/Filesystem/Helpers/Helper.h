#pragma once
#include <Windows.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Filesystem::Helpers
{
	class Handle
	{
	  public:
		Handle( ) = default;
		explicit Handle( HANDLE Value );
		~Handle( );
		Handle( Handle&& Other ) noexcept;
		Handle& operator=( Handle&& Other ) noexcept;
		Handle( const Handle& ) = delete;
		Handle& operator=( const Handle& ) = delete;
		HANDLE Get( ) const;
		explicit operator bool( ) const;

	  private:
		HANDLE m_Value = INVALID_HANDLE_VALUE;
	};

	class Storage
	{
	  public:
		explicit Storage( const std::filesystem::path& Root );
		std::string Read( std::string_view Path );
		void Write( std::string_view Path, std::string_view Data, bool Append = false );
		bool Exists( std::string_view Path, bool Directory );
		void MakeFolder( std::string_view Path );
		void Remove( std::string_view Path, bool Directory );
		std::vector<std::string> List( std::string_view Path );
		static std::vector<std::wstring> Parse( std::string_view Path );
		static void CheckWritable( const std::wstring& Name );
		static std::string AssetName( std::string_view Path, std::string_view Data );

	  private:
		struct Entry
		{
			std::wstring Name;
			bool Directory;
		};

		std::vector<Handle> OpenParents( const std::vector<std::wstring>& Parts, bool Create );
		static Handle Open( HANDLE Parent, const std::wstring& Name, ACCESS_MASK Access, ULONG Disposition, bool Directory, bool Optional = false );
		static std::vector<Entry> Enumerate( HANDLE Directory );
		static void RemoveTree( HANDLE Directory, size_t Depth, size_t& Remaining );
		static void Erase( HANDLE File );
		std::vector<Handle> m_Ancestors;
		Handle m_Root;
		std::mutex m_Mutex;
	};

	std::shared_ptr<Storage> Workspace( );
	std::string CustomAsset( const std::shared_ptr<Storage>& Source, std::string_view Path );
}
