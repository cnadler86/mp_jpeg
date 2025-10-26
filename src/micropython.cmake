include(${MICROPY_DIR}/py/py.cmake)

function(get_jpeg_driver_version dir_path)
    file(READ "${dir_path}/idf_component.yml" _component_yml_contents)
    string(REGEX MATCH "version: ([0-9]+\\.[0-9]+(\\.[0-9]+)?)" _ ${_component_yml_contents})
    set(MP_JPEG_DRIVER_VERSION "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

add_library(usermod_mp_jpeg INTERFACE)

add_dependencies(usermod_mp_jpeg esp_new_jpeg)

target_sources(usermod_mp_jpeg INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/jpeg_esp.c
)

if(DEFINED ESP_JPEG_DIR AND EXISTS "${ESP_JPEG_DIR}")
    message(STATUS "Using user-defined esp_new_jpeg directory: ${ESP_JPEG_DIR}")

    get_jpeg_driver_version("${ESP_JPEG_DIR}")

    target_include_directories(usermod_mp_jpeg INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
        ${ESP_JPEG_DIR}/include
    )
elseif(EXISTS "${IDF_PATH}/components/esp_new_jpeg" OR EXISTS "${IDF_PATH}/components/esp-adf-libs/esp_new_jpeg")
    idf_component_get_property(JPEG_INCLUDES esp_new_jpeg INCLUDE_DIRS)
    idf_component_get_property(JPEG_DIR esp_new_jpeg COMPONENT_DIR)

    if (JPEG_DIR)
        message(STATUS "Using esp_new_jpeg component from: ${JPEG_DIR}")
        get_jpeg_driver_version("${JPEG_DIR}")

        if(JPEG_INCLUDES)
            list(TRANSFORM JPEG_INCLUDES PREPEND ${JPEG_DIR}/)
            target_include_directories(usermod_mp_jpeg INTERFACE ${JPEG_INCLUDES})
        endif()
    endif()
else()
    set(_managed_jpeg_dir "${CMAKE_BINARY_DIR}/../managed_components/espressif__esp_new_jpeg")
    if(EXISTS "${_managed_jpeg_dir}")
        message(STATUS "Using esp_new_jpeg from managed_components.")
        get_jpeg_driver_version("${_managed_jpeg_dir}")
    else()
        message(WARNING "esp_new_jpeg component not found (neither as IDF component nor in managed_components)")
    endif()
endif()

# Module strings are not suitable for compression and may cause size increase
target_compile_definitions(usermod_mp_jpeg INTERFACE 
    MICROPY_ROM_TEXT_COMPRESSION=0
    $<$<BOOL:MP_JPEG_DRIVER_VERSION>:MP_JPEG_DRIVER_VERSION=\"${MP_JPEG_DRIVER_VERSION}\">
)

target_link_libraries(usermod INTERFACE usermod_mp_jpeg)
# Gather target properties for MicroPython build system
micropy_gather_target_properties(usermod_mp_jpeg)
