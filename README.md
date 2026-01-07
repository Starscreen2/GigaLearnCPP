# GigaLearnCPP
-------------
GigaLearn is an even-faster C++ machine learning framework for Rocket League bots.

## Features
**Basic Features (Shared With Most Frameworks)**:
- Fast PPO implementation
- Configurable model layer sizes
- Checkpoint saving/loading
- Working example
- Return standardization and obs standardization support
- Built-in visualization support
- Easy custom metrics suppport

**Unique Learning Features**:
- Extremely fast monolithic single-process inference model
- Complete and proper action masking
- Built-in shared layers support (enabled by default)
- Built-in configurable ELO-based skill tracking system 
- Configurable model activation functions
- Configurable model optimizers
- Configurable model layer norm (recommended)
- Policy version saving system (required if using the skill tracker)
- Built-in reward logging

**Unique Environment/State Features**:
- Access to previous states (e.g. `player.prev->pos`)
- Simpler access to previous actions, final bools
- Inherented access to all `CarState` and `BallState` fields (e.g. `player.isFlipping`)
- No more duplicate state fields
- Simpler access to current state events (e.g. `if (player.eventState.shot) ...`)
- User-led setup of arenas and cars during environment creation
- RocketSim-based state setting (you are given the arena to state-set)
- Configurable action delay

***Coming Soon**:
- Training against older versions

## Installation
*There's no installation guide for now as I plan to rework several aspects of the library to make it easier to install.*

## Bringing In Rewards/Obs Builders/Etc. from RLGymPPO_CPP
State changes:
- `PlayerData` -> `Player`
- `player.phys` -> `player.`
- `player.carState` -> `player`
- `state.ballState` -> `state.ball`

Reward changes:
- `GetReward(const PlayerData& player, const GameState& state, const Action& prevAction)` -> `GetReward(const Player& player, const GameState& state, bool isFinal) override`
- `prevAction (argument)` -> `player.prevAction`
- `GetFinalReward()` -> `isFinal (argument)`

Metrics:
- `metrics.AccumAvg(), metrics.GetAvg()` -> `report.AddAvg()`

Learner config:
- `cfg.numThreads, cfg.numGamesPerThread` -> `cfg.numGames`
- `cfg.ppo.policyLayerSizes` -> `cfg.ppo.policy.layerSizes`
- `cfg.ppo.criticLayerSizes` -> `cfg.ppo.critic.layerSizes`
- `cfg.expBufferSize` -> `(experience buffer removed)`
