#include "Helper.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <d2d1.h>
#include <dwrite.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <wincodec.h>
#include <wrl/client.h>

#pragma comment( lib, "d2d1.lib" )
#pragma comment( lib, "dwrite.lib" )
#pragma comment( lib, "windowscodecs.lib" )
#pragma comment( lib, "ole32.lib" )
#pragma comment( lib, "gdi32.lib" )
#pragma comment( lib, "user32.lib" )

namespace Drawing::Helpers
{
	using Microsoft::WRL::ComPtr;

	namespace
	{
		std::wstring Wide( const std::string& Value )
		{
			int Length = MultiByteToWideChar( CP_UTF8, 0, Value.data( ), static_cast<int>( Value.size( ) ), nullptr, 0 );
			std::wstring Result( Length, L'\0' );
			MultiByteToWideChar( CP_UTF8, 0, Value.data( ), static_cast<int>( Value.size( ) ), Result.data( ), Length );
			return Result;
		}

		ComPtr<IDWriteTextLayout> Layout( IDWriteFactory* Factory, const Object& Value )
		{
			const wchar_t* Fonts[] = { L"Arial", L"Arial", L"Segoe UI", L"Consolas" };
			ComPtr<IDWriteTextFormat> Format;
			ComPtr<IDWriteTextLayout> Result;
			if ( !Factory || FAILED( Factory->CreateTextFormat( Fonts[ Value.Font ], nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, std::max( 1.0f, Value.TextSize ), L"en-us", &Format ) ) )
				return Result;
			Format->SetWordWrapping( DWRITE_WORD_WRAPPING_NO_WRAP );
			std::wstring Text = Wide( Value.Text );
			Factory->CreateTextLayout( Text.data( ), static_cast<UINT32>( Text.size( ) ), Format.Get( ), 1048576, 1048576, &Result );
			return Result;
		}

		D2D1_POINT_2F Point( Vector Value )
		{
			return D2D1::Point2F( Value.X, Value.Y );
		}
	}

	class Renderer::Implementation
	{
	  public:
		std::mutex ObjectsMutex;
		std::mutex TextMutex;
		std::vector<std::shared_ptr<Object>> Objects;
		std::atomic<bool> Running{ false };
		std::atomic<bool> ExcludeCapture{ true };
		std::atomic<bool> CaptureExcluded{ false };
		std::thread Thread;
		uint64_t NextId = 0;
		uint64_t Revision = 0;
		uint64_t Lifetime = 0;
		struct TextEntry
		{
			std::string Text;
			int Font = -1;
			float Size = 0;
			ComPtr<IDWriteTextLayout> Layout;
			Vector Bounds;
		};
		std::unordered_map<uint64_t, TextEntry> Texts;
		ComPtr<IDWriteFactory> TextFactory;

		TextEntry& TextLayout( const Object& Value )
		{
			auto& Entry = Texts[ Value.Id ];
			if ( Entry.Font != Value.Font || Entry.Size != Value.TextSize || Entry.Text != Value.Text )
			{
				Entry.Text = Value.Text;
				Entry.Font = Value.Font;
				Entry.Size = Value.TextSize;
				Entry.Layout = Layout( TextFactory.Get( ), Value );
				DWRITE_TEXT_METRICS Metrics{ };
				if ( Entry.Layout )
					Entry.Layout->GetMetrics( &Metrics );
				Entry.Bounds = { Metrics.widthIncludingTrailingWhitespace, Metrics.height };
			}
			return Entry;
		}

		Implementation( )
		{
			DWriteCreateFactory( DWRITE_FACTORY_TYPE_SHARED, __uuidof( IDWriteFactory ), reinterpret_cast<IUnknown**>( TextFactory.GetAddressOf( ) ) );
		}

		Vector Measure( const Object& Value )
		{
			std::lock_guard<std::mutex> Lock( TextMutex );
			return TextLayout( Value ).Bounds;
		}

