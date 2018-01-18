
#include <iostream>
#include <ros/ros.h>
#include <std_msgs/Char.h>
#include <sstream>
#include "std_msgs/String.h"
#include <stdio.h>
#include <ros/callback_queue.h>
#include <stdlib.h>
#include <chrono>
#include <sys/types.h>
#include <sys/stat.h>
#include <fstream>
#include <vector>
#include <rrtstar_msgs/rrtStarSRV.h>
#include <rrtstar_msgs/Region.h>
#include <geometry_msgs/Vector3.h>
//#include <cmat/cmat.h>
#include <simRobot_msgs/simulateRobotSRV.h>
#include <simRobot_msgs/sim_robot.h>
#include <simRobot_msgs/transformation.h>


#include"RobotCallback.hpp"
#include "controlCommnad_msgs/control.h"



typedef vector < vector <float> > vect2;

void wayPointOrientation(const int , float * , float* ,vect2& );
int pathsVector2PosePoint(int armState,vect2  pathVector,  vector<bool>  &pathPointsFlagRef,int pathSize , double * wTo,
		controlCommnad_msgs::control &control_msg, bool & robot_send_control_command_flag);

int main(int argc, char** argv) {
	ros::init(argc, argv, "api_hri_pitt");
	ros::NodeHandle nh;

	//	ros::Publisher pubPathReg=nh.advertise<std_msgs::String>("pathPlannerRegions",1); // later change as a srv
	//	ros::Publisher pubPathObs=nh.advertise<std_msgs::String>("pathPlannerObstacles",1);

	//! Definition control interface parameters:
	ros::Publisher pub_ctrl_cmnd=nh.advertise<controlCommnad_msgs::control>("robot_control_command",80);
	ros::Publisher pub_ctrl_error=nh.advertise<std_msgs::String>("hri_control_error_check",80);
	const int NO_ARMS=2;
	const int No_Sphere=0;		//! number of sphere in workspace
	const int No_Cylinder=2;	//! number of Cylinder in workspace
	const int No_Cone=0;		//! number of Cone in workspace
	const int No_Plane=1;		//! number of Plane in workspace
	const int No_Unknown=0;		//! number of Unknown Objects in workspace

	int NO_ArmState=3;	//left,right,bimanual
	int NO_ctrlCmdType=5;	//SigleArm,biManual,Gripper, Stop,HoldingMode
	int ctrlCmdTypeGoalReachedCounter=0, armStateGoalReachedCounter=0;
	int responsible_arm=0; //! the arm which should grasp the object (0,1,2)
	int num_joint=7;

	const int No_Objects=No_Sphere+No_Cylinder+No_Cone+No_Plane;

	std_msgs::String msg_ctrl_err;
	bool control_error_flag=true, control_error_stop_flag=true; //DEL, first
	std_msgs::String msg_ctrl_cmnd[NO_ArmState] ;
	bool robot_send_control_command_flag[NO_ArmState];
	int control_count[NO_ArmState], control_goal_count[NO_ArmState][NO_ctrlCmdType];

	for (int i1=0;i1<NO_ArmState;i1++)
	{
		robot_send_control_command_flag[i1]=false;// when true robot sends a command
		control_count[i1]=0;
		for (int j1=0;j1<NO_ctrlCmdType;j1++)
			control_goal_count[i1][j1]=0;
	}
	int rob_goal_reach_flag_counter=0;
	std::stringstream ss_ctrl_cmnd;
	int noParamCtrlInit=2;//Initial Control Parameters
	int noParamCtrl=6;//Execution Control Parameters, 		[1st: set/get, , 2-4: Actions Parameters,]
	float paramCtrlInit [noParamCtrlInit];// Initial Parameters of Controller
	float paramCtrl[noParamCtrl];// first input: set Data: 1, or get Data:0
	int max_time_rob_reach_goal=50;//sec

// rrt* Service:
	ros::ServiceClient rrtStar_client = nh.serviceClient<rrtstar_msgs::rrtStarSRV>("rrtStarService");
	rrtstar_msgs::rrtStarSRV rrtSRV;
	rrtstar_msgs::Region obstacle;
	geometry_msgs::Vector3 pints;

// robot simulator service:
	ros::ServiceClient simRobot_client = nh.serviceClient<simRobot_msgs::simulateRobotSRV>("robotSimulator_service");
	simRobot_msgs::simulateRobotSRV simRobot_srv;
	simRobot_msgs::transformation simRobot_pose;



	controlCommnad_msgs::control control_msg[NO_ArmState];

	float InitPose[6],obstacleBoundingBox[6],pathPlanningPoseGoal[6], graspingPoseGoal[6];
	float GoalOrientation[3],goalRegion[3];
	for (int i=0;i<6;i++)
	{
		InitPose[i]=0.0;
		obstacleBoundingBox[i]=0.0;
		pathPlanningPoseGoal[i]=0.0;
		graspingPoseGoal[i]=0.0;
	}

	GoalOrientation[0]=0.0;GoalOrientation[1]=0.0;GoalOrientation[2]=0.0;
	goalRegion[0]=0.04;goalRegion[1]=0.04;goalRegion[2]=0.04;

	//	vector <float **> pathVector;
	vector<float> pathPoint(6,0.0); //! each points of path
	vect2 Path;
	vector < vect2  > pathVector;	//! we can compute for each arm state the path (left and right or biman)
	vector<bool> pathPointsFlag;	//! save each point in the path if it is done (true) or it is not done (false)
	vector < vector<bool> > pathPointsFlagVector; //! save pathPointsFlag for all arm states in a vector
	vector <int> pathNO_ArmStateVector; //! save for each element in pathvector, which arm state it is.

	vector <int> pathSizeVector; //! for each arm state save what is the path size.
	pathVector.resize(NO_ArmState);
	pathPointsFlagVector.resize(NO_ArmState);
	pathSizeVector.resize(NO_ArmState);

	bool pathCheck[NO_ArmState];
	pathCheck[0]=false;pathCheck[1]=false;pathCheck[2]=false;
	int pathPointNumber[NO_ArmState];	//! save the number of point we are in the path
	pathPointNumber[0]=0;pathPointNumber[1]=0;pathPointNumber[2]=0;
	string controlState="armControl";

	int pathNO_ArmState;

	//float **Path;
	int pathSize=0;			//! rrtStar output path size
	//Path=new float*[6]; 	//! Yaw pitch roll, x y z

	int pathPointsFlagCounter=0;
	int pathDoneCounterArms=0;
	int No_activeArmState=0;

	//	bool arms[NO_ArmState];
	//	arms[0]=false;arms[1]=false;arms[2]=false;


	int aNumber;

	int ros_freq=80;//hz
	ros::Rate loop_rate(ros_freq);//
	long int count=0;
	usleep(1e2);

	robotCallback robot_call(No_Sphere,No_Cylinder,No_Plane,No_Cone,No_Unknown);

	bool ObjectRecognition=false;			//! if this flag is true we should check environment and get information from it;
	bool pathPlanningFlag=true;	//! if it is true we should do path planning
	bool NopathPlanningFlag=false;	//! path planning is not necessary.
	bool simulationFlag=false;
	bool controllerFlag= false;		//! if it is true we should do path planning
	int grasping_obj_id=0;

	controllerFlag=true;
//	ObjectRecognition=true;
//	pathPlanningFlag=false;
	//	arms[2]=true;
	//	arms[1]=true;
	//	controllerFlag=true;

	// consider small sphere as our goal now
	// now consider left armsm when it is done consider both arms together to define it.
	while (ros::ok()) {

		// if y grasping > 0 --> grasp by left hand
		// if y grasping < 0 --> grasp by right hand
		/*
		if (robot_call.obj_call_back_flag==false && ObjectRecognition==true){

			grasping_obj_id=0;
			// left, right, biman checking for grasping --> in biman case it is different, in other cases they are the same.

			robot_call.objectsVector[grasping_obj_id]->GraspingPosition(graspingPoseGoal,pathPlanningPoseGoal,"top");
			responsible_arm=robot_call.objectsVector[grasping_obj_id]->RobotResponsibleArm();
			robot_call.armsStateFlag_NotIdle[responsible_arm]=true;
			robot_call.pathPlanningFlag=false;
			ObjectRecognition=false;
		}
		*/

/*
		if (robot_call.pathPlanningFlag==true ){
			//			pitt_call.pitt_call_back_flag=false;
			//! 1- get info of initPos from controller *
			//! 2- give the working Environment,  Goal Region, initPos, to the planner
			//! 3- give obstacle to path planner
			//! wait for planner to return path points
			//! check number of way point to reach the goal
			//! 	(for computing orientation of the controller at each way point and give it to the controller (finally be smooth))
			//! make interpolation for orientation of ee (using a function)

			//! 2&3
			//Grasping Position:
			for (int arm_counter=0;arm_counter<NO_ArmState;arm_counter++)
				if (robot_call.armsStateFlag_NotIdle[arm_counter]==true){
					vect2 Path;
					vector<bool> pathPointsFlag;
					InitPose[0]=robot_call.initPos[arm_counter][0];
					InitPose[1]=robot_call.initPos[arm_counter][1];
					InitPose[2]=robot_call.initPos[arm_counter][2];
					InitPose[3]=robot_call.initPos[arm_counter][3];
					InitPose[4]=robot_call.initPos[arm_counter][4];
					InitPose[5]=robot_call.initPos[arm_counter][5];

					//					GoalOrientation[0]=robot_call.orientationGoal[arm_counter][0];
					//					GoalOrientation[1]=robot_call.orientationGoal[arm_counter][1];
					//					GoalOrientation[2]=robot_call.orientationGoal[arm_counter][2];

					for (int i=0;i<3;i++)
						GoalOrientation[i]=pathPlanningPoseGoal[i]; //Y,P,R



					//cout<<"InitPosition: "<<InitPosition[0]<<" "<<InitPosition[1]<<" "<<InitPosition[2]<<" "<<InitPosition[3]<<" "<<InitPosition[4]<<" "<<InitPosition[5]<<endl;

					cout<<"pathPlanningPoseGoal: "<<pathPlanningPoseGoal[0]<<" "<<pathPlanningPoseGoal[1]<<" "<<pathPlanningPoseGoal[2]<<" "<<pathPlanningPoseGoal[3]<<" "<<pathPlanningPoseGoal[4]<<" "<<pathPlanningPoseGoal[5]<<endl;
					cout<<"graspingPoseGoal: "<<graspingPoseGoal[0]<<" "<<graspingPoseGoal[1]<<" "<<graspingPoseGoal[2]<<" "<<graspingPoseGoal[3]<<" "<<graspingPoseGoal[4]<<" "<<graspingPoseGoal[5]<<endl;
					cout<<"GoalOrientation: "<<GoalOrientation[0]<<" "<<GoalOrientation[1]<<" "<<GoalOrientation[2]<<endl;


					//					rrtSRV.request.Goal.center_x=robot_call.regionGoal[arm_counter][0];
					//					rrtSRV.request.Goal.center_y=robot_call.regionGoal[arm_counter][1];
					//					rrtSRV.request.Goal.center_z=robot_call.regionGoal[arm_counter][2];

					rrtSRV.request.Goal.center_x=pathPlanningPoseGoal[3];
					rrtSRV.request.Goal.center_y=pathPlanningPoseGoal[4];
					rrtSRV.request.Goal.center_z=pathPlanningPoseGoal[5];

					rrtSRV.request.Goal.size_x=goalRegion[0];
					rrtSRV.request.Goal.size_y=goalRegion[1];
					rrtSRV.request.Goal.size_z=goalRegion[2];

					//cout<<"regionGoal: "<<pitt_call.regionGoal[arm_counter][0]<<" "<<pitt_call.regionGoal[arm_counter][1]<<" "<<pitt_call.regionGoal[arm_counter][2]<<
					//" "<<pitt_call.regionGoal[arm_counter][3]<<" "<<pitt_call.regionGoal[arm_counter][4]<<" "<<pitt_call.regionGoal[arm_counter][5]<<endl;

					rrtSRV.request.WS.center_x=robot_call.regionOperating[0];
					rrtSRV.request.WS.center_y=robot_call.regionOperating[1];
					rrtSRV.request.WS.center_z=robot_call.regionOperating[2];
					rrtSRV.request.WS.size_x=robot_call.regionOperating[3];
					rrtSRV.request.WS.size_y=robot_call.regionOperating[4];
					rrtSRV.request.WS.size_z=robot_call.regionOperating[5];

					//cout<<"regionGoal: "<<pitt_call.regionGoal[arm_counter][0]<<" "<<pitt_call.regionGoal[arm_counter][1]<<" "<<pitt_call.regionGoal[arm_counter][2]<<
					//" "<<pitt_call.regionGoal[arm_counter][3]<<" "<<pitt_call.regionGoal[arm_counter][4]<<" "<<pitt_call.regionGoal[arm_counter][5]<<endl;

					rrtSRV.request.Init.x=InitPose[3];
					rrtSRV.request.Init.y=InitPose[4];
					rrtSRV.request.Init.z=InitPose[5];

					//			if (pitt_call.NoCorrectRecognizedObject!=2)
					//				cout<<FRED("No of Recognized Obstacles is not Correct")<<endl;
					//			else{
					for (int i=0;i<robot_call.NoObstacles;i++){
						//						if (i!=robot_call.goalID){
						if (i!=grasping_obj_id){
							robot_call.objectsVector[i]->BoundingBox(obstacleBoundingBox);
							cout<<"obstacleBoundingBox["<<i<<"]: "<<
									obstacleBoundingBox[0]<<" "<<obstacleBoundingBox[1]<<obstacleBoundingBox[2]<<" "<<
									obstacleBoundingBox[3]<<" "<<obstacleBoundingBox[4]<<" "<<obstacleBoundingBox[5]<<endl;
							obstacle.center_x=obstacleBoundingBox[0];
							obstacle.center_y=obstacleBoundingBox[1];
							obstacle.center_z=obstacleBoundingBox[2];
							obstacle.size_x=obstacleBoundingBox[3];
							obstacle.size_y=obstacleBoundingBox[4];
							obstacle.size_z=obstacleBoundingBox[5];

							rrtSRV.request.Obstacles.push_back(obstacle);
						}
					}

					if (arm_counter==2){
						robot_call.biManualControlCmndParameters();
						robot_call.publishControlTasksParam();

					}

					//				for (int i=0;i<6;i++)
					//					delete [] Path[i]; // when receive a new path delete the previous path (because depend on path size)
					//				delete [] pathPointsFlag;
					//			}

					if (rrtStar_client.call(rrtSRV)){

						pathSize=rrtSRV.response.path.size();// the +1 is from grasping pos to actual goal pos.
						if (rrtSRV.response.path.size()>0){
							//							for (int i=0;i<6;i++)
							//								Path.resize(pathSize); // No. of points of the trajectory
							pathPointsFlag.resize(pathSize);
							for(int i=0;i<pathSize;i++)
								pathPointsFlag[i]=false;
							pathPointsFlag[0]=true;

						}
						else {
							cout<<FRED("No Trajectory is Returned from rrtStar")<<endl;
							//							exit(1);
						}
						//						cout<<1111<<endl;
						for (int i=0;i<pathSize;i++){
							pathPoint[3]=rrtSRV.response.path[i].x;	//x
							pathPoint[4]=rrtSRV.response.path[i].y;	//y
							pathPoint[5]=rrtSRV.response.path[i].z;	//z
						//  cout<<"pathPoint[]"<<pathPoint[0]<<" "<<pathPoint[1]<<" "<<pathPoint[2]<<" "<<pathPoint[3]<<" "<<pathPoint[4]<<" "<<pathPoint[5]<<endl;

							Path.push_back(pathPoint);
						}
						//						cout<<22222222<<endl;
						//						for (int i=0;i<Path.size();i++)
						//							cout<<"Path[]["<<i<<"]: "<<Path[i][0]<<" "<<Path[i][1]<<" "<<Path[i][2]<<" "<<Path[i][3]<<" "<<Path[i][4]<<" "<<Path[i][5]<<endl;
						//						wayPointOrientation(pathSize, InitPose, GoalOrientation, Path);
						//						wayPointOrientation(pathSize, InitPose, InitPose, Path);
						wayPointOrientation(pathSize, GoalOrientation, GoalOrientation, Path);
						std::cout << std::fixed;
						std::cout<<std::setprecision(5);

						//						Path.clear();

						//						for (int i=0;i<6;i++){
						//							pathPoint[i]=InitPose[i];
						//						}
						//						Path.push_back(pathPoint);
						//						pathSize++;
						//						pathPointsFlag.push_back(true);


						//						for (int i=0;i<6;i++){
						//							pathPoint[i]=pathPlanningPoseGoal[i];
						//						}
						//						Path.push_back(pathPoint);
						//						pathSize++;
						//						pathPointsFlag.push_back(false);


						for (int i=0;i<6;i++){
							pathPoint[i]=graspingPoseGoal[i];
						}
						Path.push_back(pathPoint);
						pathSize++;
						pathPointsFlag.push_back(false);

						for (int i=0;i<Path.size();i++)
							cout<<"Path[]["<<i<<"]: "<<Path[i][0]<<" "<<Path[i][1]<<" "<<Path[i][2]<<" "<<Path[i][3]<<" "<<Path[i][4]<<" "<<Path[i][5]<<endl;

						cout<<"pathSize: "<<pathSize<<endl;
						cout<<"arm_counter: "<<arm_counter<<endl;\
						cout<<"pathPointsFlag.size(): "<<pathPointsFlag.size()<<endl;

						pathVector[arm_counter]=Path;
						pathNO_ArmStateVector.push_back(arm_counter);
						pathSizeVector[arm_counter]=pathSize;
						pathPointsFlagVector[arm_counter]=pathPointsFlag;
						//						cout<<"***"<<endl;
						//						cout<<pathSizeVector[0]<<endl;
						//						cout<<pathVector.size()<<endl;

						cout<<"***"<<endl;
						cout<<"arm_counter: "<<arm_counter<<endl;
						cout<<pathSizeVector[arm_counter]<<endl;
						cout<<pathVector[arm_counter].size()<<endl;
						cout<<pathVector[arm_counter][0].size()<<endl;
						cout<<pathPointsFlagVector[arm_counter].size()<<endl;
						for (int i=0;i<pathSizeVector[arm_counter];i++){
							for (int j=0; j<6;j++)
								cout<<pathVector[arm_counter][i][j]<<" ";
							cout<<pathPointsFlagVector[arm_counter][i]<<endl;
						}
						robot_call.pathPlanningFlag=false;
//						robot_call.controllerFlag=true;
						robot_call.simulationFlag=true;
						pathCheck[arm_counter]=true;

						robot_call.robotCommandType=e_pointReach;
						robot_call.armstateFlag_doAction[arm_counter]=true;
					}
				}
			//			cout<<pathSizeVector[0]<<endl;
			//			cout<<pathVector.size()<<endl;

		}
	*/



		//		if (NopathPlanningFlag==true &&  pitt_call.NoCorrectRecognizedObject==2){
		//			for (int arm_counter=0;arm_counter<3;arm_counter++)
		//				if (arms[arm_counter]==true){
		//
		//					pathSize=3;
		////			if (sizeof(Path[0])/sizeof(*Path[0])>1)
		////				for (int i=0;i<6;i++)
		////					delete [] Path[i]; // when receive a new path delete the previous path (because depend on path size)
		////			if (sizeof(pathPointsFlag)/sizeof(*pathPointsFlag)>1)
		////				delete [] pathPointsFlag;
		//					for (int i=0;i<6;i++)
		//						Path[i]=new float[pathSize]; // No. of points of the trajectory
		//
		//					pathPointsFlag=new bool[pathSize];
		//					for(int i=0;i<pathSize;i++)
		//						pathPointsFlag[i]=false;
		//					pathPointsFlag[0]=true;
		//			//initial position
		//					Path[0][0]=pitt_call.initPos[arm_counter][0];		//yaw
		//					Path[1][0]=pitt_call.initPos[arm_counter][1];		//pitch
		//					Path[2][0]=pitt_call.initPos[arm_counter][2];		//roll
		//					Path[3][0]=pitt_call.initPos[arm_counter][3];		//x
		//					Path[4][0]=pitt_call.initPos[arm_counter][4];		//y
		//					Path[5][0]=pitt_call.initPos[arm_counter][5];		//z
		//			// final position
		//					Path[0][1]=pitt_call.orientationGoal[arm_counter][0];		//yaw
		//					Path[1][1]=pitt_call.orientationGoal[arm_counter][1];		//pitch
		//					Path[2][1]=pitt_call.orientationGoal[arm_counter][2];		//roll
		//					Path[3][1]=pitt_call.regionGoal[arm_counter][0];			//x
		//					Path[4][1]=pitt_call.regionGoal[arm_counter][1];			//y
		//					Path[5][1]=pitt_call.regionGoal[arm_counter][2]+0.1;		//z
		//
		//					Path[0][2]=pitt_call.orientationGoal[arm_counter][0];	//yaw
		//					Path[1][2]=pitt_call.orientationGoal[arm_counter][1];	//pitch
		//					Path[2][2]=pitt_call.orientationGoal[arm_counter][2];	//roll
		//					Path[3][2]=pitt_call.regionGoal[arm_counter][0];			//x
		//					Path[4][2]=pitt_call.regionGoal[arm_counter][1];			//y
		//					Path[5][2]=pitt_call.regionGoal[arm_counter][2]-0.03;	//z
		//
		//					cout<<"init:   "<< Path[0][0]<<" "<<Path[1][0]<<" "<<Path[2][0]<<" "<< Path[3][0]<<" "<<Path[4][0]<<" "<<Path[5][0]<<endl;
		//					cout<<"middle: "<< Path[0][1]<<" "<<Path[1][1]<<" "<<Path[2][1]<<" "<< Path[3][1]<<" "<<Path[4][1]<<" "<<Path[5][1]<<endl;
		//					cout<<"final:  "<< Path[0][2]<<" "<<Path[1][2]<<" "<<Path[2][2]<<" "<< Path[3][2]<<" "<<Path[4][2]<<" "<<Path[5][2]<<endl;
		//				}
		//
		//			NopathPlanningFlag=false;
		//			pathCheck[0]=true;
		//			controllerFlag=true;
		//			controlState="ArmControl";
		//
		//		}
/*
		if (robot_call.directPathAllocationFalg==true){
			for (int i=0;i<NO_ArmState;i++)
				if (robot_call.readArmPathFlag[i]==true){
					vect2 Path;
					vector<bool> pathPointsFlag;
					pathSize=1;
					pathPointsFlag.resize(pathSize);
					for(int i=0;i<pathSize;i++)
						pathPointsFlag[i]=false;
					for (int j=0;j<6;j++)
						pathPoint[j]=robot_call.ReachingPoint[i][j];

					Path.push_back(pathPoint);
					pathVector[i]=Path;
					pathSizeVector[i]=pathSize;
					pathPointsFlagVector[i]=pathPointsFlag;
					robot_call.directPathAllocationFalg=false;
					robot_call.readArmPathFlag[i]=false;
					cout<<"333333 "<<pathVector[i][0][0]<<" "<<pathVector[i][0][5]<<endl;
				}
		}
		*/

/*
		if (robot_call.simulationFlag==true){
			for (int arm_counter=0; arm_counter<NO_ArmState;arm_counter++)
				if (robot_call.armsStateFlag_NotIdle[arm_counter]==true){
					//! fill the simulation service with the path points and the q_init from the robot and call the service:
					if (arm_counter<2) // single Arm:
					{
						simRobot_srv.request.simRobot.Activation=1;
						simRobot_srv.request.simRobot.sim_single_arm.armIndex=arm_counter;
						simRobot_srv.request.simRobot.sim_single_arm.NoGoals=pathSizeVector[arm_counter]-1; // because the first path pose is the inital point of the baxter
						for (int i=1;i<pathSizeVector[arm_counter];i++){
							for (int j=0;j<6;j++){
								simRobot_pose.cartesianPosition[j]=pathVector[arm_counter][i][j];
							}
							simRobot_srv.request.simRobot.sim_single_arm.cartGoal.push_back(simRobot_pose);
						}
						for (int i=0;i<num_joint;i++)
						simRobot_srv.request.simRobot.sim_single_arm.jointsInit.jointPosition[i]=robot_call.init_q_[arm_counter][i];


					}
					else // bimanual Arm:
					{
						simRobot_srv.request.simRobot.Activation=2;
						int armIndex1=0 ,armIndex2=1;
						simRobot_srv.request.simRobot.sim_biman_arm.arm1_Index=armIndex1;
						simRobot_srv.request.simRobot.sim_biman_arm.arm2_Index=armIndex2;
						simRobot_srv.request.simRobot.sim_biman_arm.NoGoals=pathSizeVector[arm_counter]-1; // because the first path pose is the inital point of the baxter
						for (int j=0;j<6;j++)
							simRobot_srv.request.simRobot.sim_biman_arm.wTo.cartesianPosition[j]=pathVector[arm_counter][0][j];
						for (int i=1;i<pathSizeVector[arm_counter];i++){
							for (int j=0;j<6;j++){
								simRobot_pose.cartesianPosition[j]=pathVector[arm_counter][i][j];
						}
						simRobot_srv.request.simRobot.sim_biman_arm.wTg.push_back(simRobot_pose);
					}
						for (int i=0;i<num_joint;i++){
							simRobot_srv.request.simRobot.sim_biman_arm.jointsInit_arm1.jointPosition[i]=robot_call.init_q_[armIndex1][i];
							simRobot_srv.request.simRobot.sim_biman_arm.jointsInit_arm2.jointPosition[i]=robot_call.init_q_[armIndex2][i];
						}
					}
					/**
					*  if the result of the simulation is true:
					*  	robot_call.armsStateFlag_NotIdle[arm_counter]=true
					*  else
					*  	robot_call.armsStateFlag_NotIdle[arm_counter]=false
					*/
/*					if (simRobot_client.call(simRobot_srv)){
						if (simRobot_srv.response.simResponse){
							robot_call.armsStateFlag_NotIdle[arm_counter]=true;
							cout<<"**** Simulation Shoes Robot Can Follow the Given Path ****"<<endl;
						}
						else{
							/**
							 *  send a msg to hri package that robot can not perform the action
							 *  , human does it, or change sth in environment , so that robot can perform this action
							 */
/*							cout<<"**** Simulation Shoes Robot Can NOT Follow the Given Path ****"<<endl;
//							robot_call.armsStateFlag_NotIdle[arm_counter]=false;
						}
					}
				}

			// if at least one arm can follow the path:
			robot_call.controllerFlag=false;
			for (int arm_counter=0; arm_counter<NO_ArmState;arm_counter++)
				if (robot_call.armsStateFlag_NotIdle[arm_counter]==true)
					robot_call.controllerFlag=true;


			robot_call.simulationFlag=false;

			cin>>aNumber;
			usleep(2e6);
		}
*/
		/*! Flags:
		 * controllerFlag: True: when there are a command for the controller to send
		 * armsStateFlag_NotIdle[i]: True: for each of the arms states (left, right, biman) check if there is non-finished command to be sent.
		 * */


/*
		if (robot_call.controllerFlag==true){

			/*!
			 *  1- check here if the command sent before to controller is reached or not
			 *  	if it is reached
			 *  			if command: e_pointReach
			 *  				check for the next point flag in the path
			 *  			else
			 *
			 *

Take care of reaching the the goal of each points in path
			 */
			//!	If there is a command for one of the arms the "controller section"should be active, if not -> inactive:
/*			armStateGoalReachedCounter=0;
			//			cout<<"controller01 "<<endl;
			for (int i=0; i<NO_ArmState;i++)
				//
				if (robot_call.armsStateFlag_NotIdle[i]==true){
					//					cout<<"controller02 "<<i<<endl;
					ctrlCmdTypeGoalReachedCounter=0;
					// check for if one of the cmd typed reached its goal update its corresponding flags
					for (int j=0;j<NO_ctrlCmdType;j++){
						// here we check if cmd types: stop, hold mode, gripper reached their goal, change their corresponding flags
						if(j>1 && robot_call.controlCmndSent_goalNotReached[i][j]==true){
							if (robot_call.control_goal_reach_ack[i][j]==true){
								robot_call.controlCmndSent_goalNotReached[i][j]=false;
								robot_call.control_goal_reach_ack[i][j]=false;
								robot_call.PublishRobotAck(i,j);

							}
						}
						// here we check if cmd types: point reach for left, right, biman
						else if (j<=1 && robot_call.controlCmndSent_goalNotReached[i][j]==true){
							if (robot_call.control_goal_reach_ack[i][j]==true){
								robot_call.control_goal_reach_ack[i][j]=false;
								cout<<"robot_call.control_goal_reach_ack[i][j]: "<<i<<j<<robot_call.control_goal_reach_ack[i][j]<<endl;

								vector<bool> &pathPointsFlagRef=pathPointsFlagVector[i];
								pathPointsFlagRef[pathPointNumber[i]]=true;// check here if it changes values in vector or not.
								pathSize=pathSizeVector[i];
								cout<<"pathPointNumber[i]: "<<pathPointNumber[i]<<endl;
								cout<<"pathPointsFlagRef[pathPointNumber[i]]: "<<pathPointsFlagRef[pathPointNumber[i]]<<endl;
								if (pathPointsFlagRef[pathSize-1]==true){
									cout<<FGRN("Path Is Reached: ")<< i<<endl;
									robot_call.controlCmndSent_goalNotReached[i][j]=false;
									//									pathCheck[i]=false;
									robot_call.PublishRobotAck(i,j);
								}
								else{
									cout<<"controller023 "<<endl;
									//									pathCheck[i]=true;// change place! maybe not need!
									robot_call.armstateFlag_doAction[i]=true;
									robot_call.robotCommandType=e_pointReach;
								}
							}
						}
						else if(robot_call.controlCmndSent_goalNotReached[i][j]==false){
							ctrlCmdTypeGoalReachedCounter++;
							//							cout<<333333<<endl;
						}
						if (count>=control_goal_count[i][j]+(max_time_rob_reach_goal*ros_freq)
								&& robot_call.controlCmndSent_goalNotReached[i][j]==true){
							cout<<"Robot can not execute the command for arm state: "<<i<<", and for command type: "<<j<<endl;
							// maybe send the command to controller again, or ask planner for another plan or human to help!
						}

					}
					if (ctrlCmdTypeGoalReachedCounter==NO_ctrlCmdType && robot_call.armstateFlag_doAction[i]==false){
						// 2nd condition: when new cmnd arrive the first condition is true, but we can not say the arm_state go to idle mode
						//						cout<<"All the control goals for this arm state are reached"<<endl;
						robot_call.armsStateFlag_NotIdle[i]=false;
						armStateGoalReachedCounter++;
					}
					//! If a new command received or one of command types are unfinished we should fill control msg and send
					//! assumption: at each moment one command from robot to controller can be sent
					//! if stop Arm command received paths should be empty
					if (robot_call.armstateFlag_doAction[i]==true){
						//						cout<<"controller03 "<<i<<endl;
						switch (robot_call.robotCommandType){

						case e_grasp:
							control_msg[i].Activation=2;
							control_msg[i].GripArm.arm=i;
							control_msg[i].GripArm.value=0;
							robot_call.controlCmndSent_goalNotReached[i][2]=true;
							robot_send_control_command_flag[i]=true;
							break;
						case e_unGrasp:
							control_msg[i].Activation=2;
							control_msg[i].GripArm.arm=i;
							control_msg[i].GripArm.value=1;
							robot_call.controlCmndSent_goalNotReached[i][2]=true;
							robot_send_control_command_flag[i]=true;
							break;
						case e_stop:
							control_msg[i].Activation=3;
							control_msg[i].stopArm.arm=i;
							robot_call.controlCmndSent_goalNotReached[i][3]=true;
							robot_send_control_command_flag[i]=true;
							// think more bout this command, because of path points
							if (i==0 || i==1)
								robot_call.controlCmndSent_goalNotReached[i][0]=false;
							if (robot_call.controlCmndSent_goalNotReached[0][0]==false
									&&	robot_call.controlCmndSent_goalNotReached[0][0]==false)
								robot_call.controlCmndSent_goalNotReached[i][1]=false;

							break;
						case e_holdOn:
							control_msg[i].Activation=4;
							control_msg[i].holdModeArm.arm=i;
							control_msg[i].holdModeArm.holdingmode=1;
							robot_call.controlCmndSent_goalNotReached[i][4]=true;
							robot_send_control_command_flag[i]=true;
							break;
						case e_holdOff:
							control_msg[i].Activation=5;
							control_msg[i].holdModeArm.arm=i;
							control_msg[i].holdModeArm.holdingmode=0;
							robot_call.controlCmndSent_goalNotReached[i][4]=true;
							robot_send_control_command_flag[i]=true;
							break;
						case e_pointReach:
							cout<<"controller041111 "<<endl;
							cout<<i<<endl;
							cout<<pathSizeVector[i]<<endl;
							cout<<pathVector[i].size()<<endl;
							cout<<"controller04222222 "<<endl;
							pathPointsFlagCounter=pathsVector2PosePoint(i, pathVector[i], pathPointsFlagVector[i],pathSizeVector[i],
									robot_call.wTo_BiMan, control_msg[i], robot_send_control_command_flag[i] );
							cout<<"robot_send_control_command_flag[i]: "<<robot_send_control_command_flag[i]<<endl;
							if (robot_send_control_command_flag[i]==true){
								cout<<"controller06 "<<endl;
								//								pathCheck[i]=false;
								//								cout<<"444444 "<<control_msg[i].oneArm.cartGoal.cartesianPosition[0]<<endl;
								pathPointNumber[i]=pathPointsFlagCounter;
								if (i==0 || i==1)
									robot_call.controlCmndSent_goalNotReached[i][0]=true;
								if (i==2)
									robot_call.controlCmndSent_goalNotReached[i][1]=true;
							}
							else{
								cout<<"55555 "<<control_msg[i].oneArm.cartGoal.cartesianPosition[0]<<endl;
								if (i==0 || i==1)
									robot_call.controlCmndSent_goalNotReached[i][0]=false;
								if (i==2)
									robot_call.controlCmndSent_goalNotReached[i][1]=false;
							}
							//							cout<<"666666 "<<control_msg[i].oneArm.cartGoal.cartesianPosition[0]<<endl;
							float zz8=control_msg[i].oneArm.cartGoal.cartesianPosition[0];
							float zz9=control_msg[i].oneArm.cartGoal.cartesianPosition[3];

							int zz7=control_msg[i].oneArm.armIndex;
							cout<<"reaching point: ";
							for (int mm=0;mm<6;mm++)
								cout<<control_msg[i].oneArm.cartGoal.cartesianPosition[mm]<<" ";
							cout<<endl;
							cout<<"0000::"<<zz7<<" "<<zz8<<" "<<zz9<<endl;
							break;
						}
						robot_call.armstateFlag_doAction[i]=false;
					}
					//
					if ( robot_send_control_command_flag[i]==true){
						ROS_INFO("Robot Interface:: publish Control Command: cmndType: %d, arm:%d ",control_msg[i].Activation,i);
						pub_ctrl_cmnd.publish(control_msg[i]);
						robot_send_control_command_flag[i]=false;
						robot_call.control_ack_flag[i]=false;
						control_count[i]=count;
						int k=control_msg[i].Activation;
						control_goal_count[i][k]=count;
					}

				}
				else
					armStateGoalReachedCounter++;
			if (armStateGoalReachedCounter==NO_ArmState){
				robot_call.controllerFlag=false;
				//				cout<<"All the Arms state goals are reached"<<endl;
			}

			////////////////////////////////////////////////
			/////////////////////////////////////////////////

			//			rob_goal_reach_flag_counter=0;
			//			for (int i1=0;i1<NO_ArmState;i1++)
			//				if (pitt_call.rob_goal_reach_flag[i1]==false )
			//					rob_goal_reach_flag_counter++;
			//			if(rob_goal_reach_flag_counter==NO_ArmState)// it means that both arms reached their goal.
			//			{
			//				for (int i1=0;i1<NO_ArmState;i1++)
			//					pitt_call.rob_goal_reach_flag[i1]=true;
			//				pathPointsFlag[pathPointNumber]=true;	//! if the specified point in path is reached its flag become true from false
			//				pathCheck=true;
			//			}


			//			//!			Take care a path is reached, and if all the paths are reached.
			//						pathDoneCounterArms=0;
			//						No_activeArmState=0;
			//						for (int i=0;i<NO_ArmState;i++){
			//			//				pathNO_ArmState=pathNO_ArmStateVector[i];
			//							pathPointsFlag=pathPointsFlagVector[i];
			//							pathSize=pathSizeVector[i];
			//							if (pathSize>0){
			//								No_activeArmState++;
			//								if (pathPointsFlag[pathSize-1]==true){
			//									cout<<FGRN("Path Is Reached: ")<< pathNO_ArmState<<endl;
			//									cout<<7<<pathSize-1<<endl;
			//									pathDoneCounterArms++;
			//			//						controllerFlag=false;	//! the assigned path is reached.
			//			//						exit(1);
			//								}
			//						}
			//							if (pathDoneCounterArms==No_activeArmState){// all pathes are reached
			//								cout<<FGRN("All Paths Are Reached")<<endl;
			//								robot_call.controllerFlag=false;	//! the assigned path is reached.
			//			//					exit(1);
			//							}
			//						}




			//			for (int i=0;i<NO_ArmState;i++){
			////				pathNO_ArmState=pathNO_ArmStateVector[i];
			//				vector<bool> &pathPointsFlagRef=pathPointsFlagVector[i];
			//				if (robot_call.rob_goal_reach_flag[i]==false){
			//					cout<<5<<pathNO_ArmState<<endl;
			//					robot_call.rob_goal_reach_flag[i]=true;
			//					pathCheck[i]=true;
			//					pathPointsFlagRef[pathPointNumber[i]]=true;// check here if it changes values in vector or not.
			//				}
			//			}


			// send the commands to controller when it reach the "goal reached flag"

			/////////////////////////////////////////////////////////////////////


			//				for (int NO_ArmState_counter=0;NO_ArmState_counter<pathVector.size();NO_ArmState_counter++)
			//				{
			//					vect2 Path= pathVector[NO_ArmState_counter];
			//					vector<bool> &pathPointsFlagRef=pathPointsFlagVector[NO_ArmState_counter];
			//					pathNO_ArmState=pathNO_ArmStateVector[NO_ArmState_counter];
			//					pathSize=pathSizeVector[NO_ArmState_counter];

			//					pathPointsFlagCounter=0;
			//					while(pathPointsFlagRef[pathPointsFlagCounter]==true && pathPointsFlagCounter<pathSize-1 && pathCheck[pathNO_ArmState]==true){
			////						"pathSize-1" because we increase after entering the loop.
			//						pathPointsFlagCounter++;
			//						cout<<"1111111111111"<<endl;
			//					for (int i=0;i<pathSize;i++)
			//						cout<<"Path[]["<<i<<"]: "<<Path[0][i]<<" "<<Path[1][i]<<" "<<Path[2][i]<<" "<<Path[3][i]<<" "<<Path[4][i]<<" "<<Path[5][i]<<endl;	//y
			//					cout<<3<<": pathNO_ArmState: "<<pathNO_ArmState<<" pathVector.size():"<<pathVector.size()<<" pathSize:"<<pathSize<<endl;
			//						if (pathPointsFlagRef[pathPointsFlagCounter]==false ){ // until this point in the array is true, and this one is false
			//							control_command_flag[pathNO_ArmState]=false;
			//							ss_ctrl_cmnd.str(std::string());
			//							control_msg[pathNO_ArmState].Activation[0]=0;control_msg[pathNO_ArmState].Activation[1]=0;
			//							control_msg[pathNO_ArmState].Activation[2]=0;control_msg[pathNO_ArmState].Activation[3]=0;
			//							control_msg[pathNO_ArmState].Activation[4]=0;control_msg[pathNO_ArmState].Activation[5]=0;

			//							if (pathNO_ArmState==0){
			//							ss_ctrl_cmnd<<"cartPos left";
			//							for (int j=0; j< 6; j++){
			////								ss_ctrl_cmnd<<" "<<path[j][pathPointsFlagCounter];
			//								cout<<Path[pathPointsFlagCounter][j]<<endl;
			//							}
			//							if (pathNO_ArmState==0||pathNO_ArmState==1){
			////							msg_ctrl_cmnd[pathNO_ArmState].data=ss_ctrl_cmnd.str();
			//							control_msg[pathNO_ArmState].Activation[0]=1;
			//							control_msg[pathNO_ArmState].oneArm.armIndex=pathNO_ArmState;
			//							control_msg[pathNO_ArmState].oneArm.cartGoal.cartesianPosition[0]=Path[pathPointsFlagCounter][0];
			//							control_msg[pathNO_ArmState].oneArm.cartGoal.cartesianPosition[1]=Path[pathPointsFlagCounter][1];
			//							control_msg[pathNO_ArmState].oneArm.cartGoal.cartesianPosition[2]=Path[pathPointsFlagCounter][2];
			//							control_msg[pathNO_ArmState].oneArm.cartGoal.cartesianPosition[3]=Path[pathPointsFlagCounter][3];
			//							control_msg[pathNO_ArmState].oneArm.cartGoal.cartesianPosition[4]=Path[pathPointsFlagCounter][4];
			//							control_msg[pathNO_ArmState].oneArm.cartGoal.cartesianPosition[5]=Path[pathPointsFlagCounter][5];
			//							}

			//							if (pathNO_ArmState==2){
			//								control_msg[pathNO_ArmState].Activation[1]=1;
			//								control_msg[pathNO_ArmState].bimanualArm.arm1=0;
			//								control_msg[pathNO_ArmState].bimanualArm.arm2=1;
			//								control_msg[pathNO_ArmState].bimanualArm.tTo1.cartesianPosition[0]=robot_call.tTo1_arr[0];
			//								control_msg[pathNO_ArmState].bimanualArm.tTo1.cartesianPosition[1]=robot_call.tTo1_arr[1];
			//								control_msg[pathNO_ArmState].bimanualArm.tTo1.cartesianPosition[2]=robot_call.tTo1_arr[2];
			//								control_msg[pathNO_ArmState].bimanualArm.tTo1.cartesianPosition[3]=robot_call.tTo1_arr[3];
			//								control_msg[pathNO_ArmState].bimanualArm.tTo1.cartesianPosition[4]=robot_call.tTo1_arr[4];
			//								control_msg[pathNO_ArmState].bimanualArm.tTo1.cartesianPosition[5]=robot_call.tTo1_arr[5];
			//
			//								control_msg[pathNO_ArmState].bimanualArm.tTo2.cartesianPosition[0]=robot_call.tTo2_arr[0];
			//								control_msg[pathNO_ArmState].bimanualArm.tTo2.cartesianPosition[1]=robot_call.tTo2_arr[1];
			//								control_msg[pathNO_ArmState].bimanualArm.tTo2.cartesianPosition[2]=robot_call.tTo2_arr[2];
			//								control_msg[pathNO_ArmState].bimanualArm.tTo2.cartesianPosition[3]=robot_call.tTo2_arr[3];
			//								control_msg[pathNO_ArmState].bimanualArm.tTo2.cartesianPosition[4]=robot_call.tTo2_arr[4];
			//								control_msg[pathNO_ArmState].bimanualArm.tTo2.cartesianPosition[5]=robot_call.tTo2_arr[5];
			//
			//								control_msg[pathNO_ArmState].bimanualArm.wTg.cartesianPosition[0]=Path[pathPointsFlagCounter][0];
			//								control_msg[pathNO_ArmState].bimanualArm.wTg.cartesianPosition[1]=Path[pathPointsFlagCounter][1];
			//								control_msg[pathNO_ArmState].bimanualArm.wTg.cartesianPosition[2]=Path[pathPointsFlagCounter][2];
			//								control_msg[pathNO_ArmState].bimanualArm.wTg.cartesianPosition[3]=Path[pathPointsFlagCounter][3];
			//								control_msg[pathNO_ArmState].bimanualArm.wTg.cartesianPosition[4]=Path[pathPointsFlagCounter][4];
			//								control_msg[pathNO_ArmState].bimanualArm.wTg.cartesianPosition[5]=Path[pathPointsFlagCounter][5];
			//							}

			//							pathCheck[pathNO_ArmState]=false;
			////							pitt_call.rob_goal_reach_flag[pathNO_ArmState]=false;
			//							pathPointNumber[pathNO_ArmState]=pathPointsFlagCounter;
			//						}
			//					}
			//				}


			//			rob_goal_reach_flag_counter=0;
			//			for (int i1=0;i1<NO_ArmState;i1++)
			//				if (pitt_call.rob_goal_reach_flag[i1]==false )
			//					rob_goal_reach_flag_counter++;
			//			if(rob_goal_reach_flag_counter==NO_ArmState)// it means that both arms reached their goal.
			//			{
			//				for (int i1=0;i1<NO_ArmState;i1++)
			//					pitt_call.rob_goal_reach_flag[i1]=true;
			//				pathPointsFlag[pathPointNumber]=true;	//! if the specified point in path is reached its flag become true from false
			//				pathCheck=true;
			//			}


			////!			Take care a path is reached, and if all the paths are reached.
			//			pathDoneCounterArms=0;
			//			No_activeArmState=0;
			//			for (int i=0;i<NO_ArmState;i++){
			////				pathNO_ArmState=pathNO_ArmStateVector[i];
			//				pathPointsFlag=pathPointsFlagVector[i];
			//				pathSize=pathSizeVector[i];
			//				if (pathSize>0){
			//					No_activeArmState++;
			//					if (pathPointsFlag[pathSize-1]==true){
			//						cout<<FGRN("Path Is Reached: ")<< pathNO_ArmState<<endl;
			//						cout<<7<<pathSize-1<<endl;
			//						pathDoneCounterArms++;
			////						controllerFlag=false;	//! the assigned path is reached.
			////						exit(1);
			//					}
			//			}
			//				if (pathDoneCounterArms==No_activeArmState){// all pathes are reached
			//					cout<<FGRN("All Paths Are Reached")<<endl;
			//					robot_call.controllerFlag=false;	//! the assigned path is reached.
			////					exit(1);
			//				}
			//			}


			////			Take care of flags related to controller.
			//			for (int i1=0;i1<NO_ArmState;i1++)
			//			{
			////				if it does not receive from the control interface after "max_time_rob_reach_goal*ros_freq" seconds from the received ack
			////				here it change the flags to send the commands to controller again
			////				if (count>=control_goal_count[i1]+(max_time_rob_reach_goal*ros_freq)
			////						&& robot_call.hri_control_goal_flag[i1]==false)// && obj_nodeAction.actionCommand[i1]!="0")
			////				{
			////					cout<<9<<endl;
			////					control_command_flag[i1]=false;
			////					robot_call.hri_control_goal_flag[i1]=true;
			////				}
			////					if control interface does not send an acknowledge that says it receives
			////					control command after 10 loops it sends again the last control command to it.
			//					if (count>=control_count[i1]+10 &&robot_call.control_ack_flag[i1]==false){
			//						control_command_flag[i1]=false;//*** make this flag false in real test
			//						cout<<10<<endl;					}
			//			}
			//	control msg publish here
			//			for (int i1=0;i1<NO_ArmState;i1++)
			//				if ( robot_send_control_command_flag[i1]==true)
			//				{
			//					cout<<11<<": "<<i1<<endl;
			//					ROS_INFO("Robot Interface:: publish Control Command");
			//					pub_ctrl_cmnd.publish(control_msg[i1]);
			//					robot_send_control_command_flag[i1]=false;
			//					robot_call.control_ack_flag[i1]=false;
			//					control_count[i1]=count;
			//					int j1=control_msg[i1].Activation;
			//					control_goal_count[i1][j1]=count;
			//				}
			//			if ( control_error_flag==false ) //DEL
			//			{
			//				//ROS_INFO("I publish Control error: %s",msg_ctrl_err.data.c_str());
			//				pub_ctrl_error.publish(msg_ctrl_err);//DEL
			//				control_error_flag=true;//DEL
			//			}

		}

		*/


		robot_call.FailureCheck();


		if(count==0)
			{usleep(0.5e6);}

		loop_rate.sleep();
		ros::spinOnce();
		count++;
	}
	return 0;
}

