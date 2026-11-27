add_dolphin_test(MMIOTest MMIOTest.cpp)
add_dolphin_test(PageFaultTest PageFaultTest.cpp)
add_dolphin_test(CoreTimingTest CoreTimingTest.cpp)
add_dolphin_test(CPUManagerTest CPUManagerTest.cpp)
add_dolphin_test(PatchAllowlistTest PatchAllowlistTest.cpp)
add_dolphin_test(GameModsTest GameModsTest.cpp)
add_dolphin_test(GameCoversTest GameCoversTest.cpp)
add_dolphin_test(ModeSwitchTest ModeSwitchTest.cpp)
add_dolphin_test(ProblemReportTest ProblemReportTest.cpp)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_dolphin_test(CrashRecordTest CrashRecordTest.cpp)
endif()

if(ENABLE_VR)
  add_dolphin_test(WiimoteMotionTrackerTest HW/WiimoteMotionTrackerTest.cpp)
  add_dolphin_test(VRPatchTest VRPatch/VRPatchTest.cpp)
  add_dolphin_test(ArmCannonPatchTest VRPatch/ArmCannonPatchTest.cpp)
  add_dolphin_test(FirstPersonPatchTest VRPatch/FirstPersonPatchTest.cpp)
  add_dolphin_test(FrameRatePatchTest VRPatch/FrameRatePatchTest.cpp)
  add_dolphin_test(MetroidStatePatchTest VRPatch/MetroidStatePatchTest.cpp)
endif()

add_dolphin_test(DSPAcceleratorTest DSP/DSPAcceleratorTest.cpp)
add_dolphin_test(DSPAssemblyTest
  DSP/DSPAssemblyTest.cpp
  DSP/DSPTestBinary.cpp
  DSP/DSPTestText.cpp
  DSP/HermesBinary.cpp
  DSP/HermesText.cpp
)

add_dolphin_test(ESFormatsTest IOS/ES/FormatsTest.cpp)

add_dolphin_test(FileSystemTest IOS/FS/FileSystemTest.cpp)

add_dolphin_test(SkylandersTest IOS/USB/SkylandersTest.cpp)

if(_M_X86_64)
  add_dolphin_test(PowerPCTest
    PowerPC/DivUtilsTest.cpp
    PowerPC/PageTableHostMappingTest.cpp
    PowerPC/Jit64Common/ConvertDoubleToSingle.cpp
    PowerPC/Jit64Common/Fres.cpp
    PowerPC/Jit64Common/Frsqrte.cpp
  )
elseif(_M_ARM_64)
  add_dolphin_test(PowerPCTest
    PowerPC/DivUtilsTest.cpp
    PowerPC/PageTableHostMappingTest.cpp
    PowerPC/JitArm64/ConvertSingleDouble.cpp
    PowerPC/JitArm64/FPRF.cpp
    PowerPC/JitArm64/Fres.cpp
    PowerPC/JitArm64/Frsqrte.cpp
    PowerPC/JitArm64/MovI2R.cpp
  )
else()
  add_dolphin_test(PowerPCTest
    PowerPC/DivUtilsTest.cpp
    PowerPC/PageTableHostMappingTest.cpp
  )
endif()

target_sources(PowerPCTest PRIVATE
  PowerPC/TestValues.h
  StubJit.h
)
