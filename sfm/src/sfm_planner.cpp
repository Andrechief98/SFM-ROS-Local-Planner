#include "sfm_planner.h"
#include <pluginlib/class_list_macros.h>
#include <nav_msgs/Odometry.h>
#include <vector>
#include <cmath>
#include <boost/thread.hpp>
#include <iostream>
#include <tf2/buffer_core.h>
#include <gazebo_msgs/ModelStates.h>
#include <gazebo_msgs/GetModelState.h>
#include <gazebo_msgs/SetModelState.h>


#include "functions.h"


PLUGINLIB_EXPORT_CLASS(sfm_planner::SfmPlanner, nav_core::BaseLocalPlanner)


//***********FUNZIONI UTILI PER ESEGUIRE VARIE TASK PRELIMINARI PER IL CALCOLO DELLE FORZE (direttamente all'interno del file sfm.cppp):*************

//CALCOLO DISTANZA (norma del vettore differenza tra due vettori, vec1 e vec2)
double vect_norm2(std::vector<double> vec1, std::vector<double> vec2){
        double norma;
        std::vector<double> difference={0,0};
        
        for (int i=0; i<2; i++){
            difference[i]=vec1[i]-vec2[i];
        }
        
        norma = sqrt(difference[0]*difference[0]+difference[1]*difference[1]);
        return norma;
    }

//CALCOLO NORMA DI UN VETTORE
double vect_norm1(std::vector<double> vec1){
        double norma=0;
        norma = sqrt(vec1[0]*vec1[0]+vec1[1]*vec1[1]);
        return norma;
    }

//STABILISCE IL VERSORE DIREZIONE RIVOLTO DA vec1 A vec2
std::vector<double> compute_direction(std::vector<double> vec1, std::vector<double> vec2){
        
        std::vector<double> dir={0,0};
        double norm = vect_norm2(vec1, vec2);

        if (norm<0.11) norm=0.1; //se tende a zero allora la forza diventa troppo grande e può causare problemi

        for(int i=0; i<2; i++){
            dir[i]=(vec1[i]-vec2[i])/norm;
        }

        return dir;
    }
    
//CALCOLA IL COSENO DELL'ANGOLO TRA I DUE VETTORI
double compute_cos_gamma(std::vector<double> vec1, std::vector<double> vec2){
    double coseno=0;
    for(int i=0; i<vec1.size(); i++){
        vec2[i]=-vec2[i];
    }

    coseno=(vec1[0]*vec2[0]+vec1[1]*vec2[1])/(vect_norm1(vec1)*vect_norm1(vec2));
    return coseno;
}

//FUNZIONE SEGNO:
int sign(double expression){
    if (expression<1){
        return -1;
    }
    else if(expression>1){
        return 1;
    }
    else{
        return 0;
    }
}


// IMPLEMENTAZIONE EFFETTIVA PLUGIN

void odom_callback(const nav_msgs::Odometry::ConstPtr& msg)
{
  robot_pose_ = *msg;
  //ROS_INFO("Odometria ricevuta!");
}

void people_callback(const gazebo_msgs::ModelStates msg_people)
{
  people_ = msg_people;

  std::cout << "*********MESSAGGI PEOPLE RICEVUTI ****************" << std::endl;
  std::cout << people_.name[2] << std::endl;
}

namespace sfm_planner{

    SfmPlanner::SfmPlanner() : costmap_ros_(NULL), tf_(NULL), initialized_(false){}

    SfmPlanner::SfmPlanner(std::string name, tf2_ros::Buffer* tf, costmap_2d::Costmap2DROS* costmap_ros) : costmap_ros_(NULL), tf_(NULL), initialized_(false)
    {
        initialize(name, tf, costmap_ros);
    }

    SfmPlanner::~SfmPlanner() {}

    // Take note that tf::TransformListener* has been changed to tf2_ros::Buffer* in ROS Noetic
    void SfmPlanner::initialize(std::string name, tf2_ros::Buffer* tf, costmap_2d::Costmap2DROS* costmap_ros)
    {
        if(!initialized_)
        {   
            tf_ = tf;
            costmap_ros_ = costmap_ros;
            initialized_ = true;
            
            goal_reached=false; 

        }

        initialized_=true;
        ROS_INFO("inizializzazione local planner avvenuta");
    }

