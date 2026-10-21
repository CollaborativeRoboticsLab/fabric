cmake_minimum_required(VERSION 3.8)
project(fabric_monitor)

# Default to C++17
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(capabilities2_msgs REQUIRED)
find_package(fabric_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(pluginlib REQUIRED)
find_package(fabric_base REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(rcl_interfaces REQUIRED)

include_directories(
  include
)

add_library(${PROJECT_NAME} SHARED
  src/ros2_monitor.cpp
)

ament_target_dependencies(${PROJECT_NAME}
  builtin_interfaces
  capabilities2_msgs
  fabric_msgs
  rclcpp
  pluginlib
  fabric_base
  diagnostic_msgs
  rcl_interfaces
)

pluginlib_export_plugin_description_file(fabric_base plugins.xml)

install(TARGETS ${PROJECT_NAME}
  EXPORT ${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY include/
  DESTINATION include
)

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_resource_monitor test/test_resource_monitor.cpp TIMEOUT 20)
  ament_target_dependencies(test_resource_monitor rclcpp fabric_base capabilities2_msgs pluginlib)
endif()

ament_package()
