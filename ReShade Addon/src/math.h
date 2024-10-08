#pragma once

#include <array>

std::array<unsigned int, 2> FindClosestIntegerResolutionForAspectRatio(double width, double height, double aspectRatio)
{
	// We can't round both the x and y resolution as that might generate an aspect ratio
	// further away from the target one, we also can't either ceil or floor both sides,
	// so we find the combination or flooring and ceiling that is closest to the target ar.
	const unsigned int ceiledWidth = static_cast<unsigned int>(std::ceil(width));
	const unsigned int ceiledHeight = static_cast<unsigned int>(std::ceil(height));
	const unsigned int flooredWidth = static_cast<unsigned int>(std::floor(width));
	const unsigned int flooredHeight = static_cast<unsigned int>(std::floor(height));

	unsigned int intWidth = flooredWidth;
	unsigned int intHeight = flooredHeight;

	double minAspectRatioDistance = (std::numeric_limits<double>::max)(); // Wrap it around () because "max" might already be defined as macro
	for (const unsigned int newWidth : std::array<unsigned int, 2>{ceiledWidth, flooredWidth})
	{
		for (const unsigned int newHeight : std::array<unsigned int, 2>{ceiledHeight, flooredHeight})
		{
			const double newAspectRatio = static_cast<double>(newWidth) / newHeight;
			const double aspectRatioDistance = std::abs((newAspectRatio / aspectRatio) - 1.f);
			if (aspectRatioDistance < minAspectRatioDistance)
			{
				minAspectRatioDistance = aspectRatioDistance;
				intWidth = newWidth;
				intHeight = newHeight;
			}
		}
	}

	return std::array<unsigned int, 2>{ intWidth, intHeight };
}

template<typename T>
bool AlmostEqual(T a, T b, T tolerance)
{
	return std::abs(a - b) <= tolerance;
}

// Emulates hlsl "asfloat(float x)"
template<typename T>
float AsFloat(T value)
{
	static_assert(sizeof(T) <= sizeof(uint32_t) && sizeof(uint32_t) == sizeof(float));
	uint32_t value_uint32 = value;
	return *reinterpret_cast<float*>(&value_uint32);
}