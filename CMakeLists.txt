cmake_minimum_required(VERSION 3.16)

project(DemoEMG LANGUAGES CXX)

# Standalone Qt demo for the sensor C++ SDK, shipped with prebuilt SDK
# libraries for every supported platform (see lib/). Windows x64/x86, Linux
# x86_64/x86, macOS arm64, iOS and Android (4 ABIs) are supported; iOS/Android
# use the phone tab layout.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Qt 6.8's FindWrapOpenGL records AGL (as an absolute /System path or a
# hardcoded "-framework AGL"), and AGL was removed from the macOS 26 SDK —
# strip it from the link interface or the app fails to link on current Xcode.
if(TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_wrap_opengl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_wrap_opengl_libs)
        list(FILTER _wrap_opengl_libs EXCLUDE REGEX "AGL")
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_wrap_opengl_libs}")
    endif()
endif()

# -- Prebuilt SDK library selection -------------------------------------------
#
# The package layout:
#   include/                                  public SDK headers
#   lib/windows/<x64|x86>/<Debug|Release>/    sensor.dll/.lib (x86: sensor32.*)
#   lib/linux/<x86_64|x86>/libsensor.so
#   lib/android/<abi>/libsensor.so            arm64-v8a, armeabi-v7a, x86, x86_64
#   lib/xcframework/sensor.xcframework        ios-arm64, ios-simulator
#   lib/macos/libsensor.dylib                 macOS (universal, @rpath)
#
# Every platform's library choice has a cache variable below; pass
# -D<NAME>=<value> at configure time to override.

set(SENSOR_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH
    "Sensor SDK package root (the directory containing include/ and lib/)")

if(WIN32)
    set(SENSOR_SDK_WIN_ARCH "x64" CACHE STRING
        "Prebuilt SDK library on Windows: x64 (msvc2022_64 kit) or x86 (32-bit msvc2022 kit)")
    set_property(CACHE SENSOR_SDK_WIN_ARCH PROPERTY STRINGS x64 x86)
    if(SENSOR_SDK_WIN_ARCH STREQUAL "x64" AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "SENSOR_SDK_WIN_ARCH=x64 requires a 64-bit Qt kit (msvc2022_64)")
    endif()
    if(SENSOR_SDK_WIN_ARCH STREQUAL "x86" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "SENSOR_SDK_WIN_ARCH=x86 requires a 32-bit Qt kit (msvc2022)")
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SENSOR_SDK_LINUX_ARCH "x86_64" CACHE STRING
        "Prebuilt SDK library on Linux: x86_64 or x86")
    set_property(CACHE SENSOR_SDK_LINUX_ARCH PROPERTY STRINGS x86_64 x86)
endif()

if(APPLE)
    set(SENSOR_SDK_XCFRAMEWORK "${SENSOR_SDK_ROOT}/lib/xcframework/sensor.xcframework"
        CACHE PATH "Static sensor.xcframework linked on iOS")
    set(SENSOR_SDK_MAC_DYLIB "${SENSOR_SDK_ROOT}/lib/macos/libsensor.dylib"
        CACHE PATH "Shared libsensor.dylib linked on macOS")
endif()

# Android: the ABI comes from the Qt kit (CMAKE_ANDROID_ARCH_ABI); the
# matching lib/android/<abi>/libsensor.so is linked automatically.

set(DEMO_SOURCES
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/SdkBridge.h
    src/SdkBridge.cpp
    src/DeviceState.h
    src/DeviceState.cpp
    src/WaveformWidget.h
    src/WaveformWidget.cpp
    src/SpectrumWidget.h
    src/SpectrumWidget.cpp
    src/LiveFilter.h
    src/LiveFilter.cpp
    src/CubeWidget.h
    src/CubeWidget.cpp
)

if(ANDROID)
    qt_add_executable(DemoEMG ${DEMO_SOURCES})
elseif(IOS)
    add_executable(DemoEMG MACOSX_BUNDLE ${DEMO_SOURCES})
else()
    add_executable(DemoEMG ${DEMO_SOURCES})
endif()

target_include_directories(DemoEMG PRIVATE "${SENSOR_SDK_ROOT}/include")

target_link_libraries(DemoEMG PRIVATE Qt6::Widgets)

# Force the phone tab layout (QTabWidget pages) on a desktop build.
option(DEMO_MOBILE_LAYOUT "Force the phone tab layout on desktop builds" OFF)
if(DEMO_MOBILE_LAYOUT)
    target_compile_definitions(DemoEMG PRIVATE DEMO_MOBILE_LAYOUT)
endif()

