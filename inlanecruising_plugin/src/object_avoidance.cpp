
#include <inlanecruising_plugin/object_avoidance.h>

namespace inlanecruising_plugin
{
namespace object_avoidance
{
    ObjectAvoidance::ObjectAvoidance(){}

    cav_msgs::TrajectoryPlan ObjectAvoidance::update_traj_for_object(cav_msgs::TrajectoryPlan& original_tp, const carma_wm::WorldModelConstPtr& wm_, double current_speed_) 
    {
        
        cav_msgs::TrajectoryPlan update_tpp_vector;

        geometry_msgs::Twist current_velocity;
        current_velocity.linear.x = current_speed_;

        // // TODO get roadway object
        std::vector<cav_msgs::RoadwayObstacle> rwol = wm_->getRoadwayObjects();
        cav_msgs::RoadwayObstacleList rwol2;
        rwol2.roadway_obstacles = rwol;

        std::cout << "host_vehicle_size" << host_vehicle_size.x << std::endl;

        std::vector<cav_msgs::RoadwayObstacle> rwol_collision = carma_wm::collision_detection::WorldCollisionDetection(rwol2, original_tp, host_vehicle_size, current_velocity, 10.0);// 10 replace to target_time
        std::cout << "rwol_collision" << rwol_collision.size() << std::endl;
        std::cout << "rwol" << rwol.size() << std::endl;

        // correct the input types
        if(rwol_collision.size() > 0) {

            std::cout << "Collision!" << std::endl;

            // use trajectory utiles to update trajectory plan points
            
            // lead vehicle trjactory
            double dist_x = rwol_collision[0].object.pose.pose.position.x - original_tp.trajectory_points[0].x;
            double dist_y = rwol_collision[0].object.pose.pose.position.y - original_tp.trajectory_points[0].y;
            double x_lead = sqrt(dist_x*dist_x + dist_y*dist_y);

            // roadway object position
            double gap_time = (x_lead - x_gap)/current_speed_;

            double collision_time = 0; //\TODO comming from carma_wm collision detection in future (not used now)

            double goal_velocity = rwol_collision[0].object.velocity.twist.linear.x;

            double goal_pos = x_lead - goal_velocity * gap_time - 10.0; 

            double initial_time = 0;
            double initial_pos = 0.0; //relative initial position (first trajectory point)

            double initial_accel = 0;
            double goal_accel = 0;

            // TODO: Add special case for when the object is not moving

            double delta_v_max = abs(rwol_collision[0].object.velocity.twist.linear.x - max_trajectory_speed(original_tp.trajectory_points));
            
            double t_ref = (original_tp.trajectory_points[original_tp.trajectory_points.size() - 1].target_time.toSec() - original_tp.trajectory_points[0].target_time.toSec());

            double t_ph = 4 * delta_v_max / maximum_deceleration_value;

            double tp = 0;

            if(t_ph > tpmin && t_ref < t_ph){
                tp = t_ph;
            }
            else if(t_ph < tpmin){
                tp = tpmin;
            }
            else {
                tp = t_ref;
            }

            std::cout << "tp:  "<< tp << std::endl;

            std::vector<double> values = quintic_coefficient_calculator::quintic_coefficient_calculator(initial_pos, 
                                                                                                        goal_pos, 
                                                                                                        current_speed_, 
                                                                                                        goal_velocity, 
                                                                                                        initial_accel, 
                                                                                                        goal_accel, 
                                                                                                        initial_time, 
                                                                                                        tp);

            std::vector<cav_msgs::TrajectoryPlanPoint> new_trajectory_points;

            new_trajectory_points.push_back(original_tp.trajectory_points[0]);

            std::vector<double> original_traj_downtracks = get_relative_downtracks(original_tp);


            for(size_t i = 1; i < original_tp.trajectory_points.size(); i++ )
            {            
                double traj_target_time = i * tp / original_tp.trajectory_points.size();
                std::cout << "traj_target_time: "<< traj_target_time << std::endl;
                double dt_dist = polynomial_calc(values, traj_target_time);
                double dv = polynomial_calc_d(values, traj_target_time);

                std::cout << "new dist: "<< dt_dist << std::endl;
                std::cout << "new velocity: "<< dv << std::endl; //TODO Consider case when resulting velocity is 0 or <0

                cav_msgs::TrajectoryPlanPoint new_tpp;
                new_tpp.x = original_tp.trajectory_points[i].x;
                new_tpp.y = original_tp.trajectory_points[i].y;
                new_tpp.target_time = new_trajectory_points[0].target_time + ros::Duration(original_traj_downtracks[i]/dv);
                new_trajectory_points.push_back(new_tpp);
            }

            update_tpp_vector.header = original_tp.header;

            update_tpp_vector.trajectory_id = original_tp.trajectory_id;

            update_tpp_vector.trajectory_points = new_trajectory_points;

            return update_tpp_vector;
        }

        return original_tp;
    } 


    std::vector<double> ObjectAvoidance::get_relative_downtracks(cav_msgs::TrajectoryPlan& trajectory_plan)
    {
        std::vector<double> downtracks;
        downtracks.reserve(trajectory_plan.trajectory_points.size());
        // relative downtrack distance of the fist point is 0.0
        downtracks[0] = 0.0;
        for (size_t i=1; i < trajectory_plan.trajectory_points.size(); i++){
            double dx = trajectory_plan.trajectory_points[i].x - trajectory_plan.trajectory_points[i-1].x;
            double dy = trajectory_plan.trajectory_points[i].y - trajectory_plan.trajectory_points[i-1].y;
            downtracks[i] = sqrt(dx*dx + dy*dy);
        }
        return downtracks;
    }


    double ObjectAvoidance::polynomial_calc(std::vector<double> coeff, double x)
    {
        double result = 0;

        for (size_t i = 0; i < coeff.size(); i++) {
                        
            double value = coeff[i] * pow(x, (int)(coeff.size() - 1 - i));
            result = result + value;
        }
    
        return result;
    }

    double ObjectAvoidance::polynomial_calc_d(std::vector<double> coeff, double x)
    {
        double result = 0;

        for (size_t i = 0; i < coeff.size()-1; i++) {
                        
            double value = (int)(coeff.size() - 1 - i) * coeff[i] * pow(x, (int)(coeff.size() - 2 - i));
            result = result + value;
        }
    
        return result;
    }

    double ObjectAvoidance::max_trajectory_speed(std::vector<cav_msgs::TrajectoryPlanPoint> trajectory_points) 
    {
        double max_speed = 0;

        for(size_t i = 0; i < trajectory_points.size() - 2; i++ )
        {
            double dx = trajectory_points[i + 1].x - trajectory_points[i].x;
            double dy = trajectory_points[i + 1].y - trajectory_points[i].y;
            double d = sqrt(dx*dx + dy*dy); 
            double t = (trajectory_points[i + 1].target_time.toSec() - trajectory_points[i].target_time.toSec());
            double v = d/t;

            if(v > max_speed){
                max_speed = v;
            }

        }

        return max_speed;
    }
    


};  // namespace object_avoidance
};  // namespace inlanecruising_plugin