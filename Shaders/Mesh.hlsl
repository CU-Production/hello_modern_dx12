#include "Shaders/Common.hlsl"

ConstantBuffer<MeshConstants> ObjectConstantBuffer : register(b0, perObjectSpace);
ConstantBuffer<MeshPassConstants> PassConstantBuffer : register(b0, perPassSpace);

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : WORLD_POSITION;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
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
    MeshVertex vertex = vertexBuffer.Load<MeshVertex>(vertexId * sizeof(MeshVertex));

    //Apply the world, view, projection matrices to the vertex position
    //Also store the world space position and normal for lighting purposes
    VertexOutput output;
    output.position = mul(ObjectConstantBuffer.worldMatrix, float4(vertex.position, 1));
    output.worldPosition = output.position.xyz;
    output.position = mul(PassConstantBuffer.viewMatrix, output.position);
    output.position = mul(PassConstantBuffer.projectionMatrix, output.position);
    output.uv = vertex.uv;
    output.normal = mul(ObjectConstantBuffer.worldMatrix, float4(vertex.normal, 0)).xyz;

    return output;
}

float4 PixelShader(VertexOutput input) : SV_TARGET
{
    //Similarly to the vertex buffer, we load the texture by indexing into the descriptor heap
    Texture2D<float4> colorTexture = ResourceDescriptorHeap[ObjectConstantBuffer.textureIndex];

    //We also index into the sampler heap to get the texture sampler we want
    SamplerState anisoSampler = SamplerDescriptorHeap[anisoClampSampler];

    //Apply some simple lighting
    float3 color = colorTexture.Sample(anisoSampler, input.uv).rgb;
    float3 lightDirection = normalize(PassConstantBuffer.cameraPosition);
    float3 viewDirection = normalize(PassConstantBuffer.cameraPosition - input.worldPosition);

    float3 halfVector = normalize(viewDirection + lightDirection);
    float specular = pow(saturate(dot(halfVector, input.normal)), 8.0f);
    float diffuse = saturate(dot(normalize(input.normal), lightDirection));

    float3 lighting = color * (diffuse + specular);

    return float4(lighting, 1);
}