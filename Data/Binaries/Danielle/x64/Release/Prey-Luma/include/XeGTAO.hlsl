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
// Version history:
// 1.00 (2021-08-09): Initial release
// 1.01 (2021-09-02): Fix for depth going to inf for 'far' depth buffer values that are out of fp16 range
// 1.02 (2021-09-03): More fast_acos use and made final horizon cos clamping optional (off by default): 3-4% perf boost
// 1.10 (2021-09-03): Added a couple of heuristics to combat over-darkening errors in certain scenarios
// 1.20 (2021-09-06): Optional normal from depth generation is now a standalone pass: no longer integrated into 
//                    main XeGTAO pass to reduce complexity and allow reuse; also quality of generated normals improved
// 1.21 (2021-09-28): Replaced 'groupshared'-based denoiser with a slightly slower multi-pass one where a 2-pass new
//                    equals 1-pass old. However, 1-pass new is faster than the 1-pass old and enough when TAA enabled.
// 1.22 (2021-09-28): Added 'XeGTAO_' prefix to all local functions to avoid name clashes with various user codebases.
// 1.30 (2021-10-10): Added support for directional component (bent normals).
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __XE_GTAO_H__
#define __XE_GTAO_H__

// This should be defined if you need bent normals on rgb output
#define XE_GTAO_COMPUTE_BENT_NORMALS
#ifndef XE_GTAO_ENCODE_BENT_NORMALS
#define XE_GTAO_ENCODE_BENT_NORMALS 1
#endif
#ifndef XE_GTAO_ENABLE_DENOISE
#define XE_GTAO_ENABLE_DENOISE 1
#endif
// LUMA FT: added custom code to skip the sky (depth ~1) given that it has invalid (garbage) normal maps (they aren't drawn there). This is optional anyway as no AO is drawn in the sky.
#ifndef XE_GTAO_IGNORE_SKY
#define XE_GTAO_IGNORE_SKY 1
#endif
// LUMA FT: compatibility with downscaled depth texture, where the average is on 3rd channel (z/blue)
#ifndef XE_GTAO_SCALED_DEPTH_FLOAT4
#define XE_GTAO_SCALED_DEPTH_FLOAT4 0
#endif
// LUMA FT: In Prey this is true, though the FP32 depth was sourced from a S24 or some other format like that so maybe it's not right to have it on, but we haven't seen issues from it
#define XE_GTAO_FP32_DEPTHS
// LUMA FT: this doesn't seem to do anything in DX11, also it doesn't work with "XE_GTAO_FP32_DEPTHS"
//#define XE_GTAO_USE_HALF_FLOAT_PRECISION 1
#define XE_GTAO_EXTREME_QUALITY 0

// cpp<->hlsl mapping
#define Matrix4x4       float4x4
#define Vector3         float3
#define Vector2         float2
#define Vector2i        int2
#define Vector2u        uint2

// Global consts that need to be visible from both shader and cpp side
#define XE_GTAO_DEPTH_MIP_LEVELS                    0                   // this one is hard-coded to 5 for now // LUMA FT: changed to 0 as we aren't doing depth mip maps, they were an optimization (at the cost of quality)
#define XE_GTAO_NUMTHREADS_X                        8                   // these can be changed
#define XE_GTAO_NUMTHREADS_Y                        8                   // these can be changed
    
struct GTAOConstants
{
    // LUMA FT: unused
    Vector2u                ViewportSize;
    // LUMA FT: added max to sample clamping
    Vector2u                ScaledViewportMax;
    Vector2                 ViewportPixelSize;                  // .zw == 1.0 / ViewportSize.xy
    // LUMA FT: pre-scaled by render resolution, as optimization
    Vector2                 ScaledViewportPixelSize;
    // LUMA FT: added render scale (e.g. if 0.5, only the top left half of the buffer is used) and UV clamp (for full res and scaled textures)
    Vector2                 RenderResolutionScale;
    Vector2                 SampleUVClamp;                      // 1
    Vector2                 SampleScaledUVClamp;                // 1

#if 1 // LUMA FT: changed to support Prey (make sure to align these by 64 bits (float2))
    float                   DepthFar;
    float                   RadiusScalingMinDepth;
    float                   RadiusScalingMaxDepth;
    float                   RadiusScalingMultiplier;
#else
    Vector2                 DepthUnpackConsts;
#endif
    Vector2                 CameraTanHalfFOV;

    Vector2                 NDCToViewMul;
    Vector2                 NDCToViewAdd;

    Vector2                 NDCToViewMul_x_PixelSize;
    float                   EffectRadius;                       // world (viewspace) maximum size of the shadow
    float                   EffectFalloffRange;

    float                   RadiusMultiplier;
    float                   MinVisibility;                      // e.g. 0 or 0.03...
    float                   FinalValuePower;
    float                   DenoiseBlurBeta;

    float                   SampleDistributionPower;
    float                   ThinOccluderCompensation;
    float                   DepthMIPSamplingOffset;
    int                     NoiseIndex;                         // frameIndex % 64 if using TAA or 0 otherwise
};

#ifndef XE_GTAO_USE_DEFAULT_CONSTANTS
// LUMA FT: changed this to zero, it's not providing any optimization for our implementation
#define XE_GTAO_USE_DEFAULT_CONSTANTS 0
#endif

// some constants reduce performance if provided as dynamic values; if these constants are not required to be dynamic and they match default values, 
// set XE_GTAO_USE_DEFAULT_CONSTANTS and the code will compile into a more efficient shader
#define XE_GTAO_DEFAULT_RADIUS_MULTIPLIER               (1.457f  )  // allows us to use different value as compared to ground truth radius to counter inherent screen space biases
#define XE_GTAO_DEFAULT_FALLOFF_RANGE                   (0.615f  )  // distant samples contribute less
#define XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER       (2.0f    )  // small crevices more important than big surfaces. Set to 2.1 for even better quality, at some performance cost
#define XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION      (0.0f    )  // the new 'thickness heuristic' approach
#define XE_GTAO_DEFAULT_FINAL_VALUE_POWER               (2.2f    )  // modifies the final ambient occlusion value using power function - this allows some of the above heuristics to do different things (not exactly related to display's 2.2 gamma)
#define XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET       (3.30f   )  // main trade-off between performance (memory bandwidth) and quality (temporal stability is the first affected, thin objects next)

