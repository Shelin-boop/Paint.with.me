cmake_minimum_required(VERSION 3.16)
project(PaintWithMe VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui PrintSupport Concurrent)
# zlib inflates the gzip-compressed layer chunks of Paint.NET .pdn files.
find_package(ZLIB REQUIRED)

# Single source of truth for the version shown to users. CI passes
# -DPWM_VERSION=<major.minor.commitcount>; a local build falls back to the
# project version. It reaches both the binary (title bar, About) and the .deb, so
# they can no longer drift apart — they were hard-coded and stuck at 1.0/1.1.
if(DEFINED PWM_VERSION AND NOT PWM_VERSION STREQUAL "")
  set(PWM_VERSION_STR "${PWM_VERSION}")
else()
  set(PWM_VERSION_STR "${PROJECT_VERSION}")
endif()
message(STATUS "Paint.with.me version: ${PWM_VERSION_STR}")

set(CORE_SOURCES
    src/core/adjustmentpreview.cpp
    src/core/imageoperation.cpp
    src/core/document.cpp
    src/core/pdnformat.cpp
    src/core/layer.cpp
    src/core/selection.cpp
    src/core/history.cpp
    src/core/hatchpatterns.cpp
)

set(CANVAS_SOURCES
    src/canvas/canvaswidget.cpp
)

set(TOOLS_SOURCES
    src/tools/tool.cpp
    src/tools/brushtool.cpp
    src/tools/erasertool.cpp
    src/tools/penciltool.cpp
    src/tools/filltool.cpp
    src/tools/colorpickertool.cpp
    src/tools/recolortool.cpp
    src/tools/selectiontool.cpp
    src/tools/lassotool.cpp
    src/tools/movetool.cpp
    src/tools/selectionhandles.cpp
    src/tools/moveselectiontool.cpp
    src/tools/zoomtool.cpp
    src/tools/pantool.cpp
    src/tools/texttool.cpp
    src/tools/shapetool.cpp
    src/tools/linetool.cpp
    src/tools/gradienttool.cpp
    src/tools/clonestamptool.cpp
    src/tools/magicwandtool.cpp
)

set(PANELS_SOURCES
    src/panels/layerspanel.cpp
    src/panels/colorspanel.cpp
    src/panels/historypanel.cpp
    src/panels/tooloptionspanel.cpp
    src/panels/imagelistbar.cpp
)

set(EFFECTS_SOURCES
    src/effects/effect.cpp
    src/effects/blureffect.cpp
    src/effects/sharpeneffect.cpp
    src/effects/noiseeffect.cpp
    src/effects/embosseffect.cpp
    src/effects/edgedetecteffect.cpp
    src/effects/oilpainteffect.cpp
    src/effects/pixelateeffect.cpp
    src/effects/motionblureffect.cpp
    src/effects/radialblureffect.cpp
    src/effects/zoomblureffect.cpp
    src/effects/surfaceblureffect.cpp
    src/effects/unfocuseffect.cpp
    src/effects/fragmenteffect.cpp
    src/effects/bulgeeffect.cpp
    src/effects/twisteffect.cpp
    src/effects/frostedglasseffect.cpp
    src/effects/crystalizeeffect.cpp
    src/effects/tileeffect.cpp
    src/effects/dentseffect.cpp
    src/effects/polarinversioneffect.cpp
    src/effects/medianeffect.cpp
    src/effects/reducenoiseeffect.cpp
    src/effects/quantizeeffect.cpp
    src/effects/gloweffect.cpp
    src/effects/redeyeremoveeffect.cpp
    src/effects/softenportraiteffect.cpp
    src/effects/vignetteeffect.cpp
    src/effects/turbulenceeffect.cpp
    src/effects/reliefeffect.cpp
    src/effects/outlineeffect.cpp
    src/effects/morphologyeffect.cpp
    src/effects/dropshadoweffect.cpp
)

set(ADJUSTMENTS_SOURCES
    src/adjustments/adjustment.cpp
    src/adjustments/brightnesscontrast.cpp
    src/adjustments/huesaturation.cpp
    src/adjustments/levels.cpp
    src/adjustments/curves.cpp
    src/adjustments/invertcolors.cpp
    src/adjustments/sepia.cpp
    src/adjustments/posterize.cpp
    src/adjustments/threshold.cpp
    src/adjustments/desaturate.cpp
    src/adjustments/colorbalance.cpp
    src/adjustments/exposure.cpp
    src/adjustments/highlightsshadows.cpp
    src/adjustments/temperaturetint.cpp
    src/adjustments/invertalpha.cpp
)

