////////////////////////////////////////////////////////////////////////////
//
// SafetyClient
//
// Class implements an easy way to use iarc7_safety to notify of safety events
//
////////////////////////////////////////////////////////////////////////////

// Associated header
#include "iarc7_safety/SafetyClient.hpp"

// ROS message headers

using namespace Iarc7Safety;

SafetyClient::SafetyClient(ros::NodeHandle& nh, const std::string bond_id) :
bond_id_(bond_id),
bond_("bond_topic", bond_id_, boost::bind(&SafetyClient::onBroken, this), boost::bind(&SafetyClient::onFormed, this))
{
    safety_subscriber_ = nh.subscribe("safety", 100, &SafetyClient::processSafetyMessage, this);
}

bool SafetyClient::formBond()
{
    ROS_INFO("safety_client: trying to form bond %s", bond_.getId().c_str());

    // Try to start the bond
    bond_.start();

    return waitUntilSafe();
}

// This function is a workaround since Bond::waitUntilFormed doesn't work because it doesn't spin
bool SafetyClient::waitUntilSafe()
{
    while(ros::ok())
    {
        if(broken_)
        {
            return false;
        }

        if(formed_)
        {
            return true;
        }

        ros::spinOnce();

        ros::Duration(0.1).sleep();
    }
}

void SafetyClient::processSafetyMessage(const std_msgs::String::ConstPtr& message)
{
    if(message->data == bond_id_)
    {
        safety_active_ = true;
    }
    else if(message->data == fatal_message_)
    {
        safety_active_ = true;
        fatal_active_ = true;
    }
}

bool SafetyClient::isSafetyActive()
{
    return safety_active_;
}

bool SafetyClient::isFatalActive()
{
    return fatal_active_;
}

void SafetyClient::onBroken()
{
    broken_ = true;
    formed_ = false;

    fatal_active_ = true;
    safety_active_ = true;
}

void SafetyClient::onFormed()
{
    broken_ = false;
    formed_ = true;
}
