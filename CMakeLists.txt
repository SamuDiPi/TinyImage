cmake_minimum_required(VERSION 3.21)
project(Alleggerisci
    VERSION 0.1.0
    DESCRIPTION "Foto e schermate più leggere, senza perdita di qualità visibile"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
find_package(JPEG REQUIRED)   # libjpeg-turbo
find_library(IMAGEQUANT_LIB NAMES imagequant REQUIRED)
find_library(ZOPFLIPNG_LIB NAMES zopflipng REQUIRED)
find_library(ZOPFLI_LIB NAMES zopfli REQUIRED)
find_path(IMAGEQUANT_INCLUDE libimagequant.h REQUIRED)
find_path(ZOPFLIPNG_INCLUDE zopflipng_lib.h REQUIRED)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Motore (senza finestre): usato dal programma e dai test.
add_library(alleggerisci_core STATIC src/Codec.cpp src/Files.cpp)
target_include_directories(alleggerisci_core PUBLIC src ${IMAGEQUANT_INCLUDE} ${ZOPFLIPNG_INCLUDE})
target_link_libraries(alleggerisci_core PUBLIC Qt6::Core Qt6::Gui JPEG::JPEG ${IMAGEQUANT_LIB} ${ZOPFLIPNG_LIB} ${ZOPFLI_LIB})

set(app_sources src/main.cpp src/MainWindow.cpp src/CompareDialog.cpp resources/resources.qrc)
if(WIN32)
    string(REPLACE "." "," ver_comma "${PROJECT_VERSION}.0")
    configure_file(resources/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    list(APPEND app_sources ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()
add_executable(Alleggerisci WIN32 ${app_sources})
target_compile_definitions(Alleggerisci PRIVATE ALLEGGERISCI_VERSION="${PROJECT_VERSION}")
target_link_libraries(Alleggerisci PRIVATE alleggerisci_core Qt6::Widgets Qt6::Concurrent)

enable_testing()
add_executable(alleggerisci_tests tests/tests.cpp)
target_link_libraries(alleggerisci_tests PRIVATE alleggerisci_core)
add_test(NAME motore COMMAND alleggerisci_tests)

# Windows + MinGW: dopo ogni compilazione le DLL vanno accanto all'exe (avvio con doppio clic); "cmake --install"
# prepara la cartella da distribuire con le licenze dei componenti.
if(WIN32 AND MINGW AND CMAKE_OBJDUMP)
    get_filename_component(compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(deploy_args
        -DPLATFORM_PLUGIN=$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>
        "-DSEARCH_DIRS=$<TARGET_FILE_DIR:Qt6::Core>$<SEMICOLON>${compiler_bin}"
        -DOBJDUMP=${CMAKE_OBJDUMP}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployWindows.cmake)
    add_custom_command(TARGET Alleggerisci POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:Alleggerisci> ${deploy_args}
        COMMENT "Copia delle DLL accanto ad Alleggerisci.exe"
        VERBATIM)
    add_custom_command(TARGET alleggerisci_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:alleggerisci_tests> ${deploy_args}
        COMMENT "Copia delle DLL accanto ai test"
        VERBATIM)

    install(TARGETS Alleggerisci RUNTIME DESTINATION .)
    install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\"
        \"-DEXE=\${CMAKE_INSTALL_PREFIX}/$<TARGET_FILE_NAME:Alleggerisci>\"
        \"-DPLATFORM_PLUGIN=$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>\"
        \"-DSEARCH_DIRS=$<TARGET_FILE_DIR:Qt6::Core>;${compiler_bin}\"
        \"-DOBJDUMP=${CMAKE_OBJDUMP}\"
        -P \"${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployWindows.cmake\"
        COMMAND_ERROR_IS_FATAL ANY)")
    get_filename_component(msys_root "${compiler_bin}/../.." ABSOLUTE)
    install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E env MSYS2_ROOT= \"${msys_root}/usr/bin/bash.exe\"
        \"${CMAKE_CURRENT_SOURCE_DIR}/scripts/collect-licenses.sh\" \"\${CMAKE_INSTALL_PREFIX}\"
        COMMAND_ERROR_IS_FATAL ANY)")
endif()
