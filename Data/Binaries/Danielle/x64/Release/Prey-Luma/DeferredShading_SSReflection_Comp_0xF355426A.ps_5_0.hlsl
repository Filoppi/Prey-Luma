#include "include/Common.hlsl"

SamplerState ssReflectionLinear : register(s1); // Bilinear sampler with clamp
Texture2D<float4> ssrComposeSpecularTex : register(t0); // G-Buffer
Texture2D<float4> ssrComposeReflection0Tex : register(t1); // 1/2 resolution
Texture2D<float4> ssrComposeReflection1Tex : register(t2); // 1/4 resolution + blurred
Texture2D<float4> ssrComposeReflection2Tex : register(t3); // 1/8 resolution + blurred (again)
Texture2D<float4> ssrComposeReflection3Tex : register(t4); // 1/16 resolution + blurred (again)
Texture2D<float4> ssrComposeReflectionFullTex : register(t5); // LUMA: Full resolution
Texture2D<float> ssrComposeDiffuseTex : register(t6); // LUMA: Diffuseness map

#include "include/CBuffer_PerViewGlobal.hlsl"
#include "include/GBuffer.hlsl"

// Disabled as this is slower, there's no noticeable boost in quality (if not in mirrors like surfaces?) and possibly introduces more shimmering
#define USE_BASE_MIP 0
#define CREATE_NEW_MIP 1

float2 ClampScreenTC(float2 TC, float2 maxTC)
{
	return min(TC, maxTC.xy); // LUMA FT: optmized away the max with 0, it's not needed
}
float2 ClampScreenTC(float2 TC)
{
	return ClampScreenTC(TC, CV_HPosClamp.xy);
}
float2 ClampScreenTC(float2 TC, Texture2D _texture)
{
#if 1
    float2 textureSize;
    _texture.GetDimensions(textureSize.x, textureSize.y);
	return ClampScreenTC(TC, CV_HPosScale.xy - (0.5 / textureSize));
#else // Optimized branch that is likely to make little difference on tiny mips
	return ClampScreenTC(TC, CV_HPosClamp.xy);
#endif
}

