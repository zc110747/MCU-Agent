# Post-build sanity check for the DMA-visible sections.
#
# Every buffer a DMA engine writes into must:
#   - live outside DTCM (0x20000000..0x2001FFFF): DMA1/DMA2 cannot reach it,
#   - sit on a 32-byte boundary with a 32-byte-multiple size, because the cache
#     maintenance is done with SCB_Clean/InvalidateDCache_by_Addr, which works
#     on whole 32-byte lines. A section that shares a line with ordinary data
#     lets one invalidate silently discard that data.
#
# Checked sections:
#   .dma_buf   UART4 receive staging      -> AXI SRAM (RAM_D1, 0x24000000)
#   .cam_buf   DCMI frame ping-pong       -> D2 SRAM  (RAM_D2, 0x30000000)
#
# A missing section is only an error for .dma_buf: the camera can be compiled
# out, the command port cannot.

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

# check_section(<name> <required>)
function(check_section section_name required)
  string(REPLACE "." "\\." escaped "${section_name}")

  if(NOT SECTIONS MATCHES "${escaped}")
    if(required)
      message(FATAL_ERROR "[dma-check] no ${section_name} section in ${TARGET_FILE}")
    else()
      message(STATUS "[dma-check] ${section_name} absent - nothing to check")
    endif()
    return()
  endif()

  # objdump -h columns: Idx Name Size VMA LMA File-off Algn
  string(REGEX MATCH
    "${escaped}[ \t]+([0-9a-f]+)[ \t]+([0-9a-f]+)"
    _m "${SECTIONS}")
  if(NOT _m)
    message(FATAL_ERROR "[dma-check] cannot parse ${section_name} header from objdump output")
    return()
  endif()

  set(_size "${CMAKE_MATCH_1}")
  set(_vma  "${CMAKE_MATCH_2}")

  math(EXPR _size_dec "0x${_size}")
  math(EXPR _vma_dec  "0x${_vma}")
  math(EXPR _vma_mod  "${_vma_dec} % 32")
  math(EXPR _size_mod "${_size_dec} % 32")
  math(EXPR _dtcm_off "${_vma_dec} - 0x20000000")

  if(_size_dec EQUAL 0)
    message(FATAL_ERROR "[dma-check] ${section_name} is empty")
  endif()
  if(_dtcm_off GREATER_EQUAL 0 AND _dtcm_off LESS 131072)
    message(FATAL_ERROR
      "[dma-check] ${section_name} is in DTCM (0x${_vma}) - DMA1/DMA2 cannot access DTCM")
  endif()
  if(NOT _vma_mod EQUAL 0)
    message(FATAL_ERROR "[dma-check] ${section_name} VMA 0x${_vma} is not 32-byte aligned")
  endif()
  if(NOT _size_mod EQUAL 0)
    message(FATAL_ERROR "[dma-check] ${section_name} size 0x${_size} is not a multiple of 32")
  endif()

  message(STATUS "[dma-check] ${section_name} VMA=0x${_vma} size=0x${_size} "
                 "(${_size_dec} B) - OK (outside DTCM, 32B aligned)")
endfunction()

check_section(".dma_buf" TRUE)
check_section(".cam_buf" FALSE)
