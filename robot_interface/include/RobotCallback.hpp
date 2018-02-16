
#include <iostream>
#include <stdio.h>
#include <ros/ros.h>
#include <std_msgs/Char.h>
#include <std_msgs/String.h>
#include <string>
#include <algorithm>
#include <iterator>
#include <sstream>
#include <vector>
#include <map>
#include <math.h>
//#include <cmat/cmat.h>
#include <geometry_msgs/Accel.h>
#include <boost/shared_ptr.hpp>
#include <boost/algorithm/string.hpp>
#include <std_msgs/Float64MultiArray.h>
#include <chrono>

#include "pitt_msgs/TrackedShape.h"
#include "pitt_msgs/TrackedShapes.h"
#include "pitt_msgs/ClustersOutput.h"
#include "controlCommnad_msgs/controlTaskParam.h"
#include "controlCommnad_msgs/transformation.h"
#include "controlCommnad_msgs/controlGoalReachAck.h"
#include "controlCommnad_msgs/control.h"
#include "Objects.hpp"
#include "knowledge_msgs/knowledgeSRV.h"
// from robot interface to the simulation module
#include <simRobot_msgs/simulateRobotSRV.h>
#include <simRobot_msgs/sim_robot.h>
#include <simRobot_msgs/transformation.h>
// from the planner to the robot interface
#include "robot_interface_msgs/Joints.h"
#include "robot_interface_msgs/SimulationRequestMsg.h"
#include "robot_interface_msgs/SimulationResponseMsg.h"

#define RST  "\x1B[0m"
#define KBLU  "\x1B[34m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define FBLU(x) KBLU x RST
#define FRED(x) KRED x RST
#define FGRN(x) KGRN x RST
#define BOLD(x) "\x1B[1m" x RST

using namespace std;
using namespace std::chrono;

typedef ::pitt_msgs::TrackedShapes_<std::allocator<void> > TrackedShapes;
typedef ::pitt_msgs::TrackedShape_<std::allocator<void> > TrackedShape;


using namespace pitt_msgs;

enum Ecmnd{e_grasp, e_unGrasp, e_pointReach, e_stop, e_holdOn, e_holdOff, e_obstacleAvoidanceTask, e_objectsRecognition, e_NotResolved};//! enumerated commands
enum ErobotState{singleLeft=0,singleRight=1,BiMan=2};

class agents_tasks{
public:
	vector<string> agents; 			//! the different agents combination for performing actions like: leftArm, rightArm, leftArm+rightArm.
	int agentsNumber; // Left: 0, right=1, left+right=2
	vector<string> collaborators;
	string lastAssignedAction;
	bool isActionSuccessfullyDone;
	bool isBusy;
	microseconds microSec_StartingTime;
	bool emergencyFlag; // stop robot emergency flag

	agents_tasks(){
//		agents=NULL;
//		collaborators=NULL;
		lastAssignedAction="";
		isActionSuccessfullyDone=false;
		isBusy=false;
		agentsNumber=0;
		microSec_StartingTime=duration_cast< microseconds >(system_clock::now().time_since_epoch());
		emergencyFlag=false;
	};
	~agents_tasks(){};

	void Print(void){
		cout<<"Agents: ";
		for(int i=0;i<agents.size();i++){
			cout<<agents[i]<<" ";
		}
		cout<<endl;

		cout<<"collaborators: ";
		for(int i=0;i<collaborators.size();i++){
			cout<<collaborators[i]<<" ";
		}
		cout<<endl;

		cout<<"Last Assigned Action: "<<lastAssignedAction<<endl;
		cout<<"Is Action successfully done? "<< isActionSuccessfullyDone<<endl;
		cout<<"Is Agent busy? "<<isBusy<<endl;
		cout<<"emergency Flag: "<<emergencyFlag<<endl;
		cout<<"Agent Number: "<<agentsNumber<<endl;
		cout<<"Last action command time: "<<microSec_StartingTime.count()<<endl;

	};
};




class robotCallback {
	public:
//		vector<pittObjects::Objects *> objectsVector2;

		vector<agents_tasks> agents_list;

//		bool ransacFlag;			//! if this flag is true we should check environment and get information from it;
		bool pathPlanningFlag;	//! if it is true we should do path planning
		bool NopathPlanningFlag;	//! path planning is not necessary.
		bool simulationFlag;
		bool controllerFlag;		//! if it is true we should do path planning
		bool directPathAllocationFalg; //! directly read path points from a file or memory and write to path vectors, to be reached by the controller.

		vector<shared_ptr<pittObjects::Objects>> objectsVector;

