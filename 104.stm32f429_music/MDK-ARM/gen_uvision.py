#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Generate Keil MDK-ARM project files for the stm32f429_music project.

Source of truth : CMakeLists.txt (app/bsp/HAL/FreeRTOS/FatFs/LVGL/minimp3 file
                  lists & include paths, compile definitions).
Startup         : sys_startup/arm/startup_stm32f429xx.s (ARM ADS syntax) is
                  copied into MDK-ARM/ unchanged.  app/FreeRTOSConfig.h maps
                  vPortSVCHandler/xPortPendSVHandler/xPortSysTickHandler onto
                  SVC_Handler/PendSV_Handler/SysTick_Handler, exactly the names
                  the stock vector table references - so the GCC startup
                  (sys_startup/gcc/...) and this one stay in lockstep.
Scatter file    : MDK-ARM/stm32f429_music.sct mirrors
                  sys_startup/STM32F429IGTx_FLASH.ld.  The FreeRTOS heap_5
                  region (.bss.freertos_heap, 512 KB) lives in external SDRAM
                  at 0xC0000000 in an UNINIT execution region placed BEFORE
                  the .ANY regions: __main must not zero or copy it because
                  the FMC/SDRAM controller is only initialized inside main().

Notes honored:
  * app/syscalls.c is gcc/newlib specific -> NOT added (MicroLIB is enabled
    instead; MDK-ARM/mdk_target.c provides the semihosting suppression and
    the fputc retarget).
  * No gcc/ld flags such as -Wl,--no-warn-rwx-segments (Keil/armlink does
    not use them).
  * Only the HAL modules enabled in app/stm32f4xx_hal_conf.h are added
    (same list as CMakeLists.txt).
  * LVGL sources are compiled with -w (inhibit all warnings), exactly like
    the GCC build does via set_source_files_properties(COMPILE_OPTIONS "-w"),
    so the project's own code stays strictly warning-clean.
  * minimp3 is compiled with -O2 -w, mirroring the per-file GCC override in
    CMakeLists.txt (float-heavy MDCT: stays fast without -O2).
  * All build output (Objects/, Listings/) lives under MDK-ARM/ -> no
    pollution elsewhere.
  * verify_file_lists() cross-checks the generated groups against the real
    source tree (app/, app/ui/, bsp/ are GLOBbed by CMake) so this file can
    never silently drift from the project again.
