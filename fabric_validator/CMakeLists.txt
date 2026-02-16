cmake_minimum_required(VERSION 3.8)
project(fabric_validator)

# Default to C++17
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(pluginlib REQUIRED)
find_package(fabric_base REQUIRED)
find_package(capabilities2_events REQUIRED)

include_directories(
  include
)

add_library(${PROJECT_NAME} SHARED
  src/validators.cpp
)

ament_target_dependencies(${PROJECT_NAME}
  rclcpp
  pluginlib
  fabric_base
  capabilities2_events
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
ament_package()
