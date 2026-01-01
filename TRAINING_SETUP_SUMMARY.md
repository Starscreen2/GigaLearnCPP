# ?? Complete Summary: Training Your Rocket League Bot with GigaLearnCPP

## ?? **Current Status**

You've been working on setting up **GigaLearnCPP**, a fast C++ machine learning framework for training Rocket League bots using PPO (Proximal Policy Optimization). Here's where you are now:

---

## ? **What You've Successfully Completed**

### 1. **Project Setup**
- ? **Forked and cloned** the GigaLearnCPP repository
- ? **Collision meshes obtained** and placed at:
  - `C:\Users\thark\OneDrive\Desktop\GitHubStuff\GigaLearnCPP\collision_meshes\soccar\`
  - These are **essential** for RocketSim physics simulation

### 2. **CUDA Installation**
- ? **CUDA 12.8 installed** and verified working
- ? Command `nvcc --version` confirms CUDA 12.8 is operational
- ? CUDA installed at: `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8`

### 3. **LibTorch Downloads**
You have **two versions** of LibTorch downloaded:
- ? **CUDA 12.8 version**: `libtorch-win-shared-with-deps-2.9.1+cu128` in Downloads folder
- ? **CPU version**: `libtorch-win-shared-with-deps-2.4.0+cpu` in Downloads folder

### 4. **Visual Studio Installation**
- ? **VS 2022 Community** just installed with **Desktop development with C++** workload
- ?? **VS 2026 Insiders** also installed (causing compatibility issues)

### 5. **Code Configuration**
Your `src/ExampleMain.cpp` is configured with:
- ? **Collision mesh path** correctly set
- ? **1v1 training setup** (1 player per team)
- ? **Comprehensive reward system** for scoring behavior
- ? **Training parameters** configured (256 games, 50k timesteps per iteration)
- ?? **Currently set to CPU mode** (needs GPU enabled)

---

## ? **Current Blockers**

### **Main Issue: Compiler Incompatibility**

**The Problem:**
- Visual Studio **2026 Insiders** (v143) is **too new** for LibTorch 2.4/2.9
- You get `error C3861: '_addcarry_u64': identifier not found` when building
- LibTorch's CUDA detection can't find CUDA libraries even though CUDA 12.8 is installed

**Why This Happens:**
- LibTorch 2.4 doesn't fully support VS 2026 Insiders
- PyTorch/LibTorch is officially tested with **VS 2022 (stable)**
- The `_addcarry_u64` intrinsic changed in newer MSVC versions

---

## ?? **What Needs to Happen Next**

### **Immediate Next Steps (After VS 2022 Installation Completes):**

### **1. Restart Your Computer**
- **Why:** Ensures VS 2022 is fully registered and CMake can detect it
- **Important:** This will make CMake prefer VS 2022 over VS 2026

### **2. Install CUDA 12.8 LibTorch**
```powershell
cd "C:\Users\thark\OneDrive\Desktop\GitHubStuff\GigaLearnCPP\GigaLearnCPP"
Remove-Item -Recurse -Force "libtorch"
Copy-Item -Recurse -Force "C:\Users\thark\Downloads\libtorch-win-shared-with-deps-2.9.1+cu128\libtorch" "."
```

### **3. Enable GPU Mode in Code**
Edit `src/ExampleMain.cpp` line 108:
```cpp
// Change this line:
cfg.deviceType = LearnerDeviceType::CPU;

// To this:
cfg.deviceType = LearnerDeviceType::GPU_CUDA;
```

### **4. Clean and Rebuild**
```powershell
cd "C:\Users\thark\OneDrive\Desktop\GitHubStuff\GigaLearnCPP"
Remove-Item -Recurse -Force "build"
mkdir build
cd build

# Configure with CMake
cmake -G "Ninja" .. -DCMAKE_BUILD_TYPE=Release -DPython_EXECUTABLE="C:/Users/thark/AppData/Local/Programs/Python/Python311/python.exe"

