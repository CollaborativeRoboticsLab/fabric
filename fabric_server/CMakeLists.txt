cmake_minimum_required(VERSION 3.8)
project(fabric_server)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(pluginlib REQUIRED)
find_package(fabric_msgs REQUIRED)
find_package(fabric_base REQUIRED)
find_package(capabilities2_msgs REQUIRED)
find_package(capabilities2_events REQUIRED)
find_package(bondcpp REQUIRED)

find_package(PkgConfig)
pkg_check_modules(UUID REQUIRED uuid)

include_directories(
  include
  ${UUID_INCLUDE_DIRS}
)

############################################################################
# fabric node executable
############################################################################

add_executable(${PROJECT_NAME}
  src/server_node.cpp
)

ament_target_dependencies(${PROJECT_NAME}
  rclcpp
  bondcpp
  pluginlib
  fabric_msgs
  fabric_base
  capabilities2_msgs
  capabilities2_events
  UUID
)

install(TARGETS ${PROJECT_NAME}
  DESTINATION lib/${PROJECT_NAME}
)


############################################################################
# fabric component library
############################################################################

add_library(${PROJECT_NAME}_comp SHARED
  src/server_component.cpp
)

ament_target_dependencies(${PROJECT_NAME}_comp
  rclcpp
  rclcpp_components
  bondcpp
  pluginlib
  fabric_msgs
  fabric_base
  capabilities2_msgs
  capabilities2_events
  UUID
)

rclcpp_components_register_node(${PROJECT_NAME}_comp
  PLUGIN "Fabric"
  EXECUTABLE fabric_component
)

ament_export_targets(fabric_component)

install(TARGETS ${PROJECT_NAME}_comp
  EXPORT fabric_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

############################################################################
# Miscellaneous installations
############################################################################

install(DIRECTORY include/
  DESTINATION include
)

ament_export_libraries(${PROJECT_NAME}_comp)
ament_package()
