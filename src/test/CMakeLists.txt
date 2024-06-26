if (NOT USE_SYSTEM_CATCH2)
  Include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v2.13.1)
  FetchContent_MakeAvailable(Catch2)
else()
  find_package(Catch2 2.13 REQUIRED)
endif()

file(GLOB_RECURSE SRC_FILES ../mahjong/*.cpp)
file(GLOB ENTRY_FILES ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)

set(CMAKE_TESTCASE_DIR ${CMAKE_SOURCE_DIR}/data/testcase)
add_definitions("-DCMAKE_TESTCASE_DIR=\"${CMAKE_TESTCASE_DIR}\"")

foreach(ENTRY_FILE ${ENTRY_FILES})
  get_filename_component(BASE_NAME ${ENTRY_FILE} NAME_WE)

  add_executable(${BASE_NAME} ${SRC_FILES} ${ENTRY_FILE} naiveexpectedvaluecalculator.cpp)
  target_link_libraries(${BASE_NAME} Catch2::Catch2 ${Boost_LIBRARIES} spdlog ${CMAKE_DL_LIBS})

  add_custom_command(TARGET ${BASE_NAME} PRE_BUILD
                     COMMAND ${CMAKE_COMMAND} -E copy_directory
                     ${CMAKE_SOURCE_DIR}/data/config/ $<TARGET_FILE_DIR:${BASE_NAME}>)
endforeach(ENTRY_FILE ${ENTRY_FILES})