"""

import os

PROJ_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
MDK_DIR = os.path.join(PROJ_ROOT, "MDK-ARM")
TARGET = "stm32f429_music"
DEVICE = "STM32F429IGTx"
PACKID = "Keil.STM32F4xx_DFP.2.16.0"

# ----------------------------------------------------------------------------
# File groups (FilePath is relative to the MDK-ARM/ directory, i.e. "..\\" -> project root)
# FileType: 1 = C source, 2 = Assembler source
# ----------------------------------------------------------------------------
C = 1
ASM = 2

GROUPS = [
    ("Startup/MDK-ARM", [
        ("startup_stm32f429xx.s", ASM, "startup_stm32f429xx.s"),
    ]),
    ("Application/User", [
        ("main.c", C, "..\\app\\main.c"),
        ("app_ui.c", C, "..\\app\\app_ui.c"),
        ("audio_player.c", C, "..\\app\\audio_player.c"),
        ("fs_diskio.c", C, "..\\app\\fs_diskio.c"),
        ("log.c", C, "..\\app\\log.c"),
        ("mp3_decoder.c", C, "..\\app\\mp3_decoder.c"),
        ("playlist.c", C, "..\\app\\playlist.c"),
        ("sd_card.c", C, "..\\app\\sd_card.c"),
        ("sdram_heap.c", C, "..\\app\\sdram_heap.c"),
        ("track_decoder.c", C, "..\\app\\track_decoder.c"),
        ("wav_decoder.c", C, "..\\app\\wav_decoder.c"),
        ("stm32f4xx_it.c", C, "..\\app\\stm32f4xx_it.c"),
        ("stm32f4xx_hal_timebase_tim.c", C, "..\\app\\stm32f4xx_hal_timebase_tim.c"),
        ("mdk_target.c", C, "mdk_target.c"),
    ]),
    ("Drivers/BSP", [
        ("bsp_delay.c", C, "..\\bsp\\bsp_delay.c"),
        ("bsp_iic.c", C, "..\\bsp\\bsp_iic.c"),
        ("bsp_key.c", C, "..\\bsp\\bsp_key.c"),
        ("bsp_led.c", C, "..\\bsp\\bsp_led.c"),
        ("bsp_uart.c", C, "..\\bsp\\bsp_uart.c"),
        ("bsp_sai.c", C, "..\\bsp\\bsp_sai.c"),
        ("bsp_wm8978.c", C, "..\\bsp\\bsp_wm8978.c"),
        ("bsp_sdram.c", C, "..\\bsp\\bsp_sdram.c"),
        ("bsp_sdio.c", C, "..\\bsp\\bsp_sdio.c"),
        ("bsp_lcd.c", C, "..\\bsp\\bsp_lcd.c"),
        ("bsp_lcd_text.c", C, "..\\bsp\\bsp_lcd_text.c"),
        ("lcd_ascii_font.c", C, "..\\bsp\\lcd_ascii_font.c"),
        ("lv_font_gbk.c", C, "..\\bsp\\lv_font_gbk.c"),
        ("lv_gbk_map.c", C, "..\\bsp\\lv_gbk_map.c"),
        ("lv_port_disp.c", C, "..\\bsp\\lv_port_disp.c"),
    ]),
    ("Drivers/STM32F4xx_HAL_Driver", [
        ("stm32f4xx_hal.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal.c"),
        ("stm32f4xx_hal_cortex.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_cortex.c"),
        ("stm32f4xx_hal_rcc.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_rcc.c"),
        ("stm32f4xx_hal_rcc_ex.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_rcc_ex.c"),
        ("stm32f4xx_hal_gpio.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_gpio.c"),
        ("stm32f4xx_hal_dma.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_dma.c"),
        ("stm32f4xx_hal_dma_ex.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_dma_ex.c"),
        ("stm32f4xx_hal_pwr.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_pwr.c"),
        ("stm32f4xx_hal_pwr_ex.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_pwr_ex.c"),
        ("stm32f4xx_hal_flash.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_flash.c"),
        # Keil-only addition: HAL_FLASH_IRQHandler() in hal_flash.c calls
        # FLASH_Erase_Sector()/FLASH_FlushCaches(), which live in the *_ex
        # file.  The GCC build never notices because -ffunction-sections +
        # --gc-sections drops the unused handler before the undefined
        # references are resolved; armlink resolves every symbol of every
        # linked object, so the pair has to be complete here.
        ("stm32f4xx_hal_flash_ex.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_flash_ex.c"),
        ("stm32f4xx_hal_tim.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_tim.c"),
        ("stm32f4xx_hal_tim_ex.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_tim_ex.c"),
        ("stm32f4xx_hal_uart.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_uart.c"),
        ("stm32f4xx_hal_sai.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_sai.c"),
        ("stm32f4xx_hal_sai_ex.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_sai_ex.c"),
        ("stm32f4xx_hal_sd.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_sd.c"),
        ("stm32f4xx_ll_sdmmc.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_ll_sdmmc.c"),
        ("stm32f4xx_hal_sdram.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_sdram.c"),
        ("stm32f4xx_hal_sram.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_sram.c"),
        ("stm32f4xx_ll_fmc.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_ll_fmc.c"),
    ]),
    ("Drivers/CMSIS", [
        ("system_stm32f4xx.c", C, "..\\sys_startup\\system_stm32f4xx.c"),
    ]),
    ("Middlewares/FreeRTOS", [
        ("tasks.c", C, "..\\third_party\\FreeRTOS-Kernel\\tasks.c"),
        ("queue.c", C, "..\\third_party\\FreeRTOS-Kernel\\queue.c"),
        ("list.c", C, "..\\third_party\\FreeRTOS-Kernel\\list.c"),
        ("timers.c", C, "..\\third_party\\FreeRTOS-Kernel\\timers.c"),
        ("event_groups.c", C, "..\\third_party\\FreeRTOS-Kernel\\event_groups.c"),
        ("stream_buffer.c", C, "..\\third_party\\FreeRTOS-Kernel\\stream_buffer.c"),
        ("port.c", C, "..\\third_party\\FreeRTOS-Kernel\\portable\\GCC\\ARM_CM4F\\port.c"),
        ("heap_5.c", C, "..\\third_party\\FreeRTOS-Kernel\\portable\\MemMang\\heap_5.c"),
    ]),
    ("Middlewares/minimp3", [
        ("minimp3.c", C, "..\\third_party\\minimp3\\minimp3.c"),
    ]),
    ("Middlewares/FatFs", [
        ("ff.c", C, "..\\third_party\\FatFs\\ff.c"),
        ("ffunicode.c", C, "..\\third_party\\FatFs\\ffunicode.c"),
        ("ffsystem.c", C, "..\\third_party\\FatFs\\ffsystem.c"),
    ]),
]

# Per-file compiler options (MiscControls), keyed by path prefix.
#   LVGL is a mature vendored library: silence it exactly like the GCC build
#   does (set_source_files_properties(... COMPILE_OPTIONS "-w")) so the
#   project's own code stays strictly warning-clean.
#   minimp3 additionally gets -O2 (float-heavy MDCT), matching the per-file
#   COMPILE_OPTIONS override in CMakeLists.txt.
FILE_MISC = {
    "..\\third_party\\lvgl\\": "-w",
    "..\\third_party\\minimp3\\": "-O2 -w",
}

# app/, app/ui/ and bsp/ are GLOBbed by CMakeLists.txt -> every .c on disk
# must be present in the Keil groups.  Exceptions are documented here.
GLOB_EXCLUDE = {
    # GCC/newlib syscall glue (_write).  Keil uses MicroLIB and
    # MDK-ARM/mdk_target.c instead (semihosting suppression + fputc
    # retarget to uart_write()).
    "syscalls.c",
}

# Files that this script *generates* into MDK-ARM/ (they do not exist before
# the first run, so verify_file_lists() must check their source instead).
GENERATED = {
    "startup_stm32f429xx.s": os.path.join(
        PROJ_ROOT, "sys_startup", "arm", "startup_stm32f429xx.s"),
}


def discover_lvgl_groups():
    """Mirror CMakeLists' file(GLOB_RECURSE LVGL_SOURCES ...) one-to-one.

    Groups are named after the source directory (max two path components
    below third_party/lvgl/src) so the µVision tree stays navigable:
        src/core/lv_obj.c            -> Middlewares/LVGL/core
        src/draw/sw/lv_draw_sw.c     -> Middlewares/LVGL/draw/sw
        src/extra/layouts/flex/x.c   -> Middlewares/LVGL/extra/layouts
    """
    src_root = os.path.join(PROJ_ROOT, "third_party", "lvgl", "src")
    collected = {}
    for dirpath, _dirnames, filenames in os.walk(src_root):
        for fn in sorted(filenames):
            if not fn.endswith(".c"):
                continue
            rel = os.path.relpath(os.path.join(dirpath, fn), src_root)
            parts = rel.split(os.sep)[:-1]
            sub = os.sep.join(parts[:2]) if parts else ""
            gname = "Middlewares/LVGL" + ("/" + sub.replace(os.sep, "/") if sub else "")
            fpath = "..\\third_party\\lvgl\\src\\" + rel.replace(os.sep, "\\")
            collected.setdefault(gname, []).append((fn, C, fpath))
    return [(g, collected[g]) for g in sorted(collected)]


def verify_file_lists():
    """Fail fast if the generated groups drift from the real source tree.

    * Every file referenced by GROUPS must exist on disk.
    * Every .c under app/, app/ui/, bsp/ (GLOBbed by the GCC build) must be
      part of the Keil project unless explicitly excluded above.
    """
    problems = []

    referenced = []
    for _gname, files in GROUPS:
        for fname, _ftype, fpath in files:
            referenced.append(fname)
            if fname in GENERATED:
                if not os.path.isfile(GENERATED[fname]):
                    problems.append("missing source for generated file: %s"
                                    % GENERATED[fname])
                continue
            abspath = os.path.normpath(os.path.join(MDK_DIR, fpath))
            if not os.path.isfile(abspath):
                problems.append("missing file: %s (group %s)" % (fpath, _gname))

    for gname, files in discover_lvgl_groups():
        for fname, _ftype, fpath in files:
            referenced.append(fname)
            abspath = os.path.normpath(os.path.join(MDK_DIR, fpath))
            if not os.path.isfile(abspath):
                problems.append("missing file: %s (group %s)" % (fpath, gname))

    for sub in ("app", os.path.join("app", "ui"), "bsp"):
        base = os.path.join(PROJ_ROOT, sub)
        # app/ui only exists in projects that split their UI pages out.
        if not os.path.isdir(base):
            continue
        for fn in sorted(os.listdir(base)):
            if not fn.endswith(".c"):
                continue
            if fn in GLOB_EXCLUDE:
                continue
            if fn not in referenced:
                problems.append("%s is GLOBbed by CMakeLists.txt but missing "
                                "from the Keil project" % os.path.join(sub, fn))

    if problems:
        raise SystemExit("ERROR: Keil file list does not match the source tree:\n  "
                         + "\n  ".join(problems))


INCLUDE_PATH = ";".join([
    "..\\app",
    "..\\bsp",
    "..\\sys_startup",
    "..\\Drivers\\CMSIS\\Include",
    "..\\Drivers\\STM32F4xx_HAL_Driver\\Inc",
    "..\\third_party\\FreeRTOS-Kernel\\include",
    "..\\third_party\\FreeRTOS-Kernel\\portable\\GCC\\ARM_CM4F",
    "..\\third_party\\FatFs",
    "..\\third_party\\minimp3",
    "..\\third_party\\lvgl",
    "..\\third_party\\lvgl\\src",
])

# Mirrors add_compile_definitions() in CMakeLists.txt.
DEFINE = "STM32F429xx,USE_HAL_DRIVER,PRINT_LOG_ENABLE=1"


def _file_xml(fname, ftype, fpath):
    # Vendored libraries inherit their per-path MiscControls (see FILE_MISC).
    misc = ""
    for prefix, opts in FILE_MISC.items():
        if fpath.startswith(prefix):
            misc = opts
            break

    opt_xml = ""
    if misc:
        opt_xml = (
            "              <Option>\n"
            "                <CommonProperty>\n"
            "                  <Cads>\n"
            "                    <VariousControls>\n"
            "                      <MiscControls>%s</MiscControls>\n"
            "                    </VariousControls>\n"
            "                  </Cads>\n"
            "                </CommonProperty>\n"
            "              </Option>\n" % misc
        )
    return (
        "            <File>\n"
        "              <FileName>%s</FileName>\n"
        "              <FileType>%d</FileType>\n"
        "              <FilePath>%s</FilePath>\n"
        "%s"
        "            </File>" % (fname, ftype, fpath, opt_xml)
    )


def _all_groups():
    return GROUPS + discover_lvgl_groups()


def gen_uvprojx():
    groups_xml = []
    for gname, files in _all_groups():
        file_xml = [_file_xml(fname, ftype, fpath) for fname, ftype, fpath in files]
        groups_xml.append(
            "        <Group>\n"
            "          <GroupName>%s</GroupName>\n"
            "          <Files>\n%s\n"
            "          </Files>\n"
            "        </Group>" % (gname, "\n".join(file_xml))
        )

    xml = """<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<Project xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="project_projx.xsd">

  <SchemaVersion>2.1</SchemaVersion>

  <Header>### uVision Project, (C) Keil Software</Header>

  <Targets>
    <Target>
      <TargetName>%s</TargetName>
      <ToolsetNumber>0x4</ToolsetNumber>
      <ToolsetName>ARM-ADS</ToolsetName>
      <pCCUsed>6140000::V6.14::ARMCLANG</pCCUsed>
      <uAC6>1</uAC6>
      <TargetOption>
        <TargetCommonOption>
          <Device>%s</Device>
          <Vendor>STMicroelectronics</Vendor>
          <PackID>%s</PackID>
          <PackURL>https://www.keil.com/pack/</PackURL>
          <Cpu>IRAM(0x20000000,0x00030000) IRAM2(0x10000000,0x00010000) IROM(0x08000000,0x00100000) CLOCK(25000000) CPUTYPE("Cortex-M4") ELITTLE FPU2</Cpu>
          <FlashUtilSpec></FlashUtilSpec>
          <StartupFile></StartupFile>
          <FlashDriverDll>UL2CM3(-S0 -C0 -P0 -FD20000000 -FC1000 -FN1 -FF0STM32F4xx_1024 -FS08000000 -FL0100000 -FP0($$Device:STM32F429IGTx$CMSIS\\Flash\\STM32F4xx_1024.FLM))</FlashDriverDll>
          <DeviceId>0</DeviceId>
          <RegisterFile></RegisterFile>
          <MemoryEnv></MemoryEnv>
          <Cmp></Cmp>
          <Asm></Asm>
          <Linker></Linker>
          <OHString></OHString>
          <InfinionOptionDll></InfinionOptionDll>
          <SLE66CMisc></SLE66CMisc>
          <SLE66AMisc></SLE66AMisc>
          <SLE66LinkerMisc></SLE66LinkerMisc>
          <SFDFile>$$Device:%s$CMSIS\\SVD\\STM32F429.svd</SFDFile>
          <bCustSvd>0</bCustSvd>
          <UseEnv>0</UseEnv>
          <BinPath></BinPath>
          <IncludePath></IncludePath>
          <LibPath></LibPath>
          <RegisterFilePath></RegisterFilePath>
          <DBRegisterFilePath></DBRegisterFilePath>
          <TargetStatus>
            <Error>0</Error>
            <ExitCodeStop>0</ExitCodeStop>
            <ButtonStop>0</ButtonStop>
            <NotGenerated>0</NotGenerated>
            <InvalidFlash>1</InvalidFlash>
          </TargetStatus>
          <OutputDirectory>.\\Objects\\</OutputDirectory>
          <OutputName>%s</OutputName>
          <CreateExecutable>1</CreateExecutable>
          <CreateLib>0</CreateLib>
          <CreateHexFile>1</CreateHexFile>
          <DebugInformation>1</DebugInformation>
          <BrowseInformation>1</BrowseInformation>
          <ListingPath>.\\Listings\\</ListingPath>
          <HexFormatSelection>1</HexFormatSelection>
          <Merge32K>0</Merge32K>
          <CreateBatchFile>0</CreateBatchFile>
          <BeforeCompile>
            <RunUserProg1>0</RunUserProg1>
            <RunUserProg2>0</RunUserProg2>
            <UserProg1Name></UserProg1Name>
            <UserProg2Name></UserProg2Name>
            <UserProg1Dos16Mode>0</UserProg1Dos16Mode>
            <UserProg2Dos16Mode>0</UserProg2Dos16Mode>
            <nStopU1X>0</nStopU1X>
            <nStopU2X>0</nStopU2X>
          </BeforeCompile>
          <BeforeMake>
            <RunUserProg1>0</RunUserProg1>
            <RunUserProg2>0</RunUserProg2>
            <UserProg1Name></UserProg1Name>
            <UserProg2Name></UserProg2Name>
            <UserProg1Dos16Mode>0</UserProg1Dos16Mode>
            <UserProg2Dos16Mode>0</UserProg2Dos16Mode>
            <nStopB1X>0</nStopB1X>
            <nStopB2X>0</nStopB2X>
          </BeforeMake>
          <AfterMake>
            <RunUserProg1>0</RunUserProg1>
            <RunUserProg2>1</RunUserProg2>
            <UserProg1Name></UserProg1Name>
            <UserProg2Name>fromelf --bin !L --output .\\Objects\\%s.bin</UserProg2Name>
            <UserProg1Dos16Mode>0</UserProg1Dos16Mode>
            <UserProg2Dos16Mode>0</UserProg2Dos16Mode>
            <nStopA1X>0</nStopA1X>
            <nStopA2X>0</nStopA2X>
          </AfterMake>
          <SelectedForBatchBuild>1</SelectedForBatchBuild>
          <SVCSIdString></SVCSIdString>
        </TargetCommonOption>
        <CommonProperty>
          <UseCPPCompiler>0</UseCPPCompiler>
          <RVCTCodeConst>0</RVCTCodeConst>
          <RVCTZI>0</RVCTZI>
          <RVCTOtherData>0</RVCTOtherData>
          <ModuleSelection>0</ModuleSelection>
          <IncludeInBuild>1</IncludeInBuild>
          <AlwaysBuild>0</AlwaysBuild>
          <GenerateAssemblyFile>0</GenerateAssemblyFile>
          <AssembleAssemblyFile>0</AssembleAssemblyFile>
          <PublicsOnly>0</PublicsOnly>
          <StopOnExitCode>3</StopOnExitCode>
          <CustomArgument></CustomArgument>
          <IncludeLibraryModules></IncludeLibraryModules>
          <ComprImg>0</ComprImg>
        </CommonProperty>
        <DllOption>
          <SimDllName>SARMCM3.DLL</SimDllName>
          <SimDllArguments>-REMAP -MPU</SimDllArguments>
          <SimDlgDll>DCM.DLL</SimDlgDll>
          <SimDlgDllArguments>-pCM4</SimDlgDllArguments>
          <TargetDllName>SARMCM3.DLL</TargetDllName>
          <TargetDllArguments> -MPU</TargetDllArguments>
          <TargetDlgDll>TCM.DLL</TargetDlgDll>
          <TargetDlgDllArguments>-pCM4</TargetDlgDllArguments>
        </DllOption>
        <DebugOption>
          <OPTHX>
            <HexSelection>1</HexSelection>
            <HexRangeLowAddress>0</HexRangeLowAddress>
            <HexRangeHighAddress>0</HexRangeHighAddress>
            <HexOffset>0</HexOffset>
            <Oh166RecLen>16</Oh166RecLen>
          </OPTHX>
        </DebugOption>
        <Utilities>
          <Flash1>
            <UseTargetDll>1</UseTargetDll>
            <UseExternalTool>0</UseExternalTool>
            <RunIndependent>0</RunIndependent>
            <UpdateFlashBeforeDebugging>1</UpdateFlashBeforeDebugging>
            <Capability>1</Capability>
            <DriverSelection>4096</DriverSelection>
          </Flash1>
          <bUseTDR>1</bUseTDR>
          <Flash2>BIN\\UL2CM3.DLL</Flash2>
          <Flash3></Flash3>
          <Flash4></Flash4>
          <pFcarmOut></pFcarmOut>
          <pFcarmGrp></pFcarmGrp>
          <pFcArmRoot></pFcArmRoot>
          <FcArmLst>0</FcArmLst>
        </Utilities>
        <TargetArmAds>
          <ArmAdsMisc>
            <GenerateListings>0</GenerateListings>
            <asHll>1</asHll>
            <asAsm>1</asAsm>
            <asMacX>1</asMacX>
            <asSyms>1</asSyms>
            <asFals>1</asFals>
            <asDbgD>1</asDbgD>
            <asForm>1</asForm>
            <ldLst>0</ldLst>
            <ldmm>1</ldmm>
            <ldXref>1</ldXref>
            <BigEnd>0</BigEnd>
            <AdsALst>1</AdsALst>
            <AdsACrf>1</AdsACrf>
            <AdsANop>0</AdsANop>
            <AdsANot>0</AdsANot>
            <AdsLLst>1</AdsLLst>
            <AdsLmap>1</AdsLmap>
            <AdsLcgr>1</AdsLcgr>
            <AdsLsym>1</AdsLsym>
            <AdsLszi>1</AdsLszi>
            <AdsLtoi>1</AdsLtoi>
            <AdsLsun>1</AdsLsun>
            <AdsLven>1</AdsLven>
            <AdsLsxf>1</AdsLsxf>
            <RvctClst>0</RvctClst>
            <GenPPlst>0</GenPPlst>
            <AdsCpuType>"Cortex-M4"</AdsCpuType>
            <RvctDeviceName>%s</RvctDeviceName>
            <mOS>0</mOS>
            <uocRom>0</uocRom>
            <uocRam>0</uocRam>
            <hadIROM>1</hadIROM>
            <hadIRAM>1</hadIRAM>
            <hadXRAM>0</hadXRAM>
            <uocXRam>0</uocXRam>
            <RvdsVP>2</RvdsVP>
            <RvdsMve>0</RvdsMve>
            <RvdsCdeCp>0</RvdsCdeCp>
            <hadIRAM2>1</hadIRAM2>
            <hadIROM2>0</hadIROM2>
            <StupSel>8</StupSel>
            <useUlib>1</useUlib>
            <EndSel>0</EndSel>
            <uLtcg>0</uLtcg>
            <nSecure>0</nSecure>
            <RoSelD>3</RoSelD>
            <RwSelD>4</RwSelD>
            <CodeSel>0</CodeSel>
            <OptFeed>0</OptFeed>
            <NoZi1>0</NoZi1>
            <NoZi2>0</NoZi2>
            <NoZi3>0</NoZi3>
            <NoZi4>0</NoZi4>
            <NoZi5>0</NoZi5>
            <Ro1Chk>0</Ro1Chk>
            <Ro2Chk>0</Ro2Chk>
            <Ro3Chk>0</Ro3Chk>
            <Ir1Chk>1</Ir1Chk>
            <Ir2Chk>0</Ir2Chk>
            <Ra1Chk>0</Ra1Chk>
            <Ra2Chk>0</Ra2Chk>
            <Ra3Chk>0</Ra3Chk>
            <Im1Chk>1</Im1Chk>
            <Im2Chk>1</Im2Chk>
            <OnChipMemories>
              <Ocm1>
                <Type>0</Type>
                <StartAddress>0x0</StartAddress>
                <Size>0x0</Size>
              </Ocm1>
              <Ocm2>
                <Type>0</Type>
                <StartAddress>0x0</StartAddress>
                <Size>0x0</Size>
              </Ocm2>
              <Ocm3>
                <Type>0</Type>
                <StartAddress>0x0</StartAddress>
                <Size>0x0</Size>
              </Ocm3>
              <Ocm4>
                <Type>0</Type>
                <StartAddress>0x0</StartAddress>
                <Size>0x0</Size>
              </Ocm4>
              <Ocm5>
                <Type>0</Type>
                <StartAddress>0x0</StartAddress>
                <Size>0x0</Size>
              </Ocm5>
              <Ocm6>
                <Type>0</Type>
                <StartAddress>0x0</StartAddress>
                <Size>0x0</Size>
              </Ocm6>
              <IRAM>
                <Type>0</Type>
                <StartAddress>0x20000000</StartAddress>
                <Size>0x30000</Size>
              </IRAM>
              <IROM>
                <Type>1</Type>
                <StartAddress>0x8000000</StartAddress>
                <Size>0x100000</Size>
              </IROM>
              <XRAM>
                <Type>0</Type>
                <StartAddress>0x0</StartAddress>
                <Size>0x0</Size>
              </XRAM>
              <OCR_RVCT1>
                <Type>1</Type>
                <StartAddress>0x0</StartAddress>
                <Size>0x0</Size>
              </OCR_RVCT1>
              <OCR_RVCT2>
                <Type>1</Type>
                <StartAddress>0x0</StartAddress>
                <Size>0x0</Size>
              </OCR_RVCT2>
              <OCR_RVCT3>
                <Type>1</Type>
                <StartAddress>0x0</StartAddress>
                <Size>0x0</Size>
              </OCR_RVCT3>
              <OCR_RVCT4>
                <Type>1</Type>
                <StartAddress>0x8000000</StartAddress>
                <Size>0x100000</Size>
              </OCR_RVCT4>
              <OCR_RVCT5>
                <Type>1</Type>
                <StartAddress>0x0</StartAddress>
                <Size>0x0</Size>
              </OCR_RVCT5>
              <OCR_RVCT6>
                <Type>0</Type>
                <StartAddress>0x0</StartAddress>
                <Size>0x0</Size>
              </OCR_RVCT6>
              <OCR_RVCT7>
                <Type>0</Type>
                <StartAddress>0x0</StartAddress>
                <Size>0x0</Size>
              </OCR_RVCT7>
              <OCR_RVCT8>
                <Type>0</Type>
                <StartAddress>0x0</StartAddress>
                <Size>0x0</Size>
              </OCR_RVCT8>
              <OCR_RVCT9>
                <Type>0</Type>
                <StartAddress>0x20000000</StartAddress>
                <Size>0x30000</Size>
              </OCR_RVCT9>
              <OCR_RVCT10>
                <Type>0</Type>
                <StartAddress>0x10000000</StartAddress>
                <Size>0x10000</Size>
              </OCR_RVCT10>
            </OnChipMemories>
            <RvctStartVector></RvctStartVector>
          </ArmAdsMisc>
          <Cads>
            <interw>1</interw>
            <Optim>2</Optim>
            <oTime>0</oTime>
            <SplitLS>0</SplitLS>
            <OneElfS>1</OneElfS>
            <Strict>0</Strict>
            <EnumInt>0</EnumInt>
            <PlainCh>0</PlainCh>
            <Ropi>0</Ropi>
            <Rwpi>0</Rwpi>
            <wLevel>3</wLevel>
            <uThumb>0</uThumb>
            <uSurpInc>0</uSurpInc>
            <uC99>1</uC99>
            <uGnu>1</uGnu>
            <useXO>0</useXO>
            <v6Lang>3</v6Lang>
            <v6LangP>3</v6LangP>
            <vShortEn>1</vShortEn>
            <vShortWch>1</vShortWch>
            <v6Lto>0</v6Lto>
            <v6WtE>0</v6WtE>
            <v6Rtti>1</v6Rtti>
            <VariousControls>
              <MiscControls></MiscControls>
              <Define>%s</Define>
              <Undefine></Undefine>
              <IncludePath>%s</IncludePath>
            </VariousControls>
          </Cads>
          <Aads>
            <interw>1</interw>
            <Ropi>0</Ropi>
            <Rwpi>0</Rwpi>
            <thumb>0</thumb>
            <SplitLS>0</SplitLS>
            <SwStkChk>0</SwStkChk>
            <NoWarn>0</NoWarn>
            <uSurpInc>0</uSurpInc>
            <useXO>0</useXO>
            <ClangAsOpt>1</ClangAsOpt>
            <VariousControls>
              <MiscControls></MiscControls>
              <Define></Define>
              <Undefine></Undefine>
              <IncludePath></IncludePath>
            </VariousControls>
          </Aads>
          <LDads>
            <umfTarg>0</umfTarg>
            <Ropi>0</Ropi>
            <Rwpi>0</Rwpi>
            <noStLib>0</noStLib>
            <RepFail>1</RepFail>
            <useFile>1</useFile>
            <TextAddressRange></TextAddressRange>
            <DataAddressRange></DataAddressRange>
            <pXoBase></pXoBase>
            <ScatterFile>.\\%s.sct</ScatterFile>
            <IncludeLibs></IncludeLibs>
            <IncludeLibsPath></IncludeLibsPath>
            <Misc>--remove</Misc>
            <LinkerInputFile></LinkerInputFile>
            <DisabledWarnings></DisabledWarnings>
          </LDads>
        </TargetArmAds>
      </TargetOption>
      <Groups>
