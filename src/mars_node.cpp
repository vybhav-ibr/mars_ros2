// Copyright (C) 2022 Christian Brommer, Control of Networked Systems, University of Klagenfurt, Austria.
//
// All rights reserved.
//
// This software is licensed under the terms of the BSD-2-Clause-License with
// no commercial use allowed, the full terms of which are made available
// in the LICENSE file. No license in patents is granted.
//
// You can contact the author at <christian.brommer@ieee.org>

#include <ros/ros.h>

#ifdef POSE
#include "mars_wrapper_pose.h"
#endif
#ifdef POSITION
#include "mars_wrapper_position.h"
#endif
#ifdef GPS
#include "mars_wrapper_gps.h"
#endif
#ifdef GPS_MAG
#include "mars_wrapper_gps_mag.h"
#endif
#ifdef DUALPOSE_FULL
#include "mars_wrapper_dualpose_full.h"
#endif
#ifdef GPS_VISION
#include "mars_wrapper_gps_vision.h"
#endif
#ifdef UWB
#include "mars_wrapper_uwb.h"
#endif

int main(int argc, char* argv[])
{
  ros::init(argc, argv, "mars_ros_node");
  ros::NodeHandle nh("~");

  if (nh.param<bool>("use_tcpnodelay", false))
  {
    ROS_INFO("Using tcpNoDelay ");
    ros::TransportHints().tcpNoDelay();
  }

  ROS_INFO("Starting the MaRS Framework");

#ifdef POSE
  MarsWrapperPose mars_core(nh);
#endif
#ifdef POSITION
  MarsWrapperPosition mars_core(nh);
#endif
#ifdef GPS
  MarsWrapperGps mars_core(nh);
#endif
#ifdef GPS_MAG
  MarsWrapperGpsMag mars_core(nh);
#endif
#ifdef DUALPOSE_FULL
  MarsWrapperDualPoseFull mars_core(nh);
#endif
#ifdef GPS_VISION
  MarsWrapperGpsVision mars_core(nh);
#endif
#ifdef UWB
  MarsWrapperUwb mars_core(nh);
#endif

  ros::spin();
}