void wayPointOrientation(const int pathsize, float * initState, float* goalOrientation,vect2 & path){

	for(int i=0;i<pathsize;i++){
		path[i][0]=initState[0]+	1.0*(goalOrientation[0]-initState[0])*( double(i)/double(pathsize-1) );	// yaw
		path[i][1]=initState[1]+	1.0*(goalOrientation[1]-initState[1])*( double(i)/double(pathsize-1) );	// pitch
		path[i][2]=initState[2]+	1.0*(goalOrientation[2]-initState[2])*( double(i)/double(pathsize-1) );	// roll
	}

};

int pathsVector2PosePoint(int pathNO_ArmState,vect2  pathVector,  vector<bool>  &pathPointsFlagRef, int pathSize, double * wTo,
		controlCommnad_msgs::control &control_msg,bool &robot_send_control_command_flag){
	/*!
	 *  In this function we get as input for each arm state (L,R, BiMan) and paths vector and their flags,
		as an output will be just a vector of six floats (YPR XYZ)

	 */
	cout<<"*****Function****"<<endl;
	cout<<"pathSize: "<<pathSize<<endl;
	cout<<"pathNO_ArmState: "<<pathNO_ArmState<<endl;
	for (int i =0;i<pathPointsFlagRef.size();i++)
		cout<<pathPointsFlagRef[i]<<endl;




	int pathPointsFlagCounter=0;
	switch (pathNO_ArmState){

	case 0:
	case 1:
		cout<<777777777<<endl;
		if (pathSize==0){
			cout<<"pathSize is zero"<<endl;
			robot_send_control_command_flag=false;
		}
		else if (pathSize==1 ){
			if (pathPointsFlagRef[0]==false){
				control_msg.Activation=0;
				control_msg.oneArm.armCmndType="cartPos";
				control_msg.oneArm.armIndex=pathNO_ArmState;
				control_msg.oneArm.cartGoal.cartesianPosition[0]=pathVector[0][0];
				control_msg.oneArm.cartGoal.cartesianPosition[1]=pathVector[0][1];
				control_msg.oneArm.cartGoal.cartesianPosition[2]=pathVector[0][2];
				control_msg.oneArm.cartGoal.cartesianPosition[3]=pathVector[0][3];
				control_msg.oneArm.cartGoal.cartesianPosition[4]=pathVector[0][4];
				control_msg.oneArm.cartGoal.cartesianPosition[5]=pathVector[0][5];
				robot_send_control_command_flag=true;
				pathPointsFlagCounter=0;
				cout<<"func: 1111111111"<<endl;
			}
			else{
				cout<<"the path is solved before!";
				robot_send_control_command_flag=false;
			}
		}
		else{
			cout<<8888888888<<endl;
			for (int i=0;i<pathSize-1;i++){
				if (pathPointsFlagRef[i]==true && pathPointsFlagRef[i+1]==false ){
					control_msg.Activation=0;
					control_msg.oneArm.armCmndType="cartPos";
					control_msg.oneArm.armIndex=pathNO_ArmState;
					control_msg.oneArm.cartGoal.cartesianPosition[0]=pathVector[i+1][0];
					control_msg.oneArm.cartGoal.cartesianPosition[1]=pathVector[i+1][1];
					control_msg.oneArm.cartGoal.cartesianPosition[2]=pathVector[i+1][2];
					control_msg.oneArm.cartGoal.cartesianPosition[3]=pathVector[i+1][3];
					control_msg.oneArm.cartGoal.cartesianPosition[4]=pathVector[i+1][4];
					control_msg.oneArm.cartGoal.cartesianPosition[5]=pathVector[i+1][5];
					robot_send_control_command_flag=true;
					pathPointsFlagCounter=i+1;
					cout<<"function"<<i<<endl;
					cout<<"function: pathPointsFlagCounter: "<<pathPointsFlagCounter<<endl;
					cout<<"function: robot_send_control_command_flag: "<<robot_send_control_command_flag<<endl;

				}
				else if (pathPointsFlagRef[pathSize-1]==true){
					cout<<"the path is solved before!";
					robot_send_control_command_flag=false;
				}
				else
					cout<<"000000000000000000"<<endl;
			}
		}

		break;
	case 2:
		if (pathSize==0){
			cout<<"pathSize is zero"<<endl;
			robot_send_control_command_flag=false;
		}
		else if (pathSize==1 )
			if (pathPointsFlagRef[0]==false){
				control_msg.Activation=1;
				control_msg.bimanualArm.arm1=0;
				control_msg.bimanualArm.arm2=1;
				control_msg.bimanualArm.wTo.cartesianPosition[0]=wTo[0];
				control_msg.bimanualArm.wTo.cartesianPosition[1]=wTo[1];
				control_msg.bimanualArm.wTo.cartesianPosition[2]=wTo[2];
				control_msg.bimanualArm.wTo.cartesianPosition[3]=wTo[3];
				control_msg.bimanualArm.wTo.cartesianPosition[4]=wTo[4];
				control_msg.bimanualArm.wTo.cartesianPosition[5]=wTo[5];
				control_msg.bimanualArm.wTg.cartesianPosition[0]=pathVector[0][0];
				control_msg.bimanualArm.wTg.cartesianPosition[1]=pathVector[0][1];
				control_msg.bimanualArm.wTg.cartesianPosition[2]=pathVector[0][2];
				control_msg.bimanualArm.wTg.cartesianPosition[3]=pathVector[0][3];
				control_msg.bimanualArm.wTg.cartesianPosition[4]=pathVector[0][4];
				control_msg.bimanualArm.wTg.cartesianPosition[5]=pathVector[0][5];
				pathPointsFlagCounter=0;
				robot_send_control_command_flag=true;
			}
			else{
				cout<<"the path is solved before!";
				robot_send_control_command_flag=false;
			}
		else
			for (int i=0;i<pathSize-1;i++)
				if (pathPointsFlagRef[i]==true && pathPointsFlagRef[i+1]==false ){
					control_msg.Activation=1;
					control_msg.bimanualArm.arm1=0;
					control_msg.bimanualArm.arm2=1;
					control_msg.bimanualArm.wTo.cartesianPosition[0]=wTo[0];
					control_msg.bimanualArm.wTo.cartesianPosition[1]=wTo[1];
					control_msg.bimanualArm.wTo.cartesianPosition[2]=wTo[2];
					control_msg.bimanualArm.wTo.cartesianPosition[3]=wTo[3];
					control_msg.bimanualArm.wTo.cartesianPosition[4]=wTo[4];
					control_msg.bimanualArm.wTo.cartesianPosition[5]=wTo[5];
					control_msg.bimanualArm.wTg.cartesianPosition[0]=pathVector[i+1][0];
					control_msg.bimanualArm.wTg.cartesianPosition[1]=pathVector[i+1][1];
					control_msg.bimanualArm.wTg.cartesianPosition[2]=pathVector[i+1][2];
					control_msg.bimanualArm.wTg.cartesianPosition[3]=pathVector[i+1][3];
					control_msg.bimanualArm.wTg.cartesianPosition[4]=pathVector[i+1][4];
					control_msg.bimanualArm.wTg.cartesianPosition[5]=pathVector[i+1][5];
					pathPointsFlagCounter=i+1;
					robot_send_control_command_flag=true;
				}
				else if (pathPointsFlagRef[pathSize-1]==true){
					cout<<"the path is solved before!";
					robot_send_control_command_flag=false;
				}
		break;
	}

	cout<<"function2: "<<pathNO_ArmState<<endl;
	cout<<"function2: pathPointsFlagCounter: "<<pathPointsFlagCounter<<endl;
	cout<<"function2: robot_send_control_command_flag: "<<robot_send_control_command_flag<<endl;


	return pathPointsFlagCounter;
};
