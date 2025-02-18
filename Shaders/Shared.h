#ifndef __SHARED_HLSL__
#define __SHARED_HLSL__

#ifndef __cplusplus
//This is a subset of HLSL types mapped to corresponding SimpleMath types
#define uint32_t uint
#define int32_t  int
#define Vector2  float2
#define Vector3  float3
#define Vector4  float4
#define Matrix   float4x4
#endif

struct TriangleVertex
{
    Vector2 position;
    Vector3 color; //You can/should pack a color in a uint32_t and unpack it in the shader, this is a Vector3 for simplicity
};

struct TriangleConstants
{
    uint32_t vertexBufferIndex;
};

struct MeshVertex
{
    Vector3 position;
    Vector2 uv;
    Vector3 normal;
};

struct MeshConstants
{
    Matrix worldMatrix;
    uint32_t vertexBufferIndex;
    uint32_t textureIndex;
};

struct MeshPassConstants
{
    Matrix viewMatrix;
    Matrix projectionMatrix;
    Vector3 cameraPosition;
};

#endif