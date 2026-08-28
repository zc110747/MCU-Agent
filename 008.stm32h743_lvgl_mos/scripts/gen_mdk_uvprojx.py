#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Generate Keil MDK-ARM project files (stm32h743.uvprojx / .uvoptx + scatter file)
for the STM32H743ZIT6 NES+LVGL firmware, faithfully mirroring CMakeLists.txt.

Design decisions (see task brief):
  * Startup: sys_startup/arm/startup_stm32h743xx.s  (NOT the gcc one)
  * syscalls.c is GCC/newlib-specific  -> EXCLUDED (Keil ARMCLANG does not need it)
  * GNU linker flag --no-warn-rwx-segments -> NOT emitted (Keil does not support it)
  * C library: MicroLIB (matches the ST startup's __MICROLIB branch; avoids the
    armclang-incompatible __use_two_region_memory reference of the non-MicroLIB path)
  * All build output/listing dirs live under MDK-ARM/  (Debug / Release) so no
    other directory in the tree is polluted.
  * A scatter file STM32H743ZITX.sct mirrors sys_startup/STM32H743ZITX_FLASH.ld:
    FLASH + RAM_D1(.data/.bss) + RAM_D3(usb_ram, UNINIT). DTCM and RAM_D2 are
    intentionally left OUT (carved at runtime by bsp/sram_pool.c).

