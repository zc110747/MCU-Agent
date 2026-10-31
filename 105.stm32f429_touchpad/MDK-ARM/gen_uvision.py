#!/usr/bin/env python3
"""Generate the MDK-ARM (Keil uVision) project for 105.stm32f429_touchpad.

Everything Keil needs lives in this folder, the same layout as 101/102:
the .uvprojx, the .uvoptx, the scatter file and this generator.  The file list
follows exactly the same rules as ../CMakeLists.txt, so adding a .c under app/
or bsp/ only needs a re-run instead of hand-editing 260+ XML nodes:

    python MDK-ARM/gen_uvision.py            # (re)write MDK-ARM/stm32f429_touchpad.uvprojx
    python MDK-ARM/gen_uvision.py --check    # exit 1 if the file is stale

Paths inside the project are relative to this folder (..\\app, ..\\bsp, ...),
which is why uVision must be opened on MDK-ARM/stm32f429_touchpad.uvprojx and
not from the project root.  Build output goes to MDK-ARM/Objects/<target>/.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROJECT = "stm32f429_touchpad"
# The project lives next to this script, so every path inside it is one level
# below ROOT (see rel() and INCLUDES).
MDK = Path(__file__).resolve().parent
UVPROJX = MDK / f"{PROJECT}.uvprojx"
SCT = f".\\{PROJECT}.sct"

DEVICE = "STM32F429IGTx"
VENDOR = "STMicroelectronics"
PACK_ID = "Keil.STM32F4xx_DFP.2.14.0"

CPU = ('IRAM(0x20000000,0x00030000) IROM(0x08000000,0x00100000) '
       'CPUTYPE("Cortex-M4") FPU2 CLOCK(25000000) ELITTLE')

FLASH_DRIVER = (
    "UL2CM3(-S0 -C0 -P0 -FD20000000 -FC1000 -FN1 -FF0STM32F4xx_1024 "
    "-FS08000000 -FL0100000 -FP0($$Device:STM32F429IGTx$CMSIS\\Flash\\STM32F4xx_1024.FLM))"
)

# No extra compiler switches are needed: <RvdsVP> in the target options already
# selects the FPU.  The value that means "Single Precision" for ARM Compiler 6
# is 2 (1/3/4 all leave the FPU off, which makes the FreeRTOS ARM_CM4F port
# fail its __VFP_FP__ guard) -- verified by building each value.
MISC = ""

DEFINES = ",".join([
    "STM32F429xx",
    "USE_HAL_DRIVER",
    "CFG_TUSB_MCU=OPT_MCU_STM32F4",
    "CFG_TUSB_OS=OPT_OS_FREERTOS",
    "PRINT_LOG_ENABLE=1",
])

INCLUDES = ";".join([
    "..\\app",
    "..\\bsp",
    "..\\sys_startup",
    "..\\Drivers\\CMSIS\\Include",
    "..\\Drivers\\STM32F4xx_HAL_Driver\\Inc",
    "..\\third_party\\FreeRTOS-Kernel\\include",
    "..\\third_party\\FreeRTOS-Kernel\\portable\\GCC\\ARM_CM4F",
    "..\\third_party\\Fatfs",
    "..\\third_party\\lvgl",
    "..\\third_party\\lvgl\\src",
    "..\\third_party\\tinyusb\\src",
])

# Same sources CMakeLists.txt links, in the same order.  Kept as an explicit
# list on purpose: a silent "compile everything under Drivers/" would pull in
# HAL modules whose *_MODULE_ENABLED is off in stm32f4xx_hal_conf.h.
HAL_SOURCES = [
    "stm32f4xx_hal.c",
    "stm32f4xx_hal_cortex.c",
    "stm32f4xx_hal_rcc.c",
    "stm32f4xx_hal_rcc_ex.c",
    "stm32f4xx_hal_gpio.c",
    "stm32f4xx_hal_exti.c",
    "stm32f4xx_hal_uart.c",
    "stm32f4xx_hal_dma.c",
    "stm32f4xx_hal_pwr.c",
    "stm32f4xx_hal_pwr_ex.c",
    "stm32f4xx_hal_flash.c",
    # Required even though GCC drops it: hal_flash.c references
    # FLASH_Erase_Sector()/FLASH_FlushCaches() from this file, and armlink
    # resolves symbols before removing unused sections.
    "stm32f4xx_hal_flash_ex.c",
    "stm32f4xx_hal_tim.c",
    "stm32f4xx_hal_tim_ex.c",
    "stm32f4xx_hal_sdram.c",
    "stm32f4xx_hal_sram.c",
    "stm32f4xx_ll_fmc.c",
    "stm32f4xx_hal_sd.c",
    "stm32f4xx_ll_sdmmc.c",
]

FREERTOS_SOURCES = [
    "third_party/FreeRTOS-Kernel/tasks.c",
    "third_party/FreeRTOS-Kernel/queue.c",
    "third_party/FreeRTOS-Kernel/list.c",
    "third_party/FreeRTOS-Kernel/timers.c",
    "third_party/FreeRTOS-Kernel/event_groups.c",
    "third_party/FreeRTOS-Kernel/stream_buffer.c",
    # ARM Compiler 6 is clang based: it takes the GNU port (inline asm), not
    # RVDS/ARM_CM4F (armcc embedded assembler, rejected by armclang).
    "third_party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.c",
    "third_party/FreeRTOS-Kernel/portable/MemMang/heap_5.c",
]

FATFS_SOURCES = [
    "third_party/Fatfs/ff.c",
    "third_party/Fatfs/ffunicode.c",
    "third_party/Fatfs/ffsystem.c",
]

TINYUSB_SOURCES = [
    "third_party/tinyusb/src/tusb.c",
    "third_party/tinyusb/src/common/tusb_fifo.c",
    "third_party/tinyusb/src/device/usbd.c",
    "third_party/tinyusb/src/class/hid/hid_device.c",
    "third_party/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c",
    "third_party/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c",
]


def rel(p: Path) -> str:
    """Path from MDK-ARM/ to p, in the backslash form uVision expects."""
    return "..\\" + str(p.relative_to(ROOT)).replace("/", "\\")


def collect() -> list[tuple[str, list[tuple[str, str, int]]]]:
    """Build the group list: [(group name, [(name, path, file type)])]."""
    groups: list[tuple[str, list[tuple[str, str, int]]]] = []

    for name, folder in (("App", ROOT / "app"), ("BSP", ROOT / "bsp")):
        files = [(p.name, rel(p), 1) for p in sorted(folder.glob("*.c"))]
        groups.append((name, files))

    groups.append(("CMSIS / Startup", [
        ("system_stm32f4xx.c", rel(ROOT / "sys_startup/system_stm32f4xx.c"), 1),
        ("startup_stm32f429xx.s", rel(ROOT / "sys_startup/arm/startup_stm32f429xx.s"), 2),
    ]))

    groups.append(("HAL", [
        (n, rel(ROOT / "Drivers/STM32F4xx_HAL_Driver/Src" / n), 1) for n in HAL_SOURCES
    ]))

    groups.append(("FreeRTOS", [(Path(s).name, rel(ROOT / s), 1) for s in FREERTOS_SOURCES]))
    groups.append(("FatFs", [(Path(s).name, rel(ROOT / s), 1) for s in FATFS_SOURCES]))

    lvgl = sorted((ROOT / "third_party/lvgl/src").rglob("*.c"))
    groups.append(("LVGL", [(p.name, rel(p), 1) for p in lvgl]))

    groups.append(("TinyUSB", [(Path(s).name, rel(ROOT / s), 1) for s in TINYUSB_SOURCES]))
    return groups


def groups_xml(groups) -> str:
    out = ["      <Groups>"]
    for name, files in groups:
        out.append("        <Group>")
        out.append(f"          <GroupName>{name}</GroupName>")
        for fname, path, ftype in files:
            out.append("          <Files>")
            out.append("            <File>")
            out.append(f"              <FileName>{fname}</FileName>")
            out.append(f"              <FileType>{ftype}</FileType>")
            out.append(f"              <FilePath>{path}</FilePath>")
            out.append("            </File>")
            out.append("          </Files>")
        out.append("        </Group>")
    out.append("      </Groups>")
    return "\n".join(out)


TARGET_TMPL = """    <Target>
      <TargetName>{name}</TargetName>
      <ToolsetNumber>0x4</ToolsetNumber>
      <ToolsetName>ARM-ADS</ToolsetName>
      <pCCUsed>6060016::V6.16::ARMCLANG</pCCUsed>
      <uAC6>1</uAC6>
      <TargetOption>
        <TargetCommonOption>
          <Device>{device}</Device>
          <Vendor>{vendor}</Vendor>
          <PackID>{pack}</PackID>
          <PackURL>https://www.keil.com/pack/</PackURL>
          <Cpu>{cpu}</Cpu>
          <FlashUtilSpec />
          <StartupFile />
          <FlashDriverDll>{flash_driver}</FlashDriverDll>
          <DeviceId>0</DeviceId>
          <RegisterFile>$$Device:{device}$Drivers\\CMSIS\\Device\\ST\\STM32F4xx\\Include\\stm32f4xx.h</RegisterFile>
          <MemoryEnv />
          <Cmp />
          <Asm />
          <Linker />
          <OHString />
          <InfinionOptionDll />
          <SLE66CMisc />
          <SLE66AMisc />
          <SLE66LinkerMisc />
          <SFDFile>$$Device:{device}$CMSIS\\SVD\\STM32F429.svd</SFDFile>
          <bCustSvd>0</bCustSvd>
          <UseEnv>0</UseEnv>
          <BinPath />
          <IncludePath />
          <LibPath />
          <RegisterFilePath />
          <DBRegisterFilePath />
          <TargetStatus>
            <Error>0</Error>
            <ExitCodeStop>0</ExitCodeStop>
            <ButtonStop>0</ButtonStop>
            <NotGenerated>0</NotGenerated>
            <InvalidFlash>1</InvalidFlash>
          </TargetStatus>
          <OutputDirectory>.\\Objects\\{name}\\</OutputDirectory>
          <OutputName>{project}</OutputName>
          <CreateExecutable>1</CreateExecutable>
          <CreateLib>0</CreateLib>
          <CreateHexFile>1</CreateHexFile>
          <DebugInformation>{debug_info}</DebugInformation>
          <BrowseInformation>1</BrowseInformation>
          <ListingPath>.\\Objects\\{name}\\</ListingPath>
          <HexFormatSelection>1</HexFormatSelection>
          <Merge32K>0</Merge32K>
          <CreateBatchFile>0</CreateBatchFile>
          <BeforeCompile>
            <RunUserProg1>0</RunUserProg1>
            <RunUserProg2>0</RunUserProg2>
            <UserProg1Name />
            <UserProg2Name />
            <UserProg1Dos16Mode>0</UserProg1Dos16Mode>
            <UserProg2Dos16Mode>0</UserProg2Dos16Mode>
            <nStopU1X>0</nStopU1X>
            <nStopU2X>0</nStopU2X>
          </BeforeCompile>
          <BeforeMake>
            <RunUserProg1>0</RunUserProg1>
            <RunUserProg2>0</RunUserProg2>
            <UserProg1Name />
            <UserProg2Name />
            <UserProg1Dos16Mode>0</UserProg1Dos16Mode>
            <UserProg2Dos16Mode>0</UserProg2Dos16Mode>
            <nStopB1X>0</nStopB1X>
            <nStopB2X>0</nStopB2X>
          </BeforeMake>
          <AfterMake>
            <RunUserProg1>1</RunUserProg1>
            <RunUserProg2>0</RunUserProg2>
            <UserProg1Name>fromelf --bin -o "$L@L.bin" "#L"</UserProg1Name>
            <UserProg2Name />
            <UserProg1Dos16Mode>0</UserProg1Dos16Mode>
            <UserProg2Dos16Mode>0</UserProg2Dos16Mode>
            <nStopA1X>2</nStopA1X>
            <nStopA2X>0</nStopA2X>
          </AfterMake>
          <SelectedForBatchBuild>0</SelectedForBatchBuild>
          <SVCSIdString />
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
          <CustomArgument />
          <IncludeLibraryModules />
          <ComprImg>1</ComprImg>
        </CommonProperty>
        <DllOption>
          <SimDllName />
          <SimDllArguments />
          <SimDlgDll />
          <SimDlgDllArguments />
          <TargetDllName>SARMCM3.DLL</TargetDllName>
          <TargetDllArguments />
          <TargetDlgDll>DARMSTM.DLL</TargetDlgDll>
          <TargetDlgDllArguments>-pSTM32F429IG</TargetDlgDllArguments>
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
            <DriverSelection>4100</DriverSelection>
          </Flash1>
          <bUseTDR>1</bUseTDR>
          <Flash2>STLink\\ST-LINKIII-KEIL_SWO.dll</Flash2>
          <Flash3>"" ()</Flash3>
          <Flash4 />
          <pFcarmOut />
          <pFcarmGrp />
          <pFcArmRoot />
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
            <RvctDeviceName />
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
            <nBranchProt>0</nBranchProt>
            <hadIRAM2>0</hadIRAM2>
            <hadIROM2>0</hadIROM2>
            <StupSel>8</StupSel>
            <useUlib>1</useUlib>
            <EndSel>0</EndSel>
            <uLtcg>0</uLtcg>
            <nSecure>0</nSecure>
            <RoSelD>3</RoSelD>
            <RwSelD>3</RwSelD>
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
            <Ra1Chk>1</Ra1Chk>
            <Ra2Chk>0</Ra2Chk>
            <Ra3Chk>0</Ra3Chk>
            <Im1Chk>1</Im1Chk>
            <Im2Chk>0</Im2Chk>
            <OnChipMemories>
              <Ocm1><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></Ocm1>
              <Ocm2><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></Ocm2>
              <Ocm3><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></Ocm3>
              <Ocm4><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></Ocm4>
              <Ocm5><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></Ocm5>
              <Ocm6><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></Ocm6>
              <IRAM><Type>0</Type><StartAddress>0x20000000</StartAddress><Size>0x30000</Size></IRAM>
              <IROM><Type>1</Type><StartAddress>0x8000000</StartAddress><Size>0x100000</Size></IROM>
              <XRAM><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></XRAM>
              <OCR_RVCT1><Type>1</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></OCR_RVCT1>
              <OCR_RVCT2><Type>1</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></OCR_RVCT2>
              <OCR_RVCT3><Type>1</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></OCR_RVCT3>
              <OCR_RVCT4><Type>1</Type><StartAddress>0x8000000</StartAddress><Size>0x100000</Size></OCR_RVCT4>
              <OCR_RVCT5><Type>1</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></OCR_RVCT5>
              <OCR_RVCT6><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></OCR_RVCT6>
              <OCR_RVCT7><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></OCR_RVCT7>
              <OCR_RVCT8><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></OCR_RVCT8>
              <OCR_RVCT9><Type>0</Type><StartAddress>0x20000000</StartAddress><Size>0x30000</Size></OCR_RVCT9>
              <OCR_RVCT10><Type>0</Type><StartAddress>0x0</StartAddress><Size>0x0</Size></OCR_RVCT10>
            </OnChipMemories>
            <RvctStartVector />
          </ArmAdsMisc>
          <Cads>
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
            <wLevel>0</wLevel>
            <uThumb>0</uThumb>
            <uSurpInc>0</uSurpInc>
            <uC99>1</uC99>
            <uGnu>1</uGnu>
            <useXO>0</useXO>
            <v6Lang>4</v6Lang>
            <v6LangP>4</v6LangP>
            <vShortEn>0</vShortEn>
            <vShortWch>0</vShortWch>
            <v6Lto>0</v6Lto>
            <v6WtE>0</v6WtE>
            <v6Rtti>0</v6Rtti>
            <VariousControls>
              <MiscControls>{misc}</MiscControls>
              <Define>{defines}</Define>
              <Undefine />
              <IncludePath>{includes}</IncludePath>
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
            <ClangAsOpt>4</ClangAsOpt>
            <VariousControls>
              <MiscControls />
              <Define />
              <Undefine />
              <IncludePath />
            </VariousControls>
          </Aads>
          <LDads>
            <umfTarg>0</umfTarg>
            <Ropi>0</Ropi>
            <Rwpi>0</Rwpi>
            <noStLib>0</noStLib>
            <RepFail>0</RepFail>
            <useFile>0</useFile>
            <TextAddressRange>0x08000000</TextAddressRange>
            <DataAddressRange>0x20000000</DataAddressRange>
            <pXoBase />
            <ScatterFile>{sct}</ScatterFile>
            <IncludeLibs />
            <IncludeLibsPath />
            <Misc />
            <LinkerInputFile />
            <DisabledWarnings>6314,6319</DisabledWarnings>
          </LDads>
        </TargetArmAds>
      </TargetOption>
{groups}
    </Target>
