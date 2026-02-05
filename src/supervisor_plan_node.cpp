#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/trigger.hpp>

#include <memory>
#include <string>
#include <sstream>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace std;

class SupervisorPlanNode : public rclcpp::Node {
public:
    SupervisorPlanNode() : Node("supervisor_plan_node") {
        RCLCPP_INFO(this->get_logger(), "supervisor_plan_node started");

        // Subscriber to receive full plans from CLIPS
        plan_sub_ = this->create_subscription<std_msgs::msg::String>(
            "/clips/clips/results", 10,
            std::bind(&SupervisorPlanNode::planCallback, this, std::placeholders::_1)
        );

        // Subscriber to receive instruction overrides
        instruction_override_sub_ = this->create_subscription<std_msgs::msg::String>(
            "/supervisor_manager/instruction", 10,
            std::bind(&SupervisorPlanNode::instructionOverrideCallback, this, std::placeholders::_1)
        );

        // Service for the robot to request the next instruction
        service_ = this->create_service<std_srvs::srv::Trigger>(
            "/request_instruction",
            std::bind(&SupervisorPlanNode::handleInstructionRequest, this,
                     std::placeholders::_1, std::placeholders::_2));

        // Publisher to send instructions
        publisher_ = this->create_publisher<std_msgs::msg::String>("/instruction_msg", 10);

        // Initialize plan state
        current_plan_index_ = -1;
        instruction_counter_ = 0;
        has_active_plan_ = false;
        override_instruction_ = "";
        
        RCLCPP_INFO(this->get_logger(), "Supervisor Plan initialized");
    }

private:
    // Variables to store the current plan
    std::vector<std::string> current_plan_;
    int current_plan_index_;
    int instruction_counter_;
    bool has_active_plan_;
    std::string override_instruction_;
    
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr plan_sub_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr instruction_override_sub_;
    rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr service_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;

    // --------------------------
    // INSTRUCTION OVERRIDE CALLBACK
    // --------------------------
    void instructionOverrideCallback(const std_msgs::msg::String::SharedPtr msg) {
        if (!msg->data.empty()) {
            override_instruction_ = msg->data;
            RCLCPP_INFO(this->get_logger(), 
                        "Override instruction received: %s", 
                        override_instruction_.c_str());
        }
    }

    // --------------------------
    // CLIPS TOPIC CALLBACK
    // --------------------------
    void planCallback(const std_msgs::msg::String::SharedPtr msg) {
        std::string full_plan = msg->data;
        if (full_plan.empty()) {
            RCLCPP_WARN(this->get_logger(), "Empty plan received");
            return;
        }

        RCLCPP_INFO(this->get_logger(), "Plan received: %s", full_plan.c_str());

        // Clear previous plan
        current_plan_.clear();
        
        std::vector<std::string> lines;
        boost::split(lines, full_plan, boost::is_any_of("\n"));

        for (const string& line : lines) {
            if (line.find("Paso") != string::npos) {
                // This line corresponds to a plan step
                vector<string> tokens;
                boost::algorithm::split(tokens, line, boost::algorithm::is_any_of(" "));
                
                if (tokens.size() > 5) {
                    // Rebuild instruction (skip "Paso X:")
                    string instruction = "";
                    bool start_collecting = false;
                    
                    for (size_t i = 0; i < tokens.size(); i++) {                        
                        if (start_collecting)
                        {
                            instruction += tokens[i];
                            if (i < tokens.size() - 1) instruction += " ";
                            if (tokens[i].find("Justina") != string::npos) i++;

                        }
                        // Start collecting after the step number
                        if (i >= 1 && tokens[i].find(":") != string::npos)
                        {
                            start_collecting = true;
                        }
                    }
                    
                    if (!instruction.empty()) {
                        current_plan_.push_back(instruction);
                        RCLCPP_DEBUG(this->get_logger(), "Instruction added: %s", instruction.c_str());
                    }
                }                
            }
        }

        // Configure robot state if a valid plan was received
        if (!current_plan_.empty()) {
            current_plan_index_ = 0;
            instruction_counter_ = 0;
            has_active_plan_ = true;
            
            RCLCPP_INFO(this->get_logger(),
                        "Plan processed: %zu instructions received",
                        current_plan_.size());
            RCLCPP_INFO(this->get_logger(), "Plan ready for execution");
        } else {
            RCLCPP_WARN(this->get_logger(),
                        "Plan received but no valid instructions were found");
        }
    }

    // --------------------------
    // SERVICE HANDLER
    // --------------------------
    void handleInstructionRequest(
        const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
        std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
        
        (void)request;  // Request is not used

        RCLCPP_INFO(this->get_logger(), "Instruction request received");

        // Check if there's an override instruction
        if (!override_instruction_.empty()) {
            std::string instruction = override_instruction_;
            override_instruction_ = "";  // Clear after using
            
            // Publish instruction on topic
            auto instruction_msg = std_msgs::msg::String();
            instruction_msg.data = instruction;
            publisher_->publish(instruction_msg);
            
            // Fill service response
            response->success = true;
            response->message = instruction;
            
            RCLCPP_INFO(this->get_logger(),
                        "Override instruction sent: %s",
                        instruction.c_str());
            return;
        }

        // Check if there is an active plan
        if (!has_active_plan_ || current_plan_.empty()) {
            RCLCPP_WARN(this->get_logger(), "NoPlanAssigned: send a default instruction for wait for the keyword"); 
            response->message = "Justina listen keyword";
            response->success = true;
            return;
        }

        // Get next instruction
        std::string instruction = current_plan_[instruction_counter_];
        
        // Increment counter for next request
        instruction_counter_++;
        
        // Publish instruction on topic (optional)
        auto instruction_msg = std_msgs::msg::String();
        instruction_msg.data = instruction;
        publisher_->publish(instruction_msg);
        
        // Fill service response
        response->success = true;
        response->message = instruction;
        
        RCLCPP_INFO(this->get_logger(),
                    "Instruction %d/%zu sent: %s",
                    instruction_counter_,
                    current_plan_.size(),
                    instruction.c_str());
        
        // Check if plan execution is complete
        if (instruction_counter_ >= (int)current_plan_.size()) {
            has_active_plan_ = false;
            RCLCPP_INFO(this->get_logger(),
                        "Last instruction of the plan has been sent");
        }
    }
};

// --------------------------
// MAIN
// --------------------------
int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<SupervisorPlanNode>();
    
    RCLCPP_INFO(node->get_logger(), "Plan Manager Node started");
    RCLCPP_INFO(node->get_logger(), "Waiting for plans on /clips/results");
    RCLCPP_INFO(node->get_logger(), "Listening for override instructions on /supervisor_manager/instruction");
    RCLCPP_INFO(node->get_logger(), "Service available at /request_instruction");
    
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}