#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/trigger.hpp>
#include <std_msgs/msg/int32.hpp>
#include <atomic>


#include "supervisor_plan/supervisor_plan_core.hpp"

#include <memory>

class SupervisorPlanNode : public rclcpp::Node
{
public:
  SupervisorPlanNode()
  : Node("supervisor_plan_node")
  {
    RCLCPP_INFO(this->get_logger(), "supervisor_plan_node started");

    plan_sub_ = this->create_subscription<std_msgs::msg::String>("/planning/clips/results",
                                                                10,
                                                                std::bind(&SupervisorPlanNode::on_plan_raw,
                                                                this,
                                                                std::placeholders::_1)
                                                                );

    instruction_override_sub_ = this->create_subscription<std_msgs::msg::String>("/supervisor/supervisor_manager/instruction",
                                                                                10,
                                                                                std::bind(&SupervisorPlanNode::on_override_raw,
                                                                                this,
                                                                                std::placeholders::_1)
                                                                                );

    instruction_cmd_sub_ = this->create_subscription<std_msgs::msg::Int32>("/supervisor/supervisor_manager/instruction_command",
                                                                          10,
                                                                          std::bind(&SupervisorPlanNode::on_instruction_cmd, this, std::placeholders::_1)
                                                                          );

    service_ = this->create_service<std_srvs::srv::Trigger>("/request_instruction",
                                                            std::bind(&SupervisorPlanNode::on_request_instruction,
                                                            this,
                                                            std::placeholders::_1,
                                                            std::placeholders::_2)
                                                        );

    publisher_ = this->create_publisher<std_msgs::msg::String>("/instruction_msg", 10);

    RCLCPP_INFO(this->get_logger(),"Listening for instruction commands on /supervisor/supervisor_manager/instruction_command (1=retry)");
    RCLCPP_INFO(this->get_logger(), "Waiting for plans on /planning/clips/results");
    RCLCPP_INFO(this->get_logger(), "Listening for override instructions on /supervisor_manager/instruction");
    RCLCPP_INFO(this->get_logger(), "Service available at /request_instruction");
  }

private:
  supervisor_plan::SupervisorPlanCore plan_core_;

  rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr instruction_cmd_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr plan_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr instruction_override_sub_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr service_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;

  // ===== Callbacks: update-only =====
  void on_plan_raw(const std_msgs::msg::String::SharedPtr msg)
  {
    plan_core_.update_raw_plan(msg->data);
    RCLCPP_INFO(this->get_logger(), "Raw plan received (%zu chars)", msg->data.size());
  }

  void on_override_raw(const std_msgs::msg::String::SharedPtr msg)
  {
    plan_core_.update_override_instruction(msg->data);
    RCLCPP_INFO(this->get_logger(), "Override received (%zu chars)", msg->data.size());
  }

  // ===== Service callback: delegates all logic to PlanManager =====
  void on_request_instruction(
    const std::shared_ptr<std_srvs::srv::Trigger::Request> request,
    std::shared_ptr<std_srvs::srv::Trigger::Response> response)
  {
    (void)request;

    // If a new raw plan arrived, parse it now (logic lives in PlanManager)
    plan_core_.process_pending_plan();

    auto res = plan_core_.next_instruction();

    // Publish and respond
    std_msgs::msg::String out;
    out.data = res.instruction;
    publisher_->publish(out);

    response->success = res.ok;
    response->message = res.instruction;

    RCLCPP_INFO(this->get_logger(), "Sent instruction: %s", res.instruction.c_str());
  }

  void on_instruction_cmd(const std_msgs::msg::Int32::SharedPtr msg)
  {
    if (msg->data == 1)
    {
      retry_requested_.store(true);
      RCLCPP_WARN(this->get_logger(), "Retry requested (command=1). Next /request_instruction will resend last instruction.");
    }
  }


  std::atomic_bool retry_requested_{false};
  std::string last_sent_instruction_;
  std::mutex last_sent_mtx_;
};

int main(int argc, char **argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<SupervisorPlanNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
