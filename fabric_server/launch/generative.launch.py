import os
from launch import LaunchDescription
from launch_ros.actions import Node
from launch_ros.actions import ComposableNodeContainer
from launch_ros.descriptions import ComposableNode
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():

    # load config file
    capability_config = os.path.join(get_package_share_directory('capabilities2_server'), 'config', 'capabilities.yaml')
    fabric_config = os.path.join(get_package_share_directory('fabric_server'), 'config', 'fabric.yaml')
    prompt_config = os.path.join(get_package_share_directory('prompt_bridge'), 'config', 'prompt_bridge.yaml')

    # create bridge composition
    nongenerative_container = ComposableNodeContainer(
        name='nongenerative_container',
        namespace='',
        package='rclcpp_components',
        executable='component_container_mt',
        # prefix=['xterm -e gdb -ex run --args'],  # Add GDB debugging prefix
        arguments=['--ros-args', '--log-level', 'info'],
        composable_node_descriptions=[
            ComposableNode(
                package='capabilities2_server',
                plugin='capabilities2_server::CapabilitiesServer',
                name='capabilities',
                parameters=[capability_config],
                output='screen'
            ),
            ComposableNode(
                package='fabric_server',
                plugin='fabric_server::FabricServer',
                name='fabric_server',
                output='screen',
                parameters=[fabric_config]
            ),
            ComposableNode(
                package='prompt_bridge',
                plugin='prompt_bridge::PromptBridge',
                name='prompt_bridge',
                parameters=[prompt_config],
                output='screen'
            )
        ]
    )

    return LaunchDescription([
        nongenerative_container
    ])
