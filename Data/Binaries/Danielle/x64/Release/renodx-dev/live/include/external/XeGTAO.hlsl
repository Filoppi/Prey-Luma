///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// XeGTAO is based on GTAO/GTSO "Jimenez et al. / Practical Real-Time Strategies for Accurate Indirect Occlusion", 
// https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
// 
// Implementation:  Filip Strugar (filip.strugar@intel.com), Steve Mccalla <stephen.mccalla@intel.com>         (\_/)
// Version:         (see XeGTAO.h)                                                                            (='.'=)
// Details:         https://github.com/GameTechDev/XeGTAO                                                     (")_(")
//
// Version history: see XeGTAO.h
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// This should be defined if you need bent normals on rgb output
#define XE_GTAO_COMPUTE_BENT_NORMALS
// LUMA FT: ... (move these to our code?)
// Define this if your depth texture is in "linear" space (0-1 range, where 0 is camera near and 1 is camera far (or the opposite))
//#define XE_GTAO_DEPTH_TEXTURE_LINEAR
//#define XE_GTAO_DEPTH_TEXTURE_INVERTED
#define XE_GTAO_FP32_DEPTHS
#define XE_GTAO_USE_HALF_FLOAT_PRECISION 0
//#define XE_GTAO_GENERATE_NORMALS_INPLACE
static const float DepthYDir = -1.0;

#if defined( XE_GTAO_SHOW_NORMALS ) || defined( XE_GTAO_SHOW_EDGES ) || defined( XE_GTAO_SHOW_BENT_NORMALS )
RWTexture2D<float4>         g_outputDbgImage    : register( u2 );
#endif

#include "include/external/XeGTAO.h"

#define XE_GTAO_PI               	(3.1415926535897932384626433832795)
#define XE_GTAO_PI_HALF             (1.5707963267948966192313216916398)

#ifndef XE_GTAO_USE_HALF_FLOAT_PRECISION
#define XE_GTAO_USE_HALF_FLOAT_PRECISION 1
#endif

#if defined(XE_GTAO_FP32_DEPTHS) && XE_GTAO_USE_HALF_FLOAT_PRECISION
#error Using XE_GTAO_USE_HALF_FLOAT_PRECISION with 32bit depths is not supported yet unfortunately (it is possible to apply fp16 on parts not related to depth but this has not been done yet)
#endif 


#if (XE_GTAO_USE_HALF_FLOAT_PRECISION != 0)
#if 1 // old fp16 approach (<SM6.2)
    typedef min16float      lpfloat; 
    typedef min16float2     lpfloat2;
    typedef min16float3     lpfloat3;
    typedef min16float4     lpfloat4;
    typedef min16float3x3   lpfloat3x3;
#else // new fp16 approach (requires SM6.2 and -enable-16bit-types) - WARNING: perf degradation noticed on some HW, while the old (min16float) path is mostly at least a minor perf gain so this is more useful for quality testing
    typedef float16_t       lpfloat; 
    typedef float16_t2      lpfloat2;
    typedef float16_t3      lpfloat3;
    typedef float16_t4      lpfloat4;
    typedef float16_t3x3    lpfloat3x3;
#endif
#else
    typedef float           lpfloat;
    typedef float2          lpfloat2;
    typedef float3          lpfloat3;
    typedef float4          lpfloat4;
    typedef float3x3        lpfloat3x3;
#endif


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Inputs are screen XY and viewspace depth, output is viewspace position
float3 XeGTAO_ComputeViewspacePosition( const float2 screenPos, const float viewspaceDepth, const GTAOConstants consts )
{
    float3 ret;
    ret.xy = (consts.NDCToViewMul * screenPos.xy + consts.NDCToViewAdd) * viewspaceDepth;
    ret.z = viewspaceDepth;
    return ret;
}

// Converts depth from linear unbound native space (the actual scene physics distance) to the near/far 0-1 normalized range
float XeGTAO_ScreenSpaceToViewSpaceDepth( float screenDepth, const GTAOConstants consts )
{
#ifdef XE_GTAO_DEPTH_TEXTURE_INVERTED
    screenDepth = 1.0 - screenDepth; // LUMA FT: added depth flip
#endif
    //return screenDepth * consts.DepthUnpackConsts.x + consts.DepthUnpackConsts.y; //TODOFT
    float depthLinearizeMul = consts.DepthUnpackConsts.x;
    float depthLinearizeAdd = consts.DepthUnpackConsts.y;
    // Optimised version of "-cameraClipNear / (cameraClipFar - projDepth * (cameraClipFar - cameraClipNear)) * cameraClipFar"
    return depthLinearizeMul / (depthLinearizeAdd - screenDepth);
}

