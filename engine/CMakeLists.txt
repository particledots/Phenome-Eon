# The PhenomeEon audio engine. Plain C++17, no JUCE, no plugin code.
# Later plugins (Lubadh-, Iroi-, Mimeophon-style) can reuse this folder.
#   include/phe/core/   generic, reusable parts (scales, level helpers, waveform picture; resampler, file links later)
#   include/phe/        the reel (Reel.h), the engine (Engine.h) and the list of control values it understands (Params.h)

add_library(phe_engine STATIC
    src/Engine.cpp
    src/Reel.cpp
)

target_include_directories(phe_engine PUBLIC include)
target_compile_features(phe_engine PUBLIC cxx_std_17)

if(PHE_BUILD_TESTS)
    add_executable(phe_engine_test tests/engine_test.cpp)
    target_link_libraries(phe_engine_test PRIVATE phe_engine)
    add_test(NAME engine COMMAND phe_engine_test)
endif()
