cmake_minimum_required(VERSION 3.28)
project(hello_modern_dx12)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(3rd_party)

add_executable(hello_modern_dx12 main.cpp)
target_precompile_headers(hello_modern_dx12 PRIVATE stdafx.h)
target_link_libraries(hello_modern_dx12 PRIVATE glfw D3D12Lite)
add_custom_command(TARGET hello_modern_dx12 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory    "$<TARGET_FILE_DIR:hello_modern_dx12>/D3D12/"
        COMMAND ${CMAKE_COMMAND} -E copy           -t "$<TARGET_FILE_DIR:hello_modern_dx12>/D3D12/" ${AGILITYSDK_D3D12CORE_DLL}
        COMMAND ${CMAKE_COMMAND} -E copy           -t "$<TARGET_FILE_DIR:hello_modern_dx12>/D3D12/" ${AGILITYSDK_D3D12SDKLAYERS_DLL}
        COMMAND ${CMAKE_COMMAND} -E copy           -t "$<TARGET_FILE_DIR:hello_modern_dx12>/" "${DXC_BIN_DIR}/dxc.exe"
        COMMAND ${CMAKE_COMMAND} -E copy           -t "$<TARGET_FILE_DIR:hello_modern_dx12>/" "${DXC_BIN_DIR}/dxcompiler.dll"
        COMMAND ${CMAKE_COMMAND} -E copy           -t "$<TARGET_FILE_DIR:hello_modern_dx12>/" "${DXC_BIN_DIR}/dxil.dll"
        COMMAND ${CMAKE_COMMAND} -E make_directory    "$<TARGET_FILE_DIR:hello_modern_dx12>/Shaders/Compiled"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Shaders" "$<TARGET_FILE_DIR:hello_modern_dx12>/Shaders"
        COMMAND ${CMAKE_COMMAND} -E copy           -t "$<TARGET_FILE_DIR:hello_modern_dx12>/" "${CMAKE_CURRENT_SOURCE_DIR}/wood.dds"
        COMMAND ${CMAKE_COMMAND} -E copy           -t "$<TARGET_FILE_DIR:hello_modern_dx12>/" "${CMAKE_CURRENT_SOURCE_DIR}/missing_tex.png"
        COMMAND_EXPAND_LISTS
)
