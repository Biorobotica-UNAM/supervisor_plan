#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/empty.hpp>
#include <std_msgs/msg/int32.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_srvs/srv/trigger.hpp>
#include <atomic>
#include <mutex>

#include "supervisor_plan/supervisor_plan_core.hpp"

#include <memory>

class SupervisorPlanNode : public rclcpp::Node
{
public:
  SupervisorPlanNode()
  : Node("supervisor_plan_node")
  {
    RCLCPP_INFO(this->get_logger(), "supervisor_plan_node started");

	rewind_sub_ = create_subscription<std_msgs::msg::Empty>("/supervisor/supervisor_plan/rewind_one", 10,
													  		[this](std_msgs::msg::Empty::SharedPtr) {
															plan_core_.rewind_one();
															RCLCPP_WARN(get_logger(), "Plan rewound one instruction.");
													  	});

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

    allow_instruction_sub_ = this->create_subscription<std_msgs::msg::Empty>(
      "/supervisor/supervisor_manager/allow_plan_instruction",
      10,
      std::bind(&SupervisorPlanNode::on_allow_instruction, this, std::placeholders::_1)
    );

    service_ = this->create_service<std_srvs::srv::Trigger>("/request_instruction",
                                                            std::bind(&SupervisorPlanNode::on_request_instruction,
                                                            this,
                                                            std::placeholders::_1,
                                                            std::placeholders::_2)
                                                        );

    publisher_ = this->create_publisher<std_msgs::msg::String>("/instruction_msg", 10);
    active_plan_pub_ = this->create_publisher<std_msgs::msg::Bool>(
      "/supervisor/supervisor_plan/active", 10);
    plan_ready_pub_ = this->create_publisher<std_msgs::msg::Empty>(
      "/supervisor/supervisor_plan/plan_ready", 10);

    RCLCPP_INFO(this->get_logger(),"Listening for instruction commands on /supervisor/supervisor_manager/instruction_command (1=retry)");
    RCLCPP_INFO(this->get_logger(),"Listening plan semaphores on /supervisor/supervisor_manager/allow_plan_instruction");
    RCLCPP_INFO(this->get_logger(), "Waiting for plans on /planning/clips/results");
    RCLCPP_INFO(this->get_logger(), "Publishing plan-ready events on /supervisor/supervisor_plan/plan_ready");
    RCLCPP_INFO(this->get_logger(), "Listening for override instructions on /supervisor_manager/instruction");
    RCLCPP_INFO(this->get_logger(), "Service available at /request_instruction");
    publishActivePlanStatus();
  }

private:
  supervisor_plan::SupervisorPlanCore plan_core_;

  rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr instruction_cmd_sub_;
  rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr allow_instruction_sub_;
  rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr rewind_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr plan_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr instruction_override_sub_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr service_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr active_plan_pub_;
  rclcpp::Publisher<std_msgs::msg::Empty>::SharedPtr plan_ready_pub_;

  // ===== Callbacks: update-only =====
  void on_plan_raw(const std_msgs::msg::String::SharedPtr msg)
  {
    plan_core_.update_raw_plan(msg->data);
    plan_core_.process_pending_plan();
    if (plan_core_.has_active_plan()) {
      notifyPlanReady();
    }
    publishActivePlanStatus();
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

    if (!dispatch_token_available_.exchange(false))
    {
      response->success = false;
      response->message = "blocked_by_manager";
      RCLCPP_WARN(
        this->get_logger(),
        "Instruction request rejected: waiting manager semaphore allow_plan_instruction"
      );
      return;
    }

    // If a new raw plan arrived, parse it now (logic lives in PlanManager)
    plan_core_.process_pending_plan();
    publishActivePlanStatus();

    const bool retry = retry_requested_.exchange(false);
    if (retry)
    {
      std::string retry_instruction;
      {
        std::lock_guard<std::mutex> lk(last_sent_mtx_);
        retry_instruction = last_sent_instruction_;
      }

      if (!retry_instruction.empty())
      {
        std_msgs::msg::String out;
        out.data = retry_instruction;
        publisher_->publish(out);
        response->success = true;
        response->message = retry_instruction;
        publishActivePlanStatus();
        RCLCPP_WARN(
          this->get_logger(),
          "Retry active: sending same instruction again -> '%s'",
          retry_instruction.c_str()
        );
        return;
      }

      RCLCPP_WARN(
        this->get_logger(),
        "Retry was requested but there is no previous instruction. Sending next available instruction."
      );
    }

    auto res = plan_core_.next_instruction();

    // Publish and respond
    std_msgs::msg::String out;
    out.data = res.instruction;
    publisher_->publish(out);

    response->success = res.ok;
    response->message = res.instruction;

    {
      std::lock_guard<std::mutex> lk(last_sent_mtx_);
      last_sent_instruction_ = res.instruction;
    }

    publishActivePlanStatus();
    RCLCPP_INFO(
      this->get_logger(),
      "Instruction sent (ok=%s, had_plan=%s, plan_finished=%s): '%s'",
      res.ok ? "true" : "false",
      res.had_plan ? "true" : "false",
      res.plan_finished ? "true" : "false",
      res.instruction.c_str()
    );
  }

  void on_instruction_cmd(const std_msgs::msg::Int32::SharedPtr msg)
  {
    if (msg->data == 1)
    {
      retry_requested_.store(true);
      RCLCPP_WARN(this->get_logger(), "Retry requested (command=1). Next /request_instruction will resend last instruction.");
    }
  }

  void on_allow_instruction(const std_msgs::msg::Empty::SharedPtr msg)
  {
    (void)msg;
    dispatch_token_available_.store(true);
    RCLCPP_DEBUG(this->get_logger(), "Semaphore received: plan can dispatch one instruction.");
  }

  void publishActivePlanStatus()
  {
    std_msgs::msg::Bool msg;
    msg.data = plan_core_.has_active_plan();
    active_plan_pub_->publish(msg);
  }

  void notifyPlanReady()
  {
    plan_ready_pub_->publish(std_msgs::msg::Empty{});
    RCLCPP_INFO(this->get_logger(), "Plan ready notification sent to manager");
  }

  std::atomic_bool retry_requested_{false};
  std::atomic_bool dispatch_token_available_{false};
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
