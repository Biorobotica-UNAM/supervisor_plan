#pragma once

#include <string>
#include <vector>
#include <mutex>

namespace supervisor_plan
{

class SupervisorPlanCore
{
public:
  struct NextResult
  {
    bool ok{true};
    std::string instruction;
    bool used_override{false};
    bool had_plan{false};
    bool plan_finished{false};
    int big_step_idx{-1}; // -1 si no aplica
  };

  SupervisorPlanCore() = default;

  void rewind_one();
  void skip_next(int count);
  bool update_next_arrival(const std::string & original, const std::string & corrected);

  // ===== Update-only (callbacks) =====
  void update_raw_plan(const std::string& raw_plan);
  void update_override_instruction(const std::string& instruction);

  // ===== Logic API (service / main loop) =====
  void process_pending_plan();   // parsea si hay plan nuevo pendiente
  NextResult next_instruction(); // decide override/plan/fallback

  // ===== Optional getters (para debug/uso futuro) =====
  std::vector<std::string> big_steps_copy() const;
  size_t plan_size() const;
  size_t big_steps_size() const;
  bool has_active_plan() const;

private:
  // Protected state
  mutable std::mutex mtx_;

  // Raw inputs
  std::string raw_plan_;
  bool raw_plan_dirty_{false};

  // Override
  std::string override_instruction_;

  // Parsed plan
  std::vector<std::string> plan_instructions_;
  std::vector<std::string> big_steps_;
  std::vector<int> instr_big_step_idx_;
  int current_big_step_idx_{-1};

  int instruction_counter_{0};
  bool has_active_plan_{false};

  // Config
  const std::string fallback_instruction_{"Justina listen_for keyword"};

  // Internal: assumes mutex locked
  void parse_raw_plan_locked();
};

} // namespace supervisor_plan
