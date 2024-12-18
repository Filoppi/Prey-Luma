// Default DirectX 11 Geometry Shader (HLSL)

// Input structure for geometry shader, matching the output of the vertex shader
struct VS_OUTPUT {
    float4 Position : SV_POSITION; // Vertex position
    float4 Color    : COLOR;       // Vertex color
};

// Output structure for geometry shader, matching the input of the pixel shader
struct GS_OUTPUT {
    float4 Position : SV_POSITION; // Transformed vertex position
    float4 Color    : COLOR;       // Vertex color
};

// Geometry shader main function
[maxvertexcount(3)] // Specifies the maximum number of vertices this shader emits
void main(triangle VS_OUTPUT input[3], inout TriangleStream<GS_OUTPUT> outputStream) {
    // Iterate through each vertex of the input triangle
    for (int i = 0; i < 3; ++i) {
        GS_OUTPUT output;
        output.Position = input[i].Position; // Pass through position
        output.Color = input[i].Color;       // Pass through color
        outputStream.Append(output);        // Emit the vertex to the output stream
    }

    // Indicate the end of the current primitive
    outputStream.RestartStrip();
}