		int NoObjects;					//! No of Objects in working space
		int NoSphere,	EstimatedNoSphere;
		int NoCylinder,	EstimatedNoCylinder;
		int NoPlane,	EstimatedNoPlane;
		int NoCone,		EstimatedNoCone;
		int NoUnknown,	EstimatedNoUnknown;

		int NoObstacles;

		int NoCorrectRecognizedObject;
		float** objectFeatureBox;			//! Objects Features
		float** objectFeatureBall;			//! Objects Features

		float obstacleSafetyFactor;
		float obstacleSafetyFactorX;
		float obstacleSafetyFactorY;
		float obstacleSafetyFactorZ;
		float regionOperating[6]; 		//! first 3:center, second 3: size
		float perception_regionOperating[6]; 		//! first 3:center, second 3: size

		float regionGoal[3][6];			//!left,right, bimanual. first 3:center, second 3: size
		int   goalID;
		float orientationGoal[3][3];	//! left, right, bimanual. the orientation of the goal of the E.E.
		float initPosLeftArm[6];		//! the initial position of the left arm in Cartesian space  [roll pitch yaw x y z]
		float initPosRightArm[6];		//! the initial position of the right arm in Cartesian space [roll pitch yaw x y z]
		float initPos[3][6];			//! 0: left, 1: right, 2: bimanual.
		float init_q_[2][7];					//! 0: left, 1: right joint values.

										//! Initial position of the arms in Cartesian space [roll pitch yaw x y z]
		double tTo1_arr[6], tTo2_arr[6];
		int NO_ArmState;
		int NO_ctrlCmdType;


		Ecmnd robotCommandType;
		bool armsStateFlag_NotIdle[3]; // 3: Arm_states (left,right, BiMan), if true -> the arm state is not resolved
		bool armstateFlag_doAction[3]; // 3: Arm_states (left,right, BiMan), if true ->send a command to controller; if false -> do not send command to controller.

		bool hri_control_goal_flag [3];	// 2 arms, flag is false when correct goal is received by controller and the robot is not reached the goal
		bool rob_goal_reach_flag[3];	//2 arms, flag is false when robot reaches goal, otherwise is true, then we check for the next action to be done
//		bool rob_stop_reach_flag[3];
		bool control_ack_flag[3];		// when we publish a control cmnd msg until receive a response this will be false.

		controlCommnad_msgs::controlTaskParam ControlTaskParameters;
		bool control_goal_reach_ack[3][5];//  when controller reaches goal it is true, otherwise false: [L,R, BiMan][oneArm, bimanualArm, Gripper, stopArm, holdModeArm]: acknowledge from controller
		bool controlCmndSent_goalNotReached[3][5];//  when send command to controller and before controller reached the goal it is true, otherwise false: [L,R, BiMan][oneArm, bimanualArm, Gripper, stopArm, holdModeArm]: acknowledge from controller
//		controlCommnad_msgs::transformation ControlTaskParametersTrans;
		bool readArmPathFlag[3]; //! if true read the path(or point) from a provided list of points for the corresponding arm, otherwise false;
		int pointReachNumber; // the number of point robot should reach when the cmnd from higher level arrive
		float ReachingPoint[3][6] ; //the point to reach by controller, single arm,BiManual
		double wTo_BiMan[6] ; //object frame for biManual control of the robot

		bool obj_call_back_flag;
		float ReachingPointsVector[13][6]; // YPR XYZ


		robotCallback(int nosphere,int nocylinder,int noplane,int nocone,int nounknown);
		~robotCallback(void);
//		void CallBackShapes(const TrackedShapes& outShapes);
//		void CallBackClusters(const ClustersOutputConstPtr& clusterObj);
		void CallBackArmwTtLeft(const geometry_msgs::Accel& msg);
		void CallBackArmwTtRight(const geometry_msgs::Accel& msg);
		void CallBackArm_Q_Left (const std_msgs::Float64MultiArray& msg);
		void CallBackArm_Q_Right(const std_msgs::Float64MultiArray& msg);

		void pathInitialPoint(const int arm_state_no);	//! take care of different types of planning (left,right, bimanual)
		void boundBoxSphere( const TrackedShape&, const int);
		void boundBoxCylinder( const TrackedShape&, const int);
		void boundBallSphere( const TrackedShape&, const int);
		void boundBallCylinder( const TrackedShape&, const int);
		void goalCenterSet(const TrackedShape&, const int);
		void planeFrameSet(const TrackedShape&, const int);
		void biManualControlCmndParameters(void);
		void publishControlTasksParam(void);

