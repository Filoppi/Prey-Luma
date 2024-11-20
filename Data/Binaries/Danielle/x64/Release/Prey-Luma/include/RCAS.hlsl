// RCAS (Robust Contrast-Adaptive Sharpening) 
// https://github.com/GPUOpen-Effects/FidelityFX-FSR2?tab=readme-ov-file#robust-contrast-adaptive-sharpening-rcas
// 
// Our implementation is based on: "https://github.com/cdozdil/OptiScaler/blob/master/OptiScaler/shaders/rcas/precompile/rcas.hlsl",
// which is in turn based on: "https://github.com/RdenBlaauwen/RCAS-for-ReShade",
// which is in turn based on AMD FSR 1 RCAS: https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h

// Try to detect noise or grain and not over sharpen it.
#ifndef RCAS_DENOISE
#define RCAS_DENOISE 0
#endif
// Lower means less artifacts and "less sharpening"
#ifndef RCAS_LIMIT
#define RCAS_LIMIT 0.1875
#endif

Texture2D<float4> Source : register(t0);
Texture2D<float2> Motion : register(t1);

float getRCASLuma(float3 rgb)
{
#if 0 // LUMA FT: changed to use the proper Rec.709 luminance formula (but multiplied by 2 as the sum of the original RCAS formula is 2)
    return GetLuminance(rgb) * 2;
#else
    return dot(rgb, float3(0.5, 1.0, 0.5));
#endif
}

// Pass in a linear (or perceptual space color).
// The color range is roughly expected to be within the SDR 0-1 range, if not, pass in a "paperWhite" scale (which matches the "peak" of the range), that will be used as normalization.
// It's possible to pass in motion vectors to do additional sharpening based on movement.
// Sharpness is meant to be between 0 and 1.
float4 RCAS(uint3 pixelCoord, float sharpness, Texture2D<float4> linearColorTexture, Texture2D<float2> motionVectorsTexture, float paperWhite = 1.0, bool specifyLinearColor = false, float4 linearColor = 0, bool dynamicSharpening = false)
{
    float originalSharpness = sharpness;

    if (dynamicSharpening) //TODO: finish this stuff and the debug view below
    {
        static const float MotionSharpness = 1;
        static const float Threshold = 1;
        static const float ScaleLimit = 1;
        
        float2 mv = motionVectorsTexture.Load(int3(pixelCoord.x, pixelCoord.y, 0)).rg;
        float motion = max(abs(mv.r), abs(mv.g));
        float add = 0.0f;

        if (motion > Threshold)
            add = (motion / (ScaleLimit - Threshold)) * MotionSharpness;
    
        if ((add > MotionSharpness && MotionSharpness > 0.0f) || (add < MotionSharpness && MotionSharpness < 0.0f))
            add = MotionSharpness;
    
        sharpness += add;
    }
    sharpness = saturate(sharpness);

    float4 e4 = specifyLinearColor ? linearColor : linearColorTexture.Load(int3(pixelCoord.x, pixelCoord.y, 0)).rgba;

    // Optional optimization: skip sharpening if it's zero
    if (sharpness == 0.0f)
        return e4;

    float3 e = e4.rgb / paperWhite;
    // RCAS is always "pixel based" (the next 4 pixels)
    float3 b = linearColorTexture.Load(int3(pixelCoord.x, pixelCoord.y - 1, 0)).rgb / paperWhite;
    float3 d = linearColorTexture.Load(int3(pixelCoord.x - 1, pixelCoord.y, 0)).rgb / paperWhite;
    float3 f = linearColorTexture.Load(int3(pixelCoord.x + 1, pixelCoord.y, 0)).rgb / paperWhite;
    float3 h = linearColorTexture.Load(int3(pixelCoord.x, pixelCoord.y + 1, 0)).rgb / paperWhite;

#if RCAS_DENOISE >= 1
    // Get lumas times 2. Should use luma weights that are twice as large as normal.
    float bL = getRCASLuma(b);
    float dL = getRCASLuma(d);
    float eL = getRCASLuma(e);
    float fL = getRCASLuma(f);
    float hL = getRCASLuma(h);

    // denoise
    float nz = (bL + dL + fL + hL) * 0.25 - eL;
    float range = max(max(max(bL, dL), max(hL, fL)), eL) - min(min(min(bL, dL), min(eL, fL)), hL);
    nz = saturate(abs(nz) * rcp(range));
    nz = -0.5 * nz + 1.0;
#endif

    // Min and max of ring.
    float3 minRGB = min(min(b, d), min(f, h));
    float3 maxRGB = max(max(b, d), max(f, h));

#if 0 //TODOFT4: make sure it's ok if the textures aren't in the 0-1 range, clamp some stuff otherwise. Is the "paperWhite" range useless? It's all relative to the min and max of the local pixels
    minRGB = saturate(minRGB);
    maxRGB = saturate(maxRGB);
#endif

    static const float samplesNum = 4.0; // There's 4 (5) colors to be mixed
    // Immediate constants for peak range.
    float2 peakC = float2(1.0, -samplesNum);

    // Limiters, these need to use high precision reciprocal operations.
    // Decided to use standard rcp for now in hopes of optimizing it
    float3 hitMin = minRGB * rcp(samplesNum * maxRGB);
    float3 hitMax = (peakC.xxx - maxRGB) * rcp(samplesNum * minRGB + peakC.yyy);
    float3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-RCAS_LIMIT, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * sharpness;
    
#if RCAS_DENOISE >= 1
    // denoise
    lobe *= nz;
#endif

    // Resolve, which needs medium precision rcp approximation to avoid visible tonality changes.
    float rcpL = rcp(samplesNum * lobe + 1.0);
    float3 output = ((b + d + f + h) * lobe + e) * rcpL;

#if 0 // Debug
    if (dynamicSharpening)
    {
        if (originalSharpness < sharpness)
            output.r *= 1 + (12.0 * (sharpness - originalSharpness));
        else
            output.g *= 1 + (12.0 * (originalSharpness - sharpness));
    }
#endif
  
    return float4(output * paperWhite, e4.a);
}