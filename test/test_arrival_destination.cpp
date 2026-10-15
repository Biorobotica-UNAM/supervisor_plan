#include <gtest/gtest.h>
#include "supervisor_plan/supervisor_plan_core.hpp"

using supervisor_plan::SupervisorPlanCore;

TEST(ArrivalDestination, UpdatesOnlyImmediateMatchingArrivalAfterOverride)
{
  SupervisorPlanCore plan;
  plan.update_raw_plan("Paso 1: Justina go_to kitchen\n"
    "Paso 2: Justina say I have arrived at kitchen\n"
    "Paso 3: Justina go_to kitchen\n"
    "Paso 4: Justina say I have arrived at kitchen");
  plan.process_pending_plan();
  EXPECT_EQ(plan.next_instruction().instruction, "Justina go_to kitchen");
  plan.update_override_instruction("Justina go_to kitchen_bar");
  EXPECT_EQ(plan.next_instruction().instruction, "Justina go_to kitchen_bar");
  EXPECT_TRUE(plan.update_next_arrival("kitchen", "kitchen_bar"));
  EXPECT_EQ(plan.next_instruction().instruction, "Justina say I have arrived at kitchen_bar");
  EXPECT_EQ(plan.next_instruction().instruction, "Justina go_to kitchen");
  EXPECT_EQ(plan.next_instruction().instruction, "Justina say I have arrived at kitchen");
}

TEST(ArrivalDestination, DoesNotSearchAheadOrModifyUnrelatedSpeech)
{
  for (const auto & next : {"Justina say kitchen is nice", "Justina go_to kitchen",
      "Justina say I have arrived at bedroom"}) {
    SupervisorPlanCore plan;
    plan.update_raw_plan(std::string("Paso 1: Justina go_to kitchen\nPaso 2: ") + next +
      "\nPaso 3: Justina say I have arrived at kitchen");
    plan.process_pending_plan(); plan.next_instruction();
    EXPECT_FALSE(plan.update_next_arrival("kitchen", "kitchen_bar"));
    EXPECT_EQ(plan.next_instruction().instruction, next);
    EXPECT_EQ(plan.next_instruction().instruction, "Justina say I have arrived at kitchen");
  }
}

TEST(ArrivalDestination, MatchesFullNamesAndPreservesPunctuation)
{
  SupervisorPlanCore plan;
  plan.update_raw_plan("Paso 1: Justina say I have arrived at Living Room.");
  plan.process_pending_plan();
  EXPECT_FALSE(plan.update_next_arrival("room", "kitchen_bar"));
  EXPECT_TRUE(plan.update_next_arrival("living_room", "kitchen_bar"));
  EXPECT_EQ(plan.next_instruction().instruction, "Justina say I have arrived at kitchen_bar.");
  EXPECT_FALSE(plan.update_next_arrival("kitchen_bar", "bedroom"));
}