		void Run( )
		{
			using SetAwareness = HANDLE( WINAPI* )( HANDLE );
			const auto SetDpi = reinterpret_cast<SetAwareness>( GetProcAddress( GetModuleHandleW( L"user32.dll" ), "SetThreadDpiAwarenessContext" ) );
			if ( SetDpi )
				SetDpi( reinterpret_cast<HANDLE>( -4 ) );
			HRESULT Apartment = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
			{
				ComPtr<ID2D1Factory> Factory;
				ComPtr<IWICImagingFactory> Images;
				D2D1CreateFactory( D2D1_FACTORY_TYPE_SINGLE_THREADED, Factory.GetAddressOf( ) );
				CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &Images ) );
				if ( Factory )
					RenderLoop( Factory.Get( ), Images.Get( ) );
			}
			if ( SUCCEEDED( Apartment ) )
				CoUninitialize( );
		}

	  private:
		struct Image
		{
			std::shared_ptr<const std::string> Data;
			ComPtr<ID2D1Bitmap> Bitmap;
		};
		struct Shape
		{
			Kind Type;
			int Sides = 0;
			float Radius = -1;
			std::array<Vector, 4> Points{ };
			ComPtr<ID2D1PathGeometry> Geometry;
		};

		static LRESULT CALLBACK WindowProcedure( HWND Window, UINT Message, WPARAM WParam, LPARAM LParam )
		{
			if ( Message == WM_NCHITTEST )
				return HTTRANSPARENT;
			if ( Message == WM_MOUSEACTIVATE )
				return MA_NOACTIVATE;
			return DefWindowProcW( Window, Message, WParam, LParam );
		}

		static HWND GameWindow( HWND Current )
		{
			if ( IsWindow( Current ) )
				return Current;
			HWND Result = nullptr;
			EnumWindows( []( HWND Window, LPARAM Context ) -> BOOL
						 {
							 DWORD Process = 0;
							 GetWindowThreadProcessId( Window, &Process );
							 wchar_t Class[ 64 ];
							 if ( Process == GetCurrentProcessId( ) && GetClassNameW( Window, Class, 64 ) && wcscmp( Class, L"WINDOWSCLIENT" ) == 0 )
							 {
								 *reinterpret_cast<HWND*>( Context ) = Window;
								 return FALSE;
							 }
							 return TRUE;
						 },
						 reinterpret_cast<LPARAM>( &Result ) );
			return Result;
		}

		static void Report( const char* Operation, DWORD Error )
		{
			char Message[ 160 ];
			sprintf_s( Message, "Drawing: %s failed (%lu)", Operation, Error );
			OutputDebugStringA( Message );
		}

		ComPtr<ID2D1PathGeometry> Polygon( ID2D1Factory* Factory, const std::vector<D2D1_POINT_2F>& Points )
		{
			ComPtr<ID2D1PathGeometry> Geometry;
			ComPtr<ID2D1GeometrySink> Sink;
			if ( FAILED( Factory->CreatePathGeometry( &Geometry ) ) || FAILED( Geometry->Open( &Sink ) ) )
				return { };
			Sink->SetFillMode( D2D1_FILL_MODE_WINDING );
			Sink->BeginFigure( Points.front( ), D2D1_FIGURE_BEGIN_FILLED );
			Sink->AddLines( Points.data( ) + 1, static_cast<UINT32>( Points.size( ) - 1 ) );
			Sink->EndFigure( D2D1_FIGURE_END_CLOSED );
			if ( FAILED( Sink->Close( ) ) )
				return { };
			return Geometry;
		}

		ComPtr<ID2D1Bitmap> Decode( IWICImagingFactory* Factory, ID2D1RenderTarget* Target, const std::string& Data )
		{
			ComPtr<ID2D1Bitmap> Bitmap;
			ComPtr<IWICStream> Stream;
			ComPtr<IWICBitmapDecoder> Decoder;
			ComPtr<IWICBitmapFrameDecode> Frame;
			ComPtr<IWICFormatConverter> Converter;
			if ( !Factory || Data.empty( ) || Data.size( ) > 32 * 1024 * 1024 || FAILED( Factory->CreateStream( &Stream ) ) )
				return Bitmap;
			if ( FAILED( Stream->InitializeFromMemory( reinterpret_cast<BYTE*>( const_cast<char*>( Data.data( ) ) ), static_cast<DWORD>( Data.size( ) ) ) ) || FAILED( Factory->CreateDecoderFromStream( Stream.Get( ), nullptr, WICDecodeMetadataCacheOnDemand, &Decoder ) ) || FAILED( Decoder->GetFrame( 0, &Frame ) ) )
				return Bitmap;
			UINT Width = 0, Height = 0;
			if ( FAILED( Frame->GetSize( &Width, &Height ) ) || !Width || !Height || Width > 8192 || Height > 8192 || static_cast<uint64_t>( Width ) * Height > 16777216 )
				return Bitmap;
			if ( SUCCEEDED( Factory->CreateFormatConverter( &Converter ) ) && SUCCEEDED( Converter->Initialize( Frame.Get( ), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom ) ) )
				Target->CreateBitmapFromWicBitmap( Converter.Get( ), nullptr, &Bitmap );
			return Bitmap;
		}

		void Draw( ID2D1Factory* Factory, IWICImagingFactory* Images, ID2D1RenderTarget* Target, ID2D1SolidColorBrush* Brush, const Object& Value, std::unordered_map<uint64_t, Image>& Cache, std::unordered_map<uint64_t, Shape>& Shapes )
		{
			const float Alpha = 1 - Value.Transparency;
			Brush->SetColor( D2D1::ColorF( Value.Tint.R, Value.Tint.G, Value.Tint.B, Alpha ) );
			switch ( Value.Type )
			{
			case Kind::Line:
				if ( Value.Thickness > 0 )
					Target->DrawLine( Point( Value.Points[ 0 ] ), Point( Value.Points[ 1 ] ), Brush, Value.Thickness );
				break;
			case Kind::Square:
			{
				auto Rectangle = D2D1::RectF( Value.Position.X, Value.Position.Y, Value.Position.X + Value.Size.X, Value.Position.Y + Value.Size.Y );
				if ( Value.Filled )
					Target->FillRectangle( Rectangle, Brush );
				else if ( Value.Thickness > 0 )
					Target->DrawRectangle( Rectangle, Brush, Value.Thickness );
				break;
			}
			case Kind::Circle:
			case Kind::Triangle:
			case Kind::Quad:
			{
				auto& Shape = Shapes[ Value.Id ];
				bool Circle = Value.Type == Kind::Circle;
				if ( !Shape.Geometry || Shape.Type != Value.Type || ( Circle ? Shape.Radius != Value.Radius || Shape.Sides != Value.NumSides : Shape.Points != Value.Points ) )
				{
					std::vector<D2D1_POINT_2F> Points;
					if ( Circle )
					{
						Points.reserve( Value.NumSides );
						for ( int Index = 0; Index < Value.NumSides; ++Index )
						{
							float Angle = Index * 6.28318530718f / Value.NumSides;
							Points.push_back( D2D1::Point2F( std::cos( Angle ) * Value.Radius, std::sin( Angle ) * Value.Radius ) );
						}
					}
					else
					{
						Points.reserve( 4 );
						for ( int Index = 0; Index < ( Value.Type == Kind::Quad ? 4 : 3 ); ++Index )
							Points.push_back( Point( Value.Points[ Index ] ) );
					}
					Shape.Type = Value.Type;
					Shape.Radius = Value.Radius;
					Shape.Sides = Value.NumSides;
					Shape.Points = Value.Points;
					Shape.Geometry = Polygon( Factory, Points );
				}
				if ( Shape.Geometry )
				{
					if ( Circle )
						Target->SetTransform( D2D1::Matrix3x2F::Translation( Value.Position.X, Value.Position.Y ) );
					if ( Value.Filled )
						Target->FillGeometry( Shape.Geometry.Get( ), Brush );
					else if ( Value.Thickness > 0 )
						Target->DrawGeometry( Shape.Geometry.Get( ), Brush, Value.Thickness );
					if ( Circle )
						Target->SetTransform( D2D1::Matrix3x2F::Identity( ) );
				}
				break;
			}
			case Kind::Text:
			{
				ComPtr<IDWriteTextLayout> Text;
				Vector Bounds;
				{
					std::lock_guard<std::mutex> Lock( TextMutex );
					const auto& Entry = TextLayout( Value );
					Text = Entry.Layout;
					Bounds = Entry.Bounds;
				}
				if ( !Text )
					break;
				auto Origin = Point( Value.Position );
				if ( Value.Center )
					Origin.x -= Bounds.X / 2;
				if ( Value.Outline )
				{
					Brush->SetColor( D2D1::ColorF( Value.OutlineColor.R, Value.OutlineColor.G, Value.OutlineColor.B, Alpha ) );
					for ( int Y = -1; Y <= 1; ++Y )
						for ( int X = -1; X <= 1; ++X )
							if ( X || Y )
								Target->DrawTextLayout( D2D1::Point2F( Origin.x + X, Origin.y + Y ), Text.Get( ), Brush );
					Brush->SetColor( D2D1::ColorF( Value.Tint.R, Value.Tint.G, Value.Tint.B, Alpha ) );
				}
				Target->DrawTextLayout( Origin, Text.Get( ), Brush );
				break;
			}
			case Kind::Image:
			{
				if ( !Value.Data )
					break;
				auto& Image = Cache[ Value.Id ];
				if ( Image.Data != Value.Data )
				{
					Image.Data = Value.Data;
					Image.Bitmap = Decode( Images, Target, *Value.Data );
				}
				if ( Image.Bitmap )
				{
					auto Rectangle = D2D1::RectF( Value.Position.X, Value.Position.Y, Value.Position.X + Value.Size.X, Value.Position.Y + Value.Size.Y );
					ComPtr<ID2D1RoundedRectangleGeometry> Clip;
					ComPtr<ID2D1Layer> Layer;
					if ( Value.Rounding > 0 && SUCCEEDED( Factory->CreateRoundedRectangleGeometry( D2D1::RoundedRect( Rectangle, Value.Rounding, Value.Rounding ), &Clip ) ) && SUCCEEDED( Target->CreateLayer( nullptr, &Layer ) ) )
						Target->PushLayer( D2D1::LayerParameters( D2D1::InfiniteRect( ), Clip.Get( ) ), Layer.Get( ) );
					Target->DrawBitmap( Image.Bitmap.Get( ), Rectangle, Alpha );
					if ( Layer )
						Target->PopLayer( );
				}
				break;
			}
			}
		}

		void RenderLoop( ID2D1Factory* Factory, IWICImagingFactory* Images )
		{
			wchar_t ClassName[ 80 ];
			swprintf_s( ClassName, L"Alpen.Drawing.%lu.%p", GetCurrentProcessId( ), this );
			WNDCLASSW Class{ };
			Class.lpfnWndProc = WindowProcedure;
			Class.hInstance = GetModuleHandleW( nullptr );
			Class.lpszClassName = ClassName;
			if ( !RegisterClassW( &Class ) )
				return;
			HWND Window = CreateWindowExW( WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, ClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, Class.hInstance, nullptr );
			bool AppliedCapture = false;
			bool CaptureFailed = false;
			HWND Game = nullptr;
			bool PresentFailed = false;
			HDC Surface = CreateCompatibleDC( nullptr );
			HBITMAP Bitmap = nullptr;
			HGDIOBJ Previous = nullptr;
			SIZE Size{ };
			ComPtr<ID2D1DCRenderTarget> Target;
			ComPtr<ID2D1SolidColorBrush> Brush;
			std::unordered_map<uint64_t, Image> Cache;
			std::unordered_map<uint64_t, Shape> Shapes;
			std::vector<Object> Snapshot;
			std::vector<const Object*> Ordered;
			uint64_t SnapshotRevision = ~uint64_t( 0 );
			uint64_t SnapshotLifetime = ~uint64_t( 0 );
			POINT LastPosition{ };
			bool PresentedFrame = false;
			while ( Running && Window && Surface )
			{
				const bool RequestedCapture = ExcludeCapture.load( );
				if ( RequestedCapture != AppliedCapture )
				{
					if ( SetWindowDisplayAffinity( Window, RequestedCapture ? 0x11 : WDA_NONE ) )
					{
						AppliedCapture = RequestedCapture;
						CaptureExcluded = RequestedCapture;
						CaptureFailed = false;
					}
					else if ( !CaptureFailed )
					{
						CaptureFailed = true;
						OutputDebugStringA( "Drawing capture exclusion is unavailable" );
					}
				}
				auto Deadline = std::chrono::steady_clock::now( ) + std::chrono::milliseconds( 16 );
				MSG Message;
				while ( PeekMessageW( &Message, nullptr, 0, 0, PM_REMOVE ) )
				{
					TranslateMessage( &Message );
					DispatchMessageW( &Message );
				}
				HWND Candidate = GameWindow( Game );
				if ( Candidate != Game )
				{
					SetLastError( ERROR_SUCCESS );
					SetWindowLongPtrW( Window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>( Candidate ) );
					DWORD Error = GetLastError( );
					if ( Error )
						Report( "assigning window owner", Error );
					else
						Game = Candidate;
				}
				RECT Client{ };
				if ( !Game || !IsWindowVisible( Game ) || IsIconic( Game ) || GetAncestor( GetForegroundWindow( ), GA_ROOTOWNER ) != Game || !GetClientRect( Game, &Client ) || Client.right <= 0 || Client.bottom <= 0 )
				{
					if ( IsWindowVisible( Window ) )
						ShowWindow( Window, SW_HIDE );
					std::this_thread::sleep_until( Deadline );
					continue;
				}
				bool Changed = false;
				std::unordered_set<uint64_t> Alive;
				bool Prune = false;
				{
					std::lock_guard<std::mutex> Lock( ObjectsMutex );
					Changed = SnapshotRevision != Revision;
					Prune = SnapshotLifetime != Lifetime;
					if ( Prune )
					{
						std::erase_if( Objects, []( const auto& Value )
									   {
										   return !Value->Exists;
									   } );
						Alive.reserve( Objects.size( ) );
						for ( const auto& Value : Objects )
							Alive.insert( Value->Id );
						SnapshotLifetime = Lifetime;
					}
					if ( Changed )
					{
						size_t Count = 0;
						for ( const auto& Value : Objects )
						{
							if ( !Value->Visible || Value->Transparency >= 1 )
								continue;
							if ( Count == Snapshot.size( ) )
								Snapshot.push_back( *Value );
							else
								Snapshot[ Count ] = *Value;
							++Count;
						}
						Snapshot.resize( Count );
						SnapshotRevision = Revision;
					}
				}
				if ( Prune )
				{
					std::erase_if( Cache, [ & ]( const auto& Entry )
								   {
									   return !Alive.contains( Entry.first );
								   } );
					std::erase_if( Shapes, [ & ]( const auto& Entry )
								   {
									   return !Alive.contains( Entry.first );
								   } );
					std::lock_guard<std::mutex> Lock( TextMutex );
					std::erase_if( Texts, [ & ]( const auto& Entry )
								   {
									   return !Alive.contains( Entry.first );
								   } );
				}
				if ( Snapshot.empty( ) )
				{
					if ( IsWindowVisible( Window ) )
						ShowWindow( Window, SW_HIDE );
					PresentedFrame = false;
					std::this_thread::sleep_until( Deadline );
					continue;
				}
				POINT Position{ }, Source{ };
				ClientToScreen( Game, &Position );
				bool Redraw = Changed || !PresentedFrame || Client.right != Size.cx || Client.bottom != Size.cy || !Target;
				if ( !Redraw && !PresentFailed )
				{
					if ( Position.x != LastPosition.x || Position.y != LastPosition.y || !IsWindowVisible( Window ) )
					{
						if ( !SetWindowPos( Window, HWND_TOPMOST, Position.x, Position.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW ) )
							PresentFailed = true;
						else
							LastPosition = Position;
					}
					std::this_thread::sleep_until( Deadline );
					continue;
				}
				if ( Client.right != Size.cx || Client.bottom != Size.cy || !Target )
				{
					Cache.clear( );
					Brush.Reset( );
					Target.Reset( );
					if ( Previous )
						SelectObject( Surface, Previous );
					if ( Bitmap )
						DeleteObject( Bitmap );
					Size = { Client.right, Client.bottom };
					BITMAPINFO Info{ };
					Info.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
					Info.bmiHeader.biWidth = Size.cx;
					Info.bmiHeader.biHeight = -Size.cy;
					Info.bmiHeader.biPlanes = 1;
					Info.bmiHeader.biBitCount = 32;
					Info.bmiHeader.biCompression = BI_RGB;
					void* Pixels = nullptr;
					Bitmap = CreateDIBSection( Surface, &Info, DIB_RGB_COLORS, &Pixels, nullptr, 0 );
					if ( !Bitmap )
						break;
					Previous = SelectObject( Surface, Bitmap );
					auto Properties = D2D1::RenderTargetProperties( D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat( DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED ), 96, 96, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE );
					if ( FAILED( Factory->CreateDCRenderTarget( &Properties, &Target ) ) || FAILED( Target->BindDC( Surface, &Client ) ) || FAILED( Target->CreateSolidColorBrush( D2D1::ColorF( 1, 1, 1 ), &Brush ) ) )
						break;
					Target->SetTextAntialiasMode( D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE );
				}
				if ( Changed )
				{
					Ordered.clear( );
					Ordered.reserve( Snapshot.size( ) );
					for ( const auto& Value : Snapshot )
						Ordered.push_back( &Value );
					std::sort( Ordered.begin( ), Ordered.end( ), []( const Object* A, const Object* B )
							   {
								   return A->ZIndex == B->ZIndex ? A->Id < B->Id : A->ZIndex < B->ZIndex;
							   } );
				}
				Target->BeginDraw( );
				Target->Clear( D2D1::ColorF( 0, 0, 0, 0 ) );
				for ( const Object* Value : Ordered )
					Draw( Factory, Images, Target.Get( ), Brush.Get( ), *Value, Cache, Shapes );
				HRESULT Result = Target->EndDraw( );
				if ( FAILED( Result ) )
				{
					Target.Reset( );
					continue;
				}
				BLENDFUNCTION Blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
				BOOL Presented = UpdateLayeredWindow( Window, nullptr, &Position, &Size, Surface, &Source, 0, &Blend, ULW_ALPHA );
				DWORD Error = Presented ? ERROR_SUCCESS : GetLastError( );
				if ( Presented && ( !IsWindowVisible( Window ) || !( GetWindowLongPtrW( Window, GWL_EXSTYLE ) & WS_EX_TOPMOST ) ) && !SetWindowPos( Window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW ) )
				{
					Presented = FALSE;
					Error = GetLastError( );
				}
				if ( !Presented )
				{
					if ( !PresentFailed )
						Report( "presenting overlay", Error );
					ShowWindow( Window, SW_HIDE );
				}
				PresentFailed = !Presented;
				PresentedFrame = Presented;
				LastPosition = Position;
				std::this_thread::sleep_until( Deadline );
			}
			Cache.clear( );
			Brush.Reset( );
			Target.Reset( );
			if ( Previous )
				SelectObject( Surface, Previous );
			if ( Bitmap )
				DeleteObject( Bitmap );
			if ( Surface )
				DeleteDC( Surface );
			if ( Window )
				DestroyWindow( Window );
			UnregisterClassW( ClassName, Class.hInstance );
			CaptureExcluded = false;
		}
	};

	Renderer::Renderer( ) : m_Impl( std::make_unique<Implementation>( ) )
	{
	}

	Renderer::~Renderer( )
	{
		Stop( );
	}

	void Renderer::Start( )
	{
		if ( !m_Impl->Running.exchange( true ) )
			m_Impl->Thread = std::thread( [ this ]
										  {
											  m_Impl->Run( );
										  } );
	}

	void Renderer::Stop( )
	{
		m_Impl->Running = false;
		if ( m_Impl->Thread.joinable( ) )
			m_Impl->Thread.join( );
		Clear( );
	}

	std::shared_ptr<Object> Renderer::Create( Kind Type )
	{
		std::lock_guard<std::mutex> Lock( m_Impl->ObjectsMutex );
		auto Value = std::make_shared<Object>( );
		Value->Type = Type;
		Value->Id = ++m_Impl->NextId;
		m_Impl->Objects.push_back( Value );
		++m_Impl->Lifetime;
		return Value;
	}

	void Renderer::Update( const std::shared_ptr<Object>& Target, Object Value )
	{
		std::lock_guard<std::mutex> Lock( m_Impl->ObjectsMutex );
		if ( !Target->Exists || *Target == Value )
			return;
		if ( Target->Visible || Value.Visible )
			++m_Impl->Revision;
		*Target = std::move( Value );
	}

	void Renderer::Remove( const std::shared_ptr<Object>& Target )
	{
		{
			std::lock_guard<std::mutex> Lock( m_Impl->ObjectsMutex );
			if ( !Target->Exists )
				return;
			Target->Exists = false;
			Target->Visible = false;
			++m_Impl->Revision;
			++m_Impl->Lifetime;
		}
		std::lock_guard<std::mutex> Lock( m_Impl->TextMutex );
		m_Impl->Texts.erase( Target->Id );
	}

	void Renderer::Clear( )
	{
		std::lock_guard<std::mutex> Lock( m_Impl->ObjectsMutex );
		for ( const auto& Value : m_Impl->Objects )
			Value->Exists = false;
		m_Impl->Objects.clear( );
		++m_Impl->Revision;
		++m_Impl->Lifetime;
		std::lock_guard<std::mutex> TextLock( m_Impl->TextMutex );
		m_Impl->Texts.clear( );
	}

	std::mutex& Renderer::Mutex( )
	{
		return m_Impl->ObjectsMutex;
	}

	Vector Renderer::Measure( const Object& Value )
	{
		return m_Impl->Measure( Value );
	}

	void Renderer::SetCaptureExcluded( bool Excluded )
	{
		m_Impl->ExcludeCapture = Excluded;
	}

	bool Renderer::IsCaptureExcluded( ) const
	{
		return m_Impl->CaptureExcluded;
	}
}
