message("found pico.cmake")

#set(CMAKE_SYSTEM_NAME Generic)
message(${CMAKE_SYSTEM_NAME})

enable_language( C CXX ASM )

# Include the Pico SDK cmake file
# Include the subsidiary .cmake file to get the SDK
list(APPEND CMAKE_MODULE_PATH "/Users/mike/development/pico-sdk/external")
include(pico_sdk_import)

set(PICO_CXX_ENABLE_EXCEPTIONS 1)

# Link the Project to a source file (step 4.6)
add_executable(${OUTPUT_NAME} src/main.cpp)

target_compile_options(${OUTPUT_NAME} PRIVATE -O3)

# Link the Project to an extra library (pico_stdlib)
target_link_libraries(${OUTPUT_NAME}
    pico_stdlib
    pico_multicore
    hub75
)

# Initalise the SDK
pico_sdk_init()

pico_add_extra_outputs(${OUTPUT_NAME})
pico_enable_stdio_usb(${OUTPUT_NAME} 1)
pico_enable_stdio_uart(${OUTPUT_NAME} 0)