set(DIALOGS_SOURCES
    src/dialogs/pastedialog.cpp
    src/dialogs/resizedialog.cpp
    src/dialogs/canvassizedialog.cpp
    src/dialogs/newdocumentdialog.cpp
    src/dialogs/huesaturationdialog.cpp
    src/dialogs/layerpropertiesdialog.cpp
    src/dialogs/curvesdialog.cpp
    src/dialogs/settingsdialog.cpp
    src/dialogs/previewdialog.cpp
    src/dialogs/saveconfigdialog.cpp
)

set(PLUGINS_SOURCES
    src/plugins/pluginmanager.cpp
    src/plugins/plugineffect.cpp
)

set(MAIN_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/desktopintegration.cpp
    src/toolicons.cpp
    src/theme.cpp
    src/dockdropoverlay.cpp
)

set(RESOURCES
    src/resources/resources.qrc
)

set(ALL_SOURCES
    ${MAIN_SOURCES}
    ${CORE_SOURCES}
    ${CANVAS_SOURCES}
    ${TOOLS_SOURCES}
    ${PANELS_SOURCES}
    ${EFFECTS_SOURCES}
    ${ADJUSTMENTS_SOURCES}
    ${DIALOGS_SOURCES}
    ${PLUGINS_SOURCES}
    ${RESOURCES}
)

add_executable(paint-with-me ${ALL_SOURCES})

