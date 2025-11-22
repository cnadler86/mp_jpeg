include(${MICROPY_DIR}/py/py.cmake)

add_library(usermod_mp_jpeg INTERFACE)

target_sources(usermod_mp_jpeg INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/jpeg_esp.c
)

# Register dependency on esp_new_jpeg component  
# The component is managed by IDF component manager via idf_component.yml
# Add include directories directly from managed_components (they exist after Component Manager ran)
if(DEFINED ESP_JPEG_DIR AND EXISTS "${ESP_JPEG_DIR}")
    message(STATUS "Using user-defined ESP_JPEG_DIR: ${ESP_JPEG_DIR}")
    set(ESP_JPEG_MANAGED_DIR "${ESP_JPEG_DIR}")
else()
    set(ESP_JPEG_MANAGED_DIR "${MICROPY_PORT_DIR}/managed_components/espressif__esp_new_jpeg")
endif()
if(EXISTS "${ESP_JPEG_MANAGED_DIR}")
    # Add standard include directories for esp_new_jpeg
    list(APPEND MICROPY_INC_USERMOD
        ${ESP_JPEG_MANAGED_DIR}/include
    )
    
    message(STATUS "Found esp_new_jpeg at: ${ESP_JPEG_MANAGED_DIR}")
    
    # Link against the component library when target exists (during actual build)
    if(TARGET espressif__esp_new_jpeg)
        idf_component_get_property(esp_jpeg_lib espressif__esp_new_jpeg COMPONENT_LIB)
        target_link_libraries(usermod_mp_jpeg INTERFACE ${esp_jpeg_lib})
    endif()
    
    if(EXISTS "${ESP_JPEG_MANAGED_DIR}/idf_component.yml")
        file(READ "${ESP_JPEG_MANAGED_DIR}/idf_component.yml" _component_yml_contents)
        string(REGEX MATCH "version: ([0-9]+\\.[0-9]+(\\.[0-9]+)?)" _ ${_component_yml_contents})
        if(CMAKE_MATCH_1)
            set(MP_JPEG_DRIVER_VERSION "${CMAKE_MATCH_1}")
            message(STATUS "Found esp_new_jpeg version: ${MP_JPEG_DRIVER_VERSION}")
        endif()
    endif()
else()
    message(WARNING "esp_new_jpeg component not found at ${ESP_JPEG_MANAGED_DIR}")
endif()

# Module strings are not suitable for compression and may cause size increase
target_compile_definitions(usermod_mp_jpeg INTERFACE 
    MICROPY_ROM_TEXT_COMPRESSION=0
    $<$<BOOL:MP_JPEG_DRIVER_VERSION>:MP_JPEG_DRIVER_VERSION=\"${MP_JPEG_DRIVER_VERSION}\">
)

target_link_libraries(usermod INTERFACE usermod_mp_jpeg)
# Gather target properties for MicroPython build system
micropy_gather_target_properties(usermod_mp_jpeg)
