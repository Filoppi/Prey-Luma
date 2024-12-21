#include "include/Common.hlsl"

/* >> Functions << */

/* S curve by JMF
   Generates smooth half-bell falloff for blur.
   Input is in [0, 1] range. */
float s_curve(float gradient)
{
	float top = max(gradient, 0.5);
	float bottom = min(gradient, 0.5);
	return 2.0*((bottom*bottom+top)-(top*top-top))-1.5;
}
/* G continuity distance function by Jakub Max Fober.
   Represents derivative level continuity. (G from 0, to 3)
   G=0   Sharp corners
   G=1   Round corners
   G=2   Smooth corners
   G=3   Luxury corners */
float glength(uint G, float2 pos)
{
	// Sharp corner
	if (G==0u) return max(abs(pos.x), abs(pos.y)); // g0
	// Higher-power length function
	pos = exp(log(abs(pos))*(++G)); // position to the power of G+1
	return exp(log(pos.x+pos.y)/G); // position to the power of G+1 root
}

/* Linear pixel step function for anti-aliasing by Jakub Max Fober.
   This algorithm is part of scientific paper:
   · arXiv:2010.04077 [cs.GR] (2020) */
float aastep(float grad)
{
#pragma warning( disable : 4008 ) //TODOFT: fix this... why does it happen?
	// Differential vector
	float2 Del = float2(ddx(grad), ddy(grad));
	// Gradient normalization to pixel size, centered at the step edge
	return saturate(mad(rsqrt(dot(Del, Del)), grad, 0.5)); // half-pixel offset
#pragma warning( default : 4008 )
}

/* Azimuthal spherical perspective projection equations © 2022 Jakub Maksymilian Fober
   These algorithms are part of the following scientific papers:
   · arXiv:2003.10558 [cs.GR] (2020)
   · arXiv:2010.04077 [cs.GR] (2020) */
float get_radius(float theta, float rcp_f, float k) // get image radius
{
	if      (k>0.0)  return tan(k*theta)/rcp_f/k; // stereographic, rectilinear projections
	else if (k<0.0)  return sin(abs(k)*theta)/rcp_f/abs(k); // equisolid, orthographic projections
	else  /*k==0.0*/ return            theta /rcp_f;        // equidistant projection
}
#define get_rcp_focal(halfOmega, radiusOfOmega, k) get_radius(halfOmega, radiusOfOmega, k) // get reciprocal focal length
float get_theta(float radius, float rcp_f, float k) // get spherical θ angle
{
	if      (k>0.0)  return atan(k*radius*rcp_f)/k; // stereographic, rectilinear projections
	else if (k<0.0)  return asin(abs(k)*radius*rcp_f)/abs(k); // equisolid, orthographic projections
	else  /*k==0.0*/ return             radius*rcp_f;         // equidistant projection
}
float get_vignette(float theta, float r, float rcp_f) // get vignetting mask in linear color space
{ return sin(theta)/r/rcp_f; }
float2 get_phi_weights(float2 viewCoord) // get aximorphic interpolation weights
{
	viewCoord *= viewCoord; // squared vector coordinates
	return viewCoord/(viewCoord.x+viewCoord.y); // [cos²φ sin²φ] vector
}

// Get radius at Ω for a given FOV type
float getRadiusOfOmega(float2 viewProportions)
{
#if 0 // vertical
	return viewProportions.y;
#else // horizontal
	return viewProportions.x;
#endif
}

// Border mask shader with rounded corners
float GetBorderMask(float2 borderCoord)
{
	// Get coordinates for each corner
	return aastep(glength(0u, abs(borderCoord))-1.0);
}