"""

HEADER = """<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<Project xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="project_projx.xsd">
  <SchemaVersion>2.1</SchemaVersion>
  <Header>### uVision Project, (C) Keil Software</Header>
  <Targets>
"""


def render(groups_xml_text: str) -> str:
    parts = [HEADER]
    for name, optim, debug_info in (("Debug", 1, 1), ("Release", 2, 0)):
        parts.append(TARGET_TMPL.format(
            name=name,
            device=DEVICE,
            vendor=VENDOR,
            pack=PACK_ID,
            cpu=CPU,
            flash_driver=FLASH_DRIVER,
            project=PROJECT,
            sct=SCT,
            optim=optim,
            debug_info=debug_info,
            defines=DEFINES,
            includes=INCLUDES,
            misc=MISC,
            groups=groups_xml_text,
        ))
    parts.append("  </Targets>\n</Project>\n")
    return "".join(parts).replace("\n", "\r\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="exit 1 when the checked-in .uvprojx is out of date")
    args = ap.parse_args()

    groups = collect()
    total = sum(len(f) for _, f in groups)
    text = render(groups_xml(groups))

    if args.check:
        current = UVPROJX.read_text(encoding="utf-8") if UVPROJX.exists() else ""
        if current.replace("\r\n", "\n") != text.replace("\r\n", "\n"):
            print("STALE: " + UVPROJX.name)
            return 1
        print("up to date: " + UVPROJX.name)
        return 0

    UVPROJX.write_text(text, encoding="utf-8", newline="")
    print(f"wrote {UVPROJX.name}: {len(groups)} groups, {total} files")
    for name, files in groups:
        print(f"  {name:<16} {len(files):>3}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
