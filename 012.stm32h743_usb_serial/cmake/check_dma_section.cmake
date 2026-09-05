# Post-build sanity check for the DMA buffers.
#
# Every buffer used by DMA1/DMA2 must live outside DTCM (DMA1/DMA2 cannot reach
# 0x20000000) and must be 32-byte aligned with a 32-byte-multiple size, because
# the cache maintenance is done with SCB_Clean/InvalidateDCache_by_Addr which
# operates on whole cache lines.
#
# Fails the build if .dma_buf is missing, in DTCM, or not cache-line aligned.

find_program(ARM_OBJDUMP arm-none-eabi-objdump)

if(NOT ARM_OBJDUMP)
  message(STATUS "[dma-check] arm-none-eabi-objdump not found - skipped")
  return()
endif()

execute_process(
  COMMAND ${ARM_OBJDUMP} -h ${TARGET_FILE}
  OUTPUT_VARIABLE SECTIONS
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT SECTIONS MATCHES "\\.dma_buf")
  message(FATAL_ERROR "[dma-check] no .dma_buf section in ${TARGET_FILE}")
endif()

# objdump -h columns:  Idx Name  Size  VMA  LMA  File off  Algn
string(REGEX MATCH
  "\\.dma_buf[ \t]+([0-9a-f]+)[ \t]+([0-9a-f]+)"
  _m "${SECTIONS}")
if(NOT _m)
  message(FATAL_ERROR "[dma-check] cannot parse .dma_buf header from objdump output")
endif()

set(_size "${CMAKE_MATCH_1}")
set(_vma  "${CMAKE_MATCH_2}")

math(EXPR _size_dec "0x${_size}")
math(EXPR _vma_dec  "0x${_vma}")
math(EXPR _vma_mod  "${_vma_dec} % 32")
math(EXPR _size_mod "${_size_dec} % 32")
math(EXPR _dtcm_top "${_vma_dec} - 0x20000000")

if(_size_dec EQUAL 0)
  message(FATAL_ERROR "[dma-check] .dma_buf is empty")
endif()
if(_vma_dec LESS 0)
  message(FATAL_ERROR "[dma-check] bad .dma_buf VMA 0x${_vma}")
endif()
if(_dtcm_top GREATER_EQUAL 0 AND _dtcm_top LESS 131072)
  message(FATAL_ERROR
    "[dma-check] .dma_buf is in DTCM (0x${_vma}) - DMA1/DMA2 cannot access DTCM")
endif()
if(NOT _vma_mod EQUAL 0)
  message(FATAL_ERROR "[dma-check] .dma_buf VMA 0x${_vma} is not 32-byte aligned")
endif()
if(NOT _size_mod EQUAL 0)
  message(FATAL_ERROR "[dma-check] .dma_buf size 0x${_size} is not a multiple of 32")
endif()

message(STATUS "[dma-check] .dma_buf VMA=0x${_vma} size=0x${_size} "
               "(${_size_dec} B) - OK (AXI SRAM, 32B aligned)")
