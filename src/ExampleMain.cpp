#include <GigaLearnCPP/Learner.h>

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>
#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>
#include <RLGymCPP/TerminalConditions/GoalScoreCondition.h>
#include <RLGymCPP/OBSBuilders/AdvancedObs.h>
#include <RLGymCPP/OBSBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include "CustomRewards.h"
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>

using namespace GGL; // GigaLearn
using namespace RLGC; // RLGymCPP

// Create the RLGymCPP environment for each of our games
EnvCreateResult EnvCreateFunc(int index) {
	// Optimized rewards for scoring and shots on target
	std::vector<WeightedReward> rewards = {

		// Movement
		{ new AirReward(), 0.25f },
		{ new KickoffSpeedFlipReward(3.0f, 1000.0f), 30.f }, // NEW: Encourage speed flips on kickoffs

		// Player-ball
		{ new FaceBallReward(), 0.25f },
		{ new VelocityPlayerToBallReward(), 4.f },
		{ new StrongTouchReward(20, 100), 30 },  // Reduced from 50 to 30 - avoid conflict with air dribble control

		// Boost - enhanced collection
		{ new PickupBoostReward(), 12.f },  // Increased from 8 to encourage more collection
		{ new BigBoostReward(), 35.f },     // Increased from 25 to prioritize big boosts
		{ new BoostPadProximityReward(2000.0f, 30.0f), 18.f }, // NEW: Reward moving toward pads when low
		{ new BoostEfficiencyReward(), 12.f }, // NEW: Reward collecting when boost is needed
		{ new SaveBoostReward(), 0.2f },

		// Ball acceleration - NEW: Rewards speeding up the ball (helps with shots)
		{ new TouchAccelReward(), 30 },  // Reduced from 50 to 30 - avoid conflict with air dribble control

		// Game events - REDUCED to focus on scoring
		{ new ZeroSumReward(new BumpReward(), 0.5f), 12 },  // Reduced from 20 → 12
		{ new ZeroSumReward(new DemoReward(), 0.5f), 40 },  // Reduced from 80 → 40

		// Scoring rewards - ENHANCED
		{ new ShotReward(), 70 },
		{ new GoalReward(), 350 },
		
		// Own goal punishment
		{ new OwnGoalPunishment(), 50.f },

		// Helper rewards - guide agent toward successful double touches (similar to OptiV2's dtap_helper/dtap_trajectory)
		{ new DoubleTouchHelperReward(300.0f, 1200.0f, 3.0f), 20 },  // Rewards first touch that sets up double touches
		{ new DoubleTouchTrajectoryReward(300.0f, 1500.0f, 100.0f, 2.0f), 12 },  // Continuous reward for good trajectory
		
		// Air dribble rewards - focus on air dribbling mechanics (DOUBLED weights to prioritize)
		{ new AirDribbleReward(0.5f), 80.f },           // Main air dribble reward (40 → 80)
		{ new AirDribbleBoostReward(500.0f), 60.f },    // Boosting toward ball (30 → 60)
		{ new AirDribbleSetupReward(2.0f, 0.3f), 35.f }, // Setup phase reward (ground/wall touches)
		{ new AirDribbleStartReward(3000.0f), 40.f },   // First aerial touch reward (20 → 40)
		{ new AirDribbleDistanceReward(3.0f), 100.f }   // Distance-based reward (50 → 100)
	};

	std::vector<TerminalCondition*> terminalConditions = {
		new NoTouchCondition(10),
		new GoalScoreCondition()
	};

	// Make the arena
	// You can change playersPerTeam to train for different team sizes (1v1, 2v2, 3v3)
	// The AdvancedObsPadded will handle padding so the observation size stays consistent
	int playersPerTeam = 1;  // 1v1 training (change to 2 for 2v2, 3 for 3v3)
	auto arena = Arena::Create(GameMode::SOCCAR);
	for (int i = 0; i < playersPerTeam; i++) {
		arena->AddCar(Team::BLUE);
		arena->AddCar(Team::ORANGE);
	}

	EnvCreateResult result = {};
	result.actionParser = new DefaultAction();
	// Use AdvancedObsPadded with maxPlayers=3 to support 1v1, 2v2, and 3v3
	// maxPlayers=3 means: up to 2 teammates (for 3v3) and 3 opponents
	result.obsBuilder = new AdvancedObsPadded(3);
	result.stateSetter = new KickoffState();
	result.terminalConditions = terminalConditions;
	result.rewards = rewards;

	result.arena = arena;

	return result;
}

void StepCallback(Learner* learner, const std::vector<GameState>& states, Report& report) {
	// To prevent expensive metrics from eating at performance, we will only run them on 1/4th of steps
	// This doesn't really matter unless you have expensive metrics (which this example doesn't)
	bool doExpensiveMetrics = (rand() % 4) == 0;

	// Track goals for this iteration
	int blueGoals = 0;
	int orangeGoals = 0;

	// Add our metrics
	for (auto& state : states) {
		if (doExpensiveMetrics) {
			for (auto& player : state.players) {
				// Basic metrics
				report.AddAvg("Player/In Air Ratio", !player.isOnGround);
				report.AddAvg("Player/Ball Touch Ratio", player.ballTouchedStep);
				report.AddAvg("Player/Demoed Ratio", player.isDemoed);

				// Speed metrics
				report.AddAvg("Player/Speed", player.vel.Length());
				Vec dirToBall = (state.ball.pos - player.pos).Normalized();
				report.AddAvg("Player/Speed Towards Ball", RS_MAX(0, player.vel.Dot(dirToBall)));

				// Boost metrics
				report.AddAvg("Player/Boost", player.boost);

				// Touch metrics
				if (player.ballTouchedStep)
					report.AddAvg("Player/Touch Height", state.ball.pos.z);
			}
		}

		// Goal tracking
		if (state.goalScored) {
			report.AddAvg("Game/Goal Speed", state.ball.vel.Length());
			
			// Determine which team scored (ball in opponent's half when goal scored)
			bool blueScored = (state.ball.pos.y > 0); // Ball in orange half = blue scored
			if (blueScored) {
				blueGoals++;
			} else {
				orangeGoals++;
			}
		}
	}
	
	// Add total goals for this iteration
	if (blueGoals > 0 || orangeGoals > 0) {
		report.Add("Game/Blue Goals", blueGoals);
		report.Add("Game/Orange Goals", orangeGoals);
	}
}

