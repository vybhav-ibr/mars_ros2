// Copyright (C) 2022 Christian Brommer, Control of Networked Systems, University of Klagenfurt, Austria.
//
// All rights reserved.
//
// This software is licensed under the terms of the BSD-2-Clause-License with
// no commercial use allowed, the full terms of which are made available
// in the LICENSE file. No license in patents is granted.
//
// You can contact the author at <christian.brommer@ieee.org>

#include "mars_wrapper_uwb.h"
#include "mars_wrapper_position.h"

#include <mars/core_logic.h>
#include <mars/core_state.h>
#include <mars/sensors/imu/imu_measurement_type.h>
#include <mars/sensors/imu/imu_sensor_class.h>

#include <mars/sensors/uwb/uwb_measurement_type.h>
#include <mars/sensors/uwb/uwb_sensor_class.h>

#include <mars/sensors/position/position_measurement_type.h>
#include <mars/type_definitions/buffer_data_type.h>
#include <mars/type_definitions/buffer_entry_type.h>
#include <mars_ros/ExtCoreState.h>
#include <mars_ros/ExtCoreStateLite.h>
#include <nav_msgs/Odometry.h>
#include <ros/ros.h>
#include <sensor_msgs/Imu.h>

#include <Eigen/Dense>
#include <iostream>
#include <string>

using namespace mars;

