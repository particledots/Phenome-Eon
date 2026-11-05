# The PhenomeEon audio engine. Plain C++17, no JUCE, no plugin code.
# Later plugins (Lubadh-, Iroi-, Mimeophon-style) can reuse this folder.
#   include/phe/core/   generic, reusable parts (scales, level helpers, waveform picture, undo store, splice map, resampler, reel WAV files)
#   include/phe/        the reel (Reel.h), the engine (Engine.h), the file link library (ReelLibrary.h) and the list of control values it understands (Params.h)

add_library(phe_engine STATIC
    src/Engine.cpp
    src/GenePlayer.cpp
    src/Reel.cpp
    src/ReelFile.cpp
    src/ReelLibrary.cpp
)

target_include_directories(phe_engine PUBLIC include)
target_compile_features(phe_engine PUBLIC cxx_std_17)

if(PHE_BUILD_TESTS)
    add_executable(phe_engine_test tests/engine_test.cpp)
    target_link_libraries(phe_engine_test PRIVATE phe_engine)
    add_test(NAME engine COMMAND phe_engine_test)

    # Milestone 9: reel files, the file link library, and the engine's save / load hand-over (uses real files in a temporary folder).
    add_executable(phe_reelfile_test tests/reelfile_test.cpp)
    target_link_libraries(phe_reelfile_test PRIVATE phe_engine)
    add_test(NAME reelfile COMMAND phe_reelfile_test)
endif()
