cmake_minimum_required(VERSION 3.16)
project(juraic_field VERSION 0.9.7.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)
if(MSVC)
    add_compile_options(/utf-8)
    # Static runtime: the distributed executables need no extra DLL.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    # core/field_bits.h uses SSE4.1; PEXT/PDEP is selected at run time.
    add_compile_options(-msse4.1 -mpopcnt)
endif()

# The engine: bitboard core (header only) and the generator.
add_library(juraic_engine STATIC
    generator/generator.cpp
    generator/runner.cpp
    generator/grid_runner.cpp
    generator/generator.h
    generator/grid_field.h generator/grid_generator.h generator/run_loop.h
    core/field_bits.h core/bit_field.h core/reach.h core/rng.h
    app/console_output.h)
find_package(Threads REQUIRED)
target_link_libraries(juraic_engine PUBLIC Threads::Threads)

add_executable(random_19_chain app/main.cpp)
target_link_libraries(random_19_chain PRIVATE juraic_engine)
if(MINGW)
    target_link_options(random_19_chain PRIVATE -municode)
endif()

if(WIN32)
    enable_language(RC)
    add_executable(juraic_generator_gui WIN32 gui/generator_gui.cpp gui/generator_gui.rc)
    set_target_properties(juraic_generator_gui PROPERTIES OUTPUT_NAME JuraicFieldGenerator)
    target_link_libraries(juraic_generator_gui PRIVATE juraic_engine comctl32 comdlg32 shell32 ole32 gdi32 user32)
    if(MSVC)
        target_link_options(juraic_generator_gui PRIVATE "/MANIFEST:EMBED" "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/gui/generator_gui.manifest")
    endif()
    if(MINGW)
        target_link_options(juraic_generator_gui PRIVATE -municode -static -static-libgcc -static-libstdc++)
    endif()
endif()

if(MSVC)
    # The playable game (Direct2D, XAudio2). One self-contained executable.
    add_executable(juraic_field_game WIN32 play/main.cpp play/session.h play/view.h play/view_state.h play/canvas.h play/gfx.h play/gfx_d2d.h play/audio.h play/media.h play/settings.h play/pad.h play/input.h play/replay.h play/net.h play/udp.h play/netplay.h play/realgame_win.h play/realgame_read.h play/realgame_follow.h play/realgame_drive.h play/realgame_capture_win.h play/realgame_pad_win.h play/realgame_log.h play/realgame_menu.h play/realgame_pace.h play/start_beat.h game/link.h)
    set_target_properties(juraic_field_game PROPERTIES OUTPUT_NAME JuraicField)
    target_link_libraries(juraic_field_game PRIVATE juraic_engine d2d1 dwrite windowscodecs xaudio2 mfplat mfreadwrite mfuuid ole32 shell32 comdlg32 user32 windowsapp xinput ws2_32 d3d11 dxgi dwmapi winmm)
    # The pad the mode of the real game presses 2P with: the client of the driver ViGEmBus (third_party/ViGEmClient, MIT).
    add_library(vigem_client STATIC third_party/ViGEmClient/src/ViGEmClient.cpp)
    target_include_directories(vigem_client PUBLIC third_party/ViGEmClient/include PRIVATE third_party/ViGEmClient/src)
    target_link_libraries(vigem_client PUBLIC setupapi)
    target_link_libraries(juraic_field_game PRIVATE vigem_client)
    # The parts of that mode for finding faults (play/realgame_win.h): the record of each reading, a table read from
    # realgame_layout.txt, the key S that saves a picture of the real game, and the counters on the screen of the mode.
    # With them, the watch over the window's thread (play/stall_win.h): where every thread is when the window stops
    # answering, named from the PDB built next to the program.
    option(JURAIC_REALGAME_DEBUG "Build the parts of the mode of the real game for finding faults" OFF)
    if(JURAIC_REALGAME_DEBUG)
        target_sources(juraic_field_game PRIVATE play/stall_win.h)
        target_compile_definitions(juraic_field_game PRIVATE JURAIC_REALGAME_DEBUG=1)
        target_compile_options(juraic_field_game PRIVATE /Zi)
        target_link_options(juraic_field_game PRIVATE /DEBUG /OPT:REF /OPT:ICF)
        target_link_libraries(juraic_field_game PRIVATE dbghelp)
    endif()
    # The pads of Sony and Nintendo are read through SDL 3 (play/pad.h), built as a static library and found by
    # CMAKE_PREFIX_PATH. Without it they are read as raw controllers of Windows, as every pad other than Xbox pads.
    find_package(SDL3 CONFIG)
    if(SDL3_FOUND)
        target_compile_definitions(juraic_field_game PRIVATE JURAIC_PADS_SDL)
        target_link_libraries(juraic_field_game PRIVATE SDL3::SDL3)
    else()
        message(WARNING "SDL3 was not found: the game reads the pads of Sony and Nintendo as raw controllers of Windows")
    endif()
    # The pictures and the music of the package (media/): next to the game, in the folders it looks into.
    add_custom_command(TARGET juraic_field_game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/media/background" "$<TARGET_FILE_DIR:juraic_field_game>/background"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/media/bgm" "$<TARGET_FILE_DIR:juraic_field_game>/bgm"
        VERBATIM)