lpfloat4 XeGTAO_CalculateEdges( const lpfloat centerZ, const lpfloat leftZ, const lpfloat rightZ, const lpfloat topZ, const lpfloat bottomZ )
{
    lpfloat4 edgesLRTB = lpfloat4( leftZ, rightZ, topZ, bottomZ ) - (lpfloat)centerZ;

    lpfloat slopeLR = (edgesLRTB.y - edgesLRTB.x) * 0.5;
    lpfloat slopeTB = (edgesLRTB.w - edgesLRTB.z) * 0.5;
    lpfloat4 edgesLRTBSlopeAdjusted = edgesLRTB + lpfloat4( slopeLR, -slopeLR, slopeTB, -slopeTB );
    edgesLRTB = min( abs( edgesLRTB ), abs( edgesLRTBSlopeAdjusted ) );
    return lpfloat4(saturate( ( 1.25 - edgesLRTB / (centerZ * 0.011) ) ));
}

// packing/unpacking for edges; 2 bits per edge mean 4 gradient values (0, 0.33, 0.66, 1) for smoother transitions!
lpfloat XeGTAO_PackEdges( lpfloat4 edgesLRTB )
{
    // integer version:
    // edgesLRTB = saturate(edgesLRTB) * 2.9.xxxx + 0.5.xxxx;
    // return (((uint)edgesLRTB.x) << 6) + (((uint)edgesLRTB.y) << 4) + (((uint)edgesLRTB.z) << 2) + (((uint)edgesLRTB.w));
    // 
    // optimized, should be same as above
    edgesLRTB = round( saturate( edgesLRTB ) * 2.9 );
    return dot( edgesLRTB, lpfloat4( 64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0 ) ) ;
}

float3 XeGTAO_CalculateNormal( const float4 edgesLRTB, float3 pixCenterPos, float3 pixLPos, float3 pixRPos, float3 pixTPos, float3 pixBPos )
{
    // Get this pixel's viewspace normal
    float4 acceptedNormals  = saturate( float4( edgesLRTB.x*edgesLRTB.z, edgesLRTB.z*edgesLRTB.y, edgesLRTB.y*edgesLRTB.w, edgesLRTB.w*edgesLRTB.x ) + 0.01 );

    pixLPos = normalize(pixLPos - pixCenterPos);
    pixRPos = normalize(pixRPos - pixCenterPos);
    pixTPos = normalize(pixTPos - pixCenterPos);
    pixBPos = normalize(pixBPos - pixCenterPos);

    float3 pixelNormal =  acceptedNormals.x * cross( pixLPos, pixTPos ) +
                        + acceptedNormals.y * cross( pixTPos, pixRPos ) +
                        + acceptedNormals.z * cross( pixRPos, pixBPos ) +
                        + acceptedNormals.w * cross( pixBPos, pixLPos );
    pixelNormal = normalize( pixelNormal );

    return pixelNormal;
}

#ifdef XE_GTAO_SHOW_DEBUG_VIZ
float4 DbgGetSliceColor(int slice, int sliceCount, bool mirror)
{
    float red = (float)slice / (float)sliceCount; float green = 0.01; float blue = 1.0 - (float)slice / (float)sliceCount;
    return (mirror)?(float4(blue, green, red, 0.9)):(float4(red, green, blue, 0.9));
}
#endif

// http://h14s.p5r.org/2012/09/0x5f3759df.html, [Drobot2014a] Low Level Optimizations for GCN, https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf slide 63
lpfloat XeGTAO_FastSqrt( float x )
{
    return (lpfloat)(asfloat( 0x1fbd1df5 + ( asint( x ) >> 1 ) ));
}
// input [-1, 1] and output [0, PI], from https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/
lpfloat XeGTAO_FastACos( lpfloat inX )
{ 
    const lpfloat PI = 3.141593;
    const lpfloat HALF_PI = 1.570796;
    lpfloat x = abs(inX); 
    lpfloat res = -0.156583 * x + HALF_PI; 
    res *= XeGTAO_FastSqrt(1.0 - x); 
    return (inX >= 0) ? res : PI - res; 
}

