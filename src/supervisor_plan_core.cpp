#include "supervisor_plan/supervisor_plan_core.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

namespace supervisor_plan
{

void SupervisorPlanCore::update_raw_plan(const std::string& raw_plan)
{
  std::lock_guard<std::mutex> lk(mtx_);
  raw_plan_ = raw_plan;
  raw_plan_dirty_ = true;
}

void SupervisorPlanCore::update_override_instruction(const std::string& instruction)
{
  if (instruction.empty()) return;
  std::lock_guard<std::mutex> lk(mtx_);
  override_instruction_ = instruction;
}

void SupervisorPlanCore::process_pending_plan()
{
  std::lock_guard<std::mutex> lk(mtx_);
  if (!raw_plan_dirty_) return;

  parse_raw_plan_locked();
  raw_plan_dirty_ = false;
}

SupervisorPlanCore::NextResult SupervisorPlanCore::next_instruction()
{
  std::lock_guard<std::mutex> lk(mtx_);

  NextResult out;

  // 1) Override
  if (!override_instruction_.empty()) {
    out.instruction = override_instruction_;
    out.used_override = true;
    out.ok = true;
    out.had_plan = has_active_plan_;
    override_instruction_.clear();
    return out;
  }

  // 2) No plan -> fallback
  if (!has_active_plan_ || plan_instructions_.empty()) {
    out.ok = true;
    out.had_plan = false;
    out.instruction = fallback_instruction_;
    return out;
  }

  // 3) Next instruction
  if (instruction_counter_ < 0 || instruction_counter_ >= (int)plan_instructions_.size()) {
    has_active_plan_ = false;
    out.ok = true;
    out.had_plan = false;
    out.plan_finished = true;
    out.instruction = fallback_instruction_;
    return out;
  }

  out.ok = true;
  out.had_plan = true;
  out.instruction = plan_instructions_[instruction_counter_];
  if (!instr_big_step_idx_.empty()) out.big_step_idx = instr_big_step_idx_[instruction_counter_];

  instruction_counter_++;

  if (instruction_counter_ >= (int)plan_instructions_.size()) {
    has_active_plan_ = false;
    out.plan_finished = true;
  }

  return out;
}

std::vector<std::string> SupervisorPlanCore::big_steps_copy() const
{
  std::lock_guard<std::mutex> lk(mtx_);
  return big_steps_;
}

size_t SupervisorPlanCore::plan_size() const
{
  std::lock_guard<std::mutex> lk(mtx_);
  return plan_instructions_.size();
}

size_t SupervisorPlanCore::big_steps_size() const
{
  std::lock_guard<std::mutex> lk(mtx_);
  return big_steps_.size();
}

void SupervisorPlanCore::parse_raw_plan_locked()
{
  // reset parsed state
  plan_instructions_.clear();
  big_steps_.clear();
  instr_big_step_idx_.clear();
  current_big_step_idx_ = -1;
  instruction_counter_ = 0;
  has_active_plan_ = false;

  std::string full_plan = raw_plan_;
  boost::algorithm::trim(full_plan);
  if (full_plan.empty()) return;

  std::vector<std::string> lines;
  boost::split(lines, full_plan, boost::is_any_of("\n"));

  for (std::string line : lines) {
    boost::algorithm::trim(line);
    if (line.empty()) continue;

    // Big steps: "==Justina ...=="
    if (boost::algorithm::starts_with(line, "==")) {
      big_steps_.push_back(line);
      current_big_step_idx_ = (int)big_steps_.size() - 1;
      continue;
    }

    // Step lines: "Paso N: <instruction>"
    if (line.find("Paso") != std::string::npos) {
      auto colon_pos = line.find(':');
      if (colon_pos == std::string::npos || colon_pos + 1 >= line.size()) {
        continue; // ignora sin tronar
      }

      std::string instruction = line.substr(colon_pos + 1);
      boost::algorithm::trim(instruction);

      if (!instruction.empty()) {
        plan_instructions_.push_back(instruction);
        instr_big_step_idx_.push_back(current_big_step_idx_); // puede ser -1
      }
      continue;
    }

    // ignore everything else safely
  }

  if (!plan_instructions_.empty()) {
    has_active_plan_ = true;
    instruction_counter_ = 0;
  }
}

} // namespace supervisor_plan
