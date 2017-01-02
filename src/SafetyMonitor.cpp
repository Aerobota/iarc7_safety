////////////////////////////////////////////////////////////////////////////
//
// Safety Node
//
// This node monitors other nodes for failures using bonds
//
// Note that a bond has to be created in both the watched node, and this
// safety node with matching names and IDs (See the Bond Example region below) 
//
// Bond should already exist as a package in ROS. Docs at:
// http://docs.ros.org/api/bondcpp/html/classbond_1_1Bond.html
////////////////////////////////////////////////////////////////////////////

#include <ros/ros.h>

#include "iarc7_safety/SafetyClient.hpp"

#include <bondcpp/bond.h>

#include "std_msgs/String.h"

// Hearbeat interval in seconds
const float kHeartbeatSec = 0.2;

// Time until a bond timeout after last received heartbeat
const float kTimeoutSec = 0.5;

// Rate in hz to check the bonds. Make it loop three times faster than the heartbeats
const float kLoopFrequencyHz = 1.0/(kHeartbeatSec/3.0);

/**
 * Starts a new Safety Node
 */
int main(int argc, char **argv)
{
   // Register this node with ROS, naming it "CORE_safety"
   ros::init(argc, argv, "iarc7_safety");
   
   // Print out that the node has started
   ROS_INFO("node_monitor has started.");

   // Create a handle for this particular node, which is
   // responsible for handling all of the ROS communications
   ros::NodeHandle nh;
   ros::NodeHandle param_nh ("iarc7_safety_node");

   // Create a publisher to advertise this node's presence.
   // This node should only publish in case of emergency, so queue length is 100
   // TODO : Change std_msgs::String to a custom type
   ros::Publisher safety_publisher = nh.advertise<std_msgs::String>("safety", 100);
   
   // Specify a time for the message loop to wait between each cycle (ms)
   ros::Rate loop_rate(kLoopFrequencyHz);

   // Read in parameter containing the bond table
   std::vector<std::string> bond_ids;
   ROS_ASSERT_MSG(param_nh.getParam("bondIds", bond_ids), "iarc7_safety: Can't load bond id list from parameter server");
   ROS_ASSERT_MSG(bond_ids.size() > 0, "iarc7_safety: bondId list is empty");

   // This is the lowest priority that is still safe. It should never be incremented.   
   int32_t lowest_safe_priority{static_cast<int32_t>(bond_ids.size()) - 1};

   // Initialize all the bonds   
   std::vector<std::unique_ptr<Iarc7Safety::SafetyClient>> bonds;
   for(std::string bond_id : bond_ids)
   {
      ROS_INFO("iarc7_safety: Starting bond: %s", bond_id.c_str());

      std::unique_ptr<Iarc7Safety::SafetyClient> bond_ptr(new Iarc7Safety::SafetyClient(nh, bond_id));

      // Start the bond
      bool success = bond_ptr->formBond();
      if(success)
      {
         ROS_INFO("iarc7_safety: Made bond: %s", bond_ptr->getId().c_str());
      }
      else
      {
         ROS_ERROR("iarc7_safety: Could not make bond: %s", bond_ptr->getId().c_str());
         
         // Stop making bonds and immediately set the lowest_safe_priority to fatal.
         // The program did not start correctly.
         lowest_safe_priority = -1;
         bonds.push_back(std::move(bond_ptr));
         break;
      }

      bonds.push_back(std::move(bond_ptr));
   }

   // Continuously loop the node program
   while(true)
   {
      // Go through every node
      for(int32_t i = 0; i < static_cast<int32_t>(bonds.size()); i++)
      {
         // If the safety is on just make sure our priority is as low as that ones
         if (bonds[i]->isSafetyActive())
         {
            // Set lowest safe priority accordingly
            lowest_safe_priority = std::min(i, lowest_safe_priority);
            ROS_ERROR("iarc7_safety: Safety status read when checking bond: %s", bonds[i]->getId().c_str());
         }

         // If fatal is on the bond is broken
         if (bonds[i]->isFatalActive())
         {
            // Make the lowest safe priority one lower, 
            lowest_safe_priority = std::min(i-1, lowest_safe_priority);
            ROS_ERROR("iarc7_safety: Fatal status read when checking bond: %s", bonds[i]->getId().c_str());
         }
      }

      // Make sure the lowest_safe_priority is in a legal range
      ROS_ASSERT_MSG((lowest_safe_priority > -2) && (lowest_safe_priority < static_cast<int32_t>(bonds.size())),
                     "node_monitor: Lowest safe priority is outside of possible range, value: %d", lowest_safe_priority);

      // If lowest_safe_priority is not fatal and not the lowest priority
      // we have a safety event, publish the name of the node to take safety control 
      if(lowest_safe_priority > -1 && lowest_safe_priority < static_cast<int32_t>(bonds.size()) - 1)
      {
         // Publish the current highest level safe node
         // If a node hears its name it should take appropriate action
         std_msgs::String safe_node_name;
         safe_node_name.data = bonds[lowest_safe_priority]->getId();
         safety_publisher.publish(safe_node_name);

         ROS_ERROR("iarc7_safety: safety event: current: priority: %d bondId: %s",
         lowest_safe_priority, bonds[lowest_safe_priority]->getId().c_str());
      }
      // Check for a fatal event
      else if(lowest_safe_priority < 0 )
      {
         // All nodes should try to exit at this point as they are not safe.
         std_msgs::String safe_node_name;
         safe_node_name.data = std::string("FATAL");
         safety_publisher.publish(safe_node_name);

         ROS_ERROR("iarc7_safety: FATAL event: current: priority: %d", lowest_safe_priority);
      }

      // Ensure that the node hasn't been shut down.
      if (!ros::ok()) {
         // If node is shutdown the bonds will break and 
         // any listening nodes will default to a fatal state.
         break;
      }

      // Give callback to subscribed events
      ros::spinOnce();
      
      loop_rate.sleep();
   }

   return 0;
}
