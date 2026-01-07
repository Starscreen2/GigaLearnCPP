## GigaLearnCPP – Training Journey

This document tracks the major decisions and changes we’ve made while training the Rocket League bot with GigaLearnCPP.

### System & Stability
- **Freeze diagnostics:** Added detailed heartbeat logging in `Learner` to track timestamps, RAM, GPU memory, and checkpoint status into `training_logs/training_heartbeat.txt`.
- **Checkpoint tuning:** Switched to keeping up to **2000** checkpoints, with logs around save/delete start/end to help diagnose any filesystem/antivirus stalls.
- **WandB integration:** Re‑enabled online WandB with robust error handling, short reconnect cooldowns, and offline fallback so logging can’t crash or freeze training.

### Training Config
- **Envs and batching:** Using `numGames = 256` and `tsPerItr = 100,000` so each PPO iteration collects 100k steps.
- **PPO settings:** `batchSize = tsPerItr`, `miniBatchSize = 25,000` (4 minibatches), `epochs = 2` for good sample efficiency and GPU utilization.

### Core Behavior Goals
- **Scoring focus:** Increased `GoalReward` and `ShotReward` weights and added a physics‑based `OpenNetReward` to strongly favor finishing truly open nets and punish giving the ball away.
- **Safety:** Added `OwnGoalPunishment` so own goals are heavily penalized relative to normal goals.
- **Boost management:** Added `BigBoostReward` and slightly reduced small‑boost reward so the bot prioritizes 100‑boost pads when reasonable.

### Double Touchs & Advanced Aerials
- **Main double‑touch rewards:** `DoubleTouchReward`, `WallDoubleTouchReward`, and `DoubleTouchGoalReward` encourage high, on‑target double touches while:
  - Removing own‑backwall farm behavior.
  - Punishing ceiling double taps that intentionally send the ball upward into bad positions.
  - Requiring strong directional alignment toward the opponent goal.
- **Helper shaping:** `DoubleTouchHelperReward` and `DoubleTouchTrajectoryReward` guide good first touches and trajectories toward future double touches.
- **Balance update:** Reduced `DoubleTouchGoalReward` weight to **80** so double‑touch goals are very valuable but not overwhelmingly more than strong air‑dribble or regular goals.

### Air Dribbles & Mechanics
- **Air dribble suite:** Added `AirDribbleReward`, `AirDribbleBoostReward`, `AirRollReward`, `FlipResetReward`, `AirDribbleStartReward`, and `AirDribbleDistanceReward` to:
  - Reward sustained aerial control near the ball.
  - Encourage boosting toward the ball and using air roll naturally.
  - Treat flip resets as “nice to have” (low value, don’t hard‑seek them).
- **Distance emphasis:** Increased `AirDribbleDistanceReward` weight to **50** so long, successful air‑dribble goals meaningfully compete with double‑touch goals.

### Open Net Logic
- **Physics‑accurate checks:** Reworked `OpenNetReward` to:
  - Use proper kinematics along the goal axis (Y) with gravity.
  - Require strict goal alignment and tight position margins.
  - Predict future ball position and test whether opponents can realistically save.
- **Reward structure:** Gives extra reward for guaranteed open‑net shots/goals and small continuous reward while the ball is safely screaming toward an unsaveable goal.

### Current Training Focus
- Let the current reward mix run long enough to stabilize.
- Watch for: over‑obsession with double touches vs. simple scoring, under‑use of air dribbles, and any remaining open‑net misses.
- Next adjustments (if needed) will likely be small weight nudges rather than new reward types.