MarsWrapperUwb::MarsWrapperUwb(ros::NodeHandle nh)
  : reconfigure_cb_(boost::bind(&MarsWrapperUwb::configCallback, this, _1, _2))
  , p_wi_init_(0, 0, 0)
  , q_wi_init_(Eigen::Quaterniond::Identity())
  , m_sett_(nh)
  , path_generator_(m_sett_.path_buffer_size_)
{
  reconfigure_srv_.setCallback(reconfigure_cb_);

  initialization_service_ = nh.advertiseService("init_service", &MarsWrapperUwb::initServiceCallback, this);

  std::cout << "Setup framework components" << std::endl;

  // Framework components
  imu_sensor_sptr_ = std::make_shared<mars::ImuSensorClass>("IMU");
  core_states_sptr_ = std::make_shared<mars::CoreState>();
  core_states_sptr_.get()->set_initial_covariance(m_sett_.core_init_cov_p_, m_sett_.core_init_cov_v_,
                                                  m_sett_.core_init_cov_q_, m_sett_.core_init_cov_bw_,
                                                  m_sett_.core_init_cov_ba_);

  core_states_sptr_.get()->set_propagation_sensor(imu_sensor_sptr_);
  core_logic_ = mars::CoreLogic(core_states_sptr_);
  core_logic_.buffer_.set_max_buffer_size(m_sett_.buffer_size_);

  core_logic_.verbose_ = m_sett_.verbose_output_;
  core_logic_.verbose_out_of_order_ = m_sett_.verbose_ooo_;
  core_logic_.discard_ooo_prop_meas_ = m_sett_.discard_ooo_prop_meas_;

  core_states_sptr_->set_noise_std(
      Eigen::Vector3d(m_sett_.g_rate_noise_, m_sett_.g_rate_noise_, m_sett_.g_rate_noise_),
      Eigen::Vector3d(m_sett_.g_bias_noise_, m_sett_.g_bias_noise_, m_sett_.g_bias_noise_),
      Eigen::Vector3d(m_sett_.a_noise_, m_sett_.a_noise_, m_sett_.a_noise_),
      Eigen::Vector3d(m_sett_.a_bias_noise_, m_sett_.a_bias_noise_, m_sett_.a_bias_noise_));

  // Sensors
  uwb_sensor_sptr_ = std::make_shared<mars::UwbSensorClass>("Uwb", core_states_sptr_);
  Eigen::Matrix<double, 3, 1> uwb_meas_std;
  // uwb_meas_std << m_sett_.uwb_meas_noise_;
  // uwb_sensor_sptr_->R_ = uwb_meas_std;
  // uwb_sensor_sptr_->use_dynamic_meas_noise_ = m_sett_.uwb_use_dyn_meas_noise_;
   std::cout << "Setup noise" << std::endl;

  UwbSensorData uwb_calibration;
  uwb_calibration.state_.p_ip_ = m_sett_.uwb_cal_p_ip_;

  Eigen::Matrix<double, 3, 3> uwb_cov;
  uwb_cov.setZero();
  uwb_cov.diagonal() << m_sett_.uwb_state_init_cov_;
  uwb_calibration.sensor_cov_ = uwb_cov;

  uwb_sensor_sptr_->set_initial_calib(std::make_shared<UwbSensorData>(uwb_calibration));
   std::cout << "Setup calib and cov components" << std::endl;

  // TODO(CHB) is set here for now, but will be managed by core logic in later versions
  uwb_sensor_sptr_->const_ref_to_nav_ = true;

  // Subscriber
  sub_imu_measurement_ =
      nh.subscribe("imu_in", m_sett_.sub_imu_cb_buffer_size_, &MarsWrapperUwb::ImuMeasurementCallback, this);
  
  // sub_point_measurement_ = nh.subscribe("point_in", m_sett_.sub_sensor_cb_buffer_size_,
  //                                       &MarsWrapperUwb::PointMeasurementCallback, this);
  // sub_pose_measurement_ =
  //     nh.subscribe("pose_in", m_sett_.sub_sensor_cb_buffer_size_, &MarsWrapperUwb::PoseMeasurementCallback, this);
  
  // sub_pose_with_cov_measurement_ = nh.subscribe("pose_with_cov_in", m_sett_.sub_sensor_cb_buffer_size_,
  //                                               &MarsWrapperUwb::PoseWithCovMeasurementCallback, this);
  // sub_odom_measurement_ =
  //     nh.subscribe("odom_in", m_sett_.sub_sensor_cb_buffer_size_, &MarsWrapperUwb::OdomMeasurementCallback, this);
  
  // sub_transform_measurement_ = nh.subscribe("transform_in", m_sett_.sub_sensor_cb_buffer_size_,
  //                                           &MarsWrapperUwb::TransformMeasurementCallback, this);
  sub_uwb_measurement_=
         nh.subscribe("uwb_in", m_sett_.sub_sensor_cb_buffer_size_, &MarsWrapperUwb::UwbMeasurementCallback, this);
  

  // Publisher
  pub_ext_core_state_ = nh.advertise<mars_ros::ExtCoreState>("core_ext_state_out", m_sett_.pub_cb_buffer_size_);
  pub_ext_core_state_lite_ =
      nh.advertise<mars_ros::ExtCoreStateLite>("core_ext_state_lite_out", m_sett_.pub_cb_buffer_size_);
  pub_core_pose_state_ = nh.advertise<geometry_msgs::PoseStamped>("core_pose_state_out", m_sett_.pub_cb_buffer_size_);
  //pub_position1_state_ =
  //    nh.advertise<geometry_msgs::PoseStamped>("position_cal_state_out", m_sett_.pub_cb_buffer_size_);

  pub_uwb_cal_state_ =
      nh.advertise<geometry_msgs::PoseStamped>("uwb_cal_state_out", m_sett_.pub_cb_buffer_size_);

  pub_core_odom_state_ = nh.advertise<nav_msgs::Odometry>("core_odom_state_out", m_sett_.pub_cb_buffer_size_);
  if (m_sett_.pub_path_)
  {
    pub_core_path_ = nh.advertise<nav_msgs::Path>("core_states_path", m_sett_.pub_cb_buffer_size_);
  }
}

bool MarsWrapperUwb::init()
{
  core_logic_.core_is_initialized_ = false;
  core_logic_.buffer_.ResetBufferData();
  uwb_sensor_sptr_->is_initialized_ = false;

  return true;
}

bool MarsWrapperUwb::initServiceCallback(std_srvs::SetBool::Request& /*request*/,
                                              std_srvs::SetBool::Response& response)
{
  init();
  ROS_INFO_STREAM("Initialized filter trough ROS Service");

  response.success = true;
  return true;
}

void MarsWrapperUwb::configCallback(mars_ros::marsConfig& config, uint32_t /*level*/)
{
  // Config parameter overwrite
  m_sett_.publish_on_propagation_ = config.pub_on_prop;
  core_logic_.verbose_ = config.verbose;
  m_sett_.verbose_output_ = config.verbose;
  m_sett_.use_ros_time_now_ = config.use_ros_time_now;

  if (config.initialize)
  {
    init();
    ROS_INFO_STREAM("Initialized filter trough Reconfigure GUI");
  }

  config.initialize = false;
}

