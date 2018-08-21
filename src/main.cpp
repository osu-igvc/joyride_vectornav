/*
 * MIT License (MIT)
 *
 * Copyright (c) 2018 Dereck Wonnacott <dereck@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <iostream>
#define PI 3.14159265358979323846  /* pi */

// ROS Libraries
#include "ros/ros.h"
#include "sensor_msgs/Imu.h"
#include "sensor_msgs/MagneticField.h"
#include "sensor_msgs/NavSatFix.h"
#include "sensor_msgs/Temperature.h"
#include "sensor_msgs/FluidPressure.h"

ros::Publisher pubIMU, pubMag, pubGPS, pubTemp, pubPres;

//Unused covariances initilized to zero's
boost::array<double, 9ul> linear_accel_covariance = { };
boost::array<double, 9ul> angular_vel_covariance = { };
boost::array<double, 9ul> orientation_covariance = { };
XmlRpc::XmlRpcValue rpc_temp;

// Include this header file to get access to VectorNav sensors.
#include "vn/sensors.h"

using namespace std;
using namespace vn::math;
using namespace vn::sensors;
using namespace vn::protocol::uart;
using namespace vn::xplat;

// Method declarations for future use.
void BinaryAsyncMessageReceived(void* userData, Packet& p, size_t index);

std::string frame_id;

//Basic loop so we can initilize our covariance parameters above
boost::array<double, 9ul> setCov(XmlRpc::XmlRpcValue rpc){
    //Output covariance vector
    boost::array<double, 9ul> output = { 0.0 };

    //Convert the RPC message to array
    ROS_ASSERT(rpc.getType() == XmlRpc::XmlRpcValue::TypeArray);

    for(int i = 0; i < 9; i++){
        ROS_ASSERT(rpc[i].getType() == XmlRpc::XmlRpcValue::TypeDouble);
        output[i] = (double)rpc[i];
    }
    return output;
}

int main(int argc, char *argv[])
{

    // ROS node init
    ros::init(argc, argv, "vectornav");
    ros::NodeHandle n;
    ros::NodeHandle pn("~");
    pubIMU = n.advertise<sensor_msgs::Imu>("vectornav/IMU", 1000);
    pubMag = n.advertise<sensor_msgs::MagneticField>("vectornav/Mag", 1000);
    pubGPS = n.advertise<sensor_msgs::NavSatFix>("vectornav/GPS", 1000);
    pubTemp = n.advertise<sensor_msgs::Temperature>("vectornav/Temp", 1000);
    pubPres = n.advertise<sensor_msgs::FluidPressure>("vectornav/Pres", 1000);

    pn.param<std::string>("frame_id", frame_id, "vectornav");

	//Call Set Cov
	if(pn.getParam("linear_accel_covariance",rpc_temp))
    {
        linear_accel_covariance = setCov(rpc_temp);
	}
    if(pn.getParam("angular_vel_covariance",rpc_temp))
    {
        angular_vel_covariance = setCov(rpc_temp);
	}
    if(pn.getParam("orientation_covariance",rpc_temp))
    {
        orientation_covariance = setCov(rpc_temp);
    }





    // Serial Port Settings
    string SensorPort;	
    int SensorBaudrate;
	
	pn.param<std::string>("serial_port", SensorPort, "/dev/ttyUSB0");
	pn.param<int>("serial_baud", SensorBaudrate, 115200);
	
    ROS_INFO("Connecting to : %s @ %d Baud", SensorPort.c_str(), SensorBaudrate);

    // Create a VnSensor object and connect to sensor
    VnSensor vs;
    vs.connect(SensorPort, SensorBaudrate);

    // Query the sensor's model number.
    string mn = vs.readModelNumber();	
    ROS_INFO("Model Number: %s", mn.c_str());

    // Set Data output Freq [Hz]
    int async_output_rate;
    pn.param<int>("async_output_rate", async_output_rate, 40);
    vs.writeAsyncDataOutputFrequency(async_output_rate);
  
	// Configure binary output message
	BinaryOutputRegister bor(
		ASYNCMODE_PORT1,
		1000 / async_output_rate,  // update rate [ms]
		COMMONGROUP_TIMESTARTUP 
        | COMMONGROUP_QUATERNION 
        | COMMONGROUP_ANGULARRATE 
        | COMMONGROUP_POSITION 
        | COMMONGROUP_ACCEL 
        | COMMONGROUP_MAGPRES,
		TIMEGROUP_NONE,
		IMUGROUP_NONE,
		GPSGROUP_NONE,
		ATTITUDEGROUP_YPRU, //<-- returning yaw pitch roll uncertainties
		INSGROUP_NONE);

    vs.writeBinaryOutput1(bor);
    vs.registerAsyncPacketReceivedHandler(NULL, BinaryAsyncMessageReceived);


    // You spin me right round, baby
    // Right round like a record, baby
    // Right round round round
    ros::spin();


    // Node has been terminated
    vs.unregisterAsyncPacketReceivedHandler();
    vs.disconnect();
	return 0;
}