if(ANDROID)
    # The SDK Android build has no per-config subdirectory.
    set(SENSOR_ANDROID_LIB "${SENSOR_SDK_ROOT}/lib/android/${CMAKE_ANDROID_ARCH_ABI}/libsensor.so")
    if(NOT EXISTS "${SENSOR_ANDROID_LIB}")
        message(FATAL_ERROR
            "No prebuilt SDK for Android ABI '${CMAKE_ANDROID_ARCH_ABI}': ${SENSOR_ANDROID_LIB} not found")
    endif()
    add_library(sensorsdk SHARED IMPORTED)
    set_target_properties(sensorsdk PROPERTIES
        IMPORTED_LOCATION "${SENSOR_ANDROID_LIB}"
        IMPORTED_NO_SONAME TRUE
    )
    target_link_libraries(DemoEMG PRIVATE sensorsdk)
    set_target_properties(DemoEMG PROPERTIES
        # Bundle libsensor.so into the APK.
        QT_ANDROID_EXTRA_LIBS "${SENSOR_ANDROID_LIB}"
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    )
    qt_finalize_executable(DemoEMG)
elseif(IOS)
    # Static sensor.xcframework; Xcode picks the device/simulator slice
    # automatically. The static SDK needs its system frameworks linked by the
    # consumer.
    target_link_libraries(DemoEMG PRIVATE
        "${SENSOR_SDK_XCFRAMEWORK}"
        "-framework CoreBluetooth"
        "-framework Foundation"
    )
    # Apple development team for device signing; empty lets Xcode pick its
    # default (configure with -DDEMO_IOS_DEV_TEAM=<team id> to override).
    set(DEMO_IOS_DEV_TEAM "" CACHE STRING "Apple development team ID for DemoEMG iOS signing")
    set_target_properties(DemoEMG PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.oymotion.DemoEMG"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${DEMO_IOS_DEV_TEAM}"
        # Qt 6.8 is built for iOS 16.0; a lower app deployment target would
        # leave Qt's strong references to newer APIs unguarded at runtime.
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "16.0"
    )
elseif(WIN32)
    if(SENSOR_SDK_WIN_ARCH STREQUAL "x86")
        set(SENSOR_WIN_LIB "${SENSOR_SDK_ROOT}/lib/windows/x86/$<CONFIG>/sensor32.lib")
        set(SENSOR_WIN_DLL "${SENSOR_SDK_ROOT}/lib/windows/x86/$<CONFIG>/sensor32.dll")
    else()
        set(SENSOR_WIN_LIB "${SENSOR_SDK_ROOT}/lib/windows/x64/$<CONFIG>/sensor.lib")
        set(SENSOR_WIN_DLL "${SENSOR_SDK_ROOT}/lib/windows/x64/$<CONFIG>/sensor.dll")
    endif()
    target_link_libraries(DemoEMG PRIVATE "${SENSOR_WIN_LIB}")

    # Deploy the SDK dll and the Qt runtime next to the executable.
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${CMAKE_PREFIX_PATH}/bin"
    )

    add_custom_command(TARGET DemoEMG POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SENSOR_WIN_DLL}"
            "$<TARGET_FILE_DIR:DemoEMG>/"
    )
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET DemoEMG POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:DemoEMG>"
        )
    else()
        message(WARNING "windeployqt not found; copy the Qt DLLs next to DemoEMG.exe manually")
    endif()
elseif(APPLE)
    # Shared libsensor.dylib. Its install name is @rpath/libsensor.dylib:
    # the dylib is copied next to the executable and found through
    # @executable_path. Qt itself is found through the build rpath.
    target_link_libraries(DemoEMG PRIVATE "${SENSOR_SDK_MAC_DYLIB}")
    set_target_properties(DemoEMG PROPERTIES
        BUILD_RPATH "@executable_path"
    )
    add_custom_command(TARGET DemoEMG POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SENSOR_SDK_MAC_DYLIB}"
            "$<TARGET_FILE_DIR:DemoEMG>/libsensor.dylib"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SENSOR_LINUX_LIB "${SENSOR_SDK_ROOT}/lib/linux/${SENSOR_SDK_LINUX_ARCH}/libsensor.so")
    if(NOT EXISTS "${SENSOR_LINUX_LIB}")
        message(FATAL_ERROR
            "No prebuilt SDK for Linux arch '${SENSOR_SDK_LINUX_ARCH}': ${SENSOR_LINUX_LIB} not found")
    endif()
    add_library(sensorsdk SHARED IMPORTED)
    set_target_properties(sensorsdk PROPERTIES
        IMPORTED_LOCATION "${SENSOR_LINUX_LIB}"
        IMPORTED_NO_SONAME TRUE
    )
    target_link_libraries(DemoEMG PRIVATE sensorsdk)
    # The executable finds libsensor.so next to itself.
    set_target_properties(DemoEMG PROPERTIES BUILD_RPATH "$ORIGIN")
    add_custom_command(TARGET DemoEMG POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SENSOR_LINUX_LIB}"
            "$<TARGET_FILE_DIR:DemoEMG>/libsensor.so"
    )
else()
    message(FATAL_ERROR "DemoEMG supports Windows, Linux, macOS, iOS and Android only")
endif()
