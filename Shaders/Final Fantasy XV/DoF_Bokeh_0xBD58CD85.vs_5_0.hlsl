#include "Includes/Common.hlsl"

cbuffer cb0_buf : register(b0)
{
    uint4 cb0_m0 : packoffset(c0);
    uint4 cb0_m1 : packoffset(c1);
    uint4 cb0_m2 : packoffset(c2);
    uint4 cb0_m3 : packoffset(c3);
    uint4 cb0_m4 : packoffset(c4);
    uint2 cb0_m5 : packoffset(c5);
    float2 cb0_m6 : packoffset(c5.z);
    float2 cb0_m7 : packoffset(c6);
    uint2 cb0_m8 : packoffset(c6.z);
};


static float4 gl_Position;
static float4 POSITION;
static float2 TEXCOORD;
static float2 TEXCOORD_1;
static float2 TEXCOORD1;
static float2 TEXCOORD2;

struct SPIRV_Cross_Input
{
    float4 POSITION : TEXCOORD0;
    float2 TEXCOORD : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float2 TEXCOORD_1 : TEXCOORD1;
    float2 TEXCOORD1 : TEXCOORD1;
    float2 TEXCOORD2 : TEXCOORD2;
    precise float4 gl_Position : SV_Position;
};

void vert_main()
{
    gl_Position.x = POSITION.x;
    gl_Position.y = POSITION.y;
    gl_Position.z = POSITION.z;
    gl_Position.w = POSITION.w;
    TEXCOORD_1.x = TEXCOORD.x;
    float resolutionScale = 1.0f;
    if (LumaData.GameData.IsUpscaling != 0)
    {
        resolutionScale = LumaData.RenderResolutionScale.x;
    }

    TEXCOORD_1.y = TEXCOORD.y;
    TEXCOORD1.x = mad(cb0_m6.x * resolutionScale, -0.5f, TEXCOORD.x);
    TEXCOORD1.y = mad(cb0_m6.y * resolutionScale, -0.5f, TEXCOORD.y);
    TEXCOORD2.x = cb0_m7.x * mad(TEXCOORD.x, 2.0f, -1.0f);
    TEXCOORD2.y = cb0_m7.y * mad(TEXCOORD.y, 2.0f, -1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    POSITION = stage_input.POSITION;
    TEXCOORD = stage_input.TEXCOORD;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output.TEXCOORD_1 = TEXCOORD_1;
    stage_output.TEXCOORD1 = TEXCOORD1;
    stage_output.TEXCOORD2 = TEXCOORD2;
    return stage_output;
}