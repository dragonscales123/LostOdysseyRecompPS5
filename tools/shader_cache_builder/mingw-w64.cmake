# Cross-compiles the shader tools for Windows x64 from Linux: clang targeting
# MinGW-w64 (POSIX threads), statically linked, so the .exe needs only the
# bundled dxcompiler.dll and dxil.dll beside it.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(LO_MINGW_TARGET x86_64-w64-mingw32)
set(CMAKE_C_COMPILER clang-20)
set(CMAKE_CXX_COMPILER clang++-20)
set(CMAKE_C_COMPILER_TARGET ${LO_MINGW_TARGET})
set(CMAKE_CXX_COMPILER_TARGET ${LO_MINGW_TARGET})
set(CMAKE_RC_COMPILER ${LO_MINGW_TARGET}-windres)
set(CMAKE_FIND_ROOT_PATH /usr/${LO_MINGW_TARGET})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_C_FLAGS_INIT "--gcc-toolchain=/usr -femulated-tls -fms-extensions")
set(CMAKE_CXX_FLAGS_INIT "--gcc-toolchain=/usr -femulated-tls -fms-extensions -include ${CMAKE_CURRENT_LIST_DIR}/mingw_shim.h -I${CMAKE_CURRENT_LIST_DIR}/mingw-include")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -static -static-libgcc -static-libstdc++ -Wl,-Bstatic -lpthread")
set(CMAKE_CXX_STANDARD_LIBRARIES "-Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic -lole32 -luuid -lshell32")
