set(SOURCES
    error.h
    json.c
    json.h
    state.c
    state.h
    http.c
    http.h
    args.c
    args.h
    commands.c
    commands.h
    help.c
    help.h
    snapshot.c
    snapshot.h
    main.c
    main.h
)

add_executable(browser4-cli ${SOURCES})

target_include_directories(browser4-cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    target_link_libraries(browser4-cli ws2_32.lib winhttp.lib)
endif()

# Read version from VERSION-CLI file if available
set(VERSION_CLI_FILE "${CMAKE_SOURCE_DIR}/../VERSION-CLI")
if(EXISTS "${VERSION_CLI_FILE}")
    file(STRINGS "${VERSION_CLI_FILE}" BROWSER4_CLI_VERSION_RAW LIMIT_COUNT 1)
    string(STRIP "${BROWSER4_CLI_VERSION_RAW}" BROWSER4_CLI_VERSION)
    # Strip -SNAPSHOT suffix
    string(REPLACE "-SNAPSHOT" "" BROWSER4_CLI_VERSION "${BROWSER4_CLI_VERSION}")
else()
    set(BROWSER4_CLI_VERSION "0.0.0-dev")
endif()
target_compile_definitions(browser4-cli PRIVATE BROWSER4_CLI_VERSION="${BROWSER4_CLI_VERSION}")
