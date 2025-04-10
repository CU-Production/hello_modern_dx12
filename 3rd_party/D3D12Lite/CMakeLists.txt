# D3D12MA
add_library(D3D12MemoryAllocator D3D12MemoryAllocator/D3D12MemAlloc.cpp)
target_include_directories(D3D12MemoryAllocator PUBLIC D3D12MemoryAllocator)
target_link_libraries(D3D12MemoryAllocator PRIVATE agilitysdk)

# dxc
add_library(dxc INTERFACE)
target_include_directories(dxc INTERFACE dxc/inc)
target_link_directories(dxc INTERFACE dxc/lib/x64)
target_link_libraries(dxc INTERFACE agilitysdk dxcompiler)
set(DXC_BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dxc/bin/x64 CACHE INTERNAL "")

# DXTex
file(GLOB DXTEX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/DXTex/*.cpp)
add_library(DXTex ${DXTEX_SRC})
target_include_directories(DXTex PUBLIC DXTex)
target_link_libraries(DXTex PRIVATE agilitysdk agilitysdk_d3dx12)

# imgui
file(GLOB IMGUI_SRC ${CMAKE_CURRENT_SOURCE_DIR}/imgui/*.cpp)
add_library(imgui ${IMGUI_SRC})
target_include_directories(imgui PUBLIC imgui)
target_link_libraries(imgui PRIVATE agilitysdk glfw)

# SimpleMath
add_library(SimpleMath INTERFACE)
target_include_directories(SimpleMath INTERFACE SimpleMath)

# D3D12Lite
add_library(D3D12Lite D3D12Lite.cpp)
target_include_directories(D3D12Lite PUBLIC .)
target_link_libraries(D3D12Lite PUBLIC
        agilitysdk
        d3d12
        dxgi
        dxguid
        D3D12MemoryAllocator
        dxc
        DXTex
        imgui
        SimpleMath)