//
// Callback function to process data packet from sensor
//
void BinaryAsyncMessageReceived(void* userData, Packet& p, size_t index)
{
	
    if (p.type() == Packet::TYPE_BINARY)
    {
        // First make sure we have a binary packet type we expect since there
        // are many types of binary output types that can be configured.
        if (!p.isCompatible(
			COMMONGROUP_TIMESTARTUP 
            | COMMONGROUP_QUATERNION 
            | COMMONGROUP_ANGULARRATE 
            | COMMONGROUP_POSITION 
            | COMMONGROUP_ACCEL 
            | COMMONGROUP_MAGPRES,
            TIMEGROUP_NONE,
            IMUGROUP_NONE,
            GPSGROUP_NONE,
            ATTITUDEGROUP_YPRU, //<-- returning yaw pitch roll uncertainties
            INSGROUP_NONE))
            // Not the type of binary packet we are expecting.
            return;


        // Unpack the packet
        uint64_t timeStartup = p.extractUint64();
        vec4f q = p.extractVec4f();
        vec3f ar = p.extractVec3f();
        vec3d lla = p.extractVec3d();
        vec3f al = p.extractVec3f();
        vec3f mag = p.extractVec3f();
        float temp = p.extractFloat();
        float pres = p.extractFloat();
		
		//TEST
		vec3f orientationStdDev = p.extractVec3f();
		
        // Publish ROS Message
		
        // IMU
        sensor_msgs::Imu msgIMU;
		
        msgIMU.header.stamp = ros::Time::now();
        msgIMU.header.frame_id = frame_id;
		
        msgIMU.orientation.x = q[0];
        msgIMU.orientation.y = q[1];
        msgIMU.orientation.z = q[2];
        msgIMU.orientation.w = q[3];
        msgIMU.orientation_covariance[0] = orientationStdDev[0]*orientationStdDev[0]*PI/180;//Convert to radians Roll
        msgIMU.orientation_covariance[4] = orientationStdDev[1]*orientationStdDev[1]*PI/180;//Convert to radians Pitch
        msgIMU.orientation_covariance[8] = orientationStdDev[2]*orientationStdDev[2]*PI/180;//Convert to radians Yaw
		
        msgIMU.angular_velocity.x = ar[0];
        msgIMU.angular_velocity.y = ar[1];
        msgIMU.angular_velocity.z = ar[2];
        msgIMU.angular_velocity_covariance = angular_vel_covariance;
		
        msgIMU.linear_acceleration.x = al[0];
        msgIMU.linear_acceleration.y = al[1];
        msgIMU.linear_acceleration.z = al[2];
        msgIMU.linear_acceleration_covariance = linear_accel_covariance;
		
        pubIMU.publish(msgIMU);

    
        // Magnetic Field
        sensor_msgs::MagneticField msgMag;
    
        msgMag.header.stamp = msgIMU.header.stamp;
        msgMag.header.frame_id = msgIMU.header.frame_id;

        msgMag.magnetic_field.x = mag[0];
        msgMag.magnetic_field.y = mag[1];
        msgMag.magnetic_field.z = mag[2];

        pubMag.publish(msgMag);
    
    
        // GPS
        sensor_msgs::NavSatFix msgGPS;
    
        msgGPS.header.stamp = msgIMU.header.stamp;
        msgGPS.header.frame_id = msgIMU.header.frame_id;

        msgGPS.latitude = lla[0];
        msgGPS.longitude = lla[1];
        msgGPS.altitude = lla[2];

        pubGPS.publish(msgGPS);
    
    
        // Temperature
        sensor_msgs::Temperature msgTemp;
    
        msgTemp.header.stamp = msgIMU.header.stamp;
        msgTemp.header.frame_id = msgIMU.header.frame_id;
    
        msgTemp.temperature = temp;
    
        pubTemp.publish(msgTemp);
    
    
        // Barometer
        sensor_msgs::FluidPressure msgPres;
    
        msgPres.header.stamp = msgIMU.header.stamp;
        msgPres.header.frame_id = msgIMU.header.frame_id;
    
        msgPres.fluid_pressure = pres;
    
        pubPres.publish(msgPres);
  
    }
}

