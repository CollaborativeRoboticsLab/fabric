cmake_minimum_required(VERSION 3.8)
project(fabric_experience_planner)


# Default to C++17
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(rclcpp REQUIRED)
find_package(fabric_base REQUIRED)
find_package(pluginlib REQUIRED)
find_package(prompt_msgs REQUIRED)

include_directories(
  include
)

add_library(${PROJECT_NAME} SHARED
  src/planner.cpp
)

target_link_libraries(${PROJECT_NAME}
  nlohmann_json::nlohmann_json
)

ament_target_dependencies(${PROJECT_NAME}
  rclcpp
  pluginlib
  fabric_base
  prompt_msgs
)

pluginlib_export_plugin_description_file(fabric_base plugins.xml)

install(TARGETS ${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY include/
  DESTINATION include
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_lint_auto REQUIRED)

  ament_add_gtest(test_planning_plugin test/test_planning_plugin.cpp)
  if(TARGET test_planning_plugin)
    ament_target_dependencies(test_planning_plugin
      rclcpp
      fabric_base
      prompt_msgs
    )
    target_link_libraries(test_planning_plugin
      ${PROJECT_NAME}
      nlohmann_json::nlohmann_json
    )
  endif()

  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  set(ament_cmake_uncrustify_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME})
ament_package()