    void SfmPlanner::getOdometry(){
        //ODOMETRIA ROBOT:
        sub_odom = nh.subscribe<nav_msgs::Odometry>("/locobot/odom", 1, &odom_callback);

        std::cout << "\n";
        std::cout << "**ODOMETRIA ROBOT RICEVUTA: " << std::endl;
        /*
        std::cout << "  *Pose (coordinate+orientation) coordinate Frame  : " << robot_pose_.header.frame_id << std::endl; //FRAME = "map"
        std::cout << "      Coordinates (meters) : " << robot_pose_.pose.pose.position.x << " " << robot_pose_.pose.pose.position.y << std::endl;
        std::cout << "      Orientation z-axis (radians) : " << tf2::getYaw(robot_pose_.pose.pose.orientation) << std::endl;
        std::cout << "\n";
        std::cout << "  *Twist (velocity) coordinate Frame  : " << robot_pose_.child_frame_id << std::endl; //FRAME = "locobot/base_footprint"
        std::cout << "      *Linear Velocity (meters/second) : " << robot_pose_.twist.twist.linear.x << " " << robot_pose_.twist.twist.linear.y << std::endl;
        std::cout << "      *Angular Velocity z-axis (radians/second) : " << robot_pose_.twist.twist.angular.z << std::endl;
        */

       return;
    }

    void SfmPlanner::set_Position_Orientation_Info(){
        curr_robot_coordinates={robot_pose_.pose.pose.position.x, robot_pose_.pose.pose.position.y}; //non so perchè con i messaggi di odometria non accetta il push_back() per inserirli in un vettore
        curr_robot_orientation=tf2::getYaw(robot_pose_.pose.pose.orientation);

        std::cout << "\n";
        std::cout << "**VETTORI COORDINATE GOAL E COORDINATE ROBOT CALCOLATI: " << std::endl;
        std::cout << "  *Goal coordinates vector  : " << goal_coordinates[0] << " " << goal_coordinates[1] << std::endl;
        std::cout << "  *Goal orientation z-axis (radians) [-pi,pi] : " << goal_orientation << std::endl;
        std::cout << "\n";
        std::cout << "  *Robot coordinates vector  : " << curr_robot_coordinates[0] << " " << curr_robot_coordinates[1] << std::endl;
        std::cout << "  *Robot orientation z-axis (radians) [-pi,pi] : " << curr_robot_orientation << std::endl; 

        return;
    }

    void SfmPlanner::setVelocityInfo(){
        curr_robot_lin_vel={robot_pose_.twist.twist.linear.x, robot_pose_.twist.twist.linear.y}; //non so perchè con i messaggi di odometria non accetta il push_back() per inserirli in un vettore
        curr_robot_ang_vel=robot_pose_.twist.twist.angular.z;
        
        std::cout << "\n";
        std::cout << "**VETTORE VELOCITÀ ROBOT ATTUALE: " << std::endl;
        std::cout << "  *Robot velocity vector  : " << curr_robot_lin_vel[0] << " " << curr_robot_lin_vel[1] << std::endl; 

        return;
    }


    void SfmPlanner::computeAttractiveForce(){
        e=compute_direction(goal_coordinates,curr_robot_coordinates);

        for(int i=0; i<e.size(); i++){
            //calcolo forza attrattiva
            F_att[i]=(desired_vel*e[i]-curr_robot_lin_vel[i])/alfa;
        }

        return;
    }

    void SfmPlanner::getPeopleInformation(){
        sub_people = nh.subscribe<gazebo_msgs::ModelStates>("/gazebo/model_states", 1, &people_callback); //per le simulazioni su Gazebo
        std::cout << "sottoscrizione avvenuta" << std::endl;

        // //RICEZIONE DELLE INFORMAZIONI DI UN DETERMINATO MODELLO SPECIFICATO SU GAZEBO (necessario andare a fornire il nome del modello)
        // people_client = nh.serviceClient <gazebo_msgs::GetModelState>("/gazebo/get_model_state");
        // gazebo_msgs::GetModelState model_to_search;
        // model_to_search.request.model_name= (std::string) "actor1";
        
        // if(people_client.call(model_to_search))
        // {
        //     ROS_INFO("PR2's magic moving success!!");
        // }
        // else
        // {
        //     ROS_ERROR("Failed to magic move PR2! Error msg:%s",model_to_search.response.status_message.c_str());
        // }

    }

