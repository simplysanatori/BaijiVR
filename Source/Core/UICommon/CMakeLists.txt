add_library(uicommon
  AutoUpdate.cpp
  AutoUpdate.h
  CommandLineParse.cpp
  CommandLineParse.h
  DiscordPresence.cpp
  DiscordPresence.h
  GameCovers.cpp
  GameCovers.h
  GameFile.cpp
  GameFile.h
  GameFileCache.cpp
  GameFileCache.h
  NetPlayIndex.cpp
  NetPlayIndex.h
  ProblemReport.cpp
  ProblemReport.h
  ProblemReportSend.cpp
  ProblemReportSend.h
  ResourcePack/Manager.cpp
  ResourcePack/Manager.h
  ResourcePack/Manifest.cpp
  ResourcePack/Manifest.h
  ResourcePack/ResourcePack.cpp
  ResourcePack/ResourcePack.h
  UICommon.cpp
  UICommon.h
)

target_link_libraries(uicommon
PUBLIC
  common
  core
  cpp-optparse
  MINIZIP::minizip-ng
  pugixml

PRIVATE
  fmt::fmt
  $<$<BOOL:APPLE>:${IOK_LIBRARY}>
)

if ((DEFINED CMAKE_ANDROID_ARCH_ABI AND CMAKE_ANDROID_ARCH_ABI MATCHES "x86|x86_64") OR
    (NOT DEFINED CMAKE_ANDROID_ARCH_ABI AND _M_X86_64))
  target_link_libraries(uicommon PRIVATE bdisasm)
endif()

if(UNIX AND NOT APPLE AND NOT ANDROID AND ENABLE_QT)
  find_package(Qt6 REQUIRED COMPONENTS DBus)
  target_sources(uicommon PRIVATE DBusUtils.cpp)
  target_compile_definitions(uicommon PRIVATE -DHAVE_QTDBUS=1)
  target_link_libraries(uicommon PUBLIC Qt6::DBus)
endif()

# Report a Problem. The address reports are sent to is in the environment (BAIJI_REPORT_DSN, or the
# file BAIJI_REPORT_DSN_FILE), not in the cache, and is never printed. The header it goes into can be
# kept out of the build folder (BAIJI_REPORT_CONFIG_DIR). See Tools/report-dsn.sh.
set(BAIJI_REPORT_DSN_VALUE "$ENV{BAIJI_REPORT_DSN}")
if(BAIJI_REPORT_DSN_VALUE STREQUAL "" AND EXISTS "$ENV{BAIJI_REPORT_DSN_FILE}")
  file(STRINGS "$ENV{BAIJI_REPORT_DSN_FILE}" BAIJI_REPORT_DSN_VALUE LIMIT_COUNT 1)
endif()
if(BAIJI_REPORT_DSN_VALUE MATCHES "^https?://[A-Za-z0-9]+@[A-Za-z0-9.:-]+/([A-Za-z0-9._/-]*/)?[0-9]+$")
  message(STATUS "Report DSN: found")
else()
  if(NOT BAIJI_REPORT_DSN_VALUE STREQUAL "")
    message(WARNING "The report DSN is not a Sentry DSN and is not used (its value is not printed)")
  endif()
  set(BAIJI_REPORT_DSN_VALUE "")
  message(STATUS "Report DSN: not found")
endif()
if(BAIJI_RELEASE_BUILD)
  set(BAIJI_REPORT_ENVIRONMENT_VALUE "production")
else()
  set(BAIJI_REPORT_ENVIRONMENT_VALUE "development")
endif()
set(BAIJI_REPORT_CONFIG_DIR_VALUE "$ENV{BAIJI_REPORT_CONFIG_DIR}")
if(BAIJI_REPORT_CONFIG_DIR_VALUE STREQUAL "")
  set(BAIJI_REPORT_CONFIG_DIR_VALUE "${CMAKE_CURRENT_BINARY_DIR}/report")
endif()
configure_file(baiji_report_config.h.in "${BAIJI_REPORT_CONFIG_DIR_VALUE}/baiji_report_config.h" @ONLY
               FILE_PERMISSIONS OWNER_READ OWNER_WRITE)
set_source_files_properties(ProblemReportSend.cpp PROPERTIES
  INCLUDE_DIRECTORIES "${BAIJI_REPORT_CONFIG_DIR_VALUE}")
target_compile_definitions(uicommon PRIVATE BAIJI_VERSION="${BAIJI_VERSION}")
if(ENABLE_TESTS AND NOT BAIJI_RELEASE_BUILD)
  target_compile_definitions(uicommon PRIVATE BAIJI_TEST_HOOKS)
endif()

if(ENABLE_VR)
  target_sources(uicommon PRIVATE VRGames.cpp VRGames.h)
endif()

if(X11_FOUND)
  target_sources(uicommon PRIVATE X11Utils.cpp)
  target_link_libraries(uicommon PUBLIC PkgConfig::XRANDR PkgConfig::X11)
endif()

if(TARGET LibUSB::LibUSB)
  target_link_libraries(uicommon PRIVATE LibUSB::LibUSB)
endif()

if(USE_DISCORD_PRESENCE)
  target_compile_definitions(uicommon PRIVATE -DUSE_DISCORD_PRESENCE)
  target_link_libraries(uicommon PRIVATE discord-rpc)
endif()

if(MSVC)
  # Add precompiled header
  target_link_libraries(uicommon PRIVATE use_pch)
endif()