target_include_directories(paint-with-me PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(paint-with-me PRIVATE
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Concurrent
    Qt6::Core
    Qt6::Gui
    ZLIB::ZLIB
)

target_compile_definitions(paint-with-me PRIVATE PWM_VERSION_STR="${PWM_VERSION_STR}")

install(TARGETS paint-with-me DESTINATION bin)

# ---- Test harness (headless functional tests) ----
set(NONMAIN_SOURCES
    ${CORE_SOURCES}
    ${CANVAS_SOURCES}
    ${TOOLS_SOURCES}
    ${PANELS_SOURCES}
    ${EFFECTS_SOURCES}
    ${ADJUSTMENTS_SOURCES}
    ${DIALOGS_SOURCES}
    ${PLUGINS_SOURCES}
    src/toolicons.cpp
    src/theme.cpp
    src/desktopintegration.cpp
    ${RESOURCES}
)

add_executable(pwm_tests EXCLUDE_FROM_ALL
    tests/test_all.cpp
    ${NONMAIN_SOURCES}
)
target_include_directories(pwm_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pwm_tests PRIVATE
    Qt6::Widgets Qt6::PrintSupport Qt6::Concurrent Qt6::Core Qt6::Gui ZLIB::ZLIB)

add_executable(pwm_brush_tests EXCLUDE_FROM_ALL
    tests/test_brush.cpp tests/referencebrush.cpp
    ${CORE_SOURCES} ${CANVAS_SOURCES} src/tools/tool.cpp src/tools/brushtool.cpp)
target_include_directories(pwm_brush_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pwm_brush_tests PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Core Qt6::Gui ZLIB::ZLIB)
add_executable(pwm_paste_tests EXCLUDE_FROM_ALL
    tests/test_paste.cpp src/mainwindow.cpp src/dockdropoverlay.cpp ${NONMAIN_SOURCES})
target_include_directories(pwm_paste_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pwm_paste_tests PRIVATE Qt6::Widgets Qt6::PrintSupport Qt6::Concurrent Qt6::Core Qt6::Gui ZLIB::ZLIB)

enable_testing()
add_test(NAME paste_regression COMMAND pwm_paste_tests)
set_tests_properties(paste_regression PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME functional COMMAND pwm_tests)
target_compile_definitions(pwm_tests PRIVATE PWM_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
add_test(NAME brush_regression COMMAND pwm_brush_tests)
set_tests_properties(functional brush_regression PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(pwm_preview_tests EXCLUDE_FROM_ALL tests/test_adjustmentpreview.cpp src/mainwindow.cpp src/dockdropoverlay.cpp ${NONMAIN_SOURCES})
target_include_directories(pwm_preview_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pwm_preview_tests PRIVATE Qt6::Widgets Qt6::PrintSupport Qt6::Concurrent Qt6::Core Qt6::Gui ZLIB::ZLIB)
add_test(NAME adjustment_preview COMMAND pwm_preview_tests)
set_tests_properties(adjustment_preview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")


# AppImage application-menu entry; small and fast, Qt Core only.
add_executable(pwm_integration_tests EXCLUDE_FROM_ALL
    tests/test_desktopintegration.cpp src/desktopintegration.cpp ${RESOURCES})
target_include_directories(pwm_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pwm_integration_tests PRIVATE Qt6::Core)
target_compile_definitions(pwm_integration_tests PRIVATE PWM_VERSION_STR="${PWM_VERSION_STR}")
add_test(NAME desktop_integration COMMAND pwm_integration_tests)

# ---- Sample plugin (demonstrates the C ABI; built into build/plugins) ----
# A MODULE library is a shared object meant to be dlopen'd, never linked against.
add_library(sample_sepia_plugin MODULE plugins/sample/sepia_plugin.c)
target_include_directories(sample_sepia_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/plugins)
set_target_properties(sample_sepia_plugin PROPERTIES
    AUTOMOC OFF                                                 # plain C, no Qt
    C_VISIBILITY_PRESET default                                 # keep psw_register_plugin exported
    PREFIX ""                                                   # -> sample_sepia_plugin.so
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

# ---- Desktop integration (installed by the package) ----
install(FILES packaging/io.github.shelin_boop.PaintWithMe.desktop DESTINATION share/applications)
# AppStream metadata, so software centers and the AppImage describe the app.
install(FILES packaging/io.github.shelin_boop.PaintWithMe.metainfo.xml
        DESTINATION share/metainfo)
install(FILES packaging/icons/io.github.shelin_boop.PaintWithMe.svg
        DESTINATION share/icons/hicolor/scalable/apps)
foreach(size 16 32 48 64 128 256 512)
  install(FILES packaging/icons/hicolor/${size}x${size}/apps/io.github.shelin_boop.PaintWithMe.png
          DESTINATION share/icons/hicolor/${size}x${size}/apps)
endforeach()
# Ship the sample plugin as a system plugin so a fresh install has a working example
# (loaded from /usr/lib/paint-with-me/plugins).
install(TARGETS sample_sepia_plugin LIBRARY DESTINATION lib/paint-with-me/plugins)

# ---- CPack: build a Debian package with `cpack -G DEB` ----
set(CPACK_PACKAGE_NAME "paint-with-me")
set(CPACK_PACKAGE_VENDOR "Shelin")
# CI passes -DPWM_VERSION=<major.minor.commitcount> so every push to main
# produces a higher version that APT sees as an upgrade; otherwise use PROJECT_VERSION.
set(CPACK_PACKAGE_VERSION "${PWM_VERSION_STR}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Image editor for Linux in the style of Paint.NET (C++/Qt 6)")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Shelin-boop/Paint.with.me")
set(CPACK_PACKAGE_CONTACT "Shelin <Shelin-boop@users.noreply.github.com>")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Shelin <Shelin-boop@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
# Auto-detect the exact Qt/GL runtime dependencies from the built binary. This
# resolves the correct package names for the build distro (e.g. the libqt6*t64
# variants on Ubuntu 24.04), so we do not hard-code a fragile fallback list.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# WebP and TIFF are decoded/encoded by Qt image-format PLUGINS that are loaded at
# runtime, not linked — so SHLIBDEPS can't see them. Depend on the plugin package
# explicitly, otherwise those formats appear in Open/Save but silently fail
# (issue #19).
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-image-formats-plugins")
# Qt renders the standard dialog buttons (OK / Cancel / Yes / No) from its own
# catalogue, which ships separately. Recommends (installed by default) rather than
# Depends: without it the app runs fine, those buttons just stay English.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-translations-l10n")
# Say "not compatible with this system" in plain words rather than leaving the
# user with a raw dependency error. Only reachable via dpkg -i / double-click:
# apt refuses before the package is ever unpacked (see the script's comment).
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb-control/preinst")
set(CPACK_PACKAGE_FILE_NAME "paint-with-me_${CPACK_PACKAGE_VERSION}_amd64")
include(CPack)