// LUMA FT: changed to 1 to be neutral, this only works if we have one filtering pass and the AO pass RT texture is float and not UNORM
#define XE_GTAO_OCCLUSION_TERM_SCALE                    (1.0f)      // for packing in UNORM (because raw, pre-denoised occlusion term can overshoot 1 but will later average out to 1)

// From https://www.shadertoy.com/view/3tB3z3 - except we're using R2 here
#define XE_HILBERT_LEVEL    6U
#define XE_HILBERT_WIDTH    ( (1U << XE_HILBERT_LEVEL) )
#define XE_HILBERT_AREA     ( XE_HILBERT_WIDTH * XE_HILBERT_WIDTH )
inline uint HilbertIndex( uint posX, uint posY )
{
    uint index = 0U;
    for( uint curLevel = XE_HILBERT_WIDTH/2U; curLevel > 0U; curLevel /= 2U )
    {
        uint regionX = ( posX & curLevel ) > 0U;
        uint regionY = ( posY & curLevel ) > 0U;
        index += curLevel * curLevel * ( (3U * regionX) ^ regionY);
        if( regionY == 0U )
        {
            if( regionX == 1U )
            {
                posX = uint( (XE_HILBERT_WIDTH - 1U) ) - posX;
                posY = uint( (XE_HILBERT_WIDTH - 1U) ) - posY;
            }

            uint temp = posX;
            posX = posY;
            posY = temp;
        }
    }
    return index;
}

#define XE_GTAO_PI               	(3.1415926535897932384626433832795)
#define XE_GTAO_PI_HALF             (1.5707963267948966192313216916398)

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

// Engine-specific screen & temporal noise loader
lpfloat2 SpatioTemporalNoise( uint2 pixCoord, uint temporalIndex )    // without TAA, temporalIndex is always 0
{
#if 1   // Hilbert curve driving R2 (see https://www.shadertoy.com/view/3tB3z3)
    uint index = HilbertIndex( pixCoord.x, pixCoord.y );
    index += 288 * (temporalIndex % 64); // why 288? tried out a few and that's the best so far (with XE_HILBERT_LEVEL 6U) - but there's probably better :)
    // R2 sequence - see http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
    return lpfloat2( frac( 0.5 + index * float2(0.75487766624669276005, 0.5698402909980532659114) ) );
#else   // Pseudo-random (fastest but looks bad - not a good choice)
    uint baseHash = Hash32( pixCoord.x + (pixCoord.y << 15) );
    baseHash = Hash32Combine( baseHash, temporalIndex );
    return lpfloat2( Hash32ToFloat( baseHash ), Hash32ToFloat( Hash32( baseHash ) ) );
#endif
}

// Inputs are screen XY and viewspace depth, output is viewspace position
float3 XeGTAO_ComputeViewspacePosition( const float2 screenPos, const float viewspaceDepth, const GTAOConstants consts )
{
    float3 ret;
    ret.xy = (consts.NDCToViewMul * screenPos.xy + consts.NDCToViewAdd) * viewspaceDepth;
    ret.z = viewspaceDepth;
    return ret;
}

