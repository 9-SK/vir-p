#include "Helper.h"
#include <ShlObj.h>
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <winternl.h>

#pragma comment( lib, "bcrypt.lib" )
#pragma comment( lib, "shell32.lib" )
#pragma comment( lib, "ole32.lib" )

namespace Filesystem::Helpers
{
	namespace
	{
		constexpr size_t MaximumFile = 256 * 1024 * 1024;
		constexpr size_t MaximumEntries = 100000;
		constexpr ULONG OpenExisting = 1;
		constexpr ULONG OpenOrCreate = 3;
		constexpr ULONG DirectoryOnly = 0x1;
		constexpr ULONG Synchronous = 0x20;
		constexpr ULONG FileOnly = 0x40;
		constexpr ULONG OpenReparsePoint = 0x200000;
		constexpr ULONG DontReparse = 0x1000;
		using NativeCreate = NTSTATUS( NTAPI* )( PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG );
		using StatusError = ULONG( NTAPI* )( NTSTATUS );

		void Fail( const char* Message, DWORD Code = GetLastError( ) )
		{
			throw std::system_error( static_cast<int>( Code ), std::system_category( ), Message );
		}

		std::wstring Wide( std::string_view Text )
		{
			if ( Text.empty( ) )
				return { };
			if ( Text.size( ) > 32760 )
				throw std::runtime_error( "path is too long" );
			const int Count = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, Text.data( ), static_cast<int>( Text.size( ) ), nullptr, 0 );
			if ( !Count )
				throw std::runtime_error( "path must be valid UTF-8" );
			std::wstring Result( Count, L'\0' );
			MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, Text.data( ), static_cast<int>( Text.size( ) ), Result.data( ), Count );
			return Result;
		}

		std::string Narrow( std::wstring_view Text )
		{
			const int Count = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, Text.data( ), static_cast<int>( Text.size( ) ), nullptr, 0, nullptr, nullptr );
			if ( !Count )
				throw std::runtime_error( "filename cannot be represented as UTF-8" );
			std::string Result( Count, '\0' );
			WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, Text.data( ), static_cast<int>( Text.size( ) ), Result.data( ), Count, nullptr, nullptr );
			return Result;
		}

		std::wstring Lower( std::wstring Value )
		{
			for ( wchar_t& Character : Value )
				if ( Character >= L'A' && Character <= L'Z' )
					Character += L'a' - L'A';
			return Value;
		}

		void Validate( HANDLE File, bool Directory )
		{
			BY_HANDLE_FILE_INFORMATION Info{ };
			if ( !GetFileInformationByHandle( File, &Info ) )
				Fail( "cannot inspect file" );
			if ( Info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT )
				throw std::runtime_error( "reparse points are not allowed" );
			if ( ( ( Info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 ) != Directory )
				throw std::runtime_error( Directory ? "expected a folder" : "expected a file" );
			if ( !Directory && Info.nNumberOfLinks != 1 )
				throw std::runtime_error( "hard links are not allowed" );
		}

		std::filesystem::path LocalData( )
		{
			PWSTR Value = nullptr;
			if ( FAILED( SHGetKnownFolderPath( FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &Value ) ) )
				throw std::runtime_error( "local application storage is unavailable" );
			std::unique_ptr<wchar_t, decltype( &CoTaskMemFree )> Owner( Value, CoTaskMemFree );
			return std::filesystem::path( Value );
		}

		std::filesystem::path Content( )
		{
			std::wstring Buffer( 32768, L'\0' );
			const DWORD Length = GetModuleFileNameW( nullptr, Buffer.data( ), static_cast<DWORD>( Buffer.size( ) ) );
			if ( !Length || Length >= Buffer.size( ) )
				Fail( "cannot locate client content directory" );
			Buffer.resize( Length );
			return std::filesystem::path( Buffer ).parent_path( ) / L"content" / L"alpen";
		}
	}

	Handle::Handle( HANDLE Value ) : m_Value( Value )
	{
	}

	Handle::~Handle( )
	{
		if ( *this )
			CloseHandle( m_Value );
	}

	Handle::Handle( Handle&& Other ) noexcept : m_Value( std::exchange( Other.m_Value, INVALID_HANDLE_VALUE ) )
	{
	}

	Handle& Handle::operator=( Handle&& Other ) noexcept
	{
		if ( this != &Other )
		{
			if ( *this )
				CloseHandle( m_Value );
			m_Value = std::exchange( Other.m_Value, INVALID_HANDLE_VALUE );
		}
		return *this;
	}

	HANDLE Handle::Get( ) const
	{
		return m_Value;
	}

	Handle::operator bool( ) const
	{
		return m_Value && m_Value != INVALID_HANDLE_VALUE;
	}

	std::vector<std::wstring> Storage::Parse( std::string_view Path )
	{
		std::wstring Text = Wide( Path );
		std::replace( Text.begin( ), Text.end( ), L'\\', L'/' );
		if ( !Text.empty( ) && Text.front( ) == L'/' )
			throw std::runtime_error( "absolute paths are not allowed" );
		std::vector<std::wstring> Parts;
		for ( size_t Begin = 0; Begin <= Text.size( ); )
		{
			const size_t End = Text.find( L'/', Begin );
			std::wstring Part = Text.substr( Begin, End == std::wstring::npos ? Text.size( ) - Begin : End - Begin );
			if ( Part == L".." )
			{
				if ( Parts.empty( ) )
					throw std::runtime_error( "path escapes the workspace" );
				Parts.pop_back( );
			}
			else if ( !Part.empty( ) && Part != L"." )
			{
				if ( Part.size( ) > 255 || Part.back( ) == L'.' || Part.back( ) == L' ' )
					throw std::runtime_error( "invalid path component" );
				for ( wchar_t Character : Part )
					if ( Character < 32 || Character == 127 || std::wstring_view( L":*?\"<>|" ).find( Character ) != std::wstring_view::npos )
						throw std::runtime_error( "invalid path character" );
				const std::wstring Stem = Lower( Part.substr( 0, Part.find( L'.' ) ) );
				if ( Stem == L"con" || Stem == L"prn" || Stem == L"aux" || Stem == L"nul" || Stem == L"conin$" || Stem == L"conout$" || Stem == L"clock$" ||
					 ( Stem.size( ) == 4 && ( Stem.starts_with( L"com" ) || Stem.starts_with( L"lpt" ) ) && std::wstring_view( L"0123456789\u00b9\u00b2\u00b3" ).find( Stem[ 3 ] ) != std::wstring_view::npos ) )
					throw std::runtime_error( "device names are not allowed" );
				Parts.push_back( std::move( Part ) );
				if ( Parts.size( ) > 128 )
					throw std::runtime_error( "path is too deep" );
			}
			if ( End == std::wstring::npos )
				break;
			Begin = End + 1;
		}
		return Parts;
	}

	void Storage::CheckWritable( const std::wstring& Name )
	{
		static constexpr std::wstring_view Blocked[] = {
			L".exe", L".com", L".scr", L".pif", L".cpl", L".msc", L".bat", L".cmd", L".ps1", L".psd1", L".vbs", L".vbe",
			L".js", L".jse", L".wsf", L".wsh", L".hta", L".scf", L".lnk", L".zip", L".rar", L".7z", L".cab", L".iso", L".img",
			L".xml", L".msi", L".msp", L".reg", L".inf", L".url", L".dll", L".sys", L".drv", L".ocx", L".psm1", L".ps1xml",
			L".ps2", L".ps2xml", L".msh", L".msh1", L".msh2", L".mshxml", L".msh1xml", L".msh2xml", L".sct", L".ws",
			L".sh", L".bash", L".zsh", L".py", L".pyw", L".rb", L".pl", L".jar", L".class", L".chm", L".gadget",
			L".application", L".appref-ms", L".settingcontent-ms", L".diagcab", L".vhd", L".vhdx", L".dmg", L".pkg", L".app",
			L".desktop", L".service", L".xll", L".xlam", L".docm", L".xlsm", L".pptm", L".msix", L".appx", L".msixbundle", L".appxbundle" };
		const size_t Dot = Name.rfind( L'.' );
		if ( Dot == std::wstring::npos )
			return;
		const std::wstring Extension = Lower( Name.substr( Dot ) );
		if ( std::find( std::begin( Blocked ), std::end( Blocked ), Extension ) != std::end( Blocked ) )
			throw std::runtime_error( "file extension is blocked" );
	}

	Handle Storage::Open( HANDLE Parent, const std::wstring& Name, ACCESS_MASK Access, ULONG Disposition, bool Directory, bool Optional )
	{
		static const auto Create = reinterpret_cast<NativeCreate>( GetProcAddress( GetModuleHandleW( L"ntdll.dll" ), "NtCreateFile" ) );
		static const auto Error = reinterpret_cast<StatusError>( GetProcAddress( GetModuleHandleW( L"ntdll.dll" ), "RtlNtStatusToDosError" ) );
		if ( !Create || !Error )
			throw std::runtime_error( "relative file access is unavailable" );
		UNICODE_STRING Text{ };
		Text.Buffer = const_cast<PWSTR>( Name.data( ) );
		Text.Length = Text.MaximumLength = static_cast<USHORT>( Name.size( ) * sizeof( wchar_t ) );
		OBJECT_ATTRIBUTES Attributes{ };
		Attributes.Length = sizeof( Attributes );
		Attributes.RootDirectory = Parent;
		Attributes.ObjectName = &Text;
		Attributes.Attributes = OBJ_CASE_INSENSITIVE | DontReparse;
		IO_STATUS_BLOCK Status{ };
		HANDLE Result = INVALID_HANDLE_VALUE;
		const NTSTATUS Code = Create( &Result, Access | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &Attributes, &Status, nullptr,
									  FILE_ATTRIBUTE_NORMAL, Directory ? FILE_SHARE_READ | FILE_SHARE_WRITE : FILE_SHARE_READ, Disposition,
									  Synchronous | OpenReparsePoint | ( Directory ? DirectoryOnly : FileOnly ), nullptr, 0 );
		if ( Code < 0 )
		{
			const DWORD WinError = Error( Code );
			if ( Optional && ( WinError == ERROR_FILE_NOT_FOUND || WinError == ERROR_PATH_NOT_FOUND ) )
				return { };
			Fail( "cannot open workspace entry", WinError );
		}
		Handle File( Result );
		Validate( File.Get( ), Directory );
		return File;
	}

	Storage::Storage( const std::filesystem::path& Root )
	{
		const auto Path = Root.lexically_normal( );
		const auto Drive = Path.root_name( ).wstring( );
		if ( !Path.is_absolute( ) || Drive.size( ) != 2 || Drive[ 1 ] != L':' )
			throw std::runtime_error( "storage root must be a local absolute path" );
		Handle Current( CreateFileW( ( Drive + L"\\" ).c_str( ), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
									 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr ) );
		if ( !Current )
			Fail( "cannot open storage volume" );
		Validate( Current.Get( ), true );
		for ( const auto& Part : Path.relative_path( ) )
		{
			if ( Part.empty( ) || Part == L"." )
				continue;
			if ( Part == L".." )
				throw std::runtime_error( "invalid storage root" );
			Handle Next = Open( Current.Get( ), Part.wstring( ), FILE_LIST_DIRECTORY, OpenOrCreate, true );
			m_Ancestors.push_back( std::move( Current ) );
			Current = std::move( Next );
		}
		m_Root = std::move( Current );
	}

	std::vector<Handle> Storage::OpenParents( const std::vector<std::wstring>& Parts, bool Create )
	{
		std::vector<Handle> Result;
		for ( size_t Index = 0; Index + 1 < Parts.size( ); ++Index )
			Result.push_back( Open( Result.empty( ) ? m_Root.Get( ) : Result.back( ).Get( ), Parts[ Index ], FILE_LIST_DIRECTORY, Create ? OpenOrCreate : OpenExisting, true ) );
		return Result;
	}

	std::string Storage::Read( std::string_view Path )
	{
		const auto Parts = Parse( Path );
		if ( Parts.empty( ) )
			throw std::runtime_error( "expected a file path" );
		std::lock_guard Lock( m_Mutex );
		auto Parents = OpenParents( Parts, false );
		auto File = Open( Parents.empty( ) ? m_Root.Get( ) : Parents.back( ).Get( ), Parts.back( ), FILE_READ_DATA, OpenExisting, false );
		LARGE_INTEGER Size{ };
		if ( !GetFileSizeEx( File.Get( ), &Size ) )
			Fail( "cannot read file size" );
		if ( Size.QuadPart < 0 || Size.QuadPart > MaximumFile )
			throw std::runtime_error( "file exceeds the size limit" );
		std::string Result( static_cast<size_t>( Size.QuadPart ), '\0' );
		for ( size_t Position = 0; Position < Result.size( ); )
		{
			DWORD Count = 0;
			if ( !ReadFile( File.Get( ), Result.data( ) + Position, static_cast<DWORD>( ( std::min ) ( Result.size( ) - Position, size_t( 1048576 ) ) ), &Count, nullptr ) )
				Fail( "cannot read file" );
			if ( !Count )
				throw std::runtime_error( "file changed while being read" );
			Position += Count;
		}
		return Result;
	}

	void Storage::Write( std::string_view Path, std::string_view Data, bool Append )
	{
		const auto Parts = Parse( Path );
		if ( Parts.empty( ) )
			throw std::runtime_error( "expected a file path" );
		CheckWritable( Parts.back( ) );
		if ( Data.size( ) > MaximumFile )
			throw std::runtime_error( "file exceeds the size limit" );
		std::lock_guard Lock( m_Mutex );
		auto Parents = OpenParents( Parts, true );
		auto File = Open( Parents.empty( ) ? m_Root.Get( ) : Parents.back( ).Get( ), Parts.back( ), FILE_WRITE_DATA, OpenOrCreate, false );
		std::vector<std::byte> NameBuffer( 65536 );
		if ( !GetFileInformationByHandleEx( File.Get( ), FileNameInfo, NameBuffer.data( ), static_cast<DWORD>( NameBuffer.size( ) ) ) )
			Fail( "cannot inspect destination name" );
		const auto* NameInfo = reinterpret_cast<const FILE_NAME_INFO*>( NameBuffer.data( ) );
		if ( NameInfo->FileNameLength > NameBuffer.size( ) - offsetof( FILE_NAME_INFO, FileName ) || NameInfo->FileNameLength % sizeof( wchar_t ) )
			throw std::runtime_error( "invalid destination name" );
		const std::wstring ActualName( NameInfo->FileName, NameInfo->FileNameLength / sizeof( wchar_t ) );
		CheckWritable( ActualName.substr( ActualName.find_last_of( L"\\/" ) + 1 ) );
		LARGE_INTEGER Size{ };
		if ( !GetFileSizeEx( File.Get( ), &Size ) )
			Fail( "cannot read file size" );
		if ( Append && ( Size.QuadPart < 0 || static_cast<uint64_t>( Size.QuadPart ) > MaximumFile - Data.size( ) ) )
			throw std::runtime_error( "file exceeds the size limit" );
		LARGE_INTEGER Position{ };
		if ( !SetFilePointerEx( File.Get( ), Position, nullptr, Append ? FILE_END : FILE_BEGIN ) || ( !Append && !SetEndOfFile( File.Get( ) ) ) )
			Fail( "cannot position file" );
		for ( size_t Position = 0; Position < Data.size( ); )
		{
			DWORD Count = 0;
			if ( !WriteFile( File.Get( ), Data.data( ) + Position, static_cast<DWORD>( ( std::min ) ( Data.size( ) - Position, size_t( 1048576 ) ) ), &Count, nullptr ) )
				Fail( "cannot write file" );
			if ( !Count )
				throw std::runtime_error( "file write made no progress" );
			Position += Count;
		}
	}

	bool Storage::Exists( std::string_view Path, bool Directory )
	{
		const auto Parts = Parse( Path );
		if ( Parts.empty( ) )
			return Directory;
		std::lock_guard Lock( m_Mutex );
		try
		{
			auto Parents = OpenParents( Parts, false );
			auto Entry = Open( Parents.empty( ) ? m_Root.Get( ) : Parents.back( ).Get( ), Parts.back( ), 0, OpenExisting, Directory, true );
			return static_cast<bool>( Entry );
		}
		catch ( const std::system_error& Error )
		{
			const int Code = Error.code( ).value( );
			if ( Code == ERROR_FILE_NOT_FOUND || Code == ERROR_PATH_NOT_FOUND || Code == ERROR_DIRECTORY || Code == ERROR_ACCESS_DENIED )
				return false;
			throw;
		}
	}

	void Storage::MakeFolder( std::string_view Path )
	{
		const auto Parts = Parse( Path );
		if ( Parts.empty( ) )
			return;
		std::lock_guard Lock( m_Mutex );
		auto Parents = OpenParents( Parts, true );
		Open( Parents.empty( ) ? m_Root.Get( ) : Parents.back( ).Get( ), Parts.back( ), FILE_LIST_DIRECTORY, OpenOrCreate, true );
	}

	std::vector<Storage::Entry> Storage::Enumerate( HANDLE Directory )
	{
		std::vector<Entry> Result;
		alignas( FILE_ID_BOTH_DIR_INFO ) std::array<std::byte, 65536> Buffer;
		bool Restart = true;
		for ( ;; )
		{
			if ( !GetFileInformationByHandleEx( Directory, Restart ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo, Buffer.data( ), static_cast<DWORD>( Buffer.size( ) ) ) )
			{
				if ( GetLastError( ) == ERROR_NO_MORE_FILES )
					break;
				Fail( "cannot enumerate directory" );
			}
			Restart = false;
			for ( size_t Position = 0;; )
			{
				if ( Position + offsetof( FILE_ID_BOTH_DIR_INFO, FileName ) > Buffer.size( ) )
					throw std::runtime_error( "invalid directory entry" );
				const auto* Info = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>( Buffer.data( ) + Position );
				if ( Info->FileNameLength % sizeof( wchar_t ) || Info->FileNameLength > Buffer.size( ) - Position - offsetof( FILE_ID_BOTH_DIR_INFO, FileName ) )
					throw std::runtime_error( "invalid directory filename" );
				std::wstring Name( Info->FileName, Info->FileNameLength / sizeof( wchar_t ) );
				if ( Name != L"." && Name != L".." )
				{
					if ( Info->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT )
						throw std::runtime_error( "reparse points are not allowed" );
					const auto Parts = Parse( Narrow( Name ) );
					if ( Parts.size( ) != 1 || Parts.front( ) != Name )
						throw std::runtime_error( "unsafe directory entry" );
					Result.push_back( { std::move( Name ), ( Info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 } );
					if ( Result.size( ) > MaximumEntries )
						throw std::runtime_error( "directory exceeds the entry limit" );
				}
				if ( !Info->NextEntryOffset )
					break;
				if ( Info->NextEntryOffset < offsetof( FILE_ID_BOTH_DIR_INFO, FileName ) || Info->NextEntryOffset > Buffer.size( ) - Position || Info->NextEntryOffset % alignof( FILE_ID_BOTH_DIR_INFO ) )
					throw std::runtime_error( "invalid directory entry chain" );
				Position += Info->NextEntryOffset;
			}
		}
		return Result;
	}

	std::vector<std::string> Storage::List( std::string_view Path )
	{
		const auto Parts = Parse( Path );
		std::lock_guard Lock( m_Mutex );
		auto Parents = OpenParents( Parts, false );
		Handle Folder;
		if ( !Parts.empty( ) )
			Folder = Open( Parents.empty( ) ? m_Root.Get( ) : Parents.back( ).Get( ), Parts.back( ), FILE_LIST_DIRECTORY, OpenExisting, true );
		std::string Prefix;
		for ( const auto& Part : Parts )
			Prefix += Narrow( Part ) + "/";
		std::vector<std::string> Result;
		for ( const auto& Entry : Enumerate( Folder ? Folder.Get( ) : m_Root.Get( ) ) )
			Result.push_back( Prefix + Narrow( Entry.Name ) );
		std::sort( Result.begin( ), Result.end( ) );
		return Result;
	}

	void Storage::Erase( HANDLE File )
	{
		FILE_DISPOSITION_INFO Info{ TRUE };
		if ( !SetFileInformationByHandle( File, FileDispositionInfo, &Info, sizeof( Info ) ) )
			Fail( "cannot delete workspace entry" );
	}

	void Storage::RemoveTree( HANDLE Directory, size_t Depth, size_t& Remaining )
	{
		if ( Depth > 128 )
			throw std::runtime_error( "directory exceeds the depth limit" );
		for ( const auto& Entry : Enumerate( Directory ) )
		{
			if ( !Remaining-- )
				throw std::runtime_error( "directory exceeds the removal limit" );
			auto Child = Open( Directory, Entry.Name, DELETE | ( Entry.Directory ? FILE_LIST_DIRECTORY : 0 ), OpenExisting, Entry.Directory );
			if ( Entry.Directory )
				RemoveTree( Child.Get( ), Depth + 1, Remaining );
			Erase( Child.Get( ) );
		}
	}

	void Storage::Remove( std::string_view Path, bool Directory )
	{
		const auto Parts = Parse( Path );
		if ( Parts.empty( ) )
			throw std::runtime_error( "cannot delete the workspace root" );
		std::lock_guard Lock( m_Mutex );
		try
		{
			auto Parents = OpenParents( Parts, false );
			auto Entry = Open( Parents.empty( ) ? m_Root.Get( ) : Parents.back( ).Get( ), Parts.back( ), DELETE | ( Directory ? FILE_LIST_DIRECTORY : 0 ), OpenExisting, Directory, true );
			if ( !Entry )
				return;
			if ( Directory )
			{
				size_t Remaining = MaximumEntries;
				RemoveTree( Entry.Get( ), 0, Remaining );
			}
			Erase( Entry.Get( ) );
		}
		catch ( const std::system_error& Error )
		{
			if ( Error.code( ).value( ) != ERROR_FILE_NOT_FOUND && Error.code( ).value( ) != ERROR_PATH_NOT_FOUND )
				throw;
		}
	}

	std::string Storage::AssetName( std::string_view Path, std::string_view Data )
	{
		const auto Parts = Parse( Path );
		if ( Parts.empty( ) )
			throw std::runtime_error( "expected an asset file" );
		const size_t Dot = Parts.back( ).rfind( L'.' );
		const std::wstring Extension = Dot == std::wstring::npos ? L"" : Lower( Parts.back( ).substr( Dot ) );
		static constexpr std::wstring_view Types[] = { L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".tga", L".dds", L".webp", L".mp3", L".ogg", L".wav", L".mp4", L".webm", L".mesh", L".ttf", L".otf", L".json" };
		if ( std::find( std::begin( Types ), std::end( Types ), Extension ) == std::end( Types ) )
			throw std::runtime_error( "unsupported custom asset type" );
		if ( Data.size( ) > MaximumFile )
			throw std::runtime_error( "asset exceeds the size limit" );
		std::array<UCHAR, 32> Hash{ };
		struct Algorithm
		{
			BCRYPT_ALG_HANDLE Value = nullptr;
			~Algorithm( )
			{
				if ( Value )
					BCryptCloseAlgorithmProvider( Value, 0 );
			}
		} Provider;
		struct Digest
		{
			BCRYPT_HASH_HANDLE Value = nullptr;
			~Digest( )
			{
				if ( Value )
					BCryptDestroyHash( Value );
			}
		} State;
		if ( BCryptOpenAlgorithmProvider( &Provider.Value, BCRYPT_SHA256_ALGORITHM, nullptr, 0 ) < 0 ||
			BCryptCreateHash( Provider.Value, &State.Value, nullptr, 0, nullptr, 0, 0 ) < 0 ||
			BCryptHashData( State.Value, reinterpret_cast<PUCHAR>( const_cast<char*>( Data.data( ) ) ), static_cast<ULONG>( Data.size( ) ), 0 ) < 0 ||
			BCryptFinishHash( State.Value, Hash.data( ), static_cast<ULONG>( Hash.size( ) ), 0 ) < 0 )
			throw std::runtime_error( "cannot hash custom asset" );
		std::string Name;
		for ( UCHAR Byte : Hash )
		{
			Name += "0123456789abcdef"[ Byte >> 4 ];
			Name += "0123456789abcdef"[ Byte & 15 ];
		}
		return Name + Narrow( Extension );
	}

	std::shared_ptr<Storage> Workspace( )
	{
		static const auto g_Workspace = std::make_shared<Storage>( LocalData( ) / L"Alpen" / L"workspace" );
		return g_Workspace;
	}

	std::string CustomAsset( const std::shared_ptr<Storage>& Source, std::string_view Path )
	{
		const std::string Data = Source->Read( Path );
		const std::string Name = Storage::AssetName( Path, Data );
		static const auto g_Assets = std::make_shared<Storage>( Content( ) );
		g_Assets->Write( Name, Data );
		return "rbxasset://alpen/" + Name;
	}
}
