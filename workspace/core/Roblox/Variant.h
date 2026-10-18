#pragma once
#include <cstddef>
#include <cstdint>

namespace RBX
{
	struct VariantOperations
	{
		void ( *Copy )( const void*, void* );
		void ( *Move )( void*, void* );
		void ( *Destroy )( void* );
	};

	struct Variant
	{
		uintptr_t Type = 0;
		const VariantOperations* Operations = nullptr;
		alignas( 8 ) unsigned char Data[ 64 ]{ };

		Variant( ) = default;
		Variant( const Variant& ) = delete;
		Variant& operator=( const Variant& ) = delete;

		~Variant( )
		{
			Reset( );
		}

		void Reset( )
		{
			if ( Operations )
				Operations->Destroy( Data );
			Operations = nullptr;
			Type = 0;
		}
	};

	static_assert( sizeof( Variant ) == 80 );
	static_assert( offsetof( Variant, Data ) == 16 );
}
