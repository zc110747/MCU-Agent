#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Generate Keil MDK-ARM project files for the stm32f429_net project.

Input template : document/stm32h743/stm32h743.uvprojx + .uvoptx (kept as reference)
Source of truth : CMakeLists.txt (app/bsp/HAL/FreeRTOS/LwIP/mbedTLS file lists & include paths)
Startup         : sys_startup/arm/startup_stm32f429xx.s (armasm syntax) -> copied into MDK-ARM/
                  with its vector table repointed at the FreeRTOS handlers, so the GCC build
                  (which relies on the GAS startup pointing directly at vPort/xPort*) stays
                  untouched.

Notes honored:
  * syscalls.c is gcc/newlib specific -> NOT added (MicroLIB is enabled instead).
  * No gcc/ld flags such as -Wl,--no-warn-rwx-segments (Keil/armlink does not use them).
  * Only the HAL modules enabled in app/stm32f4xx_hal_conf.h are added.
  * All build output (Objects/, Listings/) lives under MDK-ARM/ -> no pollution elsewhere.
"""

import os
import shutil

PROJ_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
MDK_DIR = os.path.join(PROJ_ROOT, "MDK-ARM")
TARGET = "stm32f429_net"
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
    ("Application/User/Core", [
        ("freertos_hooks.c", C, "..\\app\\freertos_hooks.c"),
        ("hwinfo.c", C, "..\\app\\hwinfo.c"),
        ("log.c", C, "..\\app\\log.c"),
        ("main.c", C, "..\\app\\main.c"),
        ("mbedtls_pool.c", C, "..\\app\\mbedtls_pool.c"),
        ("netcfg.c", C, "..\\app\\netcfg.c"),
        ("sdram_heap.c", C, "..\\app\\sdram_heap.c"),
        ("shell.c", C, "..\\app\\shell.c"),
        ("stm32f4xx_hal_timebase_tim.c", C, "..\\app\\stm32f4xx_hal_timebase_tim.c"),
        ("stm32f4xx_it.c", C, "..\\app\\stm32f4xx_it.c"),
        ("telnet_shell.c", C, "..\\app\\telnet_shell.c"),
    ]),
    ("Application/User/LwIP", [
        ("ethernetif.c", C, "..\\app\\lwip\\ethernetif.c"),
        ("lan8720.c", C, "..\\app\\lwip\\lan8720.c"),
        ("lwip.c", C, "..\\app\\lwip\\lwip.c"),
        ("sys_arch.c", C, "..\\app\\lwip\\sys_arch.c"),
    ]),
    ("Application/User/Web", [
        ("http_server.c", C, "..\\app\\web\\http_server.c"),
        ("https_server.c", C, "..\\app\\web\\https_server.c"),
        ("web_assets.c", C, "..\\app\\web\\web_assets.c"),
        ("web_serve.c", C, "..\\app\\web\\web_serve.c"),
    ]),
    ("Application/User/SNMP", [
        ("ber.c", C, "..\\app\\snmp\\ber.c"),
        ("mib.c", C, "..\\app\\snmp\\mib.c"),
        ("snmp_agent.c", C, "..\\app\\snmp\\snmp_agent.c"),
        ("snmp_msg.c", C, "..\\app\\snmp\\snmp_msg.c"),
    ]),
    ("Drivers/BSP", [
        ("bsp_ap3216.c", C, "..\\bsp\\bsp_ap3216.c"),
        ("bsp_eeprom_24c02.c", C, "..\\bsp\\bsp_eeprom_24c02.c"),
        ("bsp_i2c.c", C, "..\\bsp\\bsp_i2c.c"),
        ("bsp_led.c", C, "..\\bsp\\bsp_led.c"),
        ("bsp_mpu9250.c", C, "..\\bsp\\bsp_mpu9250.c"),
        ("bsp_pcf8574.c", C, "..\\bsp\\bsp_pcf8574.c"),
        ("bsp_sdram.c", C, "..\\bsp\\bsp_sdram.c"),
        ("bsp_uart.c", C, "..\\bsp\\bsp_uart.c"),
    ]),
    ("Drivers/STM32F4xx_HAL_Driver", [
        ("stm32f4xx_hal.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal.c"),
        ("stm32f4xx_hal_cortex.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_cortex.c"),
        ("stm32f4xx_hal_rcc.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_rcc.c"),
        ("stm32f4xx_hal_rcc_ex.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_rcc_ex.c"),
        ("stm32f4xx_hal_gpio.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_gpio.c"),
        ("stm32f4xx_hal_uart.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_uart.c"),
        ("stm32f4xx_hal_i2c.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_i2c.c"),
        ("stm32f4xx_hal_eth.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_eth.c"),
        ("stm32f4xx_hal_pwr.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_pwr.c"),
        ("stm32f4xx_hal_pwr_ex.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_pwr_ex.c"),
        ("stm32f4xx_hal_flash.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_flash.c"),
        ("stm32f4xx_hal_tim.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_tim.c"),
        ("stm32f4xx_hal_tim_ex.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_tim_ex.c"),
        ("stm32f4xx_hal_sdram.c", C, "..\\Drivers\\STM32F4xx_HAL_Driver\\Src\\stm32f4xx_hal_sdram.c"),
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
        ("heap_4.c", C, "..\\third_party\\FreeRTOS-Kernel\\portable\\MemMang\\heap_4.c"),
    ]),
    ("Middlewares/LwIP", [
        ("api_lib.c", C, "..\\third_party\\LwIP\\src\\api\\api_lib.c"),
        ("api_msg.c", C, "..\\third_party\\LwIP\\src\\api\\api_msg.c"),
        ("err.c", C, "..\\third_party\\LwIP\\src\\api\\err.c"),
        ("netbuf.c", C, "..\\third_party\\LwIP\\src\\api\\netbuf.c"),
        ("netifapi.c", C, "..\\third_party\\LwIP\\src\\api\\netifapi.c"),
        ("tcpip.c", C, "..\\third_party\\LwIP\\src\\api\\tcpip.c"),
        ("def.c", C, "..\\third_party\\LwIP\\src\\core\\def.c"),
        ("dns.c", C, "..\\third_party\\LwIP\\src\\core\\dns.c"),
        ("inet_chksum.c", C, "..\\third_party\\LwIP\\src\\core\\inet_chksum.c"),
        ("init.c", C, "..\\third_party\\LwIP\\src\\core\\init.c"),
        ("ip.c", C, "..\\third_party\\LwIP\\src\\core\\ip.c"),
        ("ip4.c", C, "..\\third_party\\LwIP\\src\\core\\ipv4\\ip4.c"),
        ("ip4_addr.c", C, "..\\third_party\\LwIP\\src\\core\\ipv4\\ip4_addr.c"),
        ("ip4_frag.c", C, "..\\third_party\\LwIP\\src\\core\\ipv4\\ip4_frag.c"),
        ("autoip.c", C, "..\\third_party\\LwIP\\src\\core\\ipv4\\autoip.c"),
        ("dhcp.c", C, "..\\third_party\\LwIP\\src\\core\\ipv4\\dhcp.c"),
        ("etharp.c", C, "..\\third_party\\LwIP\\src\\core\\ipv4\\etharp.c"),
        ("icmp.c", C, "..\\third_party\\LwIP\\src\\core\\ipv4\\icmp.c"),
        ("igmp.c", C, "..\\third_party\\LwIP\\src\\core\\ipv4\\igmp.c"),
        ("mem.c", C, "..\\third_party\\LwIP\\src\\core\\mem.c"),
        ("memp.c", C, "..\\third_party\\LwIP\\src\\core\\memp.c"),
        ("netif.c", C, "..\\third_party\\LwIP\\src\\core\\netif.c"),
        ("pbuf.c", C, "..\\third_party\\LwIP\\src\\core\\pbuf.c"),
        ("raw.c", C, "..\\third_party\\LwIP\\src\\core\\raw.c"),
        ("stats.c", C, "..\\third_party\\LwIP\\src\\core\\stats.c"),
        ("tcp.c", C, "..\\third_party\\LwIP\\src\\core\\tcp.c"),
        ("tcp_in.c", C, "..\\third_party\\LwIP\\src\\core\\tcp_in.c"),
        ("tcp_out.c", C, "..\\third_party\\LwIP\\src\\core\\tcp_out.c"),
        ("timeouts.c", C, "..\\third_party\\LwIP\\src\\core\\timeouts.c"),
        ("udp.c", C, "..\\third_party\\LwIP\\src\\core\\udp.c"),
        ("ethernet.c", C, "..\\third_party\\LwIP\\src\\netif\\ethernet.c"),
    ]),
    ("Middlewares/mbedTLS", [
        ("aes.c", C, "..\\third_party\\mbedtls\\library\\aes.c"),
        ("asn1parse.c", C, "..\\third_party\\mbedtls\\library\\asn1parse.c"),
        ("asn1write.c", C, "..\\third_party\\mbedtls\\library\\asn1write.c"),
        ("base64.c", C, "..\\third_party\\mbedtls\\library\\base64.c"),
        ("bignum.c", C, "..\\third_party\\mbedtls\\library\\bignum.c"),
        ("bignum_core.c", C, "..\\third_party\\mbedtls\\library\\bignum_core.c"),
        ("bignum_mod.c", C, "..\\third_party\\mbedtls\\library\\bignum_mod.c"),
        ("bignum_mod_raw.c", C, "..\\third_party\\mbedtls\\library\\bignum_mod_raw.c"),
        ("block_cipher.c", C, "..\\third_party\\mbedtls\\library\\block_cipher.c"),
        ("cipher.c", C, "..\\third_party\\mbedtls\\library\\cipher.c"),
        ("cipher_wrap.c", C, "..\\third_party\\mbedtls\\library\\cipher_wrap.c"),
        ("constant_time.c", C, "..\\third_party\\mbedtls\\library\\constant_time.c"),
        ("ecdh.c", C, "..\\third_party\\mbedtls\\library\\ecdh.c"),
        ("ecdsa.c", C, "..\\third_party\\mbedtls\\library\\ecdsa.c"),
        ("ecp.c", C, "..\\third_party\\mbedtls\\library\\ecp.c"),
        ("ecp_curves.c", C, "..\\third_party\\mbedtls\\library\\ecp_curves.c"),
        ("error.c", C, "..\\third_party\\mbedtls\\library\\error.c"),
        ("gcm.c", C, "..\\third_party\\mbedtls\\library\\gcm.c"),
        ("md.c", C, "..\\third_party\\mbedtls\\library\\md.c"),
        ("memory_buffer_alloc.c", C, "..\\third_party\\mbedtls\\library\\memory_buffer_alloc.c"),
        ("oid.c", C, "..\\third_party\\mbedtls\\library\\oid.c"),
        ("pem.c", C, "..\\third_party\\mbedtls\\library\\pem.c"),
        ("pk.c", C, "..\\third_party\\mbedtls\\library\\pk.c"),
        ("pk_ecc.c", C, "..\\third_party\\mbedtls\\library\\pk_ecc.c"),
        ("pk_wrap.c", C, "..\\third_party\\mbedtls\\library\\pk_wrap.c"),
        ("pkparse.c", C, "..\\third_party\\mbedtls\\library\\pkparse.c"),
        ("platform.c", C, "..\\third_party\\mbedtls\\library\\platform.c"),
        ("platform_util.c", C, "..\\third_party\\mbedtls\\library\\platform_util.c"),
        ("sha1.c", C, "..\\third_party\\mbedtls\\library\\sha1.c"),
        ("sha256.c", C, "..\\third_party\\mbedtls\\library\\sha256.c"),
        ("ssl_ciphersuites.c", C, "..\\third_party\\mbedtls\\library\\ssl_ciphersuites.c"),
        ("ssl_msg.c", C, "..\\third_party\\mbedtls\\library\\ssl_msg.c"),
        ("ssl_tls.c", C, "..\\third_party\\mbedtls\\library\\ssl_tls.c"),
        ("ssl_tls12_server.c", C, "..\\third_party\\mbedtls\\library\\ssl_tls12_server.c"),
        ("x509.c", C, "..\\third_party\\mbedtls\\library\\x509.c"),
        ("x509_crt.c", C, "..\\third_party\\mbedtls\\library\\x509_crt.c"),
    ]),
]

INCLUDE_PATH = ";".join([
    "..\\app",
    "..\\app\\lwip",
    "..\\app\\web",
    "..\\app\\snmp",
    "..\\bsp",
    "..\\sys_startup",
    "..\\Drivers\\CMSIS\\Include",
    "..\\Drivers\\STM32F4xx_HAL_Driver\\Inc",
    "..\\Drivers\\STM32F4xx_HAL_Driver\\Inc\\Legacy",
    "..\\third_party\\LwIP\\src\\include",
    "..\\third_party\\LwIP\\system\\arch",
    "..\\third_party\\FreeRTOS-Kernel\\include",
    "..\\third_party\\FreeRTOS-Kernel\\portable\\GCC\\ARM_CM4F",
    "..\\third_party\\mbedtls\\include",
])

# mbedTLS config is supplied via angle brackets so the preprocessor searches the
# include path (app/mbedtls_config.h). XML-escape the < >.
DEFINE = "STM32F429xx,USE_HAL_DRIVER,MBEDTLS_CONFIG_FILE=&lt;mbedtls_config.h&gt;"


def gen_uvprojx():
    groups_xml = []
    for gname, files in GROUPS:
        file_xml = []
        for fname, ftype, fpath in files:
            file_xml.append(
                "            <File>\n"
                "              <FileName>%s</FileName>\n"
                "              <FileType>%d</FileType>\n"
                "              <FilePath>%s</FilePath>\n"
                "            </File>" % (fname, ftype, fpath)
            )
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
            <umfTarg>1</umfTarg>
            <Ropi>0</Ropi>
            <Rwpi>0</Rwpi>
            <noStLib>0</noStLib>
            <RepFail>1</RepFail>
            <useFile>0</useFile>
            <TextAddressRange></TextAddressRange>
            <DataAddressRange></DataAddressRange>
            <pXoBase></pXoBase>
            <ScatterFile></ScatterFile>
            <IncludeLibs></IncludeLibs>
            <IncludeLibsPath></IncludeLibsPath>
            <Misc></Misc>
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
       "\n".join(groups_xml), TARGET)

    out = os.path.join(MDK_DIR, TARGET + ".uvprojx")
    with open(out, "w", encoding="utf-8") as f:
        f.write(xml)
    return out


def gen_uvoptx():
    # Group / File browser entries mirror the project structure.
    group_xml = []
    gn = 1
    fn = 1
    for gname, files in GROUPS:
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
            "    <RteFlg>0</RteFlg>\n%s\n"
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


def patch_startup():
    src = os.path.join(PROJ_ROOT, "sys_startup", "arm", "startup_stm32f429xx.s")
    dst = os.path.join(MDK_DIR, "startup_stm32f429xx.s")
    with open(src, "r", encoding="utf-8") as f:
        text = f.read()

    repls = [
        ("                DCD     SVC_Handler                ; SVCall Handler",
         "                DCD     vPortSVCHandler             ; SVCall Handler (FreeRTOS)"),
        ("                DCD     PendSV_Handler             ; PendSV Handler",
         "                DCD     xPortPendSVHandler          ; PendSV Handler (FreeRTOS)"),
        ("                DCD     SysTick_Handler            ; SysTick Handler",
         "                DCD     xPortSysTickHandler         ; SysTick Handler (FreeRTOS)"),
    ]
    for old, new in repls:
        if old not in text:
            raise SystemExit("ERROR: startup vector line not found:\n  %s" % old)
        text = text.replace(old, new, 1)

    with open(dst, "w", encoding="utf-8") as f:
        f.write(text)
    return dst


if __name__ == "__main__":
    p1 = gen_uvprojx()
    p2 = gen_uvoptx()
    p3 = patch_startup()
    print("Wrote:", p1)
    print("Wrote:", p2)
    print("Wrote:", p3)
    total = sum(len(files) for _, files in GROUPS)
    print("Groups: %d  Files: %d" % (len(GROUPS), total))
