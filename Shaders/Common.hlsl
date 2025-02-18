#ifndef __COMMON_HLSL__
#define __COMMON_HLSL__

#include "Shared.h"

//In D3D12Lite an HLSL space is equivalent to a descriptor table
//Resources are bound to tables grouped by update frequency, to optimize performance
//Tables are also in order from most frequently to least frequently updated, to optimize performance
#define perObjectSpace   space0
#define perMaterialSpace space1
#define perPassSpace     space2
#define perFrameSpace    space3

//HLSL Shader Model 6.6 gives us direct access to samplers within the sampler descriptor heap.
//There is no longer any reason I'm aware of to bother with binding samplers manually, just make all the ones you want
//in the sampler descriptor heap at startup and make a matching shader index here, job done.
//See Device::CreateSamplers for where these are made.
#define anisoClampSampler  0
#define anisoWrapSampler   1
#define linearClampSampler 2
#define linearWrapSampler  3
#define pointClampSampler  4
#define pointWrapSampler   5

#endif