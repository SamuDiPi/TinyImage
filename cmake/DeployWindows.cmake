# Copia accanto all'eseguibile tutte le DLL necessarie (Qt, runtime MinGW, dipendenze),
# così l'exe si avvia con un doppio clic anche fuori da MSYS2.
#
# Uso (in modalità script):
#   cmake -DEXE=<Alleggerisci.exe> -DPLATFORM_PLUGIN=<qwindows.dll> -DSEARCH_DIRS=<dir;dir>
#         -DOBJDUMP=<objdump.exe> -P DeployWindows.cmake

if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

foreach(var EXE PLATFORM_PLUGIN SEARCH_DIRS OBJDUMP)
    if(NOT ${var})
        message(FATAL_ERROR "DeployWindows: manca la variabile ${var}")
    endif()
endforeach()

get_filename_component(dest "${EXE}" DIRECTORY)

# Plugin di piattaforma: Qt lo cerca in <cartella dell'exe>/platforms.
file(MAKE_DIRECTORY "${dest}/platforms")
file(COPY "${PLATFORM_PLUGIN}" DESTINATION "${dest}/platforms")
get_filename_component(plugin_name "${PLATFORM_PLUGIN}" NAME)

# Plugin TLS di Windows (Schannel) per gli aggiornamenti in HTTPS: Qt lo cerca in <cartella dell'exe>/tls.
set(tls_module "")
if(TLS_PLUGIN AND EXISTS "${TLS_PLUGIN}")
    file(MAKE_DIRECTORY "${dest}/tls")
    file(COPY "${TLS_PLUGIN}" DESTINATION "${dest}/tls")
    get_filename_component(tls_name "${TLS_PLUGIN}" NAME)
    set(tls_module "${dest}/tls/${tls_name}")
endif()

set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${OBJDUMP}")

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${EXE}"
    MODULES "${dest}/platforms/${plugin_name}" ${tls_module}
    RESOLVED_DEPENDENCIES_VAR deps
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    CONFLICTING_DEPENDENCIES_PREFIX conflict
    DIRECTORIES ${SEARCH_DIRS}
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\]"
)

# Dalla seconda compilazione le DLL si trovano sia accanto all'exe sia nella cartella di Qt:
# si prende sempre l'originale (così un aggiornamento di Qt viene ricopiato).
foreach(name IN LISTS conflict_FILENAMES)
    foreach(candidate IN LISTS conflict_${name})
        get_filename_component(candidate_dir "${candidate}" DIRECTORY)
        if(NOT candidate_dir STREQUAL dest)
            list(APPEND deps "${candidate}")
            break()
        endif()
    endforeach()
endforeach()

list(LENGTH deps count)
foreach(dll IN LISTS deps)
    file(COPY "${dll}" DESTINATION "${dest}")
endforeach()

message(STATUS "DeployWindows: ${count} DLL copiate in ${dest}")
