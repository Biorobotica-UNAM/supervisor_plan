# supervisor_plan

## Overview

`supervisor_plan` is a ROS 2 package responsible for supervising and managing high-level plans within a modular robotic architecture. It operates as an intermediate supervisory layer between the symbolic planner and the task execution system.

The package validates, structures, and sequentially provides instructions derived from large, serialized plan representations. Its purpose is to ensure plan coherence, maintain execution continuity, and support runtime corrective instruction overlay without losing the original plan structure.

This component is designed as part of a layered architecture composed of Planner → Supervisor → Executor modules.

---

## Conceptual Role in the Architecture

The `supervisor_plan` package fulfills three primary responsibilities:

1. **Plan Reception and Parsing**
2. **Plan Coherence Validation (conceptual stage)**
3. **Instruction Dispatching via ROS 2 service interface**

Additionally, it supports runtime instruction overlay while preserving the integrity of the original plan.

---

## Functional Description

### 1. Plan Reception

The node receives a serialized plan in the form of a large `std_msgs/String`.  
Because the received string may be extensive and contain multiple instructions, the system:

- Parses the full plan.
- Generates an internal instruction queue.
- Structures the plan into a sequential instruction buffer.

This queue becomes the authoritative internal representation of the plan.

---

### 2. Plan Coherence Validation

Before enabling instruction dispatch, the plan undergoes a conceptual validation stage.

This stage is intended to:

- Ensure syntactic coherence.
- Validate logical ordering of actions.
- Detect incomplete or malformed instructions.
- Verify domain-level consistency (future extension).

Currently, this validation stage is conceptual and extensible, designed to support future formal plan verification mechanisms.

---

### 3. Instruction Dispatch Service

The package exposes a ROS 2 service: /request_instruction (std_srvs/Trigger)


Upon receiving a service request:

- If a valid plan exists → returns the next instruction.
- If no plan is loaded → returns a default fallback instruction: Justina listen keyword


This fallback ensures the robot remains operational and reactive even in the absence of a formal plan.

---

### 4. Instruction Overlay Mechanism

The node supports runtime insertion of corrective or higher-priority instructions originating from other supervisory components.

Key properties:

- The original plan is preserved.
- The new instruction is superimposed without erasing the existing queue.
- After execution of the corrective instruction, the system resumes the original plan.

This mechanism enables adaptive corrections while maintaining global plan continuity.

---

## Design Principles

- Modular supervisory abstraction.
- Non-destructive plan correction.
- Service-oriented instruction retrieval.
- Deterministic sequential execution.
- Extensibility for formal plan validation.

---

## Dependencies

- ROS 2 (rclcpp)
- std_msgs
- std_srvs

---

## Future Extensions

- Formal semantic plan validation.
- Temporal constraint checking.
- Plan priority arbitration.
- Plan preemption policies.
- Logging and execution traceability.

---

## Architectural Context

This package operates in coordination with:

- `supervisor_system`
- `supervisor_manager`
- Planner modules
- Executor modules

It is not responsible for executing instructions, only supervising and delivering them coherently.

---

## Joshua Martinez

Bio-Robotics Laboratory  
Universidad Nacional Autónoma de México