# Build
cmake --build . --config Release -j 4
```

### **5. Run Training**
```powershell
cd build
.\GigaLearnBot.exe
```

---

## ?? **File Locations Reference**

### **Project Structure:**
```
C:\Users\thark\OneDrive\Desktop\GitHubStuff\GigaLearnCPP\
??? collision_meshes\          # RocketSim physics meshes (? installed)
?   ??? soccar\
??? GigaLearnCPP\
?   ??? libtorch\               # Currently CPU version, need CUDA 12.8
??? src\
?   ??? ExampleMain.cpp         # Your training code (configured)
??? build\                      # Build directory (needs rebuilding)
??? CMakeLists.txt
```

### **Downloads:**
```
C:\Users\thark\Downloads\
??? libtorch-win-shared-with-deps-2.9.1+cu128\  # CUDA 12.8 LibTorch (NEED THIS!)
??? libtorch-win-shared-with-deps-2.4.0+cpu\    # CPU LibTorch (fallback)
```

---

## ?? **Your Training Configuration**

### **Hardware Setup:**
- **GPU:** NVIDIA (with CUDA 12.8 installed)
- **RAM:** Configured for 256 parallel games
- **Python:** 3.11.9 (64-bit)

### **Training Parameters:**
```cpp
cfg.deviceType = GPU_CUDA;           // Will be fastest
cfg.numGames = 256;                  // Parallel game instances
cfg.tickSkip = 8;                    // Physics simulation rate
cfg.ppo.tsPerItr = 50'000;          // Timesteps per iteration
cfg.ppo.epochs = 1;                  // Training epochs
cfg.ppo.policyLR = 1.5e-4;          // Learning rate
cfg.ppo.gaeGamma = 0.99;            // Reward discount
```

### **Bot Configuration:**
- **Game Mode:** SOCCAR (standard Rocket League)
- **Team Setup:** 1v1 (1 blue car vs 1 orange car)
- **Observations:** AdvancedObs (detailed game state)
- **Actions:** DefaultAction (standard RL controls)
- **Starting Position:** Kickoff positions

### **Reward System:**
Your bot will learn to:
- ? **Move towards the ball** (VelocityPlayerToBallReward: 4.0)
- ? **Hit the ball hard** (StrongTouchReward: 60.0)
- ? **Push ball to goal** (VelocityBallToGoalReward: 2.0)
- ? **Score goals** (GoalReward: 150.0) ? Highest reward!
- ? **Collect boost** (PickupBoostReward: 10.0)
- ? **Aerial play** (AirReward: 0.25)
- ? **Bump/Demo opponents** (20-80 points)

---

## ?? **Troubleshooting Guide**

### **If Build Still Fails After VS 2022 Install:**

**Error: `_addcarry_u64` not found**
- **Cause:** CMake is still using VS 2026
- **Fix:** Delete `build` folder completely and reconfigure after reboot

**Error: "Could NOT find CUDA"**
- **Cause:** CMake can't find CUDA libraries
- **Fix:** Set environment variable before building:
```powershell
$env:CUDA_PATH="C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8"
```

**Error: "Out of memory" during training**
- **Cause:** Too many parallel games or mini-batch too large
- **Fix:** Reduce values in code:
```cpp
cfg.numGames = 128;              // Reduce from 256
cfg.ppo.miniBatchSize = 25000;   // Reduce from 50000
```

**Error: RocketSim::Init fails**
- **Cause:** Collision meshes not found
- **Fix:** Verify path exists:
```powershell
Test-Path "C:\Users\thark\OneDrive\Desktop\GitHubStuff\GigaLearnCPP\collision_meshes\soccar"
```

---

## ?? **Expected Training Timeline**

Once you start training:
- **First 10 iterations:** Bot explores randomly, low rewards
- **10-50 iterations:** Bot learns to hit ball
- **50-200 iterations:** Bot starts scoring occasionally
- **200-1000 iterations:** Bot becomes competent
- **1000+ iterations:** Bot masters 1v1 play

**Time estimates:**
- **With GPU (CUDA 12.8):** ~5-10 minutes per million timesteps
- **With CPU:** ~50-100 minutes per million timesteps (10-20x slower!)

**Storage:**
- Checkpoints are saved automatically
- Expect ~500MB - 2GB of checkpoint data during training

---

## ?? **Key Commands Reference**

### **Verify Installations:**
```powershell
# Check CUDA
nvcc --version

# Check Python
python --version

# Check CMake
cmake --version

# Check VS 2022 compiler
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cl
```

### **Build Commands:**
```powershell
# Full rebuild
cd "C:\Users\thark\OneDrive\Desktop\GitHubStuff\GigaLearnCPP"
Remove-Item -Recurse -Force build
mkdir build
cd build
cmake -G "Ninja" .. -DCMAKE_BUILD_TYPE=Release -DPython_EXECUTABLE="C:/Users/thark/AppData/Local/Programs/Python/Python311/python.exe"
cmake --build . --config Release -j 4
```

### **Run Training:**
```powershell
cd build
.\GigaLearnBot.exe
```

---

## ?? **Useful Resources**

- **RocketSim Discord:** https://discord.gg/rocketsim (for help with meshes/setup)
- **PyTorch C++ Docs:** https://pytorch.org/cppdocs/
- **CUDA Downloads:** https://developer.nvidia.com/cuda-downloads
- **Original Project:** https://github.com/ZealanL/GigaLearnCPP-Leak

---

## ?? **What to Do Right Now**

1. **Wait for VS 2022 installation to complete**
2. **Restart your computer** (essential!)
3. **Come back to this chat** or start a new one
4. **Copy/paste the commands from "What Needs to Happen Next"**
5. **Start training your bot!**

---

## ?? **Pro Tips**

- **Start with CPU** to verify everything works, then switch to GPU
- **Monitor memory usage** during first training run
- **Save your code changes** before long training sessions
- **Join RocketSim Discord** for community support
- **Read the README** for reward/observation customization tips

---

## ? **Quick Copy-Paste for Next Session**

When you come back after reboot, run these in order:

```powershell
# 1. Install CUDA 12.8 LibTorch
cd "C:\Users\thark\OneDrive\Desktop\GitHubStuff\GigaLearnCPP\GigaLearnCPP"
Remove-Item -Recurse -Force "libtorch"
Copy-Item -Recurse -Force "C:\Users\thark\Downloads\libtorch-win-shared-with-deps-2.9.1+cu128\libtorch" "."

# 2. Enable GPU in code (edit line 108 of src/ExampleMain.cpp)
# Change: cfg.deviceType = LearnerDeviceType::CPU;
# To: cfg.deviceType = LearnerDeviceType::GPU_CUDA;

# 3. Build
cd "C:\Users\thark\OneDrive\Desktop\GitHubStuff\GigaLearnCPP"
Remove-Item -Recurse -Force "build"
mkdir build
cd build
cmake -G "Ninja" .. -DCMAKE_BUILD_TYPE=Release -DPython_EXECUTABLE="C:/Users/thark/AppData/Local/Programs/Python/Python311/python.exe"
cmake --build . --config Release -j 4

# 4. Run
.\GigaLearnBot.exe
```

---

**Good luck with your training! You're very close to having a working bot!** ?????
