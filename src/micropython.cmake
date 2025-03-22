function(check_and_register_component target component)
  get_target_property(LIBRARIES ${target} INTERFACE_LINK_LIBRARIES)
  if(NOT "${LIBRARIES}" MATCHES "${component}")
    target_link_libraries(${target} INTERFACE ${component})
  endif()
endfunction()

add_library(usermod_mp_jpeg INTERFACE)

add_dependencies(usermod_mp_jpeg esp_new_jpeg)

target_sources(usermod_mp_jpeg INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/jpeg_esp.c
)

if(DEFINED ESP_JPEG_DIR AND EXISTS "${ESP_JPEG_DIR}")
    message(STATUS "Using user-defined esp_new_jpeg directory: ${ESP_JPEG_DIR}")

    target_include_directories(usermod_mp_jpeg INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
        ${ESP_JPEG_DIR}/include
    )
elseif(EXISTS "${IDF_PATH}/components/esp_new_jpeg" OR EXISTS "${IDF_PATH}/components/esp-adf-libs/esp_new_jpeg")
    idf_component_get_property(JPEG_INCLUDES esp_new_jpeg INCLUDE_DIRS)
    idf_component_get_property(JPEG_DIR esp_new_jpeg COMPONENT_DIR)

    if (JPEG_DIR)
        message(STATUS "Using esp_new_jpeg component from: ${JPEG_DIR}")
        
        if(JPEG_INCLUDES)
            list(TRANSFORM JPEG_INCLUDES PREPEND ${JPEG_DIR}/)
            target_include_directories(usermod_mp_jpeg INTERFACE ${JPEG_INCLUDES})
        endif()
    else()
        message(WARNING "esp_new_jpeg component not found")
        target_include_directories(usermod_mp_jpeg PUBLIC ${CMAKE_CURRENT_LIST_DIR})
    endif()
endif()

check_and_register_component(usermod usermod_mp_jpeg)