void MarsWrapperUwb::ImuMeasurementCallback(const sensor_msgs::ImuConstPtr& meas)
{
  // Map the measutement to the mars type
  Time timestamp;

  if (m_sett_.use_ros_time_now_)
  {
    timestamp = Time(ros::Time::now().toSec());
  }
  else
  {
    timestamp = Time(meas->header.stamp.toSec());
  }

  // Generate a measurement data block
  BufferDataType data;
  data.set_measurement(std::make_shared<IMUMeasurementType>(MarsMsgConv::ImuMsgToImuMeas(*meas)));

  // Call process measurement
  const bool valid_update = core_logic_.ProcessMeasurement(imu_sensor_sptr_, timestamp, data);
  //std::cout<<

  // Initialize the first time at which the propagation sensor occures
  if (!core_logic_.core_is_initialized_)
  {
    core_logic_.Initialize(p_wi_init_, q_wi_init_);
  }

  if (m_sett_.publish_on_propagation_ && valid_update)
  {
    this->RunCoreStatePublisher();
  }
}

// void MarsWrapperUwb::PointMeasurementCallback(const geometry_msgs::PointStampedConstPtr& meas)
// {
//   // Map the measurement to the mars sensor type
//   Time timestamp(meas->header.stamp.toSec());

//   PositionMeasurementType position_meas = MarsMsgConv::PointMsgToPositionMeas(*meas);
//   PositionMeasurementUpdate(position1_sensor_sptr_, position_meas, timestamp);
// }

// void MarsWrapperUwb::PoseMeasurementCallback(const geometry_msgs::PoseStampedConstPtr& meas)
// {
//   // Map the measurement to the mars sensor type
//   Time timestamp(meas->header.stamp.toSec());

//   PositionMeasurementType position_meas = MarsMsgConv::PoseMsgToPositionMeas(*meas);
//   PositionMeasurementUpdate(position1_sensor_sptr_, position_meas, timestamp);
// }

void MarsWrapperUwb::UwbMeasurementCallback(const mars_ros::Uwb::ConstPtr& meas)
{
  // Map the measurement to the mars sensor type
   Time timestamp(meas->header.stamp.toSec());

  UwbMeasurementType uwb_meas = MarsMsgConv::UwbMsgToUwbMeas(*meas);////---------------To be implemneted
  UwbMeasurementUpdate(uwb_sensor_sptr_, uwb_meas, timestamp);
}

// void MarsWrapperUwb::PoseWithCovMeasurementCallback(const geometry_msgs::PoseWithCovarianceStampedConstPtr& meas)
// {
//   // Map the measurement to the mars sensor type
//   Time timestamp(meas->header.stamp.toSec());

//   PositionMeasurementType position_meas = MarsMsgConv::PoseWithCovMsgToPositionMeas(*meas);
//   PositionMeasurementUpdate(position1_sensor_sptr_, position_meas, timestamp);
// }

// void MarsWrapperUwb::TransformMeasurementCallback(const geometry_msgs::TransformStampedConstPtr& meas)
// {
//   // Map the measurement to the mars sensor type
//   Time timestamp(meas->header.stamp.toSec());

//   PositionMeasurementType position_meas = MarsMsgConv::TransformMsgToPositionMeas(*meas);
//   PositionMeasurementUpdate(position1_sensor_sptr_, position_meas, timestamp);
// }

// void MarsWrapperUwb::OdomMeasurementCallback(const nav_msgs::OdometryConstPtr& meas)
// {
//   // Map the measurement to the mars sensor type
//   Time timestamp(meas->header.stamp.toSec());

//   PositionMeasurementType position_meas = MarsMsgConv::OdomMsgToPositionMeas(*meas);
//   PositionMeasurementUpdate(position1_sensor_sptr_, position_meas, timestamp);
// }

