'''
capabilities2_server launch file
'''

import os
from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    """Generate launch description for capabilities2 server

    Returns:
        LaunchDescription: The launch description for capabilities2 executor
    """
    # load config file
    fabric_config = os.path.join(get_package_share_directory('fabric'), 'config', 'fabric.yaml')

    fabric = Node(
            package='fabric_server',
            namespace='',
            executable='fabric_server',
            name='fabric_server',
            output='screen',
            parameters=[fabric_config]
        )
    
    return LaunchDescription([
        fabric
    ])