		void arrivingCommands(const std_msgs::String::ConstPtr& msg); //! this method take care of arriving command from higher level (our case: hri pkg)
		void PublishRobotAck(agents_tasks& agent);//! publish robot commands are reached to the hri

		void SubscribeControlAck(const controlCommnad_msgs::controlGoalReachAck& msg);

		void SetAgentsList(void);

		void SendGraspingCommand(agents_tasks& agent);
		void SendHoldingCommand(agents_tasks& agent);
		void SendStoppingCommand(agents_tasks& agent);
		void SendApproachingCommand(agents_tasks& agent);
		void SendApproachingCommandSingleArm(agents_tasks& agent);
		void SendTransportingCommand(agents_tasks& agent);
		void SendTransportingCommandSingleArms(agents_tasks& agent);
		void SendTransportingCommandJointArms(agents_tasks& agent);
		void SendRestingCommand(agents_tasks& agent);
		void SendScrewingCommand(agents_tasks& agent);
		void SendUnscrewingCommand(agents_tasks& agent);
		void SendKB_ReductionCommand(agents_tasks& agent);


		//! Action Simulation Functions
		void arrivingSimulationCommand(const robot_interface_msgs::SimulationRequestMsg& msg);
		void SimulateGraspingCommand(const robot_interface_msgs::SimulationRequestMsg& msg);
		void SimulateUpdateJointValues(const robot_interface_msgs::SimulationRequestMsg& msg);
		void SimulateUpdateKnowledgeBase(const robot_interface_msgs::SimulationRequestMsg& msg);
		void SimulateHoldingCommand(const robot_interface_msgs::SimulationRequestMsg& msg);
		void SimulateStoppingCommand(const robot_interface_msgs::SimulationRequestMsg& msg);
		void SimulateApproachingCommand(const robot_interface_msgs::SimulationRequestMsg& msg);
		void SimulateApproachingCommandSingleArm(const robot_interface_msgs::SimulationRequestMsg& msg);
		void SimulateApproachingCommandJointArms(const robot_interface_msgs::SimulationRequestMsg& msg);
		void SimulateTransportingCommand(const robot_interface_msgs::SimulationRequestMsg& msg);
		void SimulateTransportingCommandSingleArm(const robot_interface_msgs::SimulationRequestMsg& msg);
		void SimulateTransportingCommandJointArms(const robot_interface_msgs::SimulationRequestMsg& msg);
		void SimulateRestingcommand(const robot_interface_msgs::SimulationRequestMsg& msg);
		void SimulateScrewingCommand(const robot_interface_msgs::SimulationRequestMsg& msg);
		void SimulateUnscrewingCommand(const robot_interface_msgs::SimulationRequestMsg& msg);
		void SimulateKB_ReductionCommand(const robot_interface_msgs::SimulationRequestMsg& msg);


		void SimulateServiceApproachSingleArm(int armIndex, vector<float> initialJointPose,vector<float> goalPose, bool &simulationResult, double &actionTime, vector<float> &finalJointPose );
		void SimulateServiceTransportSingleArms(int armIndex, vector<float> initialJointPose, vector<float> wTo ,vector<float> wTg, bool &simulationResult, double &actionTime, vector<float> &finalJointPose );
		void SimulateServiceTransportJointArms(vector<int> armIndex, vector<vector<float>> initialJointPose, vector<float> wTo ,vector<float> wTg, bool &simulationResult, double &actionTime, vector<vector<float>> &finalJointPose );
		void StopRobotEmergency(agents_tasks& agent);

		void FailureCheck(void);


		controlCommnad_msgs::control control_msg;
		int num_joint;


	private:
		ros::NodeHandle nh;
//		ros::Subscriber sub_shapes;

		ros::Subscriber sub_LeftArmwTt;
		ros::Subscriber sub_RightArmwTt;
		ros::Subscriber sub_LeftArmQ;
		ros::Subscriber sub_RightArmQ;

		ros::Subscriber sub_robotCtrlAck;
		ros::Subscriber sub_CtrlOut;

		ros::Subscriber sub_arrivingCmnd; //! command from higher level to manage
		ros::Subscriber sub_arrivingSimulationCommand;

		ros::Publisher pub_ctrl_task_param;
		ros::Publisher pub_hri_robot_ack;
		ros::Publisher pub_simulationResponse;
		ros::Publisher pub_RobotJointValues;


		ros::ServiceClient knowledgeBase_client;
		ros::ServiceClient simRobot_client;

		ros::Publisher publishControlCommand;
		ros::Publisher publishKBCommand;


		microseconds microSec_time;
		double waiting_time;// seconds


//		 TrackedShape::Ptr outShape;


};
