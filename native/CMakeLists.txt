cmake_minimum_required(VERSION 3.24)
project(OKWWDualPC VERSION 0.2.0 LANGUAGES CXX)
if(WIN32)
    enable_language(RC)
    get_filename_component(OKWW_APPLICATION_ICON "${CMAKE_CURRENT_SOURCE_DIR}/../icons/icon.ico" ABSOLUTE)
    configure_file(resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/okww-app.rc" @ONLY)
    set(APP_RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/okww-app.rc")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CLASSIC_ICONS resources/classic-ui.qrc resources/fluent-icons.qrc resources/classic-emoji.qrc)
set(CLASSIC_UI src/classic_ui.cpp src/classic_ui.hpp src/classic_calendar.cpp src/classic_window.cpp
    src/classic_characters.cpp src/classic_workshop.cpp src/classic_editor.cpp src/classic_editor.hpp src/classic_settings.cpp src/classic_schedule.cpp src/classic_preview.cpp src/classic_task_info.cpp)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent Test)
add_library(okww_native STATIC src/common.cpp src/capture.cpp src/controller.cpp src/backend.cpp)
target_include_directories(okww_native PUBLIC src)
target_compile_definitions(okww_native PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(okww_native PUBLIC /utf-8 /W4 /permissive- /EHsc)
target_link_libraries(okww_native PUBLIC Qt6::Core Qt6::Gui Qt6::Network ws2_32 ole32 oleaut32 strmiids mfplat mfreadwrite mfuuid mf windowsapp)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../main_dualpc.py")
    set(OKWW_COMPAT_DEFAULT ON)
else()
    set(OKWW_COMPAT_DEFAULT OFF)
endif()
option(OKWW_BUILD_COMPAT "Build the retained Python compatibility host" ${OKWW_COMPAT_DEFAULT})
if(OKWW_BUILD_COMPAT)
    add_executable(OKWWDualPC WIN32 src/main.cpp src/window.cpp src/window.hpp ${CLASSIC_UI} ${CLASSIC_ICONS} ${APP_RESOURCES})
    target_link_libraries(OKWWDualPC PRIVATE okww_native Qt6::Widgets Qt6::Concurrent)
    set_target_properties(OKWWDualPC PROPERTIES OUTPUT_NAME "OKWW-DualPC-Cpp")
endif()
enable_testing()
add_executable(native_tests tests/test_native.cpp)
target_link_libraries(native_tests PRIVATE okww_native Qt6::Test)
add_test(NAME native_tests COMMAND native_tests -o "${CMAKE_BINARY_DIR}/native-tests.txt,txt")

option(OKWW_FULL_NATIVE "Build the independent C++ business runtime" OFF)
if(OKWW_FULL_NATIVE)
    set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/opencv/build" CACHE PATH "OpenCV SDK")
    find_package(OpenCV 4.12 REQUIRED COMPONENTS core imgproc imgcodecs)
    target_compile_definitions(okww_native PRIVATE OKWW_FAST_PREVIEW)
    target_include_directories(okww_native PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(okww_native PUBLIC ${OpenCV_LIBS})
    set(OpenVINO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.venv/Lib/site-packages/openvino/cmake" CACHE PATH "OpenVINO SDK")
    find_package(OpenVINO REQUIRED COMPONENTS Runtime)
    add_library(okww_runtime STATIC src/runtime/input.cpp src/runtime/vision.cpp src/runtime/ocr.cpp
        src/runtime/profile.cpp src/runtime/task.cpp src/runtime/engine.cpp src/runtime/combat_rules.cpp
        src/runtime/character.cpp src/runtime/characters_basic.cpp src/runtime/characters_extended.cpp
        src/runtime/characters_stateful.cpp
        src/runtime/characters_advanced.cpp
        src/runtime/characters_gauges.cpp
        src/runtime/characters_forms.cpp
        src/runtime/characters_spectro.cpp
        src/runtime/characters_additional.cpp
        src/runtime/gauge_analysis.cpp
        src/runtime/navigation.cpp
        src/runtime/stamina.cpp
        src/runtime/echo_detector.cpp
        src/runtime/interaction.cpp
        src/runtime/domain.cpp
        src/runtime/instance_flow.cpp
        src/runtime/nightmare.cpp
        src/runtime/garden_vision.cpp
        src/runtime/garden.cpp
        src/runtime/garden_shop.cpp
        src/runtime/echo_farm.cpp
        src/runtime/echo_operations.cpp
        src/runtime/daily.cpp
        src/runtime/multi_account.cpp
        src/runtime/template_store.cpp
        src/runtime/scripts.cpp
        src/runtime/task_registry.cpp
        src/runtime/team_macro.cpp
        src/runtime/team_store.cpp
        src/runtime/team_editor.cpp
        src/runtime/workshop.cpp
        resources/builtin-code.qrc
        src/runtime/scheduler.cpp
        src/runtime/notification.cpp
        src/runtime/notification_desktop.cpp
        src/runtime/combat_vision.cpp
        src/runtime/combat_session.cpp
        third_party/clipper/clipper.cpp)
    target_include_directories(okww_runtime PUBLIC ${OpenCV_INCLUDE_DIRS} PRIVATE third_party/clipper)
    target_link_libraries(okww_runtime PUBLIC okww_native ${OpenCV_LIBS} openvino::runtime advapi32 Qt6::Concurrent Qt6::Gui)
    set_source_files_properties(third_party/clipper/clipper.cpp PROPERTIES COMPILE_OPTIONS /wd4828)
    add_executable(native_runtime_check src/runtime/check_main.cpp)
    target_link_libraries(native_runtime_check PRIVATE okww_runtime)
    add_executable(latency_bench tests/latency_bench.cpp)
    target_link_libraries(latency_bench PRIVATE okww_runtime)
    add_executable(OKWWNativeDev WIN32 src/runtime/main.cpp src/window.cpp src/window.hpp ${CLASSIC_UI} src/schedule_ui.cpp src/classic_templates.cpp src/classic_scripts.cpp src/classic_recorder.cpp ${CLASSIC_ICONS} ${APP_RESOURCES})
    target_compile_definitions(OKWWNativeDev PRIVATE OKWW_NATIVE_ONLY)
    target_link_libraries(OKWWNativeDev PRIVATE okww_runtime Qt6::Widgets Qt6::Concurrent)
    set_target_properties(OKWWNativeDev PROPERTIES OUTPUT_NAME "OKWW-DualPC-Native-Dev")
    add_executable(runtime_tests tests/test_runtime.cpp)
    target_link_libraries(runtime_tests PRIVATE okww_runtime Qt6::Test)
    target_compile_definitions(runtime_tests PRIVATE OKWW_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME runtime_tests COMMAND runtime_tests -o "${CMAKE_BINARY_DIR}/runtime-tests.txt,txt")
    add_executable(tool_tests tests/test_tools.cpp)
    target_link_libraries(tool_tests PRIVATE okww_runtime Qt6::Test)
    target_compile_definitions(tool_tests PRIVATE OKWW_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME tool_tests COMMAND tool_tests -o "${CMAKE_BINARY_DIR}/tool-tests.txt,txt")
    add_executable(combat_rules_check tests/combat_rules_check.cpp)
    target_link_libraries(combat_rules_check PRIVATE okww_runtime)
    add_executable(rotation_check tests/rotation_check.cpp)
    target_link_libraries(rotation_check PRIVATE okww_runtime)
    add_executable(echo_stats_check tests/echo_stats_check.cpp)
    target_link_libraries(echo_stats_check PRIVATE okww_runtime)
    add_executable(geometry_check tests/geometry_check.cpp)
    target_link_libraries(geometry_check PRIVATE okww_runtime)
    # Personal code is a separate target; ordinary executables do not embed it.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/personal/teams.hpp")
        set(OKWW_PERSONAL_DEFAULT ON)
    else()
        set(OKWW_PERSONAL_DEFAULT OFF)
    endif()
    option(OKWW_BUILD_PERSONAL "Build the current migrated personal teams" ${OKWW_PERSONAL_DEFAULT})
    if(OKWW_BUILD_PERSONAL)
    add_library(okww_personal STATIC personal/lucy.cpp personal/cartethyia.cpp personal/denia.cpp personal/aemeath.cpp
        personal/hsin_core.cpp personal/hsin_support.cpp personal/hsin_vision.cpp personal/hsin_combat.cpp)
    target_link_libraries(okww_personal PUBLIC okww_runtime)
    foreach(TEAM_KEY IN ITEMS lucy cart denia aemeath hsin)
        if(TEAM_KEY STREQUAL "cart")
            set(TEAM_FACTORY cartethyiaTeam)
        else()
            set(TEAM_FACTORY "${TEAM_KEY}Team")
        endif()
        configure_file(personal/module_entry.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/${TEAM_KEY}_module.cpp" @ONLY)
        add_library(okww_team_${TEAM_KEY} MODULE "${CMAKE_CURRENT_BINARY_DIR}/${TEAM_KEY}_module.cpp")
        target_include_directories(okww_team_${TEAM_KEY} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(okww_team_${TEAM_KEY} PRIVATE okww_personal)
        set_target_properties(okww_team_${TEAM_KEY} PROPERTIES PREFIX "")
    endforeach()
    add_executable(personal_check tests/personal_check.cpp)
    target_link_libraries(personal_check PRIVATE okww_personal)
    add_executable(team_tests tests/test_teams.cpp)
    target_link_libraries(team_tests PRIVATE okww_runtime Qt6::Test)
    target_compile_definitions(team_tests PRIVATE OKWW_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_dependencies(team_tests okww_team_lucy okww_team_cart okww_team_denia okww_team_aemeath okww_team_hsin)
    add_test(NAME team_tests COMMAND team_tests -o "${CMAKE_BINARY_DIR}/team-tests.txt,txt")
    endif()
    add_executable(engine_tests tests/test_engine.cpp)
    target_link_libraries(engine_tests PRIVATE okww_runtime Qt6::Test)
    target_compile_definitions(engine_tests PRIVATE OKWW_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME engine_tests COMMAND engine_tests -o "${CMAKE_BINARY_DIR}/engine-tests.txt,txt")
    add_executable(notification_tests tests/test_notification.cpp)
    target_link_libraries(notification_tests PRIVATE okww_runtime Qt6::Test)
    target_compile_definitions(notification_tests PRIVATE OKWW_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME notification_tests COMMAND notification_tests -o "${CMAKE_BINARY_DIR}/notification-tests.txt,txt")
    add_executable(workflow_tests tests/test_workflows.cpp)
    target_link_libraries(workflow_tests PRIVATE okww_runtime Qt6::Test)
    target_compile_definitions(workflow_tests PRIVATE OKWW_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME workflow_tests COMMAND workflow_tests -o "${CMAKE_BINARY_DIR}/workflow-tests.txt,txt")
    add_executable(workshop_tests tests/test_workshop.cpp)
    target_link_libraries(workshop_tests PRIVATE okww_runtime Qt6::Test)
    target_compile_definitions(workshop_tests PRIVATE OKWW_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME workshop_tests COMMAND workshop_tests -o "${CMAKE_BINARY_DIR}/workshop-tests.txt,txt")
    add_executable(classic_ui_tests tests/test_classic_ui.cpp src/window.cpp src/window.hpp ${CLASSIC_UI} src/schedule_ui.cpp src/classic_templates.cpp src/classic_scripts.cpp src/classic_recorder.cpp ${CLASSIC_ICONS})
    target_compile_definitions(classic_ui_tests PRIVATE OKWW_NATIVE_ONLY OKWW_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
    target_link_libraries(classic_ui_tests PRIVATE okww_runtime Qt6::Widgets Qt6::Concurrent Qt6::Test)
    add_test(NAME classic_ui_tests COMMAND classic_ui_tests -o "${CMAKE_BINARY_DIR}/classic-ui-tests.txt,txt")
    set_tests_properties(classic_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(engine_tests classic_ui_tests PROPERTIES TIMEOUT 120)
endif()
