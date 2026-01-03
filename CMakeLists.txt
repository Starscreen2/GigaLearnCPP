cmake_minimum_required (VERSION 3.8)

project("GigaLearnBot")

file(GLOB_RECURSE FILES_SRC "src/*.cpp" "src/*.h" "src/*.hpp")
# Remove RLBotExample from training executable
list(REMOVE_ITEM FILES_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/RLBotExample.cpp")
add_executable(GigaLearnBot ${FILES_SRC})

# Create separate executable for RLBot
add_executable(RLBotExample src/RLBotExample.cpp src/RLBotClient.cpp src/RLBotClient.h)
# Include RLBot headers for platform functions
target_include_directories(RLBotExample PRIVATE ${CMAKE_SOURCE_DIR}/RLBotCPP/inc)

# Set C++ version to 20
set_target_properties(GigaLearnBot PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(GigaLearnBot PROPERTIES CXX_STANDARD 20)

# Make sure GigaLearnCPP is going to build in the same directory as us
# Otherwise, we won't be able to import it at runtime
set(LIBRARY_OUTPUT_PATH "${CMAKE_BINARY_DIR}")
set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}")

# Include RLGymSim_PPO
add_subdirectory(GigaLearnCPP)
target_link_libraries(GigaLearnBot GigaLearnCPP)

# Include RLBot
add_subdirectory(RLBotCPP)
target_link_libraries(GigaLearnBot RLBotCPP)
target_link_libraries(RLBotExample RLBotCPP)
target_link_libraries(RLBotExample GigaLearnCPP)
set_target_properties(RLBotExample PROPERTIES CXX_STANDARD 20)