// This blends in different downscaled versions of the gbuffer diffuse color buffer,
// to create a screen space reflections map.
// This always draws at half of the rendering resolution (independently from the "r_arkssr" and "r_SSReflHalfRes" cvars).
void main(
  float4 inWPos : SV_Position0,
  float4 inBaseTC : TEXCOORD0, // Already scaled by "CV_HPosScale.xy"
  out float4 outColor : SV_Target0)
{
	// LUMAF FT: none of these textures correctly clamped the UV to the portion of the texture they are render to (e.g. CV_HPosClamp),
	// given that it changes based on the texture size and we can't know what it is without querying it.
	// Generally reflections are upwards, so not much reflects dowards at the bottom right of the screen,
	// and the inconstency could probably hide themseleves under the bluring, but we fixed it anyway.
	float4 refl0 = ssrComposeReflection0Tex.SampleLevel(ssReflectionLinear, ClampScreenTC(inBaseTC.xy, ssrComposeReflection0Tex), 0); //TODO LUMA: do 4 more samples and apply sharpening to this? It might not really work but it could help fix the blurry reflections on mirrors
	float4 refl1 = ssrComposeReflection1Tex.SampleLevel(ssReflectionLinear, ClampScreenTC(inBaseTC.xy, ssrComposeReflection1Tex), 0);
	float4 refl2 = ssrComposeReflection2Tex.SampleLevel(ssReflectionLinear, ClampScreenTC(inBaseTC.xy, ssrComposeReflection2Tex), 0);
	float4 refl3 = ssrComposeReflection3Tex.SampleLevel(ssReflectionLinear, ClampScreenTC(inBaseTC.xy, ssrComposeReflection3Tex), 0); // LUMA FT: this was sampled but never used

#if 1 // LUMA FT: refactor SSR blending by basing it on the diffuseness map we previously created, instead of simply blending on the specular properly (which won't diffuse more based on distance). For now we don't have a user shader define to turn it off, as the vanilla SSR looked bad and nobody should use them.

	const float2 ssrBaseTC = ClampScreenTC(inBaseTC.xy, ssrComposeReflectionFullTex); // Follows the resolution of the "SSR_Raytrace" pass, given it might not be the native output resolution (we can't use "CV_HPosClamp" for it, it's based on the RT)
	
#if USE_BASE_MIP || SSR_QUALITY >= 2 // I had to put these in branches as textures are sampled even if you don't use their result...
	// LUMA FT: the original highest res texture outputted by "SSR_Raytrace" wasn't available here,
	// but that shouldn't have been a problem as we never really need reflections to be too sharp, and this shader always draws at half (of the rendering) resolution (and so might "SSR_Raytrace"), and SSR are error prone so this would reveal more "problems",
	// though we added it to possibly make specular reflections even better!
	float4 reflFull = ssrComposeReflectionFullTex.SampleLevel(ssReflectionLinear, ssrBaseTC, 0); // This one is at full resolution so we can use "baseTC" clamped with "CV_HPosClamp.xy"
#endif // USE_BASE_MIP || SSR_QUALITY >= 2

#if CREATE_NEW_MIP
	// LUMA FT: added a new mip for further diffusion calculated on the spot (it's fast enough that I didn't bother adding a new pass to pre-construct it)
    float2 mip4Size;
    ssrComposeReflection3Tex.GetDimensions(mip4Size.x, mip4Size.y);
	float2 screenSize;
    ssrComposeSpecularTex.GetDimensions(screenSize.x, screenSize.y);
	float blurScale = 3.0; // Hack to blur the texture (neutral/unblurred at 1)
    float2 mip4TexelOffset = (blurScale * ((screenSize.xy * CV_HPosScale.xy) / float2(BaseHorizontalResolution, BaseVerticalResolution))) / mip4Size;
    float4 refl4;
    refl4 = ssrComposeReflection3Tex.SampleLevel(ssReflectionLinear, ClampScreenTC(inBaseTC.xy + float2(-mip4TexelOffset.x, -mip4TexelOffset.y), ssrComposeReflection3Tex), 0);
    refl4 += ssrComposeReflection3Tex.SampleLevel(ssReflectionLinear, ClampScreenTC(inBaseTC.xy + float2(mip4TexelOffset.x, -mip4TexelOffset.y), ssrComposeReflection3Tex), 0);
    refl4 += ssrComposeReflection3Tex.SampleLevel(ssReflectionLinear, ClampScreenTC(inBaseTC.xy + float2(-mip4TexelOffset.x, mip4TexelOffset.y), ssrComposeReflection3Tex), 0);
    refl4 += ssrComposeReflection3Tex.SampleLevel(ssReflectionLinear, ClampScreenTC(inBaseTC.xy + float2(mip4TexelOffset.x, mip4TexelOffset.y), ssrComposeReflection3Tex), 0);
    refl4 *= 0.25;
#endif // CREATE_NEW_MIP

	// By default this uses "1 - (smoothness * smoothness)" as the vanilla diffuseness formula used here, but if we have blurring enabled, the result is modulated,
	// and it can either be skewed towards more smoothness or more diffuseness, so it won't match the vanilla reflections either way, but, generally they'd look better!
	float diffuseness = ssrComposeDiffuseTex.SampleLevel(ssReflectionLinear, ssrBaseTC, 0);

	static const uint baselineTexturesCount = 4;
// Note that any of these will slightly alter the smoothness of reflections, but that's mostly ok
#if USE_BASE_MIP && CREATE_NEW_MIP
	static const uint texturesCount = baselineTexturesCount + 2;
	float4 reflMips[texturesCount] = { reflFull, refl0, refl1, refl2, refl3, refl4 };
#elif USE_BASE_MIP
	static const uint texturesCount = baselineTexturesCount + 1;
	float4 reflMips[texturesCount] = { reflFull, refl0, refl1, refl2, refl3 };
#elif CREATE_NEW_MIP
	static const uint texturesCount = baselineTexturesCount + 1;
	float4 reflMips[texturesCount] = { refl0, refl1, refl2, refl3, refl4 };
#else // Vanilla
	static const uint texturesCount = baselineTexturesCount;
	float4 reflMips[texturesCount] = { refl0, refl1, refl2, refl3 };
#endif

// Optionally slightly modulate the diffuseness based on the amount of textures we have added at the edges.
// Disabled as we have tweaked reflections parameters with these new mips already in the chain, so it wouldn't make sense to adjust it anymore, and it won't look more like vanilla.
#if 0
#if USE_BASE_MIP
	diffuseness = pow(diffuseness, (float)baselineTexturesCount / ((float)baselineTexturesCount + 1.0));
#endif // USE_BASE_MIP
#if CREATE_NEW_MIP
#if 1 // This probably has a better curve
	diffuseness = 1.0 - pow(1.0 - diffuseness, (float)baselineTexturesCount / ((float)baselineTexturesCount + 1.0));
#else
	diffuseness = pow(diffuseness, ((float)baselineTexturesCount + 1.0) / (float)baselineTexturesCount);
#endif
#endif // CREATE_NEW_MIP
#endif

	float alpha = diffuseness;
	float texturesRange = 1.0 / (texturesCount - 1);
	int i = min((texturesCount - 1) * alpha, texturesCount - 2);
	float localAlpha = (alpha - (texturesRange * i)) / texturesRange;
	localAlpha = pow(localAlpha, 1.333); // We apply pow to "localAlpha" (the alpha in between mips) here to make the blurring intensity between mips more perceptually linear, we found the value heuristically (quickly), but anything between 1 and 1.5 looks good
	outColor = lerp( reflMips[i], reflMips[i+1], localAlpha );

	float4 bestReflMip = reflMips[0];
#if SSR_QUALITY >= 2
	bestReflMip = reflFull;
#endif // SSR_QUALITY >= 2

#if 0 // This check is not needed and detrimental, we don't want any steps (hard branch) in reflections
	if (all(bestReflMip == 0.0))
#endif
	{
		// Reduce glow at edges of mips reflections, which makes their edges very noticeable (within the reflections texture, some parts of it actually are made of reflections, and some parts failed to find them and they'd have a rgba value of zero).
		// The higher the value, the more we reduce glow around SSR that are fading out of view (especially when they have a black border), but this will also reduce the blurriness and "visibility" of diffuse reflected objects that don't have what's behind them reflected too (due to it being too far, e.g. a column reflected in the floor in the middle of a large room).
		// At 1, we have no glow at all (which isn't even desirable really).
#if 1
		static const float glowReduction = 0.667;
		outColor.a = min(outColor.a, lerp(outColor.a, bestReflMip.a, glowReduction));
#elif 0
		outColor.a = min(reflMips[i].a, reflMips[i+1].a);
#elif 0
		outColor.a = bestReflMip.a;
#elif 0
		outColor.a = pow(outColor.a, 2.0);
#endif
	}

#if 1
	// Fall back for "rarish" case where all channels (rgb + a) of a mip are zero, which means the SSR had not found any results there, so we don't want to blend our color towards black, we take its gradient, but not its color (this works okish, but it could be better with a separate "this mip texel has a reflection" map, as this has a hard step threshold of zero)
	// See mip map shaders B969DC27 and 8B135192 for more detail.
	if (all(reflMips[i] == 0.0))
	{
		outColor.rgb = reflMips[i+1].rgb;
	}
	else if (all(reflMips[i+1] == 0.0))
	{
		outColor.rgb = reflMips[i].rgb;
	}
#endif

#if 0 // Test sharp output
	outColor = reflMips[0];
#endif
	
#if 0 //TODOFT: test peformance with hard branches, it probably doesn't make much difference... (we need to move the texture samples in the branches)
	[branch] if (alpha < 1.0 / texturesCount)
	{
		outColor = lerp( reflMips[0], reflMips[1], localAlpha ); 
	}
	else [branch] if (alpha < 2.0 / texturesCount)
	{
		outColor = lerp( reflMips[1], reflMips[2], localAlpha ); 
	}
	else [branch] if (alpha < 3.0 / texturesCount)
	{
		outColor = lerp( reflMips[2], reflMips[3], localAlpha ); 
	}
#if USE_BASE_MIP || CREATE_NEW_MIP
	else [branch] if (alpha < 4.0 / texturesCount)
	{
		outColor = lerp( reflMips[2], reflMips[3], localAlpha ); 
	}
	else
	{
		outColor = lerp( reflMips[3], reflMips[4], localAlpha ); 
	}
#else
	else
	{
		outColor = lerp( reflMips[3], reflMips[4], localAlpha ); 
	}
#endif
#endif

#else

	// LUMA FT: the uv doesn't need to be scaled by "CV_HPosScale.xy" here (it already is in the vertex shader)
	// LUMA FT: fixed missing clamp to "CV_HPosClamp.xy" (we can't use the same clamp for all other textures as its value is generated after this RT's value, which is half res)
	const float2 selfBaseTC = ClampScreenTC(inBaseTC.xy, CV_HPosClamp.xy);
	const float2 standardBaseTC = ClampScreenTC(inBaseTC.xy, ssrComposeSpecularTex);
	
	float4 GBufferC = ssrComposeSpecularTex.SampleLevel(ssReflectionLinear, standardBaseTC, 0);
	MaterialAttribsCommon attribs = DecodeGBuffer(0, 0, GBufferC);

	float gloss = sqr(attribs.Smoothness);

	float weight = frac( min( gloss, 0.99999 ) * 3 ); // LUMA FT: this was probably clamped to ~0.9999 to avoid the weight flipping over to 1 in the last iteration
	// LUMA FT: the blending weight between mips didn't really make sense and was not perceptually "linear" so we fixed it (it's flipped compared to the new implementation as we go from more blurry to less blurry here)
	weight = sqrt(weight);

	[branch] if (gloss > 2.0/3.0)
		outColor = lerp( refl1, refl0, weight );
	else if (gloss > 1.0/3.0)
		outColor = lerp( refl2, refl1, weight );
	else
		outColor = lerp( refl3, refl2, weight );
	
#endif
}