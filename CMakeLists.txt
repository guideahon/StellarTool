cmake_minimum_required(VERSION 3.21)
project(StellarTool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(BUILD_TESTS "Build unit tests (StellarToolTests)" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Quick Concurrent Widgets Test)

qt_standard_project_setup(REQUIRES 6.4)

set(CORE_SOURCES
    src/core/ModTypes.cpp
    src/core/PakService.cpp
    src/core/UAssetService.cpp
    src/core/BaselineManager.cpp
    src/core/ModImporter.cpp
    src/core/TableDiffEngine.cpp
    src/core/MergeEngine.cpp
    src/core/ProjectStore.cpp
    src/models/ModListModel.cpp
    src/models/ChangeListModel.cpp
    src/models/ConflictModel.cpp
    src/AppController.cpp
)
set(CORE_HEADERS
    src/core/ModTypes.h
    src/core/PakService.h
    src/core/UAssetService.h
    src/core/BaselineManager.h
    src/core/ModImporter.h
    src/core/TableDiffEngine.h
    src/core/MergeEngine.h
    src/core/ProjectStore.h
    src/models/ModListModel.h
    src/models/ChangeListModel.h
    src/models/ConflictModel.h
    src/AppController.h
)

qt_add_executable(StellarTool
    src/main.cpp
    ${CORE_SOURCES}
    ${CORE_HEADERS}
)

file(GLOB_RECURSE QML_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
qt_add_resources(StellarTool "qml"
    PREFIX "/"
    FILES ${QML_FILES} qml/qmldir
)

target_include_directories(StellarTool PRIVATE src)
target_link_libraries(StellarTool PRIVATE Qt6::Core Qt6::Quick Qt6::Concurrent Qt6::Widgets)
set_target_properties(StellarTool PROPERTIES WIN32_EXECUTABLE TRUE)

if (BUILD_TESTS)
    enable_testing()
    qt_add_executable(StellarToolTests
        tests/tst_main.cpp
        tests/TestDiffEngine.cpp
        tests/TestMergeEngine.cpp
        ${CORE_SOURCES}
        ${CORE_HEADERS}
    )
    target_include_directories(StellarToolTests PRIVATE src tests)
    target_link_libraries(StellarToolTests PRIVATE Qt6::Core Qt6::Quick Qt6::Concurrent Qt6::Widgets Qt6::Test)
    target_compile_definitions(StellarToolTests PRIVATE
        ST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    set_target_properties(StellarToolTests PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME StellarToolTests COMMAND StellarToolTests)
endif()
