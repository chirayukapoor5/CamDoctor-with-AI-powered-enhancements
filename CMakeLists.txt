cmake_minimum_required(VERSION 3.24)

project(WebcamSettings VERSION 0.2.1 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "CamDoctor native builds currently target Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Multimedia Concurrent Network)

qt_standard_project_setup()

set(WEBCAM_SETTINGS_SOURCES
    src/main.cpp
    src/camera_control.cpp
    src/camera_performance.cpp
    src/image_quality.cpp
    src/image_effects.cpp
    src/camera_director_tools.cpp
    src/groq_client.cpp
    src/groq_settings_dialog.cpp
    src/secure_credential_store.cpp
    src/control_row.cpp
    src/main_window.cpp
    src/profile_store.cpp
)

list(APPEND WEBCAM_SETTINGS_SOURCES
    src/windows_control_backend.cpp
    src/windows_camera_usage.cpp
    src/windows_camera_rotation.cpp
)

qt_add_executable(webcam-settings WIN32 ${WEBCAM_SETTINGS_SOURCES})

target_include_directories(webcam-settings PRIVATE src)
target_compile_definitions(webcam-settings PRIVATE WEBCAM_SETTINGS_VERSION="${PROJECT_VERSION}")
target_link_libraries(webcam-settings PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::Concurrent
    Qt6::Network
)

set(WEBCAM_SETTINGS_CLI_SOURCES
    src/cli_main.cpp
    src/camera_control.cpp
    src/profile_store.cpp
)
list(APPEND WEBCAM_SETTINGS_CLI_SOURCES
    src/windows_control_backend.cpp
    src/windows_camera_usage.cpp
    src/windows_camera_rotation.cpp
)

qt_add_executable(webcam-settings-cli ${WEBCAM_SETTINGS_CLI_SOURCES})
target_include_directories(webcam-settings-cli PRIVATE src)
target_compile_definitions(webcam-settings-cli PRIVATE WEBCAM_SETTINGS_VERSION="${PROJECT_VERSION}")
target_link_libraries(webcam-settings-cli PRIVATE Qt6::Core Qt6::Gui Qt6::Multimedia)

target_link_libraries(webcam-settings PRIVATE strmiids ole32 oleaut32 mfplat mf mfuuid cfgmgr32 advapi32 shell32)
target_link_libraries(webcam-settings-cli PRIVATE strmiids ole32 oleaut32 mfplat mf mfuuid cfgmgr32 advapi32 shell32)
target_compile_definitions(webcam-settings PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(webcam-settings-cli PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

set_target_properties(webcam-settings PROPERTIES
    OUTPUT_NAME "webcam-settings"
)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    qt_add_executable(profile-store-tests
        tests/profile_store_tests.cpp
        src/camera_control.cpp
        src/profile_store.cpp
    )
    target_include_directories(profile-store-tests PRIVATE src)
    target_link_libraries(profile-store-tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME profile-store-tests COMMAND profile-store-tests)

    qt_add_executable(windows-camera-rotation-tests
        tests/windows_camera_rotation_tests.cpp src/windows_camera_rotation.cpp)
    target_include_directories(windows-camera-rotation-tests PRIVATE src)
    target_link_libraries(windows-camera-rotation-tests PRIVATE Qt6::Core Qt6::Test cfgmgr32 advapi32 shell32)
    target_compile_definitions(windows-camera-rotation-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME windows-camera-rotation-tests COMMAND windows-camera-rotation-tests)

    qt_add_executable(camera-usage-tests
        tests/camera_usage_tests.cpp
        src/windows_camera_usage.cpp
    )
    target_include_directories(camera-usage-tests PRIVATE src)
    target_link_libraries(camera-usage-tests PRIVATE Qt6::Core Qt6::Test)
    target_compile_definitions(camera-usage-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME camera-usage-tests COMMAND camera-usage-tests)

    qt_add_executable(camera-performance-tests
        tests/camera_performance_tests.cpp
        src/camera_performance.cpp
    )
    target_include_directories(camera-performance-tests PRIVATE src)
    target_link_libraries(camera-performance-tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME camera-performance-tests COMMAND camera-performance-tests)

    qt_add_executable(image-quality-tests
        tests/image_quality_tests.cpp
        src/image_quality.cpp
    )
    target_include_directories(image-quality-tests PRIVATE src)
    target_link_libraries(image-quality-tests PRIVATE
        Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Test
    )
    add_test(NAME image-quality-tests COMMAND image-quality-tests)

    qt_add_executable(image-effects-tests
        tests/image_effects_tests.cpp
        src/image_effects.cpp
    )
    target_include_directories(image-effects-tests PRIVATE src)
    target_link_libraries(image-effects-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME image-effects-tests COMMAND image-effects-tests)

    qt_add_executable(camera-director-tools-tests
        tests/camera_director_tools_tests.cpp
        src/camera_director_tools.cpp
        src/camera_control.cpp
        src/image_effects.cpp
    )
    target_include_directories(camera-director-tools-tests PRIVATE src)
    target_link_libraries(camera-director-tools-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME camera-director-tools-tests COMMAND camera-director-tools-tests)

    qt_add_executable(groq-client-tests
        tests/groq_client_tests.cpp
        src/groq_client.cpp
    )
    target_include_directories(groq-client-tests PRIVATE src)
    target_link_libraries(groq-client-tests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME groq-client-tests COMMAND groq-client-tests)

    qt_add_executable(secure-credential-store-tests
        tests/secure_credential_store_tests.cpp
        src/secure_credential_store.cpp
    )
    target_include_directories(secure-credential-store-tests PRIVATE src)
    target_link_libraries(secure-credential-store-tests PRIVATE Qt6::Core Qt6::Test advapi32)
    target_compile_definitions(secure-credential-store-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME secure-credential-store-tests COMMAND secure-credential-store-tests)

    set(BACKEND_TEST_SOURCES tests/backend_smoke_tests.cpp src/camera_control.cpp)
    list(APPEND BACKEND_TEST_SOURCES
        src/windows_control_backend.cpp
        src/windows_camera_rotation.cpp
    )
    qt_add_executable(backend-smoke-tests ${BACKEND_TEST_SOURCES})
    target_include_directories(backend-smoke-tests PRIVATE src)
    target_link_libraries(backend-smoke-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Test)
    target_link_libraries(backend-smoke-tests PRIVATE strmiids ole32 oleaut32 mfplat mf mfuuid cfgmgr32 advapi32 shell32)
    target_compile_definitions(backend-smoke-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME backend-smoke-tests COMMAND backend-smoke-tests)
    add_test(NAME cli-help COMMAND webcam-settings-cli --help)
endif()

install(TARGETS webcam-settings
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
install(TARGETS webcam-settings-cli RUNTIME DESTINATION bin)

qt_generate_deploy_app_script(
    TARGET webcam-settings
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
