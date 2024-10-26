#define FLT_MIN	asfloat(0x00800000)  //1.175494351e-38f
#define FLT_MAX	asfloat(0x7F7FFFFF)  //3.402823466e+38f
#define FLT_EPSILON	1.192092896e-07 // Smallest positive number, such that 1.0 + FLT_EPSILON != 1.0

#define PI 3.141592653589793238462643383279502884197
#define PI_X2 (PI * 2.0)
#define PI_X4 (PI_X4 * 4.0)

float average(float3 color)
{
	return (color.x + color.y + color.z) / 3.f;
}
float average(float2 color)
{
	return (color.x + color.y) / 2.f;
}

float remap(float input, float oldMin, float oldMax, float newMin, float newMax)
{
	return ((input - oldMin) * ((newMax - newMin) / (oldMax - oldMin))) + newMin;
}

// Returns 0, 1, -1/0/+1 or +/-FLT_MAX if "dividend" is 0
float safeDivision(float quotient, float dividend, int fallbackMode = 0)
{
	if (dividend == 0) {
        if (fallbackMode == 0)
          return 0;
        if (fallbackMode == 1)
          return 1;
        if (fallbackMode == 2)
          return sign(quotient); // This will return 0 for 0
        return FLT_MAX * sign(quotient);
    }
    return quotient / dividend;
}
// Returns 0, 1 or FLT_MAX if "dividend" is 0
float3 safeDivision(float3 quotient, float3 dividend, int fallbackMode = 0)
{
    return float3(safeDivision(quotient.x, dividend.x, fallbackMode), safeDivision(quotient.y, dividend.y, fallbackMode), safeDivision(quotient.z, dividend.z, fallbackMode));
}

float safePow(float base, float exponent)
{
    return pow(abs(base), exponent) * sign(base);
}

float3 sqr(float3 x) { return x * x; }
float sqr(float x) { return x * x; }

float min3(float _a, float _b, float _c) { return min(_a, min(_b, _c)); }
float3 min3(float3 _a, float3 _b, float3 _c) { return min(_a, min(_b, _c)); }
float min3(float3 _a) { return min(_a.x, min(_a.y, _a.z)); }
float3 max3(float3 _a, float3 _b, float3 _c) { return max(_a, max(_b, _c)); }
float max3(float _a, float _b, float _c) { return max(_a, max(_b, _c)); }
float max3(float3 _a) { return max(_a.x, max(_a.y, _a.z)); }

float3 NRand3(float2 seed, float tr = 1.0)
{
  return frac(sin(dot(seed.xy, float2(34.483, 89.637) * tr)) * float3(29156.4765, 38273.5639, 47843.7546));
}

// Takes coordinates centered around zero, and a normal for a cube of side size 1, both with origin at 0.
// The normal is expected to be negative/inverted (facing origin) (basically it's just the cube side).
bool cubeCoordinatesIntersection(out float3 intersection, float3 coordinates, float3 sideNormal)
{
    intersection = 0;
    if (dot(sideNormal, coordinates) >= -1.f)
        return false; // No intersection, the line is parallel or facing away from the plane
    // Compute the X value for the directed line ray intersecting the plane
    float t = -1.f / dot(sideNormal, coordinates);
    intersection = coordinates * t;
    return true;
}