//TODOFT: expose more of these to the user?
static const float K = 0.8; // Lower is stronger distortion. 0.5 is the original default value (and a balanced one too, tough it might be a bit too strong for us). Going negative applies the opposite distortion.
static const float S = 2.0; // Higher is "less" distortion. Matches "golden standard" from the ReShade version, 1 is the original default value (and the lowest allowed).
static const float CroppingFactor = (bool)ALLOW_LENS_DISTORTION_BLACK_BORDERS ? 0.5 : 1.0; // At 0 we don't crop at all and show black borders on all sides, at 0.5 we match the borders to the edges of the distorted images, at 1 we zoom in completely so no border would be visible.
static const float AspectRatioCorrection = (bool)ALLOW_LENS_DISTORTION_BLACK_BORDERS ? (1.0 / 3.0) : 0.0; // Emulates the intensity of the lens distortion around 16:9, for consistency across aspect ratios. The higher the value, the larger the borders in Ultrawide
static const float Inverse_BorderYThreshold = 0.5; // Neutral (disabled) at 1. Lower values provide smoother transitions
static const float Inverse_OutOfBorderRestorationRange = 0.9; // Neutral at 1. Linear scaling
static const float Inverse_OutOfBorderBorderPow = 0.9; // Neutral at 1. Pow scaling

// Taken (with permission) from "https://github.com/Fubaxiusz/fubax-shaders".
//TODO LUMA: try axiomorphic mode?
float2 PerfectPerspectiveLensDistortion(float2 texCoord, float horFOV, float2 resolution, out float borderAlpha, bool NDC = false, bool clip = false)
{
//----------------------------------------------
// begin of perspective mapping

	const float currentAspectRatio = resolution.x / resolution.y;

	// This is to make the distortion look roughly the same as it would at 16:9 around the 16:9 part of the image, independently of the aspect ratio.
	// The cost is that it won't apply as "correctly" anymore for arbitrary aspect ratios, but in UW it was way too strong, so this is preferred.
	float aspectRatioOffsetScale = 1.0;
	if (AspectRatioCorrection > 0.f)
	{
		// For 32:9, this will be 0.5.
		aspectRatioOffsetScale = lerp(1.0, NativeAspectRatio / currentAspectRatio, AspectRatioCorrection);
		resolution.x *= aspectRatioOffsetScale;
		horFOV = atan( tan( horFOV * 0.5 ) * aspectRatioOffsetScale ) * 2.0;
	}

	// Aspect ratio transformation vector
	const float2 viewProportions = normalize(resolution);
	// Half field of view angle in radians
	const float halfOmega = horFOV * 0.5;
	// Get radius at Ω for a given FOV type
	const float radiusOfOmega = getRadiusOfOmega(viewProportions);
	// Reciprocal focal length
	const float rcp_focal = get_rcp_focal(halfOmega, radiusOfOmega, K);

	// Horizontal point radius
	const float croppingHorizontal = get_radius(
			atan(tan(halfOmega)/radiusOfOmega*viewProportions.x),
		rcp_focal, K)/viewProportions.x;

	// border cropping radius is in anamorphic coordinates:

	// Vertical point radius
	const float croppingVertical = get_radius(
			atan(tan(halfOmega)/radiusOfOmega*viewProportions.y*rsqrt(S)),
		rcp_focal, K)/viewProportions.y*sqrt(S);
	// Diagonal point radius
	const float anamorphicDiagonal = length(float2(
		viewProportions.x,
		viewProportions.y*rsqrt(S)
	));
	const float croppingDigonal = get_radius(
			atan(tan(halfOmega)/radiusOfOmega*anamorphicDiagonal),
		rcp_focal, K)/anamorphicDiagonal;

	// Circular fish-eye
	const float circularFishEye = max(croppingHorizontal, croppingVertical);
	// Cropped circle
	const float croppedCircle = min(croppingHorizontal, croppingVertical);
	// Full-frame
	const float fullFrame = croppingDigonal;
	
	const float croppingScalar =
		CroppingFactor<0.5
		? lerp(
			circularFishEye, // circular fish-eye
			croppedCircle,   // cropped circle
			max(CroppingFactor*2.0, 0.0) // ↤ [0,1] range
		)
		: lerp(
			croppedCircle, // cropped circle
			fullFrame, // full-frame
			min(CroppingFactor*2.0-1.0, 1.0) // ↤ [1,2] range
		);

	// Start live calculations that actually depend on the texture coordinates
	
	// Correct aspect ratio, normalized to the corner
	float2 viewCoord = NDC ? texCoord : (texCoord * 2.0 - 1.0);
	float2 originalViewCoord = viewCoord;
	// Apply aspect ratio normalization
	viewCoord /= aspectRatioOffsetScale;
	
	viewCoord *= viewProportions;

	viewCoord *= croppingScalar;

	// Image radius
	// This is the actual main and only dynamic lens distortion formula (the result of this depends by pixel, the rest is static)
	float radius = S == 1.0 ?
		dot(viewCoord, viewCoord) // spherical
		: ((viewCoord.y * viewCoord.y / S) + (viewCoord.x * viewCoord.x)); // anamorphic
	float rcp_radius = rsqrt(radius);
	radius = sqrt(radius);

	// get θ from anamorphic radius
	float theta = get_theta(radius, rcp_focal, K);

	// Rectilinear perspective transformation
	viewCoord *= tan(theta)*rcp_radius;

	// Back to normalized, centered coordinates
	const float2 toUvCoord = radiusOfOmega/(tan(halfOmega)*viewProportions);
	viewCoord *= toUvCoord;

// end of perspective mapping
//----------------------------------------------

	// Undo aspect ratio normalization
	viewCoord *= aspectRatioOffsetScale;
	if (originalViewCoord.x > 0 != viewCoord.x > 0)
	{
		viewCoord.x = originalViewCoord.x > 0 ? 2.0 : -2.0;
	}
	if (originalViewCoord.y > 0 != viewCoord.y > 0)
	{
		viewCoord.y = originalViewCoord.y > 0 ? 2.0 : -2.0;
	}
	if (clip) // This is mostly useless for normal input values/settings
	{
		viewCoord = clamp(viewCoord, -1.0, 1.0);
	}
	// Back to UV Coordinates
	texCoord = NDC ? viewCoord : (viewCoord * 0.5 + 0.5);

	// Outside border mask with anti-aliasing
	borderAlpha = GetBorderMask(viewCoord);

	return texCoord;
}