// Converts depth from linear (origin<->far 0-1 normalized) or device space (near<->far 0-1 normalized) range to unbound native space (the actual scene physicals distance)
float XeGTAO_ScreenSpaceToViewSpaceDepth( float screenDepth, const GTAOConstants consts )
{
#if 1 // LUMA FT: changed to support Prey
    return screenDepth * consts.DepthFar;
#else
    float depthLinearizeMul = consts.DepthUnpackConsts.x;
    float depthLinearizeAdd = consts.DepthUnpackConsts.y;
    // Optimised version of "-cameraClipNear / (cameraClipFar - (projDepth * (cameraClipFar - cameraClipNear))) * cameraClipFar"
    return depthLinearizeMul / (depthLinearizeAdd - screenDepth);
#endif
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

lpfloat4 XeGTAO_UnpackEdges( lpfloat _packedVal )
{
    uint packedVal = (uint)(_packedVal * 255.5);
    lpfloat4 edgesLRTB;
    edgesLRTB.x = lpfloat((packedVal >> 6) & 0x03) / 3.0;          // there's really no need for mask (as it's an 8 bit input) but I'll leave it in so it doesn't cause any trouble in the future
    edgesLRTB.y = lpfloat((packedVal >> 4) & 0x03) / 3.0;
    edgesLRTB.z = lpfloat((packedVal >> 2) & 0x03) / 3.0;
    edgesLRTB.w = lpfloat((packedVal >> 0) & 0x03) / 3.0;

    return saturate( edgesLRTB );
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

// http://h14s.p5r.org/2012/09/0x5f3759df.html, [Drobot2014a] Low Level Optimizations for GCN, https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf slide 63
lpfloat XeGTAO_FastSqrt( float x )
{
    return (lpfloat)(asfloat( 0x1fbd1df5 + ( asint( x ) >> 1 ) ));
}
// input [-1, 1] and output [0, PI], from https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/
lpfloat XeGTAO_FastACos( lpfloat inX )
{ 
    const lpfloat PI1 = 3.141593;
    const lpfloat HALF_PI = 1.570796;
    lpfloat x = abs(inX); 
    lpfloat res = -0.156583 * x + HALF_PI; 
    res *= XeGTAO_FastSqrt(1.0 - x); 
    return (inX >= 0) ? res : PI1 - res; 
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

float4 XeGTAO_MainPass( float2 pixCoord, lpfloat sliceCount, lpfloat stepsPerSlice, const lpfloat2 localNoise, lpfloat3 viewspaceNormal, const GTAOConstants consts,
    Texture2D<float> sourceViewspaceDepth,
#if XE_GTAO_SCALED_DEPTH_FLOAT4
    Texture2D<float4> sourceViewspaceScaledDepth,
#else
    Texture2D<float> sourceViewspaceScaledDepth,
#endif
    SamplerState depthSampler, out float packedEdges )
{
    float2 normalizedScreenPos = pixCoord * consts.ScaledViewportPixelSize; // 0 Up Left, 1 Bottom Right. Always matches the center of a texel
    pixCoord -= 0.5; // For depth gather/samples (given that gather needs to happen in the center of 4 texels)

    // LUMA FT: these buffers are jittered, and that theoretically helps with AO as it adds temporal information to it.
    // It won't directly react to the jitters in the way TAA reconstruction will expect, but it will still add some temporal information.
    // We couldn't easily de-jitter these as they are sampled weirdly, and also it probably would look worse.
    // 
    // We always use the full resolution depth here as this sample has a higher importance and both GTAO and SSDO natively did it this way
    // TODO: due to dynamic rendering resolution, instead of doing a clamp before the gather (which shifts all 4 samples), we could either do 4 loads and clamp them individually, or do a gather but then set the bottom right samples to the top left values in case they were out of bounds (the same in other code in XeGTAO)
    float4 valuesUL_Raw = sourceViewspaceDepth.GatherRed( depthSampler, float2( min(pixCoord * consts.ViewportPixelSize, consts.SampleUVClamp) ) ); 
    lpfloat4 valuesUL   = valuesUL_Raw * consts.DepthFar; // Up Left
    lpfloat4 valuesBR   = sourceViewspaceDepth.GatherRed( depthSampler, float2( min((pixCoord + 1.0) * consts.ViewportPixelSize, consts.SampleUVClamp) ) ) * consts.DepthFar; // Bottom Right

    // viewspace Z at the center
    // use the "y" from the gather, which matches the texel loaded at the requested coordinates.
    // Equal to "sourceViewspaceDepth.SampleLevel( depthSampler, min(normalizedScreenPos, consts.SampleUVClamp), 0 ).x * consts.DepthFar"
    // Or "sourceViewspaceDepth.Load( int3(pixCoord, 0) ).x * consts.DepthFar"
    lpfloat viewspaceZ  = valuesUL.y;
    
    // viewspace Zs left top right bottom
    const lpfloat pixLZ = valuesUL.x;
    const lpfloat pixTZ = valuesUL.z;
    const lpfloat pixRZ = valuesBR.z;
    const lpfloat pixBZ = valuesBR.x;

    lpfloat4 edgesLRTB  = XeGTAO_CalculateEdges( (lpfloat)viewspaceZ, (lpfloat)pixLZ, (lpfloat)pixRZ, (lpfloat)pixTZ, (lpfloat)pixBZ );
#if XE_GTAO_ENABLE_DENOISE
    packedEdges = XeGTAO_PackEdges(edgesLRTB);
#else
    packedEdges = 0;
#endif

#if XE_GTAO_IGNORE_SKY // Hopefully this optimized performance (we don't need the "[branch]" indicator, it's already made a hard branch by the compiler)
    if (valuesUL_Raw.y >= 0.9999999)
    {
        return 0; // Return zero even if theoretically it's not a valid normal (not normalized)
    }
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

#if XE_GTAO_USE_DEFAULT_CONSTANTS != 0
    const lpfloat effectRadius              = (lpfloat)consts.EffectRadius * (lpfloat)XE_GTAO_DEFAULT_RADIUS_MULTIPLIER;
    const lpfloat sampleDistributionPower   = (lpfloat)XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER;
    const lpfloat thinOccluderCompensation  = (lpfloat)XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION;
    const lpfloat falloffRange              = (lpfloat)XE_GTAO_DEFAULT_FALLOFF_RANGE * effectRadius;
#else
    lpfloat effectRadius              = (lpfloat)consts.EffectRadius * (lpfloat)consts.RadiusMultiplier;
    // LUMA FT: dynamically scale the radius here, to make it "bigger" when it's far, otherwise the falloff below will have a tiny range in the distance (somehow that depends on the depth length?), making AO near invisible
    float radiusIncrease = remap(clamp(viewspaceZ, consts.RadiusScalingMinDepth, consts.RadiusScalingMaxDepth), consts.RadiusScalingMinDepth, consts.RadiusScalingMaxDepth, 1.0, consts.RadiusScalingMultiplier);
    effectRadius *= radiusIncrease;
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

    // see "Algorithm 1" in https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
    {
        const lpfloat noiseSlice  = (lpfloat)localNoise.x;
        const lpfloat noiseSample = (lpfloat)localNoise.y;

        // quality settings / tweaks / hacks
        const lpfloat pixelTooCloseThreshold  = 1.3;      // if the offset is under approx pixel size (pixelTooCloseThreshold), push it out to the minimum distance // LUMA FT: this seems like a sensible default, there's no need to change it

        // approx viewspace pixel size at pixCoord; approximation of NDCToViewspace( normalizedScreenPos.xy + consts.ViewportPixelSize.xy, pixCenterPos.z ).xy - pixCenterPos.xy;
        const float2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ * consts.NDCToViewMul_x_PixelSize;

        // Use the x value of "pixelDirRBViewspaceSizeAtCenterZ", given it's the pixel size scaled by the FOV tan, so unless the FOV was stretched, it'd have the same exact value for x and y (so this supports arbitrary aspect ratios)
        lpfloat screenspaceRadius   = effectRadius / (lpfloat)pixelDirRBViewspaceSizeAtCenterZ.x;

#if 1 // LUMA FT: this can be disabled as it doesn't seem to do much helpful
        // fade out for small screen radii (so far in the distance) (values have been found heuristically)
        visibility += saturate((10.0 - screenspaceRadius)/100.0)*0.5;
#endif

#if 0 // LUMA FT: disabled as Prey is almost always indoor and has not much stuff far (nor very close to the camera), except the sky, which is already earlied out above. Also we don't need the "[branch]" indicator
        // sensible early-out for even more performance. This seems to skip drawing AO in the far distance (not close by as the name seems to imply) 
        if( screenspaceRadius < pixelTooCloseThreshold )
        {
            return float4(viewspaceNormal, 0.0);
        }
#endif

        // this is the min distance to start sampling from to avoid sampling from the center pixel (no useful data obtained from sampling center pixel)
        const lpfloat minS = (lpfloat)pixelTooCloseThreshold / screenspaceRadius;

#if !DEVELOPMENT
        [unroll]
#endif
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

#if !DEVELOPMENT
            [unroll]
#endif
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
                s += minS;

                // approx lines 21-22 from the paper, unrolled
                lpfloat2 sampleOffset = s * omega;

#if 1 // LUMA FT: optimize given that we've disabled this stuff
                const lpfloat mipLevel = 0;
#else
                lpfloat sampleOffsetLength = length( sampleOffset );

                // note: when sampling, using point_point_point or point_point_linear sampler works, but linear_linear_linear will cause unwanted interpolation between neighbouring depth values on the same MIP level!
                const lpfloat mipLevel    = (lpfloat)clamp( log2( sampleOffsetLength ) - consts.DepthMIPSamplingOffset, 0, XE_GTAO_DEPTH_MIP_LEVELS );
#endif

                // Snap to pixel center (more correct direction math, avoids artifacts due to sampling pos not matching depth texel center - messes up slope - but adds other 
                // artifacts due to them being pushed off the slice). Also use full precision for high res cases.
                // LUMA FT: here we snap by the scaled (rendering resolution) pixel offset, which is (e.g.) double as big, but then below the sampling will divide by two again and actually end up on a centered pixel as it intended.
                // LUMA FT: this won't work properly if "XE_GTAO_SCALED_DEPTH_FLOAT4" is true, we'd need to round to a unit of 2 or something, but we don't care enough until proven it's necessary.
                sampleOffset = round(sampleOffset) * (lpfloat2)consts.ScaledViewportPixelSize;

                //TODO LUMA: dynamically pick the higher or lower quality depth based on "mipLevel"? Sampling from the lower res depth currently looks bad and has a lot of artifacts on close flat surfaces (due to low precision), it's an optional lower quality setting to improving it is low priority
                float2 sampleScreenPos0 = normalizedScreenPos + sampleOffset;
#if XE_GTAO_SCALED_DEPTH_FLOAT4
                float  SZ0 = sourceViewspaceScaledDepth.SampleLevel( depthSampler, min(sampleScreenPos0 * consts.RenderResolutionScale, consts.SampleScaledUVClamp), mipLevel ).z * consts.DepthFar;
#else
                float  SZ0 = sourceViewspaceScaledDepth.SampleLevel( depthSampler, min(sampleScreenPos0 * consts.RenderResolutionScale, consts.SampleScaledUVClamp), mipLevel ).x * consts.DepthFar;
#endif
                float3 samplePos0 = XeGTAO_ComputeViewspacePosition( sampleScreenPos0, SZ0, consts );

                float2 sampleScreenPos1 = normalizedScreenPos - sampleOffset;
#if XE_GTAO_SCALED_DEPTH_FLOAT4
                float  SZ1 = sourceViewspaceScaledDepth.SampleLevel( depthSampler, min(sampleScreenPos1 * consts.RenderResolutionScale, consts.SampleScaledUVClamp), mipLevel ).z * consts.DepthFar;
#else
                float  SZ1 = sourceViewspaceScaledDepth.SampleLevel( depthSampler, min(sampleScreenPos1 * consts.RenderResolutionScale, consts.SampleScaledUVClamp), mipLevel ).x * consts.DepthFar;
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
#if XE_GTAO_USE_DEFAULT_CONSTANTS != 0
                bool ignoreThinObjectHeuristic = true;
#else
                bool ignoreThinObjectHeuristic = thinOccluderCompensation == 0.0; // Statically defined in Luma
#endif

                lpfloat weight0;
                lpfloat weight1;
                if (ignoreThinObjectHeuristic)
                {
                    weight0         = saturate( sampleDist0 * falloffMul + falloffAdd );
                    weight1         = saturate( sampleDist1 * falloffMul + falloffAdd );
                }
                else
                {
                    // this is our own thickness heuristic that relies on sooner discarding samples behind the center
                    lpfloat falloffBase0    = length( lpfloat3(sampleDelta0.x, sampleDelta0.y, sampleDelta0.z * (1+thinOccluderCompensation) ) );
                    lpfloat falloffBase1    = length( lpfloat3(sampleDelta1.x, sampleDelta1.y, sampleDelta1.z * (1+thinOccluderCompensation) ) );
                    weight0         = saturate( falloffBase0 * falloffMul + falloffAdd );
                    weight1         = saturate( falloffBase1 * falloffMul + falloffAdd );
                }

                // sample horizon cos
                lpfloat shc0 = (lpfloat)dot(sampleHorizonVec0, viewVec);
                lpfloat shc1 = (lpfloat)dot(sampleHorizonVec1, viewVec);

                // discard unwanted samples
#if !XE_GTAO_EXTREME_QUALITY
                shc0 = lerp( lowHorizonCos0, shc0, weight0 );
                shc1 = lerp( lowHorizonCos1, shc1, weight1 );
#else // this would be more correct but too expensive
                shc0 = cos(lerp( acos(lowHorizonCos0), acos(shc0), weight0 ));
                shc1 = cos(lerp( acos(lowHorizonCos1), acos(shc1), weight1 ));
#endif

#if !XE_GTAO_EXTREME_QUALITY
                ignoreThinObjectHeuristic = true;
#endif
                // thickness heuristic - see "4.3 Implementation details, Height-field assumption considerations"
                if (!ignoreThinObjectHeuristic)
                {
#if 1   // (disabled, not used) this should match the paper
                    lpfloat newhorizonCos0 = max( horizonCos0, shc0 );
                    lpfloat newhorizonCos1 = max( horizonCos1, shc1 );
                    horizonCos0 = (horizonCos0 > shc0) ? lerp( newhorizonCos0, shc0, thinOccluderCompensation ) : newhorizonCos0;
                    horizonCos1 = (horizonCos1 > shc1) ? lerp( newhorizonCos1, shc1, thinOccluderCompensation ) : newhorizonCos1;
#else // (disabled, not used) this is slightly different from the paper but cheaper and provides very similar results
                    horizonCos0 = lerp( max( horizonCos0, shc0 ), shc0, thinOccluderCompensation );
                    horizonCos1 = lerp( max( horizonCos1, shc1 ), shc1, thinOccluderCompensation );
#endif
                }
                // this is a version where thinOccluderCompensation (thickness Heuristic) is completely disabled
                else
                {
                    horizonCos0 = max( horizonCos0, shc0 );
                    horizonCos1 = max( horizonCos1, shc1 );
                }
            }

#if 1       // I can't figure out the slight overdarkening on high slopes, so I'm adding this fudge - in the training set, 0.05 is close (PSNR 21.34) to disabled (PSNR 21.45)
            projectedNormalVecLength = lerp( projectedNormalVecLength, 1, 0.05 );
#endif

            // line ~27, unrolled
            lpfloat h0 = -XeGTAO_FastACos((lpfloat)horizonCos1);
            lpfloat h1 = XeGTAO_FastACos((lpfloat)horizonCos0);
#if XE_GTAO_EXTREME_QUALITY       // we can skip clamping for a tiny little bit more performance
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
        if (consts.MinVisibility != 0) // This is statically fixed in value // LUMA FT: move this before the "FinalValuePower" is applied in, for more consistent results
        {
            visibility = max( (lpfloat)consts.MinVisibility, visibility ); // disallow total occlusion (which wouldn't make any sense anyhow since pixel is visible but also helps with packing bent normals)
        }
        visibility = pow( visibility, (lpfloat)consts.FinalValuePower ); // LUMA FT: power is the best way of scaling AO, a linear multiplication could also work (especially because AO is not applied to full intensity), but it's not as good
        
#ifdef XE_GTAO_COMPUTE_BENT_NORMALS
#if 1 // This is more accurate
        bentNormal = normalize(bentNormal);
#elif 0
        bentNormal /= (lpfloat)sliceCount;
#endif
#endif
    }

#if 0 // "XE_GTAO_OCCLUSION_TERM_SCALE == 1"
    visibility = visibility; // LUMA FT: No need to saturate as we are storing on FP16 textures
#else
    visibility = saturate( visibility / lpfloat(XE_GTAO_OCCLUSION_TERM_SCALE) ); // Saturate as visibility beyond 0-1 (after compression) makes no sense (possibly it could, as the ambient occlusion intensity is not 100%, but...)
#endif
#if XE_GTAO_ENCODE_BENT_NORMALS
    bentNormal = bentNormal * 0.5 + 0.5; // From -1|+1 range to 0|1
#endif
    return float4(bentNormal, 1.0 - visibility); // LUMA FT: flipped visibility as "w" is obscurance in Prey
}

// weighted average depth filter
lpfloat XeGTAO_DepthMIPFilter( lpfloat depth0, lpfloat depth1, lpfloat depth2, lpfloat depth3, const GTAOConstants consts )
{
    lpfloat maxDepth = max( max( depth0, depth1 ), max( depth2, depth3 ) );

    const lpfloat depthRangeScaleFactor = 0.75; // found empirically :)
#if XE_GTAO_USE_DEFAULT_CONSTANTS != 0
    const lpfloat effectRadius              = depthRangeScaleFactor * (lpfloat)consts.EffectRadius * (lpfloat)XE_GTAO_DEFAULT_RADIUS_MULTIPLIER;
    const lpfloat falloffRange              = (lpfloat)XE_GTAO_DEFAULT_FALLOFF_RANGE * effectRadius;
#else
    const lpfloat effectRadius              = depthRangeScaleFactor * (lpfloat)consts.EffectRadius * (lpfloat)consts.RadiusMultiplier;
    const lpfloat falloffRange              = (lpfloat)consts.EffectFalloffRange * effectRadius;
#endif
    const lpfloat falloffFrom       = effectRadius * ((lpfloat)1-(lpfloat)consts.EffectFalloffRange);
    // fadeout precompute optimisation
    const lpfloat falloffMul        = (lpfloat)-1.0 / ( falloffRange );
    const lpfloat falloffAdd        = falloffFrom / ( falloffRange ) + (lpfloat)1.0;

    lpfloat weight0 = saturate( (maxDepth-depth0) * falloffMul + falloffAdd );
    lpfloat weight1 = saturate( (maxDepth-depth1) * falloffMul + falloffAdd );
    lpfloat weight2 = saturate( (maxDepth-depth2) * falloffMul + falloffAdd );
    lpfloat weight3 = saturate( (maxDepth-depth3) * falloffMul + falloffAdd );

    lpfloat weightSum = weight0 + weight1 + weight2 + weight3;
    return (weight0 * depth0 + weight1 * depth1 + weight2 * depth2 + weight3 * depth3) / weightSum;
}

// This is also a good place to do non-linear depth conversion for cases where one wants the 'radius' (effectively the threshold between near-field and far-field GI), 
// is required to be non-linear (i.e. very large outdoors environments).
lpfloat XeGTAO_ClampDepth( float depth )
{
#ifdef XE_GTAO_USE_HALF_FLOAT_PRECISION
    return (lpfloat)clamp( depth, 0.0, 65504.0 );
#else
    return clamp( depth, 0.0, 3.402823466e+38 );
#endif
}

#if 0 // Depth mip generations (it's an optimization with quality downsides, so it's disabled for now)
#pragma warning( disable : 3579 ) // Disable warning of "groupshared" on pixel shaders
groupshared lpfloat g_scratchDepths[16][16];
// Copies the depth into downscaled mips
void XeGTAO_PrefilterDepths16x16( uint2 dispatchThreadID /*: SV_DispatchThreadID*/, uint2 groupThreadID /*: SV_GroupThreadID*/, const GTAOConstants consts, Texture2D<float> sourceNDCDepth, SamplerState depthSampler, RWTexture2D<lpfloat> outDepth0, RWTexture2D<lpfloat> outDepth1, RWTexture2D<lpfloat> outDepth2, RWTexture2D<lpfloat> outDepth3, RWTexture2D<lpfloat> outDepth4, RWTexture2D<lpfloat> outDepth5 )
{
    // MIP 0 (base)
    const uint2 baseCoord = dispatchThreadID;
    const uint2 pixCoord = baseCoord * 2;
    float4 depths4 = sourceNDCDepth.GatherRed( depthSampler, float2( pixCoord * consts.ViewportPixelSize ), int2(1,1) ); // TODO: add "consts.RenderResolutionScale" support
    lpfloat depth0 = XeGTAO_ClampDepth( XeGTAO_ScreenSpaceToViewSpaceDepth( depths4.w, consts ) );
    lpfloat depth1 = XeGTAO_ClampDepth( XeGTAO_ScreenSpaceToViewSpaceDepth( depths4.z, consts ) );
    lpfloat depth2 = XeGTAO_ClampDepth( XeGTAO_ScreenSpaceToViewSpaceDepth( depths4.x, consts ) );
    lpfloat depth3 = XeGTAO_ClampDepth( XeGTAO_ScreenSpaceToViewSpaceDepth( depths4.y, consts ) );
    outDepth0[ pixCoord + uint2(0, 0) ] = (lpfloat)depth0;
    outDepth0[ pixCoord + uint2(1, 0) ] = (lpfloat)depth1;
    outDepth0[ pixCoord + uint2(0, 1) ] = (lpfloat)depth2;
    outDepth0[ pixCoord + uint2(1, 1) ] = (lpfloat)depth3;

    // MIP 1
    lpfloat dm1 = XeGTAO_DepthMIPFilter( depth0, depth1, depth2, depth3, consts );
    outDepth1[ baseCoord ] = (lpfloat)dm1;
    g_scratchDepths[ groupThreadID.x ][ groupThreadID.y ] = dm1;

    GroupMemoryBarrierWithGroupSync( );

    // MIP 2
    [branch]
    if( all( ( groupThreadID.xy % uint2(2, 2) ) == 0 ) )
    {
        lpfloat inTL = g_scratchDepths[groupThreadID.x+0][groupThreadID.y+0];
        lpfloat inTR = g_scratchDepths[groupThreadID.x+1][groupThreadID.y+0];
        lpfloat inBL = g_scratchDepths[groupThreadID.x+0][groupThreadID.y+1];
        lpfloat inBR = g_scratchDepths[groupThreadID.x+1][groupThreadID.y+1];

        lpfloat dm2 = XeGTAO_DepthMIPFilter( inTL, inTR, inBL, inBR, consts );
        outDepth2[ baseCoord / 2 ] = (lpfloat)dm2;
        g_scratchDepths[ groupThreadID.x ][ groupThreadID.y ] = dm2;
    }

    GroupMemoryBarrierWithGroupSync( );

    // MIP 3
    [branch]
    if( all( ( groupThreadID.xy % uint2(4, 4) ) == 0 ) )
    {
        lpfloat inTL = g_scratchDepths[groupThreadID.x+0][groupThreadID.y+0];
        lpfloat inTR = g_scratchDepths[groupThreadID.x+2][groupThreadID.y+0];
        lpfloat inBL = g_scratchDepths[groupThreadID.x+0][groupThreadID.y+2];
        lpfloat inBR = g_scratchDepths[groupThreadID.x+2][groupThreadID.y+2];

        lpfloat dm3 = XeGTAO_DepthMIPFilter( inTL, inTR, inBL, inBR, consts );
        outDepth3[ baseCoord / 4 ] = (lpfloat)dm3;
        g_scratchDepths[ groupThreadID.x ][ groupThreadID.y ] = dm3;
    }

    GroupMemoryBarrierWithGroupSync( );

    // MIP 4
    [branch]
    if( all( ( groupThreadID.xy % uint2(8, 8) ) == 0 ) )
    {
        lpfloat inTL = g_scratchDepths[groupThreadID.x+0][groupThreadID.y+0];
        lpfloat inTR = g_scratchDepths[groupThreadID.x+4][groupThreadID.y+0];
        lpfloat inBL = g_scratchDepths[groupThreadID.x+0][groupThreadID.y+4];
        lpfloat inBR = g_scratchDepths[groupThreadID.x+4][groupThreadID.y+4];

        lpfloat dm4 = XeGTAO_DepthMIPFilter( inTL, inTR, inBL, inBR, consts );
        outDepth4[ baseCoord / 8 ] = (lpfloat)dm4;
        g_scratchDepths[ groupThreadID.x ][ groupThreadID.y ] = dm4;
    }

    GroupMemoryBarrierWithGroupSync( );
    
    // MIP 5
    [branch] // TODO: added here, incomplete
    if( all( ( groupThreadID.xy % uint2(16, 16) ) == 0 ) )
    {
        lpfloat inTL = g_scratchDepths[groupThreadID.x+0][groupThreadID.y+0];
        lpfloat inTR = g_scratchDepths[groupThreadID.x+8][groupThreadID.y+0];
        lpfloat inBL = g_scratchDepths[groupThreadID.x+0][groupThreadID.y+8];
        lpfloat inBR = g_scratchDepths[groupThreadID.x+8][groupThreadID.y+8];

        lpfloat dm5 = XeGTAO_DepthMIPFilter( inTL, inTR, inBL, inBR, consts );
        outDepth5[ baseCoord / 16 ] = (lpfloat)dm5;
        //g_scratchDepths[ groupThreadID.x ][ groupThreadID.y ] = dm5;
    }
}

// input output textures for the first pass (XeGTAO_PrefilterDepths16x16)
Texture2D<float>            g_srcRawDepth           : register( t0 );   // source depth buffer data (in NDC space in DirectX)
RWTexture2D<lpfloat>        g_outWorkingDepthMIP0   : register( u0 );   // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
RWTexture2D<lpfloat>        g_outWorkingDepthMIP1   : register( u1 );   // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
RWTexture2D<lpfloat>        g_outWorkingDepthMIP2   : register( u2 );   // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
RWTexture2D<lpfloat>        g_outWorkingDepthMIP3   : register( u3 );   // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
RWTexture2D<lpfloat>        g_outWorkingDepthMIP4   : register( u4 );   // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
RWTexture2D<lpfloat>        g_outWorkingDepthMIP5   : register( u5 );   // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)

// g_samplerPointClamp is a sampler with D3D12_FILTER_MIN_MAG_MIP_POINT filter and D3D12_TEXTURE_ADDRESS_MODE_CLAMP addressing mode
SamplerState                    g_samplerPointClamp     : register( s0 );

// Engine-specific entry point for the first pass
[numthreads(8, 8, 1)]   // <- hard coded to 8x8; each thread computes 2x2 blocks so processing 16x16 block: Dispatch needs to be called with (width + 16-1) / 16, (height + 16-1) / 16
void CSPrefilterDepths16x16( uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID )
{
    GTAOConstants g_GTAOConsts;
    XeGTAO_PrefilterDepths16x16( dispatchThreadID, groupThreadID, g_GTAOConsts, g_srcRawDepth, g_samplerPointClamp, g_outWorkingDepthMIP0, g_outWorkingDepthMIP1, g_outWorkingDepthMIP2, g_outWorkingDepthMIP3, g_outWorkingDepthMIP4, g_outWorkingDepthMIP5 );
}
#pragma warning( default : 3579 )
#endif

#ifdef XE_GTAO_COMPUTE_BENT_NORMALS
typedef lpfloat4 AOTermType;            // .xyz is bent normal, .w is visibility term
#else
typedef lpfloat AOTermType;             // .x is visibility term
#endif

void XeGTAO_AddSample( AOTermType ssaoValue, lpfloat edgeValue, inout AOTermType sum, inout lpfloat sumWeight )
{
    lpfloat weight = edgeValue;    

    sum += (weight * ssaoValue);
    sumWeight += weight;
}

void XeGTAO_DecodeVisibilityBentNormal( const float4 packedValue, out lpfloat visibility, out lpfloat3 bentNormal )
{
    lpfloat4 decoded = packedValue;
    bentNormal = decoded.xyz * 2.0 - 1.0;   // could normalize - don't want to since it's done so many times, better to do it at the final step only
    visibility = 1.f - decoded.w;
}

void XeGTAO_DecodeGatherPartial( Texture2D<float4> sourceAOTerm, const uint2 pixCoord, const uint2 maxPixCoord, out AOTermType outDecoded[4] )
{
    // LUMA FT: Follow the "Gather" sample functions order.
#ifdef XE_GTAO_COMPUTE_BENT_NORMALS
    XeGTAO_DecodeVisibilityBentNormal( sourceAOTerm.Load(int3(min(pixCoord - uint2(1, 0), maxPixCoord), 0)), outDecoded[0].w, outDecoded[0].xyz );
    XeGTAO_DecodeVisibilityBentNormal( sourceAOTerm.Load(int3(min(pixCoord - uint2(0, 0), maxPixCoord), 0)), outDecoded[1].w, outDecoded[1].xyz );
    XeGTAO_DecodeVisibilityBentNormal( sourceAOTerm.Load(int3(min(pixCoord - uint2(0, 1), maxPixCoord), 0)), outDecoded[2].w, outDecoded[2].xyz );
    XeGTAO_DecodeVisibilityBentNormal( sourceAOTerm.Load(int3(min(pixCoord - uint2(1, 1), maxPixCoord), 0)), outDecoded[3].w, outDecoded[3].xyz );
#else
    outDecoded[0] = lpfloat(sourceAOTerm.Load(int3(pixCoord - uint2(1, 0), 0)).r);
    outDecoded[1] = lpfloat(sourceAOTerm.Load(int3(pixCoord - uint2(0, 0), 0)).r);
    outDecoded[2] = lpfloat(sourceAOTerm.Load(int3(pixCoord - uint2(0, 1), 0)).r);
    outDecoded[3] = lpfloat(sourceAOTerm.Load(int3(pixCoord - uint2(1, 1), 0)).r);
#endif
}

float4 XeGTAO_Denoise( const uint2 pixCoord, const GTAOConstants consts, Texture2D<float4> sourceAOTerm, Texture2D<float> sourceEdges, SamplerState texSampler, const bool finalApply = true )
{
    // LUMA FT: added some code to allow calling this as a per pixel pixel shader, instead of the original which was a compute shader run every 2 horizontal pixels (as optimization)
    const bool odd = pixCoord.x % 2 != 0;
    const uint2 pixCoordBase = uint2(odd ? (pixCoord.x - 1) : pixCoord.x, pixCoord.y);
    
    const lpfloat blurAmount = (finalApply)?((lpfloat)consts.DenoiseBlurBeta):((lpfloat)consts.DenoiseBlurBeta/(lpfloat)5.0);
    const lpfloat diagWeight = 0.85 * 0.5; //TODOFT: magic numbers?

    // gather edge and visibility quads, used later
    const float2 gatherCenter1 = min(float2( pixCoordBase.x + 0, pixCoordBase.y + 0 ) * consts.ViewportPixelSize, consts.SampleUVClamp);
    const float2 gatherCenter2 = min(float2( pixCoordBase.x + 2, pixCoordBase.y + 0 ) * consts.ViewportPixelSize, consts.SampleUVClamp);
    const float2 gatherCenter3 = min(float2( pixCoordBase.x + 1, pixCoordBase.y + 2 ) * consts.ViewportPixelSize, consts.SampleUVClamp);
    lpfloat4 edgesQ0        = sourceEdges.GatherRed( texSampler, gatherCenter1 );
    lpfloat4 edgesQ1        = sourceEdges.GatherRed( texSampler, gatherCenter2 );
    lpfloat4 edgesQ2        = sourceEdges.GatherRed( texSampler, gatherCenter3 );
    
    //TODOFT: only sample the ones we actually need based on "odd".
    AOTermType visQ0[4];    XeGTAO_DecodeGatherPartial( sourceAOTerm, pixCoordBase /*+ uint2( 0, 0 )*/, consts.ScaledViewportMax, visQ0 );
    AOTermType visQ1[4];    XeGTAO_DecodeGatherPartial( sourceAOTerm, pixCoordBase + uint2( 2, 0 ), consts.ScaledViewportMax, visQ1 );
    AOTermType visQ2[4];    XeGTAO_DecodeGatherPartial( sourceAOTerm, pixCoordBase + uint2( 0, 2 ), consts.ScaledViewportMax, visQ2 );
    AOTermType visQ3[4];    XeGTAO_DecodeGatherPartial( sourceAOTerm, pixCoordBase + uint2( 2, 2 ), consts.ScaledViewportMax, visQ3 );

    AOTermType aoTerm;
    lpfloat4 edgesC_LRTB;
    lpfloat weightTL;
    lpfloat weightTR;
    lpfloat weightBL;
    lpfloat weightBR;

    int side = odd ? 1 : 0;

    lpfloat4 edgesL_LRTB  = XeGTAO_UnpackEdges( (side==0)?(edgesQ0.x):(edgesQ0.y) );
    lpfloat4 edgesT_LRTB  = XeGTAO_UnpackEdges( (side==0)?(edgesQ0.z):(edgesQ1.w) );
    lpfloat4 edgesR_LRTB  = XeGTAO_UnpackEdges( (side==0)?(edgesQ1.x):(edgesQ1.y) );
    lpfloat4 edgesB_LRTB  = XeGTAO_UnpackEdges( (side==0)?(edgesQ2.w):(edgesQ2.z) );

    edgesC_LRTB     = XeGTAO_UnpackEdges( (side==0)?(edgesQ0.y):(edgesQ1.x) );

    // Edges aren't perfectly symmetrical: edge detection algorithm does not guarantee that a left edge on the right pixel will match the right edge on the left pixel (although
    // they will match in majority of cases). This line further enforces the symmetricity, creating a slightly sharper blur. Works real nice with TAA.
    edgesC_LRTB *= lpfloat4( edgesL_LRTB.y, edgesR_LRTB.x, edgesT_LRTB.w, edgesB_LRTB.z );

#if 1   // this allows some small amount of AO leaking from neighbours if there are 3 or 4 edges; this reduces both spatial and temporal aliasing
    const lpfloat leak_threshold = 2.5; const lpfloat leak_strength = 0.5;
    lpfloat edginess = (saturate(4.0 - leak_threshold - dot( edgesC_LRTB, float4(1, 1, 1, 1) )) / (4-leak_threshold)) * leak_strength;
    edgesC_LRTB = saturate( edgesC_LRTB + edginess );
#endif

    // for diagonals; used by first and second pass
    weightTL = diagWeight * (edgesC_LRTB.x * edgesL_LRTB.z + edgesC_LRTB.z * edgesT_LRTB.x);
    weightTR = diagWeight * (edgesC_LRTB.z * edgesT_LRTB.y + edgesC_LRTB.y * edgesR_LRTB.z);
    weightBL = diagWeight * (edgesC_LRTB.w * edgesB_LRTB.x + edgesC_LRTB.x * edgesL_LRTB.w);
    weightBR = diagWeight * (edgesC_LRTB.y * edgesR_LRTB.w + edgesC_LRTB.w * edgesB_LRTB.y);

    // first pass
    AOTermType ssaoValue     = (side==0)?(visQ0[1]):(visQ1[0]);
    AOTermType ssaoValueL    = (side==0)?(visQ0[0]):(visQ0[1]);
    AOTermType ssaoValueT    = (side==0)?(visQ0[2]):(visQ1[3]);
    AOTermType ssaoValueR    = (side==0)?(visQ1[0]):(visQ1[1]);
    AOTermType ssaoValueB    = (side==0)?(visQ2[2]):(visQ3[3]);
    AOTermType ssaoValueTL   = (side==0)?(visQ0[3]):(visQ0[2]);
    AOTermType ssaoValueBR   = (side==0)?(visQ3[3]):(visQ3[2]);
    AOTermType ssaoValueTR   = (side==0)?(visQ1[3]):(visQ1[2]);
    AOTermType ssaoValueBL   = (side==0)?(visQ2[3]):(visQ2[2]);

    lpfloat sumWeight = blurAmount;
    AOTermType sum = ssaoValue * sumWeight;

    XeGTAO_AddSample( ssaoValueL, edgesC_LRTB.x, sum, sumWeight );
    XeGTAO_AddSample( ssaoValueR, edgesC_LRTB.y, sum, sumWeight );
    XeGTAO_AddSample( ssaoValueT, edgesC_LRTB.z, sum, sumWeight );
    XeGTAO_AddSample( ssaoValueB, edgesC_LRTB.w, sum, sumWeight );

    XeGTAO_AddSample( ssaoValueTL, weightTL, sum, sumWeight );
    XeGTAO_AddSample( ssaoValueTR, weightTR, sum, sumWeight );
    XeGTAO_AddSample( ssaoValueBL, weightBL, sum, sumWeight );
    XeGTAO_AddSample( ssaoValueBR, weightBR, sum, sumWeight );

    aoTerm = sum / sumWeight;

#ifdef XE_GTAO_COMPUTE_BENT_NORMALS
    lpfloat     visibility = aoTerm.w * (finalApply ? (lpfloat)XE_GTAO_OCCLUSION_TERM_SCALE : 1.0f);
    // LUMA FT: corrected edge case where the denoised average was all zeroes (see "XE_GTAO_IGNORE_SKY"), though to be even better, we should avoid normalizing when the length is too small, though this is likely all unnecessary
    lpfloat3    bentNormal = all(aoTerm.xyz == 0) ? 0 : normalize(aoTerm.xyz);
#if XE_GTAO_ENCODE_BENT_NORMALS
    return float4( bentNormal * 0.5 + 0.5, 1.f - visibility );
#else // !XE_GTAO_ENCODE_BENT_NORMALS
    return float4( bentNormal, 1.f - visibility );
#endif // XE_GTAO_ENCODE_BENT_NORMALS
#else // !XE_GTAO_COMPUTE_BENT_NORMALS
    aoTerm *= finalApply ? (lpfloat)XE_GTAO_OCCLUSION_TERM_SCALE : 1.0f;
    return aoTerm;
#endif // XE_GTAO_COMPUTE_BENT_NORMALS
}

#endif // __XE_GTAO_H__