Run from the project root.  Output:  MDK-ARM/
"""
import os
import glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # project root
MDK = os.path.join(ROOT, "MDK-ARM")

# ---------------------------------------------------------------------------
# Source sets (mirrors CMakeLists.txt PROJECT_SRCS / * _SRCS)
# ---------------------------------------------------------------------------
STARTUP = "sys_startup/arm/startup_stm32h743xx.s"

PROJECT_SRCS = [
    "sys_startup/system_stm32h7xx.c",
    "Core/Src/main.c",
    "Core/Src/stm32h7xx_hal_msp.c",
    "Core/Src/stm32h7xx_it.c",
    # syscalls.c EXCLUDED on purpose (GCC/newlib only)
    "bsp/drv_spi_oled.c",
    "bsp/drv_oled_fonts.c",
    "bsp/drv_oled_text.c",
    "bsp/drv_sdio.c",
    "bsp/drv_rtc.c",
    "bsp/disk_interface.c",
    "bsp/sram_pool.c",
    "bsp/jpeg_enc.c",
    "bsp/screen_cap.c",
    "bsp/lv_port_disp.c",
    "bsp/lv_font_gbk.c",
    "bsp/lv_gbk_map.c",
    "bsp/bsp_console.c",
    "bsp/bsp_key.c",
    "bsp/drv_usb_cdc.c",
    "bsp/usb_descriptors.c",
    "bsp/gbk_conv.c",
    "bsp/gbk_unicode_tbl.c",
    "bsp/drv_camera_ov5640.c",
    "bsp/drv_camera.c",
    "app/app_main.c",
    "app/app_menu.c",
    "app/app_pages.c",
    "app/menu_icons.c",
    "app/sd_browser.c",
    "app/page_nes.c",
    "app/img_decode.c",
    "app/page_image.c",
    "app/page_txt.c",
    "app/page_camera.c",
    "app/app_cmd.c",
]

TINYUSB_SRCS = [
    "third_party/tinyusb/src/tusb.c",
    "third_party/tinyusb/src/common/tusb_fifo.c",
    "third_party/tinyusb/src/device/usbd.c",
    "third_party/tinyusb/src/class/cdc/cdc_device.c",
    "third_party/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c",
    "third_party/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c",
]

NES_SRCS = [
    "third_party/nes/nes_cpu.c",
    "third_party/nes/nes_ppu.c",
    "third_party/nes/nes_bus.c",
    "third_party/nes/nes_mapper.c",
    "third_party/nes/nes_main.c",
]

TJPGD_SRCS = ["third_party/tjpgd/tjpgd.c"]

FATFS_SRCS = [
    "third_party/FatFs/diskio.c",
    "third_party/FatFs/ff.c",
    "third_party/FatFs/ffsystem.c",
    "third_party/FatFs/ffunicode.c",
]

def glob_hal():
    files = glob.glob(os.path.join(ROOT, "Drivers/STM32H7xx_HAL_Driver/Src/*.c"))
    out = []
    for f in files:
        base = os.path.basename(f)
        if base.endswith("_template.c"):
            continue
        out.append(os.path.relpath(f, ROOT).replace("\\", "/"))
    out.sort()
    return out

def glob_lvgl():
    files = glob.glob(os.path.join(ROOT, "third_party/lvgl/src/**/*.c"), recursive=True)
    out = [os.path.relpath(f, ROOT).replace("\\", "/") for f in files]
    out.sort()
    return out

HAL_SRCS = glob_hal()
LVGL_SRCS = glob_lvgl()

# ---------------------------------------------------------------------------
# Group assignment
# ---------------------------------------------------------------------------
def group_of(rel):
    if rel == STARTUP:
        return "Startup"
    if rel == "sys_startup/system_stm32h7xx.c":
        return "CMSIS"
    if rel.startswith("Core/"):
        return "Application/Core"
    if rel.startswith("bsp/"):
        return "Board Support (bsp)"
    if rel.startswith("app/"):
        return "Application (app)"
    if rel.startswith("Drivers/STM32H7xx_HAL_Driver/"):
        return "Drivers/STM32H7xx_HAL_Driver"
    if rel.startswith("third_party/lvgl/"):
        return "ThirdParty/LVGL"
    if rel.startswith("third_party/tinyusb/"):
        return "ThirdParty/TinyUSB"
    if rel.startswith("third_party/nes/"):
        return "ThirdParty/NES"
    if rel.startswith("third_party/tjpgd/"):
        return "ThirdParty/TJpgDec"
    if rel.startswith("third_party/FatFs/"):
        return "ThirdParty/FatFs"
    return "Misc"

def file_type(rel):
    return 2 if rel.endswith(".s") else 1   # 2=asm, 1=c

ALL_SOURCES = ([STARTUP] + PROJECT_SRCS + HAL_SRCS + LVGL_SRCS +
               TINYUSB_SRCS + NES_SRCS + TJPGD_SRCS + FATFS_SRCS)

# Validate existence
missing = [s for s in ALL_SOURCES if not os.path.exists(os.path.join(ROOT, s))]
if missing:
    print("WARNING: following files are referenced but MISSING (will be excluded):")
    for m in missing:
        print("   ", m)
    ALL_SOURCES = [s for s in ALL_SOURCES if s not in set(missing)]

# Build groups dict: name -> list of rel paths (in stable order)
groups = {}
for s in ALL_SOURCES:
    groups.setdefault(group_of(s), []).append(s)

# ---------------------------------------------------------------------------
# Include paths (relative to MDK-ARM) + defines (mirror CMakeLists)
# ---------------------------------------------------------------------------
INCLUDE_PATHS = [
    "../Core/Inc",
    "../app",
    "../bsp",
    "../Drivers/CMSIS/Include",
    "../Drivers/CMSIS/Core/Include",
    "../sys_startup",
    "../Drivers/STM32H7xx_HAL_Driver/Inc",
    "../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy",
    "../third_party/FatFs",
    "../third_party/lvgl",
    "../third_party/lvgl/src",
    "../third_party/nes",
    "../third_party/tinyusb/src",
    "../third_party/tjpgd",
]
DEFINES = [
    "STM32H743xx",
    "USE_HAL_DRIVER",
    "HSE_VALUE=25000000",
    "CFG_TUSB_MCU=OPT_MCU_STM32H7",
    "CFG_TUSB_OS=OPT_OS_NONE",
]

# Keep only include dirs that actually exist (avoid Keil "cannot find dir" noise)
INC_VALID = []
for inc in INCLUDE_PATHS:
    absp = os.path.normpath(os.path.join(MDK, inc))
    if os.path.isdir(absp):
        INC_VALID.append(inc)
    else:
        print("WARNING: include dir does not exist, dropped:", inc)

INCLUDE_STR = ";".join(INC_VALID)
DEFINE_STR = ",".join(DEFINES)

# ---------------------------------------------------------------------------
# Scatter file (mirror STM32H743ZITX_FLASH.ld, but Keil/ARMCLANG style)
# ---------------------------------------------------------------------------
SCATTER = """;********************************************************************************
;* Scatter file for STM32H743ZIT6  (generated to mirror STM32H743ZITX_FLASH.ld)
;*
;*   FLASH   : 0x08000000  2048 KB  (code + RO)
;*   RAM_D1  : 0x24000000   512 KB  (.data / .bss / heap / stack)
;*   RAM_D3  : 0x38000000    64 KB  (usb_ram, UNINIT -> USB OTG DMA buffers,
;*                                    mapped NON-CACHEABLE by MPU Region 2)
;*
;*   DTCM (0x20000000) and RAM_D2 (0x30000000) are DELIBERATELY absent: they are
;*   carved at runtime by bsp/sram_pool.c and must not be reserved by the linker.
;********************************************************************************
LR_IROM1 0x08000000 0x00200000 {
  ER_IROM1 0x08000000 0x00200000 {
    *.o (RESET, +First)
    *(InRoot$$Sections)
    .ANY (+RO)
    .ANY (+XO)
  }

  RW_IRAM1 0x24000000 0x00080000 {
    .ANY (+RW +ZI)
  }

  ; USB / TinyUSB DMA buffers (OTG internal DMA writes here directly).
  ; UNINIT so the startup code does not zero / touch them.
  RW_IRAM3 0x38000000 UNINIT 0x00010000 {
    *(.usb_ram, +RW)
    *(.usb_ram*)
  }
}
"""

# ---------------------------------------------------------------------------
# Reusable XML fragments (copied from the provided template, adapted)
# ---------------------------------------------------------------------------
def target_common_option(out_dir, listing_dir):
    return f"""        <TargetCommonOption>
          <Device>STM32H743ZITx</Device>
          <Vendor>STMicroelectronics</Vendor>
          <PackID>Keil.STM32H7xx_DFP.4.1.3</PackID>
          <PackURL>https://www.keil.com/pack/</PackURL>
          <Cpu>IRAM(0x20000000-0x2001FFFF) IRAM2(0x24000000-0x2407FFFF) IROM(0x8000000-0x81FFFFF) CLOCK(25000000) FPU3(DFPU) CPUTYPE("Cortex-M7") ELITTLE TZ</Cpu>
          <FlashUtilSpec></FlashUtilSpec>
          <StartupFile></StartupFile>
          <FlashDriverDll></FlashDriverDll>
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
          <SFDFile>$$Device:STM32H743ZITx$CMSIS\\SVD\\STM32H743.svd</SFDFile>
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
            <InvalidFlash>0</InvalidFlash>
          </TargetStatus>
          <OutputDirectory>{out_dir}</OutputDirectory>
          <OutputName>nes_h743</OutputName>
          <CreateExecutable>1</CreateExecutable>
          <CreateLib>0</CreateLib>
          <CreateHexFile>1</CreateHexFile>
          <DebugInformation>1</DebugInformation>
          <BrowseInformation>1</BrowseInformation>
          <ListingPath>{listing_dir}</ListingPath>
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
            <RunUserProg2>0</RunUserProg2>
            <UserProg1Name></UserProg1Name>
            <UserProg2Name></UserProg2Name>
            <UserProg1Dos16Mode>0</UserProg1Dos16Mode>
            <UserProg2Dos16Mode>0</UserProg2Dos16Mode>
            <nStopA1X>0</nStopA1X>
            <nStopA2X>0</nStopA2X>
          </AfterMake>
          <SelectedForBatchBuild>1</SelectedForBatchBuild>
          <SVCSIdString></SVCSIdString>
        </TargetCommonOption>"""

COMMON_PROPERTY = """        <CommonProperty>
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
        </CommonProperty>"""

DLL_OPTION = """        <DllOption>
          <SimDllName>SARMCM3.DLL</SimDllName>
          <SimDllArguments>-REMAP -MPU</SimDllArguments>
          <SimDlgDll>DCM.DLL</SimDlgDll>
          <SimDlgDllArguments>-pCM7</SimDlgDllArguments>
          <TargetDllName>SARMCM3.DLL</TargetDllName>
          <TargetDllArguments>-MPU</TargetDllArguments>
          <TargetDlgDll>TCM.DLL</TargetDlgDll>
          <TargetDlgDllArguments>-pCM7</TargetDlgDllArguments>
        </DllOption>"""

DEBUG_OPTION = """        <DebugOption>
          <OPTHX>
            <HexSelection>1</HexSelection>
            <HexRangeLowAddress>0</HexRangeLowAddress>
            <HexRangeHighAddress>0</HexRangeHighAddress>
            <HexOffset>0</HexOffset>
            <Oh166RecLen>16</Oh166RecLen>
          </OPTHX>
        </DebugOption>"""

UTILITIES = """        <Utilities>
          <Flash1>
            <UseTargetDll>1</UseTargetDll>
            <UseExternalTool>0</UseExternalTool>
            <RunIndependent>0</RunIndependent>
            <UpdateFlashBeforeDebugging>1</UpdateFlashBeforeDebugging>
            <Capability>1</Capability>
            <DriverSelection>4101</DriverSelection>
          </Flash1>
          <bUseTDR>1</bUseTDR>
          <Flash2>BIN\\UL2CM3.DLL</Flash2>
          <Flash3></Flash3>
          <Flash4></Flash4>
          <pFcarmOut></pFcarmOut>
          <pFcarmGrp></pFcarmGrp>
          <pFcArmRoot></pFcArmRoot>
          <FcArmLst>0</FcArmLst>
        </Utilities>"""

ARMADS_MISC = """          <ArmAdsMisc>
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
            <AdsCpuType>"Cortex-M7"</AdsCpuType>
            <RvctDeviceName></RvctDeviceName>
            <mOS>0</mOS>
            <uocRom>0</uocRom>
            <uocRam>0</uocRam>
            <hadIROM>1</hadIROM>
            <hadIRAM>1</hadIRAM>
            <hadXRAM>0</hadXRAM>
            <uocXRam>0</uocXRam>
            <RvdsVP>3</RvdsVP>
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
              <Ocm1><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></Ocm1>
              <Ocm2><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></Ocm2>
              <Ocm3><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></Ocm3>
              <Ocm4><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></Ocm4>
              <Ocm5><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></Ocm5>
              <Ocm6><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></Ocm6>
              <IRAM><Type>0</Type><StartAddress>0x20000000</StartAddress><Size>0x20000</Size></IRAM>
              <IROM><Type>1</Type><StartAddress>0x8000000</StartAddress><Size>0x200000</Size></IROM>
              <XRAM><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></XRAM>
              <OCR_RVCT1><Type>1</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></OCR_RVCT1>
              <OCR_RVCT2><Type>1</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></OCR_RVCT2>
              <OCR_RVCT3><Type>1</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></OCR_RVCT3>
              <OCR_RVCT4><Type>1</Type><StartAddress>0x8000000</StartAddress><Size>0x200000</Size></OCR_RVCT4>
              <OCR_RVCT5><Type>1</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></OCR_RVCT5>
              <OCR_RVCT6><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></OCR_RVCT6>
              <OCR_RVCT7><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></OCR_RVCT7>
              <OCR_RVCT8><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></OCR_RVCT8>
              <OCR_RVCT9><Type>0</Type><StartAddress>0x20000000</StartAddress><Size>0x20000</Size></OCR_RVCT9>
              <OCR_RVCT10><Type>0</Type><StartAddress>0x24000000</StartAddress><Size>0x80000</Size></OCR_RVCT10>
            </OnChipMemories>
            <RvctStartVector></RvctStartVector>
          </ArmAdsMisc>"""

def cads(optim):
    return f"""          <Cads>
            <interw>1</interw>
            <Optim>{optim}</Optim>
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
            <v6Rtti>0</v6Rtti>
            <VariousControls>
              <MiscControls></MiscControls>
              <Define>{DEFINE_STR}</Define>
              <Undefine></Undefine>
              <IncludePath>{INCLUDE_STR}</IncludePath>
            </VariousControls>
          </Cads>"""

AADS = """          <Aads>
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
          </Aads>"""

# note: useFile=1 -> explicit scatter file; Misc is empty (NO --no-warn-rwx-segments)
LDADS = """          <LDads>
            <umfTarg>1</umfTarg>
            <Ropi>0</Ropi>
            <Rwpi>0</Rwpi>
            <noStLib>0</noStLib>
            <RepFail>1</RepFail>
            <useFile>1</useFile>
            <TextAddressRange></TextAddressRange>
            <DataAddressRange></DataAddressRange>
            <pXoBase></pXoBase>
            <ScatterFile>.\\STM32H743ZITX.sct</ScatterFile>
            <IncludeLibs></IncludeLibs>
            <IncludeLibsPath></IncludeLibsPath>
            <Misc></Misc>
            <LinkerInputFile></LinkerInputFile>
            <DisabledWarnings></DisabledWarnings>
          </LDads>"""

def target_arm_ads(optim):
    return f"""        <TargetArmAds>
{ARMADS_MISC}
{cads(optim)}
{AADS}
{LDADS}
        </TargetArmAds>"""

def groups_xml():
    lines = ["      <Groups>"]
    for gname, files in groups.items():
        lines.append(f'        <Group>')
        lines.append(f'          <GroupName>{gname}</GroupName>')
        lines.append(f'          <Files>')
        for f in files:
            fn = os.path.basename(f)
            ft = file_type(f)
            fp = "../" + f   # relative to MDK-ARM
            lines.append(f'            <File>')
            lines.append(f'              <FileName>{fn}</FileName>')
            lines.append(f'              <FileType>{ft}</FileType>')
            lines.append(f'              <FilePath>{fp}</FilePath>')
            lines.append(f'            </File>')
        lines.append(f'          </Files>')
        lines.append(f'        </Group>')
    lines.append("      </Groups>")
    return "\n".join(lines)

def target_xml(name, optim, out_dir, listing_dir, is_current):
    return f"""    <Target>
      <TargetName>{name}</TargetName>
      <ToolsetNumber>0x4</ToolsetNumber>
      <ToolsetName>ARM-ADS</ToolsetName>
      <pCCUsed>6140000::V6.14::ARMCLANG</pCCUsed>
      <uAC6>1</uAC6>
      <TargetOption>
{target_common_option(out_dir, listing_dir)}
{COMMON_PROPERTY}
{DLL_OPTION}
{DEBUG_OPTION}
{UTILITIES}
{target_arm_ads(optim)}
      </TargetOption>
{groups_xml()}
    </Target>"""

# Build the two targets (Debug = -O0, Release = -O2)
TARGETS = [
    ("Debug",   0, "Debug\\",   "Debug\\",   1),
    ("Release", 2, "Release\\", "Release\\", 0),
]

uvprojx = []
uvprojx.append('<?xml version="1.0" encoding="UTF-8" standalone="no" ?>')
uvprojx.append('<Project xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="project_projx.xsd">')
uvprojx.append("")
uvprojx.append('  <SchemaVersion>2.1</SchemaVersion>')
uvprojx.append("")
uvprojx.append("  <Header>### uVision Project, (C) Keil Software</Header>")
uvprojx.append("")
uvprojx.append("  <Targets>")
for (name, optim, out_dir, listing_dir, is_current) in TARGETS:
    uvprojx.append(target_xml(name, optim, out_dir, listing_dir, is_current))
uvprojx.append("  </Targets>")
uvprojx.append("")
uvprojx.append("</Project>")

# ---------------------------------------------------------------------------
# .uvoptx  (debug / flash configuration, ST-Link, mirrors template target #1)
# ---------------------------------------------------------------------------
def target_opt(name, listing_dir, is_current):
    return f"""  <Target>
    <TargetName>{name}</TargetName>
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
        <ListingPath>{listing_dir}</ListingPath>
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
        <IsCurrentTarget>{is_current}</IsCurrentTarget>
      </OPTFL>
      <CpuCode>18</CpuCode>
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
          <Key>ARMRTXEVENTFLAGS</Key>
          <Name>-L70 -Z18 -C0 -M0 -T1</Name>
        </SetRegEntry>
        <SetRegEntry>
          <Number>0</Number>
          <Key>DLGTARM</Key>
          <Name>(1010=-1,-1,-1,-1,0)(6017=-1,-1,-1,-1,0)(1008=-1,-1,-1,-1,0)(6016=-1,-1,-1,-1,0)(1012=-1,-1,-1,-1,0)</Name>
        </SetRegEntry>
        <SetRegEntry>
          <Number>0</Number>
          <Key>ARMDBGFLAGS</Key>
          <Name></Name>
        </SetRegEntry>
        <SetRegEntry>
          <Number>0</Number>
          <Key>DLGUARM</Key>
          <Name>(105=-1,-1,-1,-1,0)</Name>
        </SetRegEntry>
        <SetRegEntry>
          <Number>0</Number>
          <Key>ST-LINKIII-KEIL_SWO</Key>
          <Name>-U00160038310000154E593053 -O206 -SF5000 -C0 -A0 -I0 -HNlocalhost -HP7184 -P1 -N00("ARM CoreSight SW-DP (ARM Core") -D00(6BA02477) -L00(0) -TO131090 -TC10000000 -TT10000000 -TP21 -TDS8007 -TDT0 -TDC1F -TIEFFFFFFFF -TIP8 -FO7 -FD20000000 -FC8000 -FN1 -FF0STM32H7x_2048.FLM -FS08000000 -FL0200000 -FP0($$Device:STM32H743ZITx$CMSIS\\Flash\\STM32H7x_2048.FLM)</Name>
        </SetRegEntry>
        <SetRegEntry>
          <Number>0</Number>
          <Key>UL2CM3</Key>
          <Name>UL2CM3(-S0 -C0 -P0 -FD20000000 -FC8000 -FN1 -FF0STM32H7x_2048 -FS08000000 -FL0200000 -FP0($$Device:STM32H743ZITx$CMSIS\\Flash\\STM32H7x_2048.FLM))</Name>
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
        <DbgClock>5000000</DbgClock>
      </DebugDescription>
    </TargetOption>
  </Target>"""

uvoptx = []
uvoptx.append('<?xml version="1.0" encoding="UTF-8" standalone="no" ?>')
uvoptx.append('<ProjectOpt xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="project_optx.xsd">')
uvoptx.append("")
uvoptx.append("  <SchemaVersion>1.0</SchemaVersion>")
uvoptx.append("")
uvoptx.append("  <Header>### uVision Project, (C) Keil Software</Header>")
uvoptx.append("")
uvoptx.append("  <Extensions>")
uvoptx.append("    <cExt>*.c</cExt>")
uvoptx.append("    <aExt>*.s*; *.src; *.a*</aExt>")
uvoptx.append("    <oExt>*.obj; *.o</oExt>")
uvoptx.append("    <lExt>*.lib</lExt>")
uvoptx.append("    <tExt>*.txt; *.h; *.inc; *.md</tExt>")
uvoptx.append("    <pExt>*.plm</pExt>")
uvoptx.append("    <CppX>*.cpp</CppX>")
uvoptx.append("    <nMigrate>0</nMigrate>")
uvoptx.append("  </Extensions>")
uvoptx.append("")
uvoptx.append("  <DaveTm>")
uvoptx.append("    <dwLowDateTime>0</dwLowDateTime>")
uvoptx.append("    <dwHighDateTime>0</dwHighDateTime>")
uvoptx.append("  </DaveTm>")
uvoptx.append("")
for (name, optim, out_dir, listing_dir, is_current) in TARGETS:
    uvoptx.append(target_opt(name, listing_dir, is_current))
uvoptx.append("")
uvoptx.append("</ProjectOpt>")

# ---------------------------------------------------------------------------
# Write outputs
# ---------------------------------------------------------------------------
os.makedirs(MDK, exist_ok=True)
with open(os.path.join(MDK, "stm32h743.uvprojx"), "w", encoding="utf-8") as f:
    f.write("\n".join(uvprojx))
with open(os.path.join(MDK, "stm32h743.uvoptx"), "w", encoding="utf-8") as f:
    f.write("\n".join(uvoptx))
with open(os.path.join(MDK, "STM32H743ZITX.sct"), "w", encoding="utf-8") as f:
    f.write(SCATTER)

# Report
print("Generated MDK-ARM project files:")
print("  MDK-ARM/stm32h743.uvprojx")
print("  MDK-ARM/stm32h743.uvoptx")
print("  MDK-ARM/STM32H743ZITX.sct")
print()
print("Groups / file counts:")
for g, fs in groups.items():
    print(f"  {g:35s} {len(fs)}")
print(f"  TOTAL files: {len(ALL_SOURCES)}")
print()
print("Defines:", DEFINE_STR)
print("Include dirs:", len(INC_VALID), "of", len(INCLUDE_PATHS))