// "Efficiently building a matrix to rotate one vector to another"
// http://cs.brown.edu/research/pubs/pdfs/1999/Moller-1999-EBA.pdf / https://dl.acm.org/doi/10.1080/10867651.1999.10487509
// (using https://github.com/assimp/assimp/blob/master/include/assimp/matrix3x3.inl#L275 as a code reference as it seems to be best)
lpfloat3x3 XeGTAO_RotFromToMatrix( lpfloat3 from, lpfloat3 to )
{
    const lpfloat e       = dot(from, to);
    const lpfloat f       = abs(e); //(e < 0)? -e:e;

    // WARNING: This has not been tested/worked through, especially not for 16bit floats; seems to work in our special use case (from is always {0, 0, -1}) but wouldn't use it in general
    if( f > lpfloat( 1.0 - 0.0003 ) )
        return lpfloat3x3( 1, 0, 0, 0, 1, 0, 0, 0, 1 );

    const lpfloat3 v      = cross( from, to );
    /* ... use this hand optimized version (9 mults less) */
    const lpfloat h       = (1.0)/(1.0 + e);      /* optimization by Gottfried Chen */
    const lpfloat hvx     = h * v.x;
    const lpfloat hvz     = h * v.z;
    const lpfloat hvxy    = hvx * v.y;
    const lpfloat hvxz    = hvx * v.z;
    const lpfloat hvyz    = hvz * v.y;

    lpfloat3x3 mtx;
    mtx[0][0] = e + hvx * v.x;
    mtx[0][1] = hvxy - v.z;
    mtx[0][2] = hvxz + v.y;

    mtx[1][0] = hvxy + v.z;
    mtx[1][1] = e + h * v.y * v.y;
    mtx[1][2] = hvyz - v.x;

    mtx[2][0] = hvxz - v.y;
    mtx[2][1] = hvyz + v.x;
    mtx[2][2] = e + hvz * v.z;

    return mtx;
}