    void SfmPlanner::computeRepulsiveForce(){


    }


    void SfmPlanner::computeTotalForce(){
        for(int i=0; i<e.size(); i++){

            F_tot[i]=F_att[i]+F_rep[i];   

            if(std::fabs(F_tot[i])*delta_t>max_lin_acc_x){
                F_tot[i]=sign(F_tot[i])*max_lin_acc_x;
            }
        }
    }


    bool SfmPlanner::setPlan(const std::vector<geometry_msgs::PoseStamped>& orig_global_plan)
    {
        if(!initialized_)
        {
            ROS_ERROR("This planner has not been initialized");
            return false;
        }

        global_plan_.clear();
        global_plan_ = orig_global_plan;
        
        //quando settiamo un nuovo goal (planner frequency 0 Hz nel config file .yaml -> global planner chiamato una volta, solo all'inizio), 
        //resettiamo il flag per capire se il goal è stato raggiunto
        goal_reached=false;
        //puliamo anche il vettore di coordinate che conteneva le coordinate del goal precedente
        goal_coordinates.clear();

        //RICEZIONE GOAL MESSAGE
        int size_global_plan=global_plan_.size();
        goal_pose_=global_plan_[size_global_plan-1];

        goal_coordinates={goal_pose_.pose.position.x, goal_pose_.pose.position.y};
        goal_orientation=tf2::getYaw(goal_pose_.pose.orientation);
        
        std::cout << "COORDINATE GOAL RICEVUTE: " << std::endl;
        std::cout << "Pose Frame : " << goal_pose_.header.frame_id << std::endl; //FRAME = "map" (coincide con /locobot/odom)
        std::cout << "  Coordinates (meters) : " << goal_coordinates[0] << " " << goal_coordinates[1] << std::endl;
        std::cout << "  Orientation z-axis (radians) : " << goal_orientation << std::endl;

        
    
        return true;
    }