%s
      </Groups>
    </Target>
  </Targets>

  <RTE>
    <apis/>
    <components/>
    <files/>
  </RTE>

  <LayerInfo>
    <Layers>
      <Layer>
        <LayName>%s</LayName>
        <LayDesc></LayDesc>
        <LayUrl></LayUrl>
        <LayKeys></LayKeys>
        <LayCat></LayCat>
        <LayLic></LayLic>
        <LayTarg>0</LayTarg>
        <LayPrjMark>1</LayPrjMark>
      </Layer>
    </Layers>
  </LayerInfo>

</Project>
""" % (TARGET, DEVICE, PACKID, DEVICE, TARGET, TARGET, DEVICE, DEFINE, INCLUDE_PATH,
       TARGET, "\n".join(groups_xml), TARGET)

    out = os.path.join(MDK_DIR, TARGET + ".uvprojx")
    with open(out, "w", encoding="utf-8") as f:
        f.write(xml)
    return out


def gen_uvoptx():
    # Group / File browser entries mirror the project structure.
    group_xml = []
    gn = 1
    fn = 1
    for gname, files in _all_groups():
        file_xml = []
        for fname, ftype, fpath in files:
            file_xml.append(
                "    <File>\n"
                "      <GroupNumber>%d</GroupNumber>\n"
                "      <FileNumber>%d</FileNumber>\n"
                "      <FileType>%d</FileType>\n"
                "      <tvExp>0</tvExp>\n"
                "      <tvExpOptDlg>0</tvExpOptDlg>\n"
                "      <bDave2>0</bDave2>\n"
                "      <PathWithFileName>%s</PathWithFileName>\n"
                "      <FilenameWithoutPath>%s</FilenameWithoutPath>\n"
                "      <RteFlg>0</RteFlg>\n"
                "      <bShared>0</bShared>\n"
                "    </File>" % (gn, fn, ftype, fpath, fname)
            )
            fn += 1
        group_xml.append(
            "  <Group>\n"
            "    <GroupName>%s</GroupName>\n"
            "    <tvExp>1</tvExp>\n"
            "    <tvExpOptDlg>0</tvExpOptDlg>\n"
            "    <cbSel>0</cbSel>\n"
            "    <RteFlg>0</RteFlg>%s\n"
            "  </Group>" % (gname, "\n".join(file_xml))
        )
        gn += 1

    xml = """<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<ProjectOpt xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="project_optx.xsd">

  <SchemaVersion>1.0</SchemaVersion>

  <Header>### uVision Project, (C) Keil Software</Header>

  <Extensions>
    <cExt>*.c</cExt>
    <aExt>*.s*; *.src; *.a*</aExt>
    <oExt>*.obj; *.o</oExt>
    <lExt>*.lib</lExt>
    <tExt>*.txt; *.h; *.inc; *.md</tExt>
    <pExt>*.plm</pExt>
    <CppX>*.cpp</CppX>
    <nMigrate>0</nMigrate>
  </Extensions>

  <DaveTm>
    <dwLowDateTime>0</dwLowDateTime>
    <dwHighDateTime>0</dwHighDateTime>
  </DaveTm>

  <Target>
    <TargetName>%s</TargetName>
    <ToolsetNumber>0x4</ToolsetNumber>
    <ToolsetName>ARM-ADS</ToolsetName>
    <TargetOption>
      <CLKADS>25000000</CLKADS>
      <OPTTT>
        <gFlags>1</gFlags>
        <BeepAtEnd>1</BeepAtEnd>
        <RunSim>0</RunSim>
        <RunTarget>1</RunTarget>
        <RunAbUc>0</RunAbUc>
      </OPTTT>
      <OPTHX>
        <HexSelection>1</HexSelection>
        <FlashByte>65535</FlashByte>
        <HexRangeLowAddress>0</HexRangeLowAddress>
        <HexRangeHighAddress>0</HexRangeHighAddress>
        <HexOffset>0</HexOffset>
      </OPTHX>
      <OPTLEX>
        <PageWidth>79</PageWidth>
        <PageLength>66</PageLength>
        <TabStop>8</TabStop>
        <ListingPath></ListingPath>
      </OPTLEX>
      <ListingPage>
        <CreateCListing>1</CreateCListing>
        <CreateAListing>1</CreateAListing>
        <CreateLListing>1</CreateLListing>
        <CreateIListing>0</CreateIListing>
        <AsmCond>1</AsmCond>
        <AsmSymb>1</AsmSymb>
        <AsmXref>0</AsmXref>
        <CCond>1</CCond>
        <CCode>0</CCode>
        <CListInc>0</CListInc>
        <CSymb>0</CSymb>
        <LinkerCodeListing>0</LinkerCodeListing>
      </ListingPage>
      <OPTXL>
        <LMap>1</LMap>
        <LComments>1</LComments>
        <LGenerateSymbols>1</LGenerateSymbols>
        <LLibSym>1</LLibSym>
        <LLines>1</LLines>
        <LLocSym>1</LLocSym>
        <LPubSym>1</LPubSym>
        <LXref>0</LXref>
        <LExpSel>0</LExpSel>
      </OPTXL>
      <OPTFL>
        <tvExp>1</tvExp>
        <tvExpOptDlg>0</tvExpOptDlg>
        <IsCurrentTarget>1</IsCurrentTarget>
      </OPTFL>
      <CpuCode>19</CpuCode>
      <DebugOpt>
        <uSim>0</uSim>
        <uTrg>1</uTrg>
        <sLdApp>1</sLdApp>
        <sGomain>1</sGomain>
        <sRbreak>1</sRbreak>
        <sRwatch>1</sRwatch>
        <sRmem>1</sRmem>
        <sRfunc>1</sRfunc>
        <sRbox>1</sRbox>
        <tLdApp>1</tLdApp>
        <tGomain>1</tGomain>
        <tRbreak>1</tRbreak>
        <tRwatch>1</tRwatch>
        <tRmem>1</tRmem>
        <tRfunc>0</tRfunc>
        <tRbox>1</tRbox>
        <tRtrace>1</tRtrace>
        <sRSysVw>1</sRSysVw>
        <tRSysVw>1</tRSysVw>
        <sRunDeb>0</sRunDeb>
        <sLrtime>0</sLrtime>
        <bEvRecOn>1</bEvRecOn>
        <bSchkAxf>0</bSchkAxf>
        <bTchkAxf>0</bTchkAxf>
        <nTsel>6</nTsel>
        <sDll></sDll>
        <sDllPa></sDllPa>
        <sDlgDll></sDlgDll>
        <sDlgPa></sDlgPa>
        <sIfile></sIfile>
        <tDll></tDll>
        <tDllPa></tDllPa>
        <tDlgDll></tDlgDll>
        <tDlgPa></tDlgPa>
        <tIfile></tIfile>
        <pMon>STLink\\ST-LINKIII-KEIL_SWO.dll</pMon>
      </DebugOpt>
      <TargetDriverDllRegistry>
        <SetRegEntry>
          <Number>0</Number>
          <Key>ST-LINKIII-KEIL_SWO</Key>
          <Name>-U00160038310000154E593053 -O206 -SF5000 -C0 -A0 -I0 -HNlocalhost -HP7184 -P1 -N00("ARM CoreSight SW-DP (ARM Core") -D00(2BA01477) -L00(0) -TO131090 -TC10000000 -TT10000000 -TP21 -TDS8007 -TDT0 -TDC1F -TIEFFFFFFFF -TIP8 -FO7 -FD20000000 -FC1000 -FN1 -FF0STM32F4xx_1024.FLM -FS08000000 -FL0100000 -FP0($$Device:%s$CMSIS\\Flash\\STM32F4xx_1024.FLM)</Name>
        </SetRegEntry>
        <SetRegEntry>
          <Number>0</Number>
          <Key>UL2CM3</Key>
          <Name>UL2CM3(-S0 -C0 -P0 -FD20000000 -FC1000 -FN1 -FF0STM32F4xx_1024 -FS08000000 -FL0100000 -FP0($$Device:%s$CMSIS\\Flash\\STM32F4xx_1024.FLM))</Name>
        </SetRegEntry>
      </TargetDriverDllRegistry>
      <Breakpoint/>
      <Tracepoint>
        <THDelay>0</THDelay>
      </Tracepoint>
      <DebugFlag>
        <trace>0</trace>
        <periodic>0</periodic>
        <aLwin>1</aLwin>
        <aCover>0</aCover>
        <aSer1>0</aSer1>
        <aSer2>0</aSer2>
        <aPa>0</aPa>
        <viewmode>1</viewmode>
        <vrSel>0</vrSel>
        <aSym>0</aSym>
        <aTbox>0</aTbox>
        <AscS1>0</AscS1>
        <AscS2>0</AscS2>
        <AscS3>0</AscS3>
        <aSer3>0</aSer3>
        <eProf>0</eProf>
        <aLa>0</aLa>
        <aPa1>0</aPa1>
        <AscS4>0</AscS4>
        <aSer4>0</aSer4>
        <StkLoc>0</StkLoc>
        <TrcWin>0</TrcWin>
        <newCpu>0</newCpu>
        <uProt>0</uProt>
      </DebugFlag>
      <LintExecutable></LintExecutable>
      <LintConfigFile></LintConfigFile>
      <bLintAuto>0</bLintAuto>
      <bAutoGenD>0</bAutoGenD>
      <LntExFlags>0</LntExFlags>
      <pMisraName></pMisraName>
      <pszMrule></pszMrule>
      <pSingCmds></pSingCmds>
      <pMultCmds></pMultCmds>
      <pMisraNamep></pMisraNamep>
      <pszMrulep></pszMrulep>
      <pSingCmdsp></pSingCmdsp>
      <pMultCmdsp></pMultCmdsp>
      <DebugDescription>
        <Enable>1</Enable>
        <EnableFlashSeq>0</EnableFlashSeq>
        <EnableLog>0</EnableLog>
        <Protocol>2</Protocol>
        <DbgClock>10000000</DbgClock>
      </DebugDescription>
    </TargetOption>
  </Target>