elseif(ANDROID)
    # The game for Android: the shared library "main" that the activity of SDL loads (android/), and SDL 3 built from
    # its sources (JURAIC_SDL_SOURCE: an unpacked release of SDL). The pictures, the music and the font are assets of
    # the APK (android/app/build.gradle).
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    add_subdirectory("${JURAIC_SDL_SOURCE}" sdl)
    add_library(juraic_field_game SHARED play/main.cpp play/third_party_impl.cpp play/session.h play/view.h play/view_state.h play/canvas.h
                play/gfx.h play/gfx_soft.h play/raster.h play/platform_sdl.h play/pad_sdl.h play/keys.h play/keyboard.h play/audio.h
                play/media.h play/settings.h play/input.h play/replay.h play/net.h play/udp.h play/netplay.h play/start_beat.h game/link.h)
    set_target_properties(juraic_field_game PROPERTIES OUTPUT_NAME main)
    target_link_libraries(juraic_field_game PRIVATE juraic_engine SDL3::SDL3)
else()
    # The playable game elsewhere: SDL 3 (built as a static library and found by CMAKE_PREFIX_PATH, or installed),
    # the libraries of one file in third_party/, a drawing engine of its own (play/raster.h).
    find_package(SDL3 CONFIG)
    if(SDL3_FOUND)
        add_executable(juraic_field_game play/main.cpp play/third_party_impl.cpp play/session.h play/view.h play/view_state.h play/canvas.h
                       play/gfx.h play/gfx_soft.h play/raster.h play/platform_sdl.h play/pad_sdl.h play/keys.h play/keyboard.h play/audio.h
                       play/media.h play/settings.h play/input.h play/replay.h play/net.h play/udp.h play/netplay.h play/start_beat.h game/link.h)
        set_target_properties(juraic_field_game PROPERTIES OUTPUT_NAME JuraicField)
        target_link_libraries(juraic_field_game PRIVATE juraic_engine SDL3::SDL3)
        if(APPLE)
            # An application bundle of macOS: the files that come with the game in its Resources (play/platform_sdl.h,
            # programFolders).
            set_target_properties(juraic_field_game PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_BUNDLE_NAME "Juraic Field"
                MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.nikque.juraicfield" MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
                MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}" MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/play/Info.plist.in")
            set(game_files "$<TARGET_BUNDLE_CONTENT_DIR:juraic_field_game>/Resources")
        else()
            set(game_files "$<TARGET_FILE_DIR:juraic_field_game>")
        endif()
        add_custom_command(TARGET juraic_field_game POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/media/background" "${game_files}/background"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/media/bgm" "${game_files}/bgm"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/media/fonts" "${game_files}/fonts"
            VERBATIM)
    else()
        message(STATUS "SDL3 was not found: the game is not built (the engine, the tools and the tests are)")
    endif()
endif()

# Matches between AIs without a screen (ai/, play/replay.h).
add_executable(ai_match tools/ai_match.cpp ai/agents.h ai/level.h ai/tactics.h ai/pace.h ai/exchange.h ai/gaze.h ai/value.h ai/agent.h ai/build.h ai/duel.h ai/observe.h ai/input.h play/replay.h play/settings.h)
target_link_libraries(ai_match PRIVATE Threads::Threads)
# One player by the beam search: how large a chain, how fast.
add_executable(ai_toko tools/ai_toko.cpp tools/toko_game.h ai/build.h ai/detect.h ai/params.h)
target_link_libraries(ai_toko PRIVATE Threads::Threads)
# A saved picture of the real game's window read with a table (play/realgame_read.h).
add_executable(realgame_read tools/realgame_read.cpp play/realgame_read.h)
# Speed and success rate of the search on the 10 x 19 field.
add_executable(grid_bench tools/grid_bench.cpp)
target_link_libraries(grid_bench PRIVATE juraic_engine)
# Tuning of the weights with one player.
add_executable(ai_tune tools/ai_tune.cpp tools/toko_game.h ai/build.h ai/detect.h ai/params.h)
target_link_libraries(ai_tune PRIVATE Threads::Threads)

