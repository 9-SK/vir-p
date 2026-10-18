#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Drawing::Helpers
{
	enum class Kind
	{
		Line,
		Text,
		Image,
		Circle,
		Square,
		Quad,
		Triangle
	};

	struct Vector
	{
		float X = 0;
		float Y = 0;
		bool operator==( const Vector& ) const = default;
	};

	struct Color
	{
		float R = 1;
		float G = 1;
		float B = 1;
		bool operator==( const Color& ) const = default;
	};

	struct Object
	{
		Kind Type;
		uint64_t Id = 0;
		bool Exists = true;
		bool Visible = false;
		bool Filled = false;
		bool Center = false;
		bool Outline = false;
		int ZIndex = 0;
		int Font = 0;
		int NumSides = 64;
		float Transparency = 0;
		float Thickness = 1;
		float Radius = 0;
		float TextSize = 16;
		float Rounding = 0;
		Color Tint;
		Color OutlineColor{ 0, 0, 0 };
		Vector Position;
		Vector Size;
		std::array<Vector, 4> Points;
		std::string Text;
		std::shared_ptr<const std::string> Data;
		bool operator==( const Object& ) const = default;
	};

	class Renderer
	{
	  public:
		Renderer( );
		~Renderer( );
		void Start( );
		void Stop( );
		std::shared_ptr<Object> Create( Kind Type );
		void Update( const std::shared_ptr<Object>& Target, Object Value );
		void Remove( const std::shared_ptr<Object>& Target );
		void Clear( );
		std::mutex& Mutex( );
		Vector Measure( const Object& Value );
		void SetCaptureExcluded( bool Excluded );
		bool IsCaptureExcluded( ) const;

	  private:
		class Implementation;
		std::unique_ptr<Implementation> m_Impl;
	};
}