    bool SfmPlanner::computeVelocityCommands(geometry_msgs::Twist& cmd_vel)
    {
        if(!initialized_)
        {
            ROS_ERROR("This planner has not been initialized");
            return false;
        }


        getOdometry();
        set_Position_Orientation_Info();
        setVelocityInfo();

        computeAttractiveForce();
        getPeopleInformation();     //AGGIUNGERE RICEZIONE POSIZIONI OGGETTI PERSONE
        computeRepulsiveForce();
        computeTotalForce();
        
        std::cout << "\n";
        std::cout << "**FORZA RISULTANTE CALCOLATA: " << std::endl;
        std::cout << "  *Total force vector  : " << F_tot[0] << " " << F_tot[1] << std::endl; 


        //CALCOLO NUOVA VELOCITA'
        new_robot_lin_vel={0,0};
        new_robot_pos={0,0};
        

        //VERIFICA RAGGIUNGIMENTO GOAL 
        
        if(vect_norm2(goal_coordinates,curr_robot_coordinates)<=distance_tolerance){
            std::cout << " ------------- Distanza raggiunta ----------------" << std::endl;
            //coordinate raggiunte. Vettore velocità lineare rimane nullo.
            new_robot_lin_vel={0,0};
            

           
            if(std::fabs(angles::shortest_angular_distance(curr_robot_orientation,goal_orientation))<=angle_tolerance){
                //anche l'orientazione del goal è stata raggiunta
                new_robot_ang_vel_z=0;
                goal_reached=true;
                std::cout<<"Orientazione goal raggiunta"<<std::endl;
                std::cout<<"GOAL RAGGIUNTO"<<std::endl;
            }
            else{
                // Se le coordinate del goal sono state raggiunte ma l'orientazione finale no, la velocità angolare deve 
                // essere calcolata per far ruotare il robot nella posa finale indicata
                std::cout << "Orientazione non raggiunta" << std::endl;
                new_robot_ang_vel_z=K_p*(angles::shortest_angular_distance(curr_robot_orientation,goal_orientation));
                }
            
        }
        else{
            std::cout << "------------- Distanza non raggiunta ----------------" << std::endl;

            for(int i=0; i<new_robot_lin_vel.size(); i++){
                new_robot_lin_vel[i]=curr_robot_lin_vel[i]+delta_t*F_tot[i];
                if(std::fabs(new_robot_lin_vel[i])>0.5){
                    new_robot_lin_vel[i]=sign(new_robot_lin_vel[i])*0.5;
                }

                new_robot_pos[i]=curr_robot_coordinates[i]+delta_t*new_robot_lin_vel[i]; //adoperiamo la velocità del modello (calcolata dalle forze) anzichè quella effettiva del robot
            }

            beta=std::atan2(new_robot_pos[1]-curr_robot_coordinates[1],new_robot_pos[0]-curr_robot_coordinates[0]);

            std::cout << "-------- INFO PER ANGOLI -------" << std::endl;
            std::cout << "  *beta= " << beta << std::endl;
            std::cout << "  *Robot orientation z-axis (radians) [-pi,pi] : " << curr_robot_orientation << std::endl; 
            std::cout << "  *Goal orientation z-axis (radians) [-pi,pi] : " << goal_orientation << std::endl;

            if(std::fabs(angles::shortest_angular_distance(curr_robot_orientation,beta))<=_PI/2){
                //la rotazione per muovere il robot nella direzione della forza è compresa in [-pi/2;pi/2]
                //possiamo eseguire una combo di rotazione e movimento in avanti

                //ROTAZIONE:
                new_robot_ang_vel_z=K_p*(angles::shortest_angular_distance(curr_robot_orientation, beta));

                if (std::fabs(new_robot_ang_vel_z)>1){
                    new_robot_ang_vel_z=sign(new_robot_ang_vel_z)*1;
                }

                //TRASLAZIONE GIA' CALCOLATA

            }

            else{
                //è preferibile far ruotare il robot verso la direzione della futura posizione prima di farlo muovere linearmente
                
                new_robot_lin_vel={0,0};
                new_robot_ang_vel_z=sign(angles::shortest_angular_distance(curr_robot_orientation,beta))*max_angular_vel_z;

            }

            // // new_robot_ang_vel_z=K_p*(angles::shortest_angular_distance(curr_robot_orientation, std::atan2(goal_coordinates[1]-curr_robot_coordinates[1],goal_coordinates[0]-curr_robot_coordinates[0])));
            

        }



        


            

        std::cout << "\n";
        std::cout << "**NUOVA VELOCITÀ ROBOT CALCOLATe: " << std::endl;
        std::cout << "  *New position : " << new_robot_pos[0] << " " << new_robot_pos[1] << std::endl;
        std::cout << "  *New velocity vector  : " << new_robot_lin_vel[0] << " " << new_robot_lin_vel[1] << std::endl; 
        std::cout << "  *New angular velocity : " << new_robot_ang_vel_z << std::endl;


        //PUBBLICAZIONE MESSAGGIO
        pub_cmd = nh.advertise<geometry_msgs::Twist>("/cmd_vel",1);

        cmd_vel.angular.x=0.0;
        cmd_vel.angular.y=0.0;
        cmd_vel.angular.z=new_robot_ang_vel_z;
        //cmd_vel.linear.x=vect_norm1(new_robot_lin_vel);
        cmd_vel.linear.x=std::fabs(new_robot_lin_vel[0]);
        cmd_vel.linear.y=new_robot_lin_vel[1];
        cmd_vel.linear.z=0.0;

        pub_cmd.publish(cmd_vel);

        std::cout << "\nmessaggio pubblicato\n\n\n" << std::endl;
        

        return true;
    }

    bool SfmPlanner::isGoalReached()
    {
        if(!initialized_)
        {
            ROS_ERROR("This planner has not been initialized");
            return false;
        }
        if(goal_reached){
            return true;
        }
        else{
            return false;
        }
        
    }
}