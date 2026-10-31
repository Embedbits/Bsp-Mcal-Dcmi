# Template version: 0.0.1
# Sets the version of CMakeLists.txt file.
# Never, under any circumstances, change this line. It is used by
# the module generation script to identify the template version.

#======================== Library configuration ===============================#
# Publicly included directories to provide headers include
set( Dcmi_PublicIncludeDirs

)


# Privately included directories to provide headers include
set( Dcmi_PrivateIncludeDirs
    ${CMAKE_CURRENT_SOURCE_DIR}
)


# Source files list for current library generation
set( Dcmi_SourceFiles
    Dcmi.c
    Dcmi_Reg.c
)


# List of public header files provided by the current library
set( Dcmi_PublicHeaders
    Dcmi_Types.h
    Dcmi_Pins.h
    Dcmi_Port.h
)


# List of dependent libraries that are publicly used by the current library
set( Dcmi_PublicDependentLibs
    Ral_Lib
)


# List of dependent libraries that are privately used by the current library
set( Dcmi_PrivateDependentLibs
    Rcc_Lib
    Nvic_Lib
    Gpio_Lib
    Gpdma_Lib
    Cache_Lib
)


#========================== Library generation ================================#
# Create library
add_library(Dcmi_Lib STATIC ${Dcmi_SourceFiles})


# Include necessary libraries to the target
target_link_libraries( Dcmi_Lib
    PUBLIC
        ${Dcmi_PublicDependentLibs}
    PRIVATE
        ${Dcmi_PrivateDependentLibs}
)


# Set public headers directory path in build folder
set(PUBLIC_HEADERS_DIR ${CMAKE_CURRENT_BINARY_DIR}/PublicHeaders)

# Generates the public headers directory in build folder
file(MAKE_DIRECTORY ${PUBLIC_HEADERS_DIR})

# Process all public headers
foreach(PublicHeader IN LISTS Dcmi_PublicHeaders)

    # Copy (or configure) only the public header there
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/${PublicHeader}
        ${PUBLIC_HEADERS_DIR}/${PublicHeader}
        COPYONLY
    )

endforeach()


# Set include directories for the created target
target_include_directories( Dcmi_Lib
    PUBLIC
        $<BUILD_INTERFACE:${PUBLIC_HEADERS_DIR}>
        $<INSTALL_INTERFACE:include>
        ${Dcmi_PublicIncludeDirs}
    PRIVATE
        ${Dcmi_PrivateIncludeDirs}
)


# Print status message
message(STATUS "Dcmi_Lib library created")

#========================= Documentation configuration ========================#
if(DOXYGEN_AVAILABLE STREQUAL "ON")
    Doxygen_AddPath("${CMAKE_CURRENT_SOURCE_DIR}/")
endif()

#========================= Unit tests configuration ===========================#
if(UNIT_TESTING_AVAILABLE STREQUAL "ON")
    UnitTesting_AddPath("${CMAKE_CURRENT_SOURCE_DIR}/Tests/UnitTests")
endif()

#===================== Integration tests configuration ========================#
if(INTEGRATION_TESTING_AVAILABLE STREQUAL "ON")
    IntegrationTesting_AddPath("${CMAKE_CURRENT_SOURCE_DIR}/Tests/IntegrationTests")
endif()