%s

</ProjectOpt>
""" % (TARGET, DEVICE, DEVICE, "\n".join(group_xml))

    out = os.path.join(MDK_DIR, TARGET + ".uvoptx")
    with open(out, "w", encoding="utf-8") as f:
        f.write(xml)
    return out


def gen_sct():
    """Scatter file mirroring sys_startup/STM32F429IGTx_FLASH.ld.

    Region order matters: armlink assigns each input section to the FIRST
    execution region whose selector matches, so the SDRAM region with the
    explicit *(.bss.freertos_heap) selector must precede the .ANY regions --
    otherwise the 512 KB heap would be pulled into the 192 KB SRAM region
    (or overflow it).

    The section is selected as .bss.freertos_heap (see app/sdram_heap.c):
    that name makes ARMCLANG emit it as a ZI / SHT_NOBITS section, so the
    image carries no 512 KB of zeros and __main never copies anything into
    0xC0000000 before the FMC/SDRAM controller is up.  UNINIT then keeps it
    out of the zero-fill pass as well -- together they are the armlink
    equivalent of the NOLOAD attribute in the GCC script.
    """
    xml = """; *************************************************************
; *** Scatter-Loading Description File for %s (Keil MDK-ARM)
; *** Generated by MDK-ARM/gen_uvision.py - do not edit by hand.
; *** Mirrors sys_startup/STM32F429IGTx_FLASH.ld (GCC build).
; *************************************************************