float4 XeGTAO_MainPass( const uint2 pixCoord, lpfloat sliceCount, lpfloat stepsPerSlice, const lpfloat2 localNoise, lpfloat3 viewspaceNormal, const GTAOConstants consts, 
    Texture2D<float4> sourceViewspaceDepth, SamplerState depthSampler )
{                                                                       
    float2 normalizedScreenPos = (pixCoord + 0.5.xx) * consts.ViewportPixelSize;

    lpfloat4 valuesUL   = sourceViewspaceDepth.GatherRed( depthSampler, float2( pixCoord * consts.ViewportPixelSize ) * float2(1, DepthYDir)               );
    lpfloat4 valuesBR   = sourceViewspaceDepth.GatherRed( depthSampler, float2( pixCoord * consts.ViewportPixelSize ) * float2(1, DepthYDir), int2( 1, 1 ) );
#ifdef XE_GTAO_DEPTH_TEXTURE_LINEAR
    valuesUL = XeGTAO_ScreenSpaceToViewSpaceDepth(valuesUL, consts);
    valuesBR = XeGTAO_ScreenSpaceToViewSpaceDepth(valuesBR, consts);
#endif

    // viewspace Z at the center
    lpfloat viewspaceZ  = valuesUL.y; //sourceViewspaceDepth.SampleLevel( depthSampler, normalizedScreenPos, 0 ).x; 

    // viewspace Zs left top right bottom
    const lpfloat pixLZ = valuesUL.x;
    const lpfloat pixTZ = valuesUL.z;
    const lpfloat pixRZ = valuesBR.z;
    const lpfloat pixBZ = valuesBR.x;

    lpfloat4 edgesLRTB  = XeGTAO_CalculateEdges( (lpfloat)viewspaceZ, (lpfloat)pixLZ, (lpfloat)pixRZ, (lpfloat)pixTZ, (lpfloat)pixBZ );

	// Generating screen space normals in-place is faster than generating normals in a separate pass but requires
	// use of 32bit depth buffer (16bit works but visibly degrades quality) which in turn slows everything down. So to
	// reduce complexity and allow for screen space normal reuse by other effects, we've pulled it out into a separate
	// pass.
	// However, we leave this code in, in case anyone has a use-case where it fits better.
#ifdef XE_GTAO_GENERATE_NORMALS_INPLACE
    float3 CENTER   = XeGTAO_ComputeViewspacePosition( normalizedScreenPos, viewspaceZ, consts );
    float3 LEFT     = XeGTAO_ComputeViewspacePosition( normalizedScreenPos + float2(-1,  0) * consts.ViewportPixelSize, pixLZ, consts );
    float3 RIGHT    = XeGTAO_ComputeViewspacePosition( normalizedScreenPos + float2( 1,  0) * consts.ViewportPixelSize, pixRZ, consts );
    float3 TOP      = XeGTAO_ComputeViewspacePosition( normalizedScreenPos + float2( 0, -1) * consts.ViewportPixelSize, pixTZ, consts );
    float3 BOTTOM   = XeGTAO_ComputeViewspacePosition( normalizedScreenPos + float2( 0,  1) * consts.ViewportPixelSize, pixBZ, consts );
    viewspaceNormal = (lpfloat3)XeGTAO_CalculateNormal( edgesLRTB, CENTER, LEFT, RIGHT, TOP, BOTTOM );
#endif

    // Move center pixel slightly towards camera to avoid imprecision artifacts due to depth buffer imprecision; offset depends on depth texture format used
#ifdef XE_GTAO_FP32_DEPTHS
    viewspaceZ *= 0.99999;     // this is good for FP32 depth buffer
#else
    viewspaceZ *= 0.99920;     // this is good for FP16 depth buffer
#endif

    const float3 pixCenterPos   = XeGTAO_ComputeViewspacePosition( normalizedScreenPos, viewspaceZ, consts );
    const lpfloat3 viewVec      = (lpfloat3)normalize(-pixCenterPos);
    
    // prevents normals that are facing away from the view vector - xeGTAO struggles with extreme cases, but in Vanilla it seems rare so it's disabled by default
    //viewspaceNormal = normalize( viewspaceNormal + max( 0, -dot( viewspaceNormal, viewVec ) ) * viewVec );

#ifdef XE_GTAO_SHOW_NORMALS
    g_outputDbgImage[pixCoord] = float4( DisplayNormalSRGB( viewspaceNormal.xyz ), 1 );
#endif

#ifdef XE_GTAO_SHOW_EDGES
    g_outputDbgImage[pixCoord] = 1.0 - float4( edgesLRTB.x, edgesLRTB.y * 0.5 + edgesLRTB.w * 0.5, edgesLRTB.z, 1.0 );
#endif

#if XE_GTAO_USE_DEFAULT_CONSTANTS != 0
    const lpfloat effectRadius              = (lpfloat)consts.EffectRadius * (lpfloat)XE_GTAO_DEFAULT_RADIUS_MULTIPLIER;
    const lpfloat sampleDistributionPower   = (lpfloat)XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER;
    const lpfloat thinOccluderCompensation  = (lpfloat)XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION;
    const lpfloat falloffRange              = (lpfloat)XE_GTAO_DEFAULT_FALLOFF_RANGE * effectRadius;
#else
    const lpfloat effectRadius              = (lpfloat)consts.EffectRadius * (lpfloat)consts.RadiusMultiplier;
    const lpfloat sampleDistributionPower   = (lpfloat)consts.SampleDistributionPower;
    const lpfloat thinOccluderCompensation  = (lpfloat)consts.ThinOccluderCompensation;
    const lpfloat falloffRange              = (lpfloat)consts.EffectFalloffRange * effectRadius;
#endif

    const lpfloat falloffFrom       = effectRadius * ((lpfloat)1-(lpfloat)consts.EffectFalloffRange);

    // fadeout precompute optimisation
    const lpfloat falloffMul        = (lpfloat)-1.0 / ( falloffRange );
    const lpfloat falloffAdd        = falloffFrom / ( falloffRange ) + (lpfloat)1.0;

    lpfloat visibility = 0;
#ifdef XE_GTAO_COMPUTE_BENT_NORMALS
    lpfloat3 bentNormal = 0;
#else
    lpfloat3 bentNormal = viewspaceNormal;
#endif

#ifdef XE_GTAO_SHOW_DEBUG_VIZ
    float3 dbgWorldPos          = mul(g_globals.ViewInv, float4(pixCenterPos, 1)).xyz;
#endif

    // see "Algorithm 1" in https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
    {
        const lpfloat noiseSlice  = (lpfloat)localNoise.x;
        const lpfloat noiseSample = (lpfloat)localNoise.y;

        // quality settings / tweaks / hacks
        const lpfloat pixelTooCloseThreshold  = 1.3;      // if the offset is under approx pixel size (pixelTooCloseThreshold), push it out to the minimum distance

        // approx viewspace pixel size at pixCoord; approximation of NDCToViewspace( normalizedScreenPos.xy + consts.ViewportPixelSize.xy, pixCenterPos.z ).xy - pixCenterPos.xy;
        const float2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ.xx * consts.NDCToViewMul_x_PixelSize;

        lpfloat screenspaceRadius   = effectRadius / (lpfloat)pixelDirRBViewspaceSizeAtCenterZ.x;

        // fade out for small screen radii 
        visibility += saturate((10 - screenspaceRadius)/100)*0.5;

#if 0   // sensible early-out for even more performance; disabled because not yet tested
        [branch]
        if( screenspaceRadius < pixelTooCloseThreshold )
        {
            XeGTAO_OutputWorkingTerm( pixCoord, 1, viewspaceNormal, outWorkingAOTerm );
            return;
        }
#endif

#ifdef XE_GTAO_SHOW_DEBUG_VIZ
        [branch] if (IsUnderCursorRange(pixCoord, int2(1, 1)))
        {
            float3 dbgWorldNorm     = mul((float3x3)g_globals.ViewInv, viewspaceNormal).xyz;
            float3 dbgWorldViewVec  = mul((float3x3)g_globals.ViewInv, viewVec).xyz;
            //DebugDraw3DArrow(dbgWorldPos, dbgWorldPos + 0.5 * dbgWorldViewVec, 0.02, float4(0, 1, 0, 0.95));
            //DebugDraw2DCircle(pixCoord, screenspaceRadius, float4(1, 0, 0.2, 1));
            DebugDraw3DSphere(dbgWorldPos, effectRadius, float4(1, 0.2, 0, 0.1));
            //DebugDraw3DText(dbgWorldPos, float2(0, 0), float4(0.6, 0.3, 0.3, 1), float4( pixelDirRBViewspaceSizeAtCenterZ.xy, 0, screenspaceRadius) );
        }
#endif

        // this is the min distance to start sampling from to avoid sampling from the center pixel (no useful data obtained from sampling center pixel)
        const lpfloat minS = (lpfloat)pixelTooCloseThreshold / screenspaceRadius;

        [unroll]
        for( lpfloat slice = 0; slice < sliceCount; slice++ )
        {
            lpfloat sliceK = (slice+noiseSlice) / sliceCount;
            // lines 5, 6 from the paper
            lpfloat phi = sliceK * XE_GTAO_PI;
            lpfloat cosPhi = cos(phi);
            lpfloat sinPhi = sin(phi);
            lpfloat2 omega = lpfloat2(cosPhi, -sinPhi);       //lpfloat2 on omega causes issues with big radii

            // convert to screen units (pixels) for later use
            omega *= screenspaceRadius;

            // line 8 from the paper
            const lpfloat3 directionVec = lpfloat3(cosPhi, sinPhi, 0);

            // line 9 from the paper
            const lpfloat3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);

            // line 10 from the paper
            //axisVec is orthogonal to directionVec and viewVec, used to define projectedNormal
            const lpfloat3 axisVec = normalize( cross(orthoDirectionVec, viewVec) );

            // alternative line 9 from the paper
            // float3 orthoDirectionVec = cross( viewVec, axisVec );

            // line 11 from the paper
            lpfloat3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);

            // line 13 from the paper
            lpfloat signNorm = (lpfloat)sign( dot( orthoDirectionVec, projectedNormalVec ) );

            // line 14 from the paper
            lpfloat projectedNormalVecLength = length(projectedNormalVec);
            lpfloat cosNorm = (lpfloat)saturate(dot(projectedNormalVec, viewVec) / projectedNormalVecLength);

            // line 15 from the paper
            lpfloat n = signNorm * XeGTAO_FastACos(cosNorm);

            // this is a lower weight target; not using -1 as in the original paper because it is under horizon, so a 'weight' has different meaning based on the normal
            const lpfloat lowHorizonCos0  = cos(n+XE_GTAO_PI_HALF);
            const lpfloat lowHorizonCos1  = cos(n-XE_GTAO_PI_HALF);

            // lines 17, 18 from the paper, manually unrolled the 'side' loop
            lpfloat horizonCos0           = lowHorizonCos0; //-1;
            lpfloat horizonCos1           = lowHorizonCos1; //-1;

            [unroll]
            for( lpfloat step = 0; step < stepsPerSlice; step++ )
            {
                // R1 sequence (http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/)
                const lpfloat stepBaseNoise = lpfloat(slice + step * stepsPerSlice) * 0.6180339887498948482; // <- this should unroll
                lpfloat stepNoise = frac(noiseSample + stepBaseNoise);

                // approx line 20 from the paper, with added noise
                lpfloat s = (step+stepNoise) / (stepsPerSlice); // + (lpfloat2)1e-6f);

                // additional distribution modifier
                s       = (lpfloat)pow( s, (lpfloat)sampleDistributionPower );

                // avoid sampling center pixel
                s       += minS;

                // approx lines 21-22 from the paper, unrolled
                lpfloat2 sampleOffset = s * omega;

                lpfloat sampleOffsetLength = length( sampleOffset );

                // note: when sampling, using point_point_point or point_point_linear sampler works, but linear_linear_linear will cause unwanted interpolation between neighbouring depth values on the same MIP level!
                const lpfloat mipLevel    = (lpfloat)clamp( log2( sampleOffsetLength ) - consts.DepthMIPSamplingOffset, 0, XE_GTAO_DEPTH_MIP_LEVELS );

                // Snap to pixel center (more correct direction math, avoids artifacts due to sampling pos not matching depth texel center - messes up slope - but adds other 
                // artifacts due to them being pushed off the slice). Also use full precision for high res cases.
                sampleOffset = round(sampleOffset) * (lpfloat2)consts.ViewportPixelSize;

#ifdef XE_GTAO_SHOW_DEBUG_VIZ
                int mipLevelU = (int)round(mipLevel);
                float4 mipColor = saturate( float4( mipLevelU>=3, mipLevelU>=1 && mipLevelU<=3, mipLevelU<=1, 1.0 ) );
                if( all( sampleOffset == 0 ) )
                    DebugDraw2DText( pixCoord, float4( 1, 0, 0, 1), pixelTooCloseThreshold );
                [branch] if (IsUnderCursorRange(pixCoord, int2(1, 1)))
                {
                    //DebugDraw2DText( (normalizedScreenPos + sampleOffset) * consts.ViewportSize, mipColor, mipLevelU );
                    //DebugDraw2DText( (normalizedScreenPos + sampleOffset) * consts.ViewportSize, mipColor, (uint)slice );
                    //DebugDraw2DText( (normalizedScreenPos - sampleOffset) * consts.ViewportSize, mipColor, (uint)slice );
                    //DebugDraw2DText( (normalizedScreenPos - sampleOffset) * consts.ViewportSize, saturate( float4( mipLevelU>=3, mipLevelU>=1 && mipLevelU<=3, mipLevelU<=1, 1.0 ) ), mipLevelU );
                }
#endif

                float2 sampleScreenPos0 = normalizedScreenPos + sampleOffset;
                float  SZ0 = sourceViewspaceDepth.SampleLevel( depthSampler, sampleScreenPos0 * float2(1, DepthYDir), mipLevel ).x;
#ifdef XE_GTAO_DEPTH_TEXTURE_LINEAR
                SZ0 = XeGTAO_ScreenSpaceToViewSpaceDepth(SZ0, consts);
#endif
                float3 samplePos0 = XeGTAO_ComputeViewspacePosition( sampleScreenPos0, SZ0, consts );

                float2 sampleScreenPos1 = normalizedScreenPos - sampleOffset;
                float  SZ1 = sourceViewspaceDepth.SampleLevel( depthSampler, sampleScreenPos1 * float2(1, DepthYDir), mipLevel ).x;
#ifdef XE_GTAO_DEPTH_TEXTURE_LINEAR
                SZ1 = XeGTAO_ScreenSpaceToViewSpaceDepth(SZ1, consts);
#endif
                float3 samplePos1 = XeGTAO_ComputeViewspacePosition( sampleScreenPos1, SZ1, consts );

                float3 sampleDelta0     = (samplePos0 - float3(pixCenterPos)); // using lpfloat for sampleDelta causes precision issues
                float3 sampleDelta1     = (samplePos1 - float3(pixCenterPos)); // using lpfloat for sampleDelta causes precision issues
                lpfloat sampleDist0     = (lpfloat)length( sampleDelta0 );
                lpfloat sampleDist1     = (lpfloat)length( sampleDelta1 );

                // approx lines 23, 24 from the paper, unrolled
                lpfloat3 sampleHorizonVec0 = (lpfloat3)(sampleDelta0 / sampleDist0);
                lpfloat3 sampleHorizonVec1 = (lpfloat3)(sampleDelta1 / sampleDist1);

                // any sample out of radius should be discarded - also use fallof range for smooth transitions; this is a modified idea from "4.3 Implementation details, Bounding the sampling area"
#if XE_GTAO_USE_DEFAULT_CONSTANTS != 0 && XE_GTAO_DEFAULT_THIN_OBJECT_HEURISTIC == 0
                lpfloat weight0         = saturate( sampleDist0 * falloffMul + falloffAdd );
                lpfloat weight1         = saturate( sampleDist1 * falloffMul + falloffAdd );
#else
                // this is our own thickness heuristic that relies on sooner discarding samples behind the center
                lpfloat falloffBase0    = length( lpfloat3(sampleDelta0.x, sampleDelta0.y, sampleDelta0.z * (1+thinOccluderCompensation) ) );
                lpfloat falloffBase1    = length( lpfloat3(sampleDelta1.x, sampleDelta1.y, sampleDelta1.z * (1+thinOccluderCompensation) ) );
                lpfloat weight0         = saturate( falloffBase0 * falloffMul + falloffAdd );
                lpfloat weight1         = saturate( falloffBase1 * falloffMul + falloffAdd );
#endif

                // sample horizon cos
                lpfloat shc0 = (lpfloat)dot(sampleHorizonVec0, viewVec);
                lpfloat shc1 = (lpfloat)dot(sampleHorizonVec1, viewVec);

                // discard unwanted samples
                shc0 = lerp( lowHorizonCos0, shc0, weight0 ); // this would be more correct but too expensive: cos(lerp( acos(lowHorizonCos0), acos(shc0), weight0 ));
                shc1 = lerp( lowHorizonCos1, shc1, weight1 ); // this would be more correct but too expensive: cos(lerp( acos(lowHorizonCos1), acos(shc1), weight1 ));

                // thickness heuristic - see "4.3 Implementation details, Height-field assumption considerations"
#if 0   // (disabled, not used) this should match the paper
                lpfloat newhorizonCos0 = max( horizonCos0, shc0 );
                lpfloat newhorizonCos1 = max( horizonCos1, shc1 );
                horizonCos0 = (horizonCos0 > shc0)?( lerp( newhorizonCos0, shc0, thinOccluderCompensation ) ):( newhorizonCos0 );
                horizonCos1 = (horizonCos1 > shc1)?( lerp( newhorizonCos1, shc1, thinOccluderCompensation ) ):( newhorizonCos1 );
#elif 0 // (disabled, not used) this is slightly different from the paper but cheaper and provides very similar results
                horizonCos0 = lerp( max( horizonCos0, shc0 ), shc0, thinOccluderCompensation );
                horizonCos1 = lerp( max( horizonCos1, shc1 ), shc1, thinOccluderCompensation );
#else   // this is a version where thicknessHeuristic is completely disabled
                horizonCos0 = max( horizonCos0, shc0 );
                horizonCos1 = max( horizonCos1, shc1 );
#endif


#ifdef XE_GTAO_SHOW_DEBUG_VIZ
                [branch] if (IsUnderCursorRange(pixCoord, int2(1, 1)))
                {
                    float3 WS_samplePos0 = mul(g_globals.ViewInv, float4(samplePos0, 1)).xyz;
                    float3 WS_samplePos1 = mul(g_globals.ViewInv, float4(samplePos1, 1)).xyz;
                    float3 WS_sampleHorizonVec0 = mul( (float3x3)g_globals.ViewInv, sampleHorizonVec0).xyz;
                    float3 WS_sampleHorizonVec1 = mul( (float3x3)g_globals.ViewInv, sampleHorizonVec1).xyz;
                    // DebugDraw3DSphere( WS_samplePos0, effectRadius * 0.02, DbgGetSliceColor(slice, sliceCount, false) );
                    // DebugDraw3DSphere( WS_samplePos1, effectRadius * 0.02, DbgGetSliceColor(slice, sliceCount, true) );
                    DebugDraw3DSphere( WS_samplePos0, effectRadius * 0.02, mipColor );
                    DebugDraw3DSphere( WS_samplePos1, effectRadius * 0.02, mipColor );
                    // DebugDraw3DArrow( WS_samplePos0, WS_samplePos0 - WS_sampleHorizonVec0, 0.002, float4(1, 0, 0, 1 ) );
                    // DebugDraw3DArrow( WS_samplePos1, WS_samplePos1 - WS_sampleHorizonVec1, 0.002, float4(1, 0, 0, 1 ) );
                    // DebugDraw3DText( WS_samplePos0, float2(0,  0), float4( 1, 0, 0, 1), weight0 );
                    // DebugDraw3DText( WS_samplePos1, float2(0,  0), float4( 1, 0, 0, 1), weight1 );

                    // DebugDraw2DText( float2( 500, 94+(step+slice*3)*12 ), float4( 0, 1, 0, 1 ), float4( projectedNormalVecLength, 0, horizonCos0, horizonCos1 ) );
                }
#endif
            }

#if 1       // I can't figure out the slight overdarkening on high slopes, so I'm adding this fudge - in the training set, 0.05 is close (PSNR 21.34) to disabled (PSNR 21.45)
            projectedNormalVecLength = lerp( projectedNormalVecLength, 1, 0.05 );
#endif

            // line ~27, unrolled
            lpfloat h0 = -XeGTAO_FastACos((lpfloat)horizonCos1);
            lpfloat h1 = XeGTAO_FastACos((lpfloat)horizonCos0);
#if 0       // we can skip clamping for a tiny little bit more performance
            h0 = n + clamp( h0-n, (lpfloat)-XE_GTAO_PI_HALF, (lpfloat)XE_GTAO_PI_HALF );
            h1 = n + clamp( h1-n, (lpfloat)-XE_GTAO_PI_HALF, (lpfloat)XE_GTAO_PI_HALF );
#endif
            lpfloat iarc0 = ((lpfloat)cosNorm + (lpfloat)2 * (lpfloat)h0 * (lpfloat)sin(n)-(lpfloat)cos((lpfloat)2 * (lpfloat)h0-n))/(lpfloat)4;
            lpfloat iarc1 = ((lpfloat)cosNorm + (lpfloat)2 * (lpfloat)h1 * (lpfloat)sin(n)-(lpfloat)cos((lpfloat)2 * (lpfloat)h1-n))/(lpfloat)4;
            lpfloat localVisibility = (lpfloat)projectedNormalVecLength * (lpfloat)(iarc0+iarc1);
            visibility += localVisibility;

#ifdef XE_GTAO_COMPUTE_BENT_NORMALS
            // see "Algorithm 2 Extension that computes bent normals b."
            lpfloat t0 = (6*sin(h0-n)-sin(3*h0-n)+6*sin(h1-n)-sin(3*h1-n)+16*sin(n)-3*(sin(h0+n)+sin(h1+n)))/12;
            lpfloat t1 = (-cos(3 * h0-n)-cos(3 * h1-n) +8 * cos(n)-3 * (cos(h0+n) +cos(h1+n)))/12;
            lpfloat3 localBentNormal = lpfloat3( directionVec.x * (lpfloat)t0, directionVec.y * (lpfloat)t0, -lpfloat(t1) );
            localBentNormal = (lpfloat3)mul( XeGTAO_RotFromToMatrix( lpfloat3(0,0,-1), viewVec ), localBentNormal ) * projectedNormalVecLength;
            bentNormal += localBentNormal;
#endif
        }
        visibility /= (lpfloat)sliceCount;
        visibility = pow( visibility, (lpfloat)consts.FinalValuePower );
        visibility = max( (lpfloat)0.03, visibility ); // disallow total occlusion (which wouldn't make any sense anyhow since pixel is visible but also helps with packing bent normals)

