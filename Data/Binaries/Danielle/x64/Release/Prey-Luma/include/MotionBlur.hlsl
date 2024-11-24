// Requires "CBuffer_PerViewGlobal.hlsl"

float2 MapViewportToRaster(float2 normalizedViewportPos)
{
	return normalizedViewportPos * CV_HPosScale.xy;
}

float2 UnpackLengthAndDepth( float2 packedLenDepth, float jittersLength = 0 )
{
	packedLenDepth.x = (packedLenDepth.x * packedLenDepth.x) / 32.0f;
#if 0
	packedLenDepth.x = max(packedLenDepth.x - jittersLength, 0); // LUMA FT: subtracting the length isn't exactly correct, maybe to average the error out we could do 50% of it
#endif
	packedLenDepth.y = packedLenDepth.y * 255.0f;
	return packedLenDepth;
}

float MBSampleWeight( float centerDepth, float sampleDepth, float centerVelLen, float sampleVelLen, float sampleIndex, float lenToSampleIndex )
{
	const float2 depthCompare = saturate( 0.5f + float2(1, -1) * (sampleDepth - centerDepth) );
	const float2 spreadCompare = saturate( 1 + lenToSampleIndex * float2(centerVelLen, sampleVelLen) - sampleIndex );
	return dot( depthCompare.xy, spreadCompare.xy );
}

float2 ClampScreenTC(float2 TC, float2 maxTC)
{
	return clamp(TC, 0, maxTC.xy);
}

float2 ClampScreenTC(float2 TC)
{
	return ClampScreenTC(TC, CV_HPosClamp.xy);
}

float2 EncodeMotionVector(float2 vMotion, bool bFastEncode = false)
{
	if (bFastEncode)
		return vMotion;

	vMotion = sqrt(abs(vMotion))* (vMotion.xy>0.0f ? float2(1, 1) : float2(-1, -1));
	vMotion = vMotion* 0.5h + 127.f/255.f;

	return vMotion.xy;
}

float2 DecodeMotionVector(float2 vMotionEncoded, bool bFastEncoded = false, float2 jitters = 0)
{
	if (bFastEncoded)
		return vMotionEncoded;

	vMotionEncoded.xy = (vMotionEncoded.xy - 127.f/255.f) * 2.0f;
	vMotionEncoded.xy = (vMotionEncoded.xy * vMotionEncoded.xy) * (vMotionEncoded.xy>=0.0f ? float2(1, 1) : float2(-1, -1)); // LUMA FT: corrected ">0"
#if 0 // We can't really do this because the motion blur motion vectors are scaled by the motion blur intensity, frame rate and shutter speed, so we'd need to remove the jitters at encoding time (in "PackVelocitiesPS()")
	vMotionEncoded.xy += jitters; // LUMA FT: added jitters offset (flipped, as motion vectors are "PrevPos-CurrPos")
#endif
	return vMotionEncoded;
}

float GetLinearDepth(float fLinearDepth, bool bScaled = false)
{
    return fLinearDepth * (bScaled ? CV_NearFarClipDist.y : 1.0f); // Note: dividing by CV_NearFarClipDist.w is possibly more correct
}

float GetLinearDepth(Texture2D depthTexture, int3 vPixCoord, bool bScaled = false)
{
	float fDepth = depthTexture.Load(vPixCoord).x;
	return GetLinearDepth(fDepth, bScaled);
}

#define VELOCITY_OBJECTS_FLOAT true

float4 OutputVelocityRT(float2 vCurrPos, float2 vPrevPos)
{
	float2 vVelocity = (vPrevPos - vCurrPos);
	return float4(EncodeMotionVector(vVelocity, VELOCITY_OBJECTS_FLOAT), 0, 0);
}

float2 ReadVelocityObjects(float2 _value)
{
	return DecodeMotionVector(_value, VELOCITY_OBJECTS_FLOAT);
}

float3 ReconstructWorldPos(int2 WPos, float linearDepth, bool bRelativeToCamera = false)
{
	float4 wposScaled = float4(WPos * linearDepth, linearDepth, bRelativeToCamera ? 0.0 : 1.0);
	return mul(CV_ScreenToWorldBasis, wposScaled);
}

float3 ReconstructWorldPos(int2 WPos, Texture2D sceneDepthTex, bool bRelativeToCamera = false)
{
	float linearDepth = sceneDepthTex.Load(int3(WPos, 0)).x;
	return ReconstructWorldPos(WPos, linearDepth, bRelativeToCamera);
}