LR_IROM1 0x08000000 0x00100000  {    ; load region size_region
  ER_IROM1 0x08000000 0x00100000  {  ; load address = execution address
   *.o (RESET, +First)
   *(InRoot$$Sections)
   .ANY (+RO)
   .ANY (+XO)
  }
  ; External SDRAM @ 0xC0000000: FreeRTOS heap_5 region (ucHeap, 512 KB,
  ; app/sdram_heap.c).  The section is ZI (.bss.*) and this region is UNINIT,
  ; so __main neither copies nor zero-fills it: the FMC/SDRAM controller is
  ; only initialized inside main(), and touching 0xC0000000 earlier would
  ; HardFault (NOLOAD in the GCC linker script).
  ; NOTE: armlink syntax is <name> <base> <attributes> <max_size> - UNINIT
  ; must come BEFORE the size, otherwise it is parsed as part of the size.
  RW_SDRAM 0xC0000000 UNINIT 0x02000000  {
   ; armlink warns (L6314W) about a selector that matches nothing, so only
   ; the .bss.* spelling ARMCLANG actually emits is listed here.  The GCC
   ; script keeps both names because ld stays silent about the unused one.
   *(.bss.freertos_heap)
  }
  RW_IRAM1 0x20000000 0x00030000  {  ; RW data, 192 KB on-chip SRAM
   .ANY (+RW +ZI)
  }
}
""" % TARGET

    out = os.path.join(MDK_DIR, TARGET + ".sct")
    with open(out, "w", encoding="utf-8") as f:
        f.write(xml)
    return out


def patch_startup():
    """Copy the ARM/ADS startup file into MDK-ARM/.

    No vector-table surgery is needed in this project: app/FreeRTOSConfig.h
    maps the port entry points onto the CMSIS names

        vPortSVCHandler     -> SVC_Handler
        xPortPendSVHandler  -> PendSV_Handler
        xPortSysTickHandler -> SysTick_Handler

    so port.c already exports exactly the symbols the stock vector table
    references, and the strong definition overrides the [WEAK] stub at the
    bottom of this file (same mechanism the GCC build relies on).  Patching
    the DCD lines to vPortSVCHandler & friends would in fact *break* the
    link: those symbols no longer exist once the macros above are expanded.

    This function therefore only verifies that invariant and then copies the
    file, keeping the GCC and Keil builds on a single startup source.
    """
    src = os.path.join(PROJ_ROOT, "sys_startup", "arm", "startup_stm32f429xx.s")
    dst = os.path.join(MDK_DIR, "startup_stm32f429xx.s")
    with open(src, "r", encoding="utf-8") as f:
        text = f.read()

    for expect in (
        "                DCD     SVC_Handler                ; SVCall Handler",
        "                DCD     PendSV_Handler             ; PendSV Handler",
        "                DCD     SysTick_Handler            ; SysTick Handler",
    ):
        if expect not in text:
            raise SystemExit(
                "ERROR: startup vector line changed; FreeRTOSConfig.h maps the "
                "port entry points onto the CMSIS handler names, so this "
                "vector table must keep them:\n  %s" % expect)

    with open(dst, "w", encoding="utf-8") as f:
        f.write(text)
    return dst


if __name__ == "__main__":
    verify_file_lists()
    p1 = gen_uvprojx()
    p2 = gen_uvoptx()
    p3 = gen_sct()
    p4 = patch_startup()
    total = sum(len(files) for _, files in _all_groups())
    print("Wrote:", p1)
    print("Wrote:", p2)
    print("Wrote:", p3)
    print("Wrote:", p4)
    print("Groups: %d  Files: %d" % (len(_all_groups()), total))
