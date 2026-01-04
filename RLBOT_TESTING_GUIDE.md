# RLBot Testing Guide - GigaLearn Bot

## Overview
This guide explains how to test your trained GigaLearn bot in RLBot and play against it in 1v1 matches.

## Checkpoint and Milestone Locations

### Checkpoints
- **Location**: `build/checkpoints/`
- **Format**: Each checkpoint is a folder named by timesteps (e.g., `9597633024` = 9.6 billion timesteps)
- **Contents**: Each checkpoint folder contains:
  - `SHARED_HEAD.lt` - Shared head model weights
  - `POLICY.lt` - Policy network weights
  - `CRITIC.lt` - Critic network weights
  - `SHARED_HEAD_OPTIM.lt`, `POLICY_OPTIM.lt`, `CRITIC_OPTIM.lt` - Optimizer states
  - `RUNNING_STATS.json` - Training statistics

### Latest Checkpoint
The bot automatically loads the **highest numbered checkpoint** in `build/checkpoints/`. To find the latest:
```powershell
Get-ChildItem build\checkpoints | Sort-Object Name -Descending | Select-Object -First 1
```

### Milestones (Optional)
- **Location**: `milestones/` (created by `SaveMilestones.ps1` script)
- **Purpose**: Permanent backups of important checkpoints (saves every 100 checkpoints)
- **Usage**: Run `SaveMilestones.ps1` to automatically save milestones

## RLBot Configuration

### Files Location
All RLBot config files are in: `rlbot/`

### Key Configuration Files

#### 1. `rlbot/rlbot.cfg` - Main Match Configuration
**For 1v1 Human vs Bot:**
```ini
[Match Configuration]
num_participants = 2
game_mode = Soccer
game_map = Mannfield

[Participant Configuration]
participant_config_0 = CppPythonAgent.cfg  # Human player (you)
participant_config_1 = GigaLearnBot.cfg    # Your trained bot

participant_team_0 = 0  # You (Blue team)
participant_team_1 = 1  # Bot (Orange team)

participant_type_0 = human  # You (human player)
participant_type_1 = rlbot   # GigaLearn Bot (your trained bot)
```

#### 2. `rlbot/GigaLearnBot.cfg` - Bot Configuration
```ini
[Locations]
looks_config = appearance.cfg
python_file = CppPythonAgent.py

name = GigaLearn Bot

[Bot Parameters]
cpp_executable_path = ../build/RLBotExample.exe
```

#### 3. `rlbot/port.cfg` - Communication Port
```
42653
```
**Important**: This port must match the port in `src/RLBotExample.cpp` (line 125).

## How to Test the Bot

### Step 1: Verify Checkpoint Exists
```powershell
Test-Path "build\checkpoints\9597633024"
```
Should return `True` if checkpoint exists.

### Step 2: Start RLBot Framework
1. Open **RLBot GUI**
2. Navigate to your `rlbot/` folder
3. Click **"Start Match"** or **"Run"**

### Step 3: Bot Starts Automatically
- RLBot will automatically launch `RLBotExample.exe` when the match starts
- The bot will:
  - Load the latest checkpoint from `build/checkpoints/`
  - Connect to RLBot on port `42653`
  - Start playing as "GigaLearn Bot"

### Step 4: Play Against the Bot
- You'll be on **Team 0 (Blue)** - controlled by you
- Bot will be on **Team 1 (Orange)** - controlled by the trained model
- Match starts automatically

## What to Look For

### Aerial-Focused Training Indicators
Since the bot was trained with aerial-focused rewards, watch for:
- ✅ **Higher touches** - Ball contacts at higher altitudes
- ✅ **Aerial attempts** - Bot going for aerials
- ✅ **Better ball control** - Improved approach and touches
- ✅ **Boost management** - Efficient boost usage
- ✅ **Touch height** - Touches above 200 units (good aerial training)

### Performance Metrics
- **Touch Height**: Should be 200-250+ units (from wandb metrics)
- **In Air Ratio**: Should be 0.5-0.6 (50-60% time in air)
- **Speed**: Should be 1100-1200 units/s
- **Ball Control**: Good approach and touch quality

## Troubleshooting

### Bot Doesn't Appear
1. **Check RLBot Console** - Look for error messages
2. **Verify Executable Path** - Ensure `rlbot/GigaLearnBot.cfg` points to `../build/RLBotExample.exe`
3. **Check Port** - Verify port `42653` matches in both:
   - `rlbot/port.cfg`
   - `src/RLBotExample.cpp` (line 125)
4. **Check Console Output** - Bot should print:
   ```
   Loading checkpoint from: ...
   Observation size: ...
   Starting RLBot client on port 42653
   ```

### Bot Not Moving
1. **Check Checkpoint** - Verify checkpoint folder exists and has all `.lt` files
2. **Check Model Config** - Ensure model architecture matches training config:
   - Shared Head: `{256, 256}`, `addOutputLayer = false`
   - Policy: `{256, 256, 256}`
3. **Check Observation Size** - Should match training (console will show this)

### Connection Issues
1. **Port Conflict** - Make sure nothing else is using port `42653`
2. **RLBot Not Running** - Ensure RLBot framework is started before bot
3. **Firewall** - Check if Windows Firewall is blocking the connection

## Manual Bot Testing (Without RLBot GUI)

If you want to test the bot manually:

```powershell
cd build
.\RLBotExample.exe
```

This will:
- Load the latest checkpoint automatically
- Start the RLBot client on port 42653
- Wait for RLBot framework to connect

**Note**: You still need RLBot framework running separately for this to work.

## Using a Specific Checkpoint

To test a different checkpoint (not the latest):

```powershell
cd build
.\RLBotExample.exe checkpoints\9555006976
```

Replace `9555006976` with the checkpoint timesteps you want to test.

## Configuration Summary

### Training Configuration (ExampleMain.cpp)
- **Tick Skip**: 8
- **Action Delay**: 7
- **Port**: 42653 (for RLBot)
- **Checkpoint Save**: Every 150 iterations (15M timesteps)

### Bot Configuration (RLBotExample.cpp)
- **Tick Skip**: 8 (must match training)
- **Action Delay**: 7 (must match training)
- **Port**: 42653 (must match rlbot/port.cfg)
- **Model Architecture**: Must match training config exactly

## Quick Reference

### File Locations
- **Checkpoints**: `build/checkpoints/{timesteps}/`
- **RLBot Config**: `rlbot/rlbot.cfg`
- **Bot Config**: `rlbot/GigaLearnBot.cfg`
- **Bot Executable**: `build/RLBotExample.exe`
- **Port Config**: `rlbot/port.cfg`

### Key Commands
```powershell
# Find latest checkpoint
Get-ChildItem build\checkpoints | Sort-Object Name -Descending | Select-Object -First 1

# Test bot manually
cd build
.\RLBotExample.exe

# Test specific checkpoint
.\RLBotExample.exe checkpoints\9597633024
```

## Notes for Future Reference

1. **Always match training config** - Bot config must exactly match training config (tickSkip, actionDelay, model architecture)
2. **Checkpoint location** - Bot looks for checkpoints in `build/checkpoints/` relative to executable
3. **Port must match** - Port in `rlbot/port.cfg` must match `src/RLBotExample.cpp`
4. **Human vs Bot setup** - Set `participant_type_0 = human` and `participant_type_1 = rlbot` in `rlbot/rlbot.cfg`
5. **Bot config file** - Use `GigaLearnBot.cfg` (not `CppPythonAgent.cfg`) for the trained bot