#ifdef XE_GTAO_COMPUTE_BENT_NORMALS
        bentNormal = normalize(bentNormal) ;
#endif
    }

#if defined(XE_GTAO_SHOW_DEBUG_VIZ) && defined(XE_GTAO_COMPUTE_BENT_NORMALS)
    [branch] if (IsUnderCursorRange(pixCoord, int2(1, 1)))
    {
        float3 dbgWorldViewNorm = mul((float3x3)g_globals.ViewInv, viewspaceNormal).xyz;
        float3 dbgWorldBentNorm = mul((float3x3)g_globals.ViewInv, bentNormal).xyz;
        DebugDraw3DSphereCone( dbgWorldPos, dbgWorldViewNorm, 0.3, VA_PI*0.5 - acos(saturate(visibility)), float4( 0.2, 0.2, 0.2, 0.5 ) );
        DebugDraw3DSphereCone( dbgWorldPos, dbgWorldBentNorm, 0.3, VA_PI*0.5 - acos(saturate(visibility)), float4( 0.0, 1.0, 0.0, 0.7 ) );
    }
#endif

    visibility = saturate( visibility / lpfloat(XE_GTAO_OCCLUSION_TERM_SCALE) ); //TODOFT
#ifdef XE_GTAO_COMPUTE_BENT_NORMALS
    bentNormal = bentNormal * 0.5 + 0.5;
#else
    visibility = visibility * 255.0 + 0.5;
#endif
    return float4(bentNormal, 1.0 - visibility);
}