float2 PerfectPerspectiveLensDistortion(float2 texCoord, float horFOV, float2 resolution, bool NDC = false, bool clip = false)
{
    float dummyBorderAlpha;
	return PerfectPerspectiveLensDistortion(texCoord, horFOV, resolution, dummyBorderAlpha, NDC, clip);
}

// Note: clipping is off by default as it breaks some UI polygons.
float2 PerfectPerspectiveLensDistortion_Inverse(float2 texCoord, float horFOV, float2 resolution, bool NDC = false, bool adjust = true, bool clip = false)
{
	const float currentAspectRatio = resolution.x / resolution.y;

	// This is to make the distortion look roughly the same as it would at 16:9 around the 16:9 part of the image, independently of the aspect ratio.
	// The cost is that it won't apply as "correctly" anymore for arbitrary aspect ratios, but in UW it was way too strong, so this is preferred.
	float aspectRatioOffsetScale = 1.0;
	if (AspectRatioCorrection > 0.f)
	{
		// For 32:9, this will be 0.5.
		aspectRatioOffsetScale = lerp(1.0, NativeAspectRatio / currentAspectRatio, AspectRatioCorrection);
		resolution.x *= aspectRatioOffsetScale;
		horFOV = atan( tan( horFOV * 0.5 ) * aspectRatioOffsetScale ) * 2.0;
	}

	// Aspect ratio transformation vector
	const float2 viewProportions = normalize(resolution);
	// Half field of view angle in radians
	const float halfOmega = horFOV * 0.5;
	// Get radius at Ω for a given FOV type
	const float radiusOfOmega = getRadiusOfOmega(viewProportions);
	// Reciprocal focal length
	const float rcp_focal = get_rcp_focal(halfOmega, radiusOfOmega, K);

	// Horizontal point radius
	const float croppingHorizontal = get_radius(
			atan(tan(halfOmega)/radiusOfOmega*viewProportions.x),
		rcp_focal, K)/viewProportions.x;

	// border cropping radius is in anamorphic coordinates:

	// Vertical point radius
	const float croppingVertical = get_radius(
			atan(tan(halfOmega)/radiusOfOmega*viewProportions.y*rsqrt(S)),
		rcp_focal, K)/viewProportions.y*sqrt(S);
	// Diagonal point radius
	const float anamorphicDiagonal = length(float2(
		viewProportions.x,
		viewProportions.y*rsqrt(S)
	));
	const float croppingDigonal = get_radius(
			atan(tan(halfOmega)/radiusOfOmega*anamorphicDiagonal),
		rcp_focal, K)/anamorphicDiagonal;

	// Circular fish-eye
	const float circularFishEye = max(croppingHorizontal, croppingVertical);
	// Cropped circle
	const float croppedCircle = min(croppingHorizontal, croppingVertical);
	// Full-frame
	const float fullFrame = croppingDigonal;
	
	const float croppingScalar =
		CroppingFactor<0.5
		? lerp(
			circularFishEye, // circular fish-eye
			croppedCircle,   // cropped circle
			max(CroppingFactor*2.0, 0.0) // ↤ [0,1] range
		)
		: lerp(
			croppedCircle, // cropped circle
			fullFrame, // full-frame
			min(CroppingFactor*2.0-1.0, 1.0) // ↤ [1,2] range
		);

	// Start live calculations that actually depend on the texture coordinates
	
	// Correct aspect ratio, normalized to the corner
	float2 viewCoord = NDC ? texCoord : (texCoord * 2.0 - 1.0);

	// Apply aspect ratio normalization
	viewCoord /= aspectRatioOffsetScale;

	// Back to normalized, centered coordinates
	const float2 toUvCoord = radiusOfOmega/(tan(halfOmega)*viewProportions);
	viewCoord /= toUvCoord;

	// Inverse formula from Fubax:
	float theta = atan(length(viewCoord));
	float radius = tan(theta * K) / K / rcp_focal;
	viewCoord = (S == 1.f || true) ?
		(normalize(viewCoord) * radius) :
		(normalize(viewCoord) * float2(radius, sqrt(S) * radius));

	viewCoord /= croppingScalar;
	
	viewCoord /= viewProportions;

	// Undo aspect ratio normalization
	viewCoord *= aspectRatioOffsetScale;
	
	// Adjust vertices at the edges, so that they are less 
	// Doing it only on Y because usually there's no borders cropping on X, at least with the settings we have settings.
	//TODO LUMA: If we wanted to improve it, we could link this with the current FOV and scaling parameters (UI can still go out of range at very high FOVs).
	if (adjust)
	{
		if (abs(viewCoord.y) > Inverse_BorderYThreshold)
		{
			float outOfBorderAmount = (abs(viewCoord.y) - Inverse_BorderYThreshold) / (1.0 - Inverse_BorderYThreshold); // Within custom borders at 0, outside custom borders at > 0, outside screen borders at > 1.
			outOfBorderAmount *= Inverse_OutOfBorderRestorationRange * aspectRatioOffsetScale; // Scale back towards the center of the screen some vertices that would have ended up outside...
			outOfBorderAmount = pow(outOfBorderAmount, 1.0 / lerp(1.0, Inverse_OutOfBorderBorderPow, aspectRatioOffsetScale));
			viewCoord.y = (Inverse_BorderYThreshold + (outOfBorderAmount * (1.0 - Inverse_BorderYThreshold))) * sign(viewCoord.y);
		}
	}
	// This can end up in vertices compressed as the edges, but also helps in case they ended up being cropped out
	if (clip)
	{
		viewCoord = clamp(viewCoord, -1.0, 1.0);
	}

	// Back to UV Coordinates
	texCoord = NDC ? viewCoord : (viewCoord * 0.5 + 0.5);

	return texCoord;
}