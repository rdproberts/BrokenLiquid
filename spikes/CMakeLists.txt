# Phase 0 spikes: throwaway-quality programs that answer one risky question each.
# Results are written to <build>/spikes/out and summarised in docs/adr/.

set(BL_SPIKE_OUT ${CMAKE_BINARY_DIR}/spikes/out)
file(MAKE_DIRECTORY ${BL_SPIKE_OUT})

function(bl_add_spike name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE bl::kernel)
    target_compile_definitions(${name} PRIVATE BL_SPIKE_OUT="${BL_SPIKE_OUT}")
    set_target_properties(${name} PROPERTIES AUTOMOC OFF FOLDER spikes)
    if(BL_BUILD_TESTS)
        add_test(NAME ${name} COMMAND ${name})
    endif()
endfunction()

bl_add_spike(spike_hlr_svg     hlr_svg/main.cpp)
bl_add_spike(spike_mate_solver mate_solver/main.cpp)
target_link_libraries(spike_mate_solver PRIVATE Eigen3::Eigen)
bl_add_spike(spike_naming      naming/main.cpp)
