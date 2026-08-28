# =============================================================================
#  Toolchain file for ARM Cortex-M (arm-none-eabi-gcc)
#
#  STemWin / binutils compatibility (IMPORTANT)
#  --------------------------------------------
#  The STemWin prebuilt library (STemWin_CM7_wc16.a) was built with ARM
#  Compiler (armcc). Its object members are valid Arm/Thumb, but GNU ld 2.44
#  (shipped with arm-none-eabi-gcc 15.x) refuses to resolve the interworking
#  relocations and aborts with "Unknown destination type (ARM/Thumb)" /
#  "dangerous relocation: unsupported relocation".
#
#  The accepted fix is a toolchain whose binutils is < 2.44
#  (GNU Arm Embedded 14.2.rel1). This file FORCES that toolchain by absolute
#  path so the gcc driver (and the ld/as it spawns) can never fall back to a
#  different toolchain found on PATH.
#
#  To pin a specific install, set the ARM_GNU_TOOLCHAIN environment variable
#  to the toolchain root (the directory that contains bin/arm-none-eabi-gcc).
# =============================================================================

set(CMAKE_SYSTEM_NAME    Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# --- Locate a binutils-2.44-compatible toolchain (optional) -------------------
set(_tc_candidates "$ENV{ARM_GNU_TOOLCHAIN}")
list(APPEND _tc_candidates
     "D:/Software/arm-gnu-toolchain-14.2.rel1"
     "C:/Software/arm-gnu-toolchain-14.2.rel1"
     "$ENV{ProgramFiles}/arm-gnu-toolchain-14.2.rel1"
     "$ENV{ProgramFiles\(x86\)}/arm-gnu-toolchain-14.2.rel1")
set(_tc_bin "")
foreach(_tc ${_tc_candidates})
  if(_tc AND EXISTS "${_tc}/bin/arm-none-eabi-gcc.exe")
    set(_tc_bin "${_tc}/bin")
    break()
  endif()
endforeach()

if(NOT _tc_bin)
  message(WARNING
    "Toolchain: a binutils < 2.44 toolchain (GNU Arm Embedded 14.2.rel1) was "
    "NOT found. STemWin (STemWin_CM7_wc16.a) will FAIL to link under ld 2.44+ "
    "(arm-none-eabi-gcc 15.x) with 'Unknown destination type (ARM/Thumb)'. "
    "Install 14.2.rel1 or set ARM_GNU_TOOLCHAIN to its root.")
  set(_tc_bin "")  # fall back to PATH; compiler/linker resolved by name below
endif()

# --- Absolute paths for every tool (no PATH ambiguity) -----------------------
# On Windows CMake validates the compiler path with a strict existence check
# that does NOT apply PATHEXT, so the .exe suffix must be explicit.
if(WIN32)
  set(_ext ".exe")
else()
  set(_ext "")
endif()

if(_tc_bin)
  message(STATUS "Toolchain: forcing binutils-<2.44 toolchain at ${_tc_bin}")
  set(_cc      "${_tc_bin}/arm-none-eabi-gcc${_ext}")
  set(_cxx     "${_tc_bin}/arm-none-eabi-g++${_ext}")
  set(_ar      "${_tc_bin}/arm-none-eabi-gcc-ar${_ext}")
  set(_ranlib  "${_tc_bin}/arm-none-eabi-gcc-ranlib${_ext}")
  set(_ld      "${_tc_bin}/arm-none-eabi-ld${_ext}")
  set(_as      "${_tc_bin}/arm-none-eabi-as${_ext}")
  set(_objcopy "${_tc_bin}/arm-none-eabi-objcopy${_ext}")
  set(_objdump "${_tc_bin}/arm-none-eabi-objdump${_ext}")
  set(_size    "${_tc_bin}/arm-none-eabi-size${_ext}")
  set(_gdb     "${_tc_bin}/arm-none-eabi-gdb${_ext}")
  set(_nm      "${_tc_bin}/arm-none-eabi-nm${_ext}")
else()
  # No known <2.44 toolchain: use bare names (resolved from PATH).
  set(_cc      arm-none-eabi-gcc)
  set(_cxx     arm-none-eabi-g++)
  set(_ar      arm-none-eabi-gcc-ar)
  set(_ranlib  arm-none-eabi-gcc-ranlib)
  set(_ld      arm-none-eabi-ld)
  set(_as      arm-none-eabi-as)
  set(_objcopy arm-none-eabi-objcopy)
  set(_objdump arm-none-eabi-objdump)
  set(_size    arm-none-eabi-size)
  set(_gdb     arm-none-eabi-gdb)
  set(_nm      arm-none-eabi-nm)
endif()

# Force the compilers/linker/ar into the cache so a stale cache from a previous
# (different) toolchain can never survive a reconfigure.
set(CMAKE_C_COMPILER     ${_cc} CACHE FILEPATH "C compiler" FORCE)
set(CMAKE_CXX_COMPILER   ${_cxx} CACHE FILEPATH "C++ compiler" FORCE)
set(CMAKE_ASM_COMPILER   ${_cc} CACHE FILEPATH "ASM compiler" FORCE)
set(CMAKE_LINKER         ${_ld} CACHE FILEPATH "linker" FORCE)
set(CMAKE_AR             ${_ar} CACHE FILEPATH "archive" FORCE)
set(CMAKE_RANLIB         ${_ranlib} CACHE FILEPATH "ranlib" FORCE)
set(CMAKE_ASM_COMPILER_AR     ${_ar} CACHE FILEPATH "" FORCE)
set(CMAKE_ASM_COMPILER_RANLIB ${_ranlib} CACHE FILEPATH "" FORCE)

set(CMAKE_OBJCOPY ${_objcopy} CACHE STRING "" FORCE)
set(CMAKE_OBJDUMP ${_objdump} CACHE STRING "" FORCE)
set(CMAKE_SIZE    ${_size}    CACHE STRING "" FORCE)
set(CMAKE_GDB     ${_gdb}     CACHE STRING "" FORCE)
set(CMAKE_DEBUGGER ${_gdb}    CACHE STRING "" FORCE)
set(CMAKE_NM      ${_nm}      CACHE STRING "" FORCE)

# Make the gcc driver resolve ld/as/collect2 from THIS toolchain's bin first.
# This is the bulletproof guard: even if some sub-tool invokes a bare
# 'arm-none-eabi-ld', the -B directory wins over anything on PATH.
if(_tc_bin)
  string(APPEND CMAKE_C_FLAGS_INIT   " -B${_tc_bin}")
  string(APPEND CMAKE_CXX_FLAGS_INIT " -B${_tc_bin}")
  string(APPEND CMAKE_ASM_FLAGS_INIT " -B${_tc_bin}")
  string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " -B${_tc_bin}")
endif()

# Cross-compiling: do not run the compiler to probe the host environment.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Search strategy: programs from host PATH, libs/headers from the sysroot only.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_SYSTEM  ONLY)