void MarsWrapperUwb::RunCoreStatePublisher()
{
  mars::BufferEntryType latest_state;
  const bool valid_state = core_logic_.buffer_.get_latest_state(&latest_state);

  if (!valid_state)
  {
    return;
  }

  mars::CoreStateType latest_core_state = static_cast<mars::CoreType*>(latest_state.data_.core_state_.get())->state_;

  if (m_sett_.pub_cov_)
  {
    mars::CoreStateMatrix cov = static_cast<mars::CoreType*>(latest_state.data_.core_state_.get())->cov_;
    pub_ext_core_state_.publish(
        MarsMsgConv::ExtCoreStateToMsgCov(latest_state.timestamp_.get_seconds(), latest_core_state, cov));
  }
  else
  {
    pub_ext_core_state_.publish(
        MarsMsgConv::ExtCoreStateToMsg(latest_state.timestamp_.get_seconds(), latest_core_state));
  }

  pub_ext_core_state_lite_.publish(
      MarsMsgConv::ExtCoreStateLiteToMsg(latest_state.timestamp_.get_seconds(), latest_core_state));

  pub_core_pose_state_.publish(
      MarsMsgConv::ExtCoreStateToPoseMsg(latest_state.timestamp_.get_seconds(), latest_core_state));

  pub_core_odom_state_.publish(
      MarsMsgConv::ExtCoreStateToOdomMsg(latest_state.timestamp_.get_seconds(), latest_core_state));

  if (m_sett_.pub_path_)
  {
    //    pub_core_path_.publish(
    //        MarsMsgConv::BufferCoreStateToPathMsg(latest_state.timestamp_.get_seconds(), core_logic_.buffer_));
    pub_core_path_.publish(
        path_generator_.ExtCoreStateToPathMsg(latest_state.timestamp_.get_seconds(), latest_core_state));
  }
}

void MarsWrapperUwb::UwbMeasurementUpdate(std::shared_ptr<mars::UwbSensorClass> sensor_sptr,
                                            const mars::UwbMeasurementType& uwb_meas, const mars::Time& timestamp)
{
    if (!do_state_init_)
    {
        mars::UwbSensorStateType uwb_cal = sensor_sptr->get_state(uwb_sensor_sptr_->initial_calib_);
        p_wi_init_ = uwb_cal.p_ip_;
        q_wi_init_ = Eigen::Quaterniond::Identity();

        do_state_init_ = true;
    }

    Time timestamp_corr;

    if (m_sett_.use_ros_time_now_)
    {
        timestamp_corr = Time(ros::Time::now().toSec());
    }
    else
    {
        timestamp_corr = timestamp;
    }

    // Generate a measurement data block
    BufferDataType data;
    data.set_measurement(std::make_shared<mars::UwbMeasurementType>(uwb_meas));

    // Call process measurement
    if (!core_logic_.ProcessMeasurement(sensor_sptr, timestamp_corr, data))
    {
      std::cout<<"returned false";
        return;
    }

    // Publish the latest core state
    this->RunCoreStatePublisher();

    // Publish the latest sensor state
    mars::BufferEntryType latest_result;
    const bool valid_state = core_logic_.buffer_.get_latest_sensor_handle_state(sensor_sptr, &latest_result);

    if (!valid_state)
    {
        return;
    }

    mars::UwbSensorStateType uwb_sensor_state = sensor_sptr.get()->get_state(latest_result.data_.sensor_state_);

    pub_uwb_cal_state_.publish(
      MarsMsgConv::UwbStateToPoseMsg(latest_result.timestamp_.get_seconds(), uwb_sensor_state,"uwb"));
}

// void MarsWrapperUwb::PositionMeasurementUpdate(std::shared_ptr<mars::PositionSensorClass> sensor_sptr,
//                                                     const mars::PositionMeasurementType& position_meas,
//                                                     const mars::Time& timestamp)
// {
//   Time timestamp_corr;

//   if (m_sett_.use_ros_time_now_)
//   {
//     timestamp_corr = Time(ros::Time::now().toSec());
//   }
//   else
//   {
//     timestamp_corr = timestamp;
//   }

//   // TMP feedback init position
//   p_wi_init_ = position_meas.position_;

//   // Generate a measurement data block
//   BufferDataType data;
//   data.set_measurement(std::make_shared<PositionMeasurementType>(position_meas));

//   // Call process measurement
//   if (!core_logic_.ProcessMeasurement(sensor_sptr, timestamp_corr, data))
//   {
//     return;
//   }

//   // Publish the latest core state
//   this->RunCoreStatePublisher();

//   // Publish the latest sensor state
//   mars::BufferEntryType latest_result;
//   const bool valid_state = core_logic_.buffer_.get_latest_sensor_handle_state(sensor_sptr, &latest_result);

//   if (!valid_state)
//   {
//     return;
//   }

//   mars::PositionSensorStateType position_sensor_state = sensor_sptr.get()->get_state(latest_result.data_.sensor_state_);

//   pub_position1_state_.publish(
//       MarsMsgConv::PositionStateToPoseWithCovMsg(latest_result.timestamp_.get_seconds(), position_sensor_state, "imu"));
// }
