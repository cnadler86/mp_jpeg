function(user_c_module_add_component)
    cmake_parse_arguments(COMPONENT_INCL "" "TARGET" "COMPONENTS" ${ARGV})
    foreach(_this_component ${COMPONENT_INCL_COMPONENTS})
        idf_component_get_property(_this_include ${_this_component} INCLUDE_DIRS)
        if (_this_include)
            idf_component_get_property(_this_dir ${_this_component} COMPONENT_DIR)
            list(TRANSFORM _this_include PREPEND ${_this_dir}/)
            target_include_directories(${COMPONENT_INCL_TARGET} INTERFACE ${_this_include})
        endif()
    endforeach()
endfunction()

add_library(usermod_mp_jpeg INTERFACE)

add_dependencies(usermod_mp_jpeg esp_new_jpeg)

idf_component_get_property(JPEG_INCLUDES esp_new_jpeg INCLUDE_DIRS)
idf_component_get_property(JPEG_PRIV_INCLUDES esp_new_jpeg PRIV_INCLUDE_DIRS)

# gets the path to the component
idf_component_get_property(JPEG_DIR esp_new_jpeg COMPONENT_DIR)

# sets the include paths into MOD_INCLUDES variable
if(JPEG_INCLUDES)
    list(TRANSFORM JPEG_INCLUDES PREPEND ${JPEG_DIR}/)
    list(APPEND MOD_INCLUDES ${JPEG_INCLUDES})
endif()

if(JPEG_PRIV_INCLUDES)
    list(TRANSFORM JPEG_PRIV_INCLUDES PREPEND ${JPEG_DIR}/)
    list(APPEND MOD_INCLUDES ${JPEG_PRIV_INCLUDES})
endif()

target_sources(usermod_mp_jpeg INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/jpeg_esp.c
)

target_include_directories(usermod_mp_jpeg INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

# user_c_module_add_component(TARGET usermod_mp_jpeg COMPONENTS esp_new_jpeg)

target_link_libraries(usermod INTERFACE usermod_mp_jpeg)
