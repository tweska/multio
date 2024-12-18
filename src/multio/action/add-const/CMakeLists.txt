ecbuild_add_library(

    TARGET multio-action-add-const

    TYPE SHARED # Due to reliance on factory self registration this library cannot be static

    SOURCES
        AddConst.cc
        AddConst.h

    PRIVATE_INCLUDES
        ${ECKIT_INCLUDE_DIRS}

    CONDITION

    PUBLIC_LIBS
        multio
)