include(CTest)
if(BUILD_TESTING)
    # Bitboard core and generator against cell-by-cell references, once with
    # the SSE implementation and once with the portable 64-bit one.
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE juraic_engine)
    add_test(NAME core_and_generator COMMAND core_tests)
    add_executable(core_tests_portable tests/core_tests.cpp generator/generator.cpp generator/runner.cpp generator/grid_runner.cpp)
    target_compile_definitions(core_tests_portable PRIVATE JURAIC_FORCE_PORTABLE)
    target_link_libraries(core_tests_portable PRIVATE Threads::Threads)
    add_test(NAME core_and_generator_portable COMMAND core_tests_portable)
    # The field of any size and the template search: against the 6 x 13 engine
    # and against a cell-by-cell reference on 10 x 19.
    add_executable(grid_tests tests/grid_tests.cpp)
    target_link_libraries(grid_tests PRIVATE juraic_engine)
    add_test(NAME grid_field_and_search COMMAND grid_tests)

    # Move-level game rules (game/): cell-by-cell checks, and a log that the
    # independent Python simulator replays.
    add_executable(game_tests tests/game_tests.cpp)
    add_test(NAME game_rules COMMAND game_tests "${CMAKE_CURRENT_BINARY_DIR}/game-test.log")
    # Frame-by-frame game: measured frame tables, agreement with the move level,
    # and what the controller can reach.
    add_executable(frame_tests tests/frame_tests.cpp play/start_beat.h)
    add_test(NAME game_frames COMMAND frame_tests)
    add_executable(link_tests tests/link_tests.cpp game/link.h)
    add_test(NAME game_linked_rounds COMMAND link_tests)
    add_executable(net_tests tests/net_tests.cpp play/net.h game/link.h)
    add_test(NAME net_match_over_a_line COMMAND net_tests)
    add_executable(replay_tests tests/replay_tests.cpp play/replay.h play/settings.h)
    add_test(NAME replay_files COMMAND replay_tests)
    # The UDP line with sockets of this machine alone (the host listens at ::1).
    add_executable(udp_tests tests/udp_tests.cpp play/udp.h play/net.h game/link.h)
    if(WIN32)
        target_link_libraries(udp_tests PRIVATE ws2_32)
    endif()
    add_test(NAME net_udp_on_this_machine COMMAND udp_tests)
    # What a player's machine takes from a server of Juraic Arena, the server played at 127.0.0.1.
    add_executable(arena_tests tests/arena_tests.cpp play/arena_play.h play/arena/tcp.h play/arena/follow.h play/arena/rules_text.h)
    target_link_libraries(arena_tests PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(arena_tests PRIVATE ws2_32)
    endif()
    add_test(NAME arena_lines_on_this_machine COMMAND arena_tests)
    # Versus AI (ai/).
    add_executable(ai_tests tests/ai_tests.cpp ai/observe.h ai/duel.h ai/input.h ai/detect.h ai/params.h ai/build.h ai/gaze.h ai/value.h ai/exchange.h ai/pace.h ai/tactics.h ai/level.h)
target_link_libraries(ai_tests PRIVATE Threads::Threads)
    add_test(NAME ai COMMAND ai_tests)
    # The reading of the real game's screen (play/realgame_read.h, realgame_follow.h), with pictures drawn from rounds of this program.
    add_executable(realgame_tests tests/realgame_tests.cpp play/realgame_read.h play/realgame_follow.h play/realgame_drive.h play/realgame_log.h play/realgame_menu.h play/realgame_pace.h ai/observe.h ai/agents.h)
    target_link_libraries(realgame_tests PRIVATE Threads::Threads)
    add_test(NAME realgame_screen_read COMMAND realgame_tests "${CMAKE_CURRENT_BINARY_DIR}")
    # (13 minutes on Windows run alone, longer on slower machines: more than the default 25)
    set_tests_properties(realgame_screen_read PROPERTIES TIMEOUT 3600)
    add_test(NAME ai_match_same_result COMMAND ai_match --self-test)
    add_test(NAME ai_toko_same_result COMMAND ai_toko --self-test)
    add_test(NAME ai_tune_same_result COMMAND ai_tune --self-test)
    # Replays saved by the game on Windows (tests/fixtures/replays), played by the game of this system without a window:
    # every round must end as it ended there (<name>.expected). They are copied first: --check-replay writes
    # <name>.result next to the replay.
    if(TARGET juraic_field_game)
        file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/replays/" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/replay-fixtures")
        file(GLOB replay_fixtures "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/replays/*.jfreplay")
        foreach(replay ${replay_fixtures})
            get_filename_component(name "${replay}" NAME)
            get_filename_component(stem "${replay}" NAME_WE)
            add_test(NAME replay_as_on_windows_${stem} COMMAND juraic_field_game --check-replay "${CMAKE_CURRENT_BINARY_DIR}/replay-fixtures/${name}")
        endforeach()
    endif()

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(WIN32)
        foreach(name gui_model_tests editor_tests console_tests)
            add_executable(${name} tests/${name}.cpp)
            target_link_libraries(${name} PRIVATE juraic_engine)
            if(MINGW)
                target_link_options(${name} PRIVATE -municode)
            endif()
        endforeach()
        add_test(NAME gui_model_and_archives COMMAND gui_model_tests "${CMAKE_CURRENT_BINARY_DIR}/gui-model-test")
        add_test(NAME editor_model_and_specials COMMAND editor_tests "${CMAKE_CURRENT_BINARY_DIR}/editor-test")
        add_test(NAME gui_native_integration COMMAND juraic_generator_gui --self-test "${CMAKE_CURRENT_BINARY_DIR}/gui-native-test")
        set_tests_properties(gui_native_integration PROPERTIES TIMEOUT 240)
        if(MSVC AND SDL3_FOUND)
            # The controllers of the game: which pads SDL reads (play/pad.h).
            add_executable(pad_tests tests/pad_tests.cpp play/pad.h)
            target_compile_definitions(pad_tests PRIVATE JURAIC_PADS_SDL)
            target_link_libraries(pad_tests PRIVATE SDL3::SDL3 windowsapp xinput)
            add_test(NAME game_pads COMMAND pad_tests)
        endif()
        if(MSVC)
            # The virtual pad of the mode of the real game (play/realgame_win.h): plugged in only with
            # JURAIC_TEST_VIRTUAL_PAD=1 set, skipped otherwise.
            add_executable(realgame_pad_tests tests/realgame_pad_tests.cpp play/realgame_win.h play/realgame_pad_win.h)
            target_link_libraries(realgame_pad_tests PRIVATE vigem_client windowsapp d3d11 dxgi dwmapi user32 winmm)
            if(JURAIC_REALGAME_DEBUG)
                target_compile_definitions(realgame_pad_tests PRIVATE JURAIC_REALGAME_DEBUG=1)
            endif()
            add_test(NAME realgame_virtual_pad COMMAND realgame_pad_tests)
            set_tests_properties(realgame_virtual_pad PROPERTIES SKIP_RETURN_CODE 77)
        endif()
    endif()
    if(Python3_Interpreter_FOUND)
        if(WIN32)
            add_test(NAME editor_independent_simulation
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_editor.py" "${CMAKE_CURRENT_BINARY_DIR}/editor-test")
            set_tests_properties(editor_independent_simulation PROPERTIES DEPENDS editor_model_and_specials)
            add_test(NAME gui_independent_replay
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gui_artifacts.py"
                    "${CMAKE_CURRENT_BINARY_DIR}/gui-native-test" "${CMAKE_CURRENT_BINARY_DIR}/gui-model-test")
            set_tests_properties(gui_independent_replay PROPERTIES DEPENDS "gui_native_integration;gui_model_and_archives")
            add_test(NAME windows_console_utf8
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_console.py"
                    "$<TARGET_FILE:console_tests>" "$<TARGET_FILE:random_19_chain>")
        endif()
        add_test(NAME game_independent_simulation
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_game.py" "${CMAKE_CURRENT_BINARY_DIR}/game-test.log")
        set_tests_properties(game_independent_simulation PROPERTIES DEPENDS game_rules)
        add_test(NAME independent_output_verification
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_output.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/solutions.log" --allow-extra)
        add_test(NAME config_and_generation_integration
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py"
                "$<TARGET_FILE:random_19_chain>")
        # Every chain length, colour count and extra range, verified independently.
        add_test(NAME all_chain_lengths
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_all_chains.py"
                "$<TARGET_FILE:random_19_chain>" "${CMAKE_CURRENT_SOURCE_DIR}/config.ini")
        set_tests_properties(all_chain_lengths PROPERTIES TIMEOUT 300)
        # The same for the 10 x 18 field: chain lengths 1 to 47.
        add_test(NAME all_chain_lengths_10x18
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_all_wide.py"
                "$<TARGET_FILE:random_19_chain>" "${CMAKE_CURRENT_SOURCE_DIR}/config.ini")
        set_tests_properties(all_chain_lengths_10x18 PROPERTIES TIMEOUT 1500)
    endif()
endif()

add_custom_target(generator_runtime_files ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config.ini"
        "$<TARGET_FILE_DIR:random_19_chain>/config.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/run.cmd"
        "$<TARGET_FILE_DIR:random_19_chain>/run.cmd"
    DEPENDS random_19_chain
)
