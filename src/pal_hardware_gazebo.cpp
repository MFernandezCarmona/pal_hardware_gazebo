///////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2013, PAL Robotics S.L.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright
//     notice, this list of conditions and the following disclaimer in the
//     documentation and/or other materials provided with the distribution.
//   * Neither the name of PAL Robotics S.L. nor the names of its
//     contributors may be used to endorse or promote products derived from
//     this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//////////////////////////////////////////////////////////////////////////////

#include <cassert>
#include <boost/foreach.hpp>

#include <gazebo/sensors/SensorManager.hh>

#include <urdf_parser/urdf_parser.h>
#include <pluginlib/class_list_macros.h>
#include <angles/angles.h>

#include <joint_limits_interface/joint_limits_urdf.h>
#include <transmission_interface/transmission_interface_loader.h>

#include <pal_hardware_gazebo/pal_hardware_gazebo.h>
#include <dynamic_introspection/DynamicIntrospection.h>
#include <pal_robot_tools/xmlrpc_helpers.h>

using std::vector;
using std::string;

namespace gazebo_ros_control
{

  using namespace hardware_interface;

  bool PalHardwareGazebo::parseForceTorqueSensors(ros::NodeHandle &nh,
                                                  gazebo::physics::ModelPtr model,
                                                  const urdf::Model* const urdf_model){
      using std::vector;
      using std::string;

      const string ft_ns = "force_torque";
      vector<string> ft_ids = getIds(nh, ft_ns);
      ros::NodeHandle ft_nh(nh, ft_ns);
      typedef vector<string>::const_iterator Iterator;
      for (Iterator it = ft_ids.begin(); it != ft_ids.end(); ++it){
          std::string sensor_name = *it;
          std::string sensor_joint_name;
          std::string sensor_frame_id;
          ros::NodeHandle ft_sensor_nh(ft_nh, sensor_name);
          xh::fetchParam(ft_sensor_nh, "frame", sensor_frame_id);
          xh::fetchParam(ft_sensor_nh, "sensor_joint", sensor_joint_name);
          ForceTorqueSensorDefinitionPtr ft(new ForceTorqueSensorDefinition(sensor_name,
                                                                            sensor_joint_name,
                                                                            sensor_frame_id));

          ft->gazebo_joint  = model->GetJoint(ft->sensorJointName);
          if (!ft->gazebo_joint){
            ROS_ERROR_STREAM("Could not find joint '" << ft->sensorJointName << "' to which a force-torque sensor is attached.");
            return false;
          }

          forceTorqueSensorDefinitions_.push_back(ft);
          ROS_INFO_STREAM("Parsed fake FT sensor: "<<sensor_name<<" in frame: "<<sensor_frame_id);
      }
      return true;
  }

  bool PalHardwareGazebo::parseIMUSensors(ros::NodeHandle &nh,
                                          gazebo::physics::ModelPtr model,
                                          const urdf::Model* const urdf_model){
      using std::vector;
      using std::string;

      const string imu_ns = "imu";
      vector<string> imu_ids = getIds(nh, imu_ns);
      ros::NodeHandle imu_nh(nh, imu_ns);
      typedef vector<string>::const_iterator Iterator;
      for (Iterator it = imu_ids.begin(); it != imu_ids.end(); ++it){
          std::string sensor_name = *it;
          std::string sensor_frame_id;
          ros::NodeHandle imu_sensor_nh(imu_nh, sensor_name);
          xh::fetchParam(imu_sensor_nh, "frame", sensor_frame_id);

          boost::shared_ptr<gazebo::sensors::ImuSensor> imu_sensor;
          imu_sensor =  boost::dynamic_pointer_cast<gazebo::sensors::ImuSensor>
              (gazebo::sensors::SensorManager::Instance()->GetSensor("imu_sensor"));
          if (!imu_sensor){
            ROS_ERROR_STREAM("Could not find base IMU sensor.");
            return false;
          }

          ImuSensorDefinitionPtr imu(new ImuSensorDefinition(sensor_name, sensor_frame_id));
          imu->gazebo_imu_sensor = imu_sensor;
          imuSensorDefinitions_.push_back(imu);
          ROS_INFO_STREAM("Parsed imu sensor: "<<sensor_name<<" in frame: "<<sensor_frame_id);
      }
      return true;
  }

