/*
 * slam_toolbox
 * Copyright Work Modifications (c) 2018, Simbe Robotics, Inc.
 * Copyright Work Modifications (c) 2019, Steve Macenski
 *
 * THE WORK (AS DEFINED BELOW) IS PROVIDED UNDER THE TERMS OF THIS CREATIVE
 * COMMONS PUBLIC LICENSE ("CCPL" OR "LICENSE"). THE WORK IS PROTECTED BY
 * COPYRIGHT AND/OR OTHER APPLICABLE LAW. ANY USE OF THE WORK OTHER THAN AS
 * AUTHORIZED UNDER THIS LICENSE OR COPYRIGHT LAW IS PROHIBITED.
 *
 * BY EXERCISING ANY RIGHTS TO THE WORK PROVIDED HERE, YOU ACCEPT AND AGREE TO
 * BE BOUND BY THE TERMS OF THIS LICENSE. THE LICENSOR GRANTS YOU THE RIGHTS
 * CONTAINED HERE IN CONSIDERATION OF YOUR ACCEPTANCE OF SUCH TERMS AND
 * CONDITIONS.
 *
 */

/* Author: Steven Macenski */

#include "slam_toolbox/slam_toolbox_sync.hpp"

namespace slam_toolbox
{

/*****************************************************************************/
SynchronousSlamToolbox::SynchronousSlamToolbox(ros::NodeHandle& nh)
: SlamToolbox(nh)
/*****************************************************************************/
{
  ssClear_ = nh.advertiseService("clear_queue",
    &SynchronousSlamToolbox::clearQueueCallback, this);

  threads_.push_back(std::make_unique<boost::thread>(
    boost::bind(&SynchronousSlamToolbox::run, this)));

  std::string filename;
  geometry_msgs::Pose2D pose;
  bool dock = false;
  if (shouldStartWithPoseGraph(filename, pose, dock))
  {
    slam_toolbox::DeserializePoseGraph::Request req;
    slam_toolbox::DeserializePoseGraph::Response resp;
    req.initial_pose = pose;
    req.filename = filename;
    if (dock)
    {
      req.match_type =
        slam_toolbox::DeserializePoseGraph::Request::START_AT_FIRST_NODE;
    }
    else
    {
      req.match_type =
        slam_toolbox::DeserializePoseGraph::Request::START_AT_GIVEN_POSE;      
    }
    deserializePoseGraphCallback(req, resp);
  }
}

/*****************************************************************************/
void SynchronousSlamToolbox::run()
/*****************************************************************************/
{
  ros::Rate r(100);
  while(ros::ok())
  {
    if (!q_.empty() && !isPaused(PROCESSING))
    {
      PosedScan scan_w_pose = q_.front();
      q_.pop();

      if (q_.size() > 10)
      {
        ROS_WARN_THROTTLE(10., "Queue size has grown to: %i. "
          "Recommend stopping until message is gone if online mapping.",
          (int)q_.size());
      }

      addScan(getLaser(scan_w_pose.scan), scan_w_pose);
      continue;
    }

    r.sleep();
  }
}

/*****************************************************************************/
void SynchronousSlamToolbox::laserCallback(
  const sensor_msgs::LaserScan::ConstPtr& scan)
/*****************************************************************************/
{
  // no odom info
  karto::Pose2 pose;
  if(!pose_helper_->getOdomPose(pose, scan->header.stamp))
  {
    return;
  }

  // ensure the laser can be used
  karto::LaserRangeFinder* laser = getLaser(scan);

  if(!laser)
  {
    ROS_WARN_THROTTLE(5., "SynchronousSlamToolbox: Failed to create laser"
      " device for %s; discarding scan", scan->header.frame_id.c_str());
    return;
  }

  // if sync and valid, add to queue
  if (shouldProcessScan(scan, pose))
  {
    q_.push(PosedScan(scan, pose));
  }

  return;
}

/*****************************************************************************/
bool SynchronousSlamToolbox::clearQueueCallback(
  slam_toolbox::ClearQueue::Request& req,
  slam_toolbox::ClearQueue::Response& resp)
/*****************************************************************************/
{
  ROS_INFO("SynchronousSlamToolbox: Clearing all queued scans to add to map.");
  while(!q_.empty())
  {
    q_.pop();
  }
  resp.status = true;
  return true;
}

/*****************************************************************************/
bool SynchronousSlamToolbox::deserializePoseGraphCallback(
  slam_toolbox::DeserializePoseGraph::Request& req,
  slam_toolbox::DeserializePoseGraph::Response& resp)
/*****************************************************************************/
{
  if (req.match_type == procType::LOCALIZE_AT_POSE)
  {
    ROS_ERROR("Requested a localization deserialization "
      "in non-localization mode.");
    return false;
  }
  return SlamToolbox::deserializePoseGraphCallback(req, resp);
}

} // end namespace

int main(int argc, char** argv)
{
  ros::init(argc, argv, "slam_toolbox");
  ros::NodeHandle nh("~");
  ros::spinOnce();

  int stack_size;
  if (nh.getParam("stack_size_to_use", stack_size))
  {
    ROS_INFO("Node using stack size %i", (int)stack_size);
    const rlim_t max_stack_size = stack_size;
    struct rlimit stack_limit;
    getrlimit(RLIMIT_STACK, &stack_limit);
    if (stack_limit.rlim_cur < stack_size)
    {
      stack_limit.rlim_cur = stack_size;
    }
    setrlimit(RLIMIT_STACK, &stack_limit);
  }

  slam_toolbox::SynchronousSlamToolbox sst(nh);

  ros::spin();
  return 0;
}
