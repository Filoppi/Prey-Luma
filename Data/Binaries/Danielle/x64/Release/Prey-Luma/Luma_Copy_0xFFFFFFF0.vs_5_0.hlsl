float4 main(uint vertexIdx : SV_VertexID0) : SV_Position0
{
	float2 texcoord = float2(vertexIdx & 1, vertexIdx >> 1);
	return float4((texcoord.x - 0.5) * 2, -(texcoord.y - 0.5f) * 2, 0, 1);
}