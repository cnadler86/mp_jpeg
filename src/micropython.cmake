# Register the component
# idf_component_register(SRCS "src/jpeg_esp.c"
#                        INCLUDE_DIRS "."
#                        REQUIRES esp_new_jpeg)

# Add the usermod_mp_jpeg library
add_library(usermod_mp_jpeg INTERFACE)

target_sources(usermod_mp_jpeg INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/jpeg_esp.c
)

target_include_directories(usermod_mp_jpeg INTERFACE
    ${CMAKE_CURRENT_LIST_DIR/src}
)

# Link the usermod library with usermod_mp_jpeg
target_link_libraries(usermod INTERFACE usermod_mp_jpeg)
