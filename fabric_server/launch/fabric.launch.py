import os
from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    # get the plan name from launch arguments
    plan_file_name = LaunchConfiguration('filename')

    declare_plan_file_name = DeclareLaunchArgument(
        'filename',
        default_value='default.xml',
        description='Name of the plan file'
    )

    # plan file folder
    plan_file_path = os.path.join(get_package_share_directory('fabric_server'), 'plans', plan_file_name)

    # load config file
    fabric_config = os.path.join(get_package_share_directory('fabric_server'), 'config', 'fabric.yaml')

    fabric = Node(
            package='fabric_server',
            namespace='',
            executable='fabric_server',
            name='fabric_server',
            output='screen',
            parameters=[fabric_config, 
                        {"plan_file_path": plan_file_path}]
        )
    
    return LaunchDescription([
        declare_plan_file_name,
        fabric
    ])