  PalHardwareGazebo::PalHardwareGazebo()
    : DefaultRobotHWSim()
  {}

  bool PalHardwareGazebo::initSim(const std::string& robot_ns,
                                  ros::NodeHandle nh, gazebo::physics::ModelPtr model,
                                  const urdf::Model* const urdf_model,
                                  std::vector<transmission_interface::TransmissionInfo> transmissions)
  {

    ROS_INFO_STREAM("Loading PAL HARWARE GAZEBO");

    // register hardware interfaces
    // TODO: Automate, so generic interfaces can be added
    registerInterface(&js_interface_);
    registerInterface(&ej_interface_);
    registerInterface(&pj_interface_);
    registerInterface(&vj_interface_);

    // cache transmisions information
    DefaultRobotHWSim::transmission_infos_ = transmissions;

    // populate hardware interfaces, bind them to raw Gazebo data
    namespace ti = transmission_interface;
    BOOST_FOREACH(const ti::TransmissionInfo& tr_info, transmission_infos_)
    {
      BOOST_FOREACH(const ti::JointInfo& joint_info, tr_info.joints_)
      {
        BOOST_FOREACH(const std::string& iface_type, joint_info.hardware_interfaces_)
        {
          // TODO: Wrap in method for brevity?
          RwResPtr res;
          // TODO: A plugin-based approach would do better than this 'if-elseif' chain
          // To do this, move contructor logic to init method, and unify signature
          if (iface_type == "hardware_interface/JointStateInterface")
          {
            res.reset(new internal::JointState());
          }
          else if (iface_type == "hardware_interface/PositionJointInterface")
          {
            res.reset(new internal::PositionJoint());
          }
          else if (iface_type == "hardware_interface/VelocityJointInterface")
          {
            res.reset(new internal::VelocityJoint());
          }
          else if (iface_type == "hardware_interface/EffortJointInterface")
          {
            res.reset(new internal::EffortJoint());
          }

          // initialize and add to list of managed resources
          if (res)
          {
            try
            {
              res->init(joint_info.name_,
                        nh,
                        model,
                        urdf_model,
                        this);
              rw_resources_.push_back(res);
              ROS_DEBUG_STREAM("Registered joint '" << joint_info.name_ << "' in hardware interface '" <<
                               iface_type << "'."); // TODO: Lower severity to debug!
            }
            catch (const internal::ExistingResourceException&) {} // resource already added, no problem
            catch (const std::runtime_error& ex)
            {
              ROS_ERROR_STREAM("Failed to initialize gazebo_ros_control plugin.\n" <<
                               ex.what());
              return false;
            }
            catch(...)
            {
              ROS_ERROR_STREAM("Failed to initialize gazebo_ros_control plugin.\n" <<
                               "Could not add resource '" << joint_info.name_ << "' to hardware interface '" <<
                               iface_type << "'.");
              return false;
            }
          }

        }
      }
    }

    // initialize the emergency stop code
    e_stop_active_ = false;

    // joint mode switching
    mode_switch_enabled_ = true;
    nh.getParam("gazebo_ros_control/enable_joint_mode_switching", mode_switch_enabled_); // TODO: Check namespace
    const std::string enabled_str = mode_switch_enabled_ ? "enabled" : "disabled";
    ROS_INFO_STREAM("Joint mode switching is " << enabled_str);

    // initialize active writers
    initActiveWriteResources();

    parseForceTorqueSensors(nh, model, urdf_model);

    for(size_t i=0; i<forceTorqueSensorDefinitions_.size(); ++i){
      ForceTorqueSensorDefinitionPtr &ft = forceTorqueSensorDefinitions_[i];
      ft_sensor_interface_.registerHandle(ForceTorqueSensorHandle(ft->sensorName,
                                                                  ft->sensorFrame,
                                                                  &ft->force[0],
                                                                  &ft->torque[0]));
    }

    registerInterface(&ft_sensor_interface_);
    ROS_DEBUG_STREAM("Registered force-torque sensors.");

    // Hardware interfaces: Base IMU sensors
    for(size_t i=0; i<imuSensorDefinitions_.size(); ++i){
      ImuSensorDefinitionPtr &imu = imuSensorDefinitions_[i];

      ImuSensorHandle::Data data;
      data.name = imu->sensorName;
      data.frame_id = imu->sensorFrame;
      data.orientation = &imu->orientation[0];
      data.linear_acceleration = &imu->linear_acceleration[0];
      data.angular_velocity = &imu->base_ang_vel[0];
      imu_sensor_interface_.registerHandle(ImuSensorHandle(data));
    }

    registerInterface(&imu_sensor_interface_);
    ROS_DEBUG_STREAM("Registered IMU sensor.");

    return true;
  }