int main(int argc, char* argv[]) {
	// Initialize RocketSim with collision meshes
	// Try relative path first, then absolute fallback (more portable)
	std::filesystem::path collisionPath = "collision_meshes";
	if (!std::filesystem::exists(collisionPath)) {
		// Fallback to absolute path if relative doesn't work
		collisionPath = "C:\\Users\\thark\\OneDrive\\Desktop\\GitHubStuff\\GigaLearnCPP\\collision_meshes";
	}
	RocketSim::Init(collisionPath);

	// Make configuration for the learner
	LearnerConfig cfg = {};

	// Device selection:
	// GPU_CUDA - NVIDIA GPU with CUDA (fastest, requires CUDA toolkit)
	// GPU_DIRECT_ML - Any GPU on Windows (slower than CUDA but works on AMD/Intel)
	// CPU - CPU only (slowest, but always works)
	cfg.deviceType = LearnerDeviceType::GPU_CUDA;

	cfg.tickSkip = 8;
	cfg.actionDelay = cfg.tickSkip - 1; // Normal value in other RLGym frameworks

	// Number of parallel game instances
	// More games = faster training but more RAM usage
	// 128 for 16GB RAM, 256 for 32GB RAM, 512-1024 for 64GB RAM
	// Kept at 256 to prevent "bad allocation" errors during long training
	cfg.numGames = 256;  // Kept at 256 to avoid memory issues

	// Leave this empty to use a random seed each run
	// The random seed can have a strong effect on the outcome of a run
	cfg.randomSeed = 123;

	// Timesteps per iteration - increased for better sample efficiency
	int tsPerItr = 100'000;  // Doubled from 50k for better sample efficiency
	cfg.ppo.tsPerItr = tsPerItr;
	cfg.ppo.batchSize = tsPerItr;  // Must match tsPerItr

	// True minibatching - enables 4 minibatches per batch for better GPU utilization
	cfg.ppo.miniBatchSize = 25'000;  // 4 minibatches per batch (100k / 25k = 4)

	// Using 2 epochs is optimal for sample efficiency (trains on same data twice)
	cfg.ppo.epochs = 2;  // Increased from 1 for better sample efficiency

	// This scales differently than "ent_coef" in other frameworks
	// This is the scale for normalized entropy, which means you won't have to change it if you add more actions
	cfg.ppo.entropyScale = 0.035f;

	// Rate of reward decay
	// Starting low tends to work out
	cfg.ppo.gaeGamma = 0.99;

	// Good learning rate to start
	cfg.ppo.policyLR = 1.5e-4;
	cfg.ppo.criticLR = 1.5e-4;

	cfg.ppo.sharedHead.layerSizes = { 256, 256 };
	cfg.ppo.policy.layerSizes = { 256, 256, 256 };
	cfg.ppo.critic.layerSizes = { 256, 256, 256 };

	auto optim = ModelOptimType::ADAM;
	cfg.ppo.policy.optimType = optim;
	cfg.ppo.critic.optimType = optim;
	cfg.ppo.sharedHead.optimType = optim;

	auto activation = ModelActivationType::RELU;
	cfg.ppo.policy.activationType = activation;
	cfg.ppo.critic.activationType = activation;
	cfg.ppo.sharedHead.activationType = activation;

	bool addLayerNorm = true;
	cfg.ppo.policy.addLayerNorm = addLayerNorm;
	cfg.ppo.critic.addLayerNorm = addLayerNorm;
	cfg.ppo.sharedHead.addLayerNorm = addLayerNorm;

	cfg.sendMetrics = true; // Send metrics
	cfg.metricsProjectName = "gigalearncpp"; // Project name
	cfg.metricsGroupName = "basic-rewards"; // Group for this training run
	
	cfg.resumeWandbRun = false; // Set to true for continuous graphs across training sessions
	
	if (cfg.resumeWandbRun) {
		// Use fixed run name when resuming (will resume the same run)
		cfg.metricsRunName = "basic-bot-continuous";
	} else {
		// Generate unique run name with timestamp for new runs
		auto now = std::chrono::system_clock::now();
		auto time_t = std::chrono::system_clock::to_time_t(now);
		std::stringstream ss;
		ss << "basic-bot-" << std::put_time(std::localtime(&time_t), "%Y%m%d-%H%M%S");
		cfg.metricsRunName = ss.str(); // Unique run name with timestamp
	}
	
	cfg.renderMode = false;

	// Checkpoint saving: Save every 150 iterations
	// With 50,000 timesteps per iteration, this saves every 7,500,000 timesteps
	cfg.tsPerSave = 150 * tsPerItr; // 150 iterations = 7,500,000 timesteps
	cfg.checkpointsToKeep = 2000; // Keep last 2000 checkpoints (~12.5 GB)

	// Make the learner with the environment creation function and the config we just made
	Learner* learner = new Learner(EnvCreateFunc, cfg, StepCallback);

	// Start learning!
	learner->Start();

	return EXIT_SUCCESS;
}
