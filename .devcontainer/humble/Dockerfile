FROM ros:humble

# Add vscode user with same UID and GID as your host system
# (modified from https://code.visualstudio.com/remote/advancedcontainers/add-nonroot-user#_creating-a-nonroot-user)
# ubuntu now has a 1000 user ubuntu
ARG USERNAME=ubuntu
ARG USER_UID=1000
ARG USER_GID=$USER_UID
ENV RMW_IMPLEMENTATION=rmw_cyclonedds_cpp

RUN groupadd --gid $USER_GID $USERNAME \
    && useradd --uid $USER_UID --gid $USER_GID -m $USERNAME

RUN apt-get update && apt-get upgrade -y \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install --no-install-recommends -y \
    sudo \
    git \
    libpoco-dev \
    ros-${ROS_DISTRO}-rmw-cyclonedds-cpp \
    && rm -rf /var/lib/apt/lists/*

RUN echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME

# Switch from root to user
USER $USERNAME

# Rosdep update
RUN rosdep update

# Source the ROS setup file
RUN echo "source /opt/ros/${ROS_DISTRO}/setup.bash" >> ~/.bashrc

# make sure folders exist
WORKDIR /home/$USERNAME/colcon_ws/src 

RUN git clone https://github.com/CollaborativeRoboticsLab/capabilities2.git -b develop
# RUN git clone --recurse-submodules https://github.com/CollaborativeRoboticsLab/prompt_tools.git
# RUN git clone https://github.com/CollaborativeRoboticsLab/prompt_capabilities.git
# RUN git clone https://github.com/CollaborativeRoboticsLab/fabric_capabilities.git

# git clone --recurse-submodules https://github.com/CollaborativeRoboticsLab/prompt_tools.git
# git clone https://github.com/CollaborativeRoboticsLab/prompt_capabilities.git
# git clone https://github.com/CollaborativeRoboticsLab/fabric_capabilities.git