  void PalHardwareGazebo::readSim(ros::Time time, ros::Duration period)
  {
    // read all resources
    BOOST_FOREACH(RwResPtr res, rw_resources_)
    {
      res->read(time, period, e_stop_active_);
    }

    // Read force-torque sensors
    for(size_t i = 0; i < forceTorqueSensorDefinitions_.size(); ++i){
      ForceTorqueSensorDefinitionPtr &ft = forceTorqueSensorDefinitions_[i];
      gazebo::physics::JointWrench ft_wrench = ft->gazebo_joint->GetForceTorque(0u);
      ft->force[0]  = ft_wrench.body2Force.x;
      ft->force[1]  = ft_wrench.body2Force.y;
      ft->force[2]  =  ft_wrench.body2Force.z;
      ft->torque[0] = ft_wrench.body2Torque.x;
      ft->torque[1] = ft_wrench.body2Torque.y;
      ft->torque[2] =  ft_wrench.body2Torque.z;
    }

    // Read IMU sensor
    for(size_t i = 0; i < imuSensorDefinitions_.size(); ++i){
      ImuSensorDefinitionPtr &imu = imuSensorDefinitions_[i];

      gazebo::math::Quaternion imu_quat = imu->gazebo_imu_sensor->GetOrientation();
      imu->orientation[0] = imu_quat.x;
      imu->orientation[1] = imu_quat.y;
      imu->orientation[2] = imu_quat.z;
      imu->orientation[3] = imu_quat.w;

      gazebo::math::Vector3 imu_ang_vel = imu->gazebo_imu_sensor->GetAngularVelocity();
      imu->base_ang_vel[0] = imu_ang_vel.x;
      imu->base_ang_vel[1] = imu_ang_vel.y;
      imu->base_ang_vel[2] = imu_ang_vel.z;

      gazebo::math::Vector3 imu_lin_acc = imu->gazebo_imu_sensor->GetLinearAcceleration();
      imu->linear_acceleration[0] =  imu_lin_acc.x;
      imu->linear_acceleration[1] =  imu_lin_acc.y;
      imu->linear_acceleration[2] =  imu_lin_acc.z;
    }


  }

  void PalHardwareGazebo::writeSim(ros::Time time, ros::Duration period)
  {
    boost::unique_lock<boost::mutex> lock(mutex_);
    BOOST_FOREACH(RwResPtr res, active_w_resources_rt_)
    {
      res->write(time, period, e_stop_active_);
    }
    // PUBLISH_DEBUG_DATA_TOPIC;
  }

}

PLUGINLIB_EXPORT_CLASS(gazebo_ros_control::PalHardwareGazebo, gazebo_ros_control::RobotHWSim)