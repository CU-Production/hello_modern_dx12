# Agility SDK
include(FetchContent)
set(AGILITYSDK_VERSION_MAJOR "615" CACHE INTERNAL "")
set(AGILITYSDK_VERSION_MINOR "1"   CACHE INTERNAL "")
set(AGILITYSDK_NUGET_URL "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.${AGILITYSDK_VERSION_MAJOR}.${AGILITYSDK_VERSION_MINOR}")

FetchContent_Declare(agilitysdk_fetch URL ${AGILITYSDK_NUGET_URL})
FetchContent_MakeAvailable(agilitysdk_fetch)

add_library(agilitysdk INTERFACE)
target_include_directories(agilitysdk INTERFACE "${agilitysdk_fetch_SOURCE_DIR}/build/native/include/")

set(AGILITYSDK_D3D12CORE_DLL      "${agilitysdk_fetch_SOURCE_DIR}/build/native/bin/x64/D3D12Core.dll"      CACHE INTERNAL "")
set(AGILITYSDK_D3D12SDKLAYERS_DLL "${agilitysdk_fetch_SOURCE_DIR}/build/native/bin/x64/d3d12SDKLayers.dll" CACHE INTERNAL "")

set(AGILITYSDK_DIR ${agilitysdk_fetch_SOURCE_DIR} CACHE INTERNAL "")

add_library(agilitysdk_d3dx12 STATIC)
target_link_libraries(agilitysdk_d3dx12 PRIVATE agilitysdk)
target_include_directories(agilitysdk_d3dx12 PRIVATE "${agilitysdk_fetch_SOURCE_DIR}/build/native/include/d3dx12/")
target_sources(agilitysdk_d3dx12 PRIVATE "${agilitysdk_fetch_SOURCE_DIR}/build/native/src/d3dx12/d3dx12_property_format_table.cpp"
        "${agilitysdk_fetch_SOURCE_DIR}/build/native/include/d3dx12/d3dx12.h"
        "${agilitysdk_fetch_SOURCE_DIR}/build/native/include/d3dx12/d3dx12_barriers.h"
        "${agilitysdk_fetch_SOURCE_DIR}/build/native/include/d3dx12/d3dx12_check_feature_support.h"
        "${agilitysdk_fetch_SOURCE_DIR}/build/native/include/d3dx12/d3dx12_core.h"
        "${agilitysdk_fetch_SOURCE_DIR}/build/native/include/d3dx12/d3dx12_default.h"
        "${agilitysdk_fetch_SOURCE_DIR}/build/native/include/d3dx12/d3dx12_pipeline_state_stream.h"
        "${agilitysdk_fetch_SOURCE_DIR}/build/native/include/d3dx12/d3dx12_property_format_table.h"
        "${agilitysdk_fetch_SOURCE_DIR}/build/native/include/d3dx12/d3dx12_render_pass.h"
        "${agilitysdk_fetch_SOURCE_DIR}/build/native/include/d3dx12/d3dx12_resource_helpers.h"
        "${agilitysdk_fetch_SOURCE_DIR}/build/native/include/d3dx12/d3dx12_root_signature.h"
        "${agilitysdk_fetch_SOURCE_DIR}/build/native/include/d3dx12/d3dx12_state_object.h" )

# D3D12Lite
add_subdirectory(D3D12Lite)

# GLFW
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "shared")
set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "glfw examples")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "glfw tests")
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "glfw docs")
set(GLFW_INSTALL OFF CACHE INTERNAL "glfw install")
set(GLFW_VULKAN_STATIC OFF CACHE INTERNAL "glfw vulkan")
add_subdirectory(glfw)