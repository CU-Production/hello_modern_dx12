#include "Shaders/Common.hlsl"

ConstantBuffer<TriangleConstants> ObjectConstantBuffer : register(b0, perObjectSpace);

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

/*
    With "bindless" vertex buffers, we can bypass all the nonsense of the vertex input layout in the pipeline state object, 
    and never have to manually bind them either. There's just one annoying downside: you lose out on BaseVertexLocation from
    the D3D12 Draw function, it's not incorporated in SV_VertexID and there's no system generated value that provides it. If
    you need it, you have to pass it in as a push constant or part of a constant buffer.
*/
VertexOutput VertexShader(uint vertexId : SV_VertexID)
{
    //Our constant buffer provides us the index into the D3D12 descriptor heap, where we retrieve the vertex buffer we need, as a ByteAddress (aka "raw") buffer
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[ObjectConstantBuffer.vertexBufferIndex];

    //We load the vertex at the byte offset for the current vertex ID
    TriangleVertex vertex = vertexBuffer.Load<TriangleVertex>(vertexId * sizeof(TriangleVertex));
    
    //Pass results to the pixel shader
    VertexOutput output;
    output.position = float4(vertex.position, 0, 1);
    output.color = vertex.color;
    
    return output;
}

float4 PixelShader(VertexOutput input) : SV_TARGET
{
    return float4(input.color, 1);
}