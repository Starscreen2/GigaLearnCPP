#include <GigaLearnCPP/Learner.h>

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>
#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>
#include <RLGymCPP/TerminalConditions/GoalScoreCondition.h>
#include <RLGymCPP/OBSBuilders/DefaultObs.h>
#include <RLGymCPP/OBSBuilders/AdvancedObs.h>
#include <RLGymCPP/OBSBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/StateSetters/RandomState.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include <RLGymCPP/CommonValues.h>
#include "CustomRewards.h"
#include <chrono>
#include <sstream>
#include <iomanip>

using namespace GGL; // GigaLearn
using namespace RLGC; // RLGymCPP

// Create the RLGymCPP environment for each of our games
EnvCreateResult EnvCreateFunc(int index) {
	// Dribble-to-airdribble focused rewards: get ball on car, double jump, multi-touch airdribble facing goal
	std::vector<WeightedReward> rewards = {

		// Dribble-focused rewards (NEW)
		{ new DribbleReward(100.0f, 150.0f, 0.5f), 20.0f },  // Ball on car (ground/wall): height threshold, distance threshold, interval
		{ new DoubleJumpAirdribbleReward(1.5f), 60.0f },  // Double jump during dribble: time window
		{ new MultiTouchAirdribbleReward(200.0f), 30.0f },  // Multi-touch airdribble facing goal: max distance

		// General aerial play (reduced)
		{ new AerialTouchReward(0.5f), 2.0f },  // Reduced from 10.0f - keep minimal for general aerial play

		// Player-ball (helpful for initial approach)
		{ new FaceBallReward(), 0.5f },  // Keep for positioning
		{ new VelocityPlayerToBallReward(), 6.0f },  // Keep for initial ball approach
		{ new StrongTouchReward(30, 150), 40 },  // Reduced from 120 - less focus on powerful hits, more on control
		{ new TouchAccelReward(), 20 },  // Reduced from 60 - less focus on power

		// Ball-goal
		{ new ZeroSumReward(new VelocityBallToGoalReward(), 1), 3.0f },  // Keep for general goal orientation
		{ new ShotReward(), 60 },  // Keep - airdribbles often lead to shots

		// Boost (critical for airdribbles)
		{ new PickupBoostReward(), 15.f },  // Keep - critical for airdribbles
		{ new SaveBoostReward(), 0.5f },  // Keep

		// Game events
		{ new ZeroSumReward(new BumpReward(), 0.5f), 20 },
		{ new ZeroSumReward(new DemoReward(), 0.5f), 80 },
		{ new GoalReward(), 250 },  // Keep - still important
	};

	std::vector<TerminalCondition*> terminalConditions = {
		new NoTouchCondition(10),
		new GoalScoreCondition()
	};

	// Make the arena
	int playersPerTeam = 1;  // 1v1 training
	auto arena = Arena::Create(GameMode::SOCCAR);
	for (int i = 0; i < playersPerTeam; i++) {
		arena->AddCar(Team::BLUE);
		arena->AddCar(Team::ORANGE);
	}

	EnvCreateResult result = {};
	result.actionParser = new DefaultAction();
	// Use AdvancedObsPadded to support both 1v1 and 2v2 with the same model
	// maxPlayers = 3 means: up to 2 teammates (for 3v3) and 3 opponents
	// For 1v1: pads to 2 opponents, for 2v2: 1 teammate + 2 opponents (all filled)
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

				// Aerial-specific metrics
				if (!player.isOnGround) {
					report.AddAvg("Player/Air Time", 1.0f); // Track time in air
					if (player.ballTouchedStep) {
						report.AddAvg("Player/Aerial Touch Ratio", 1.0f); // Touch while in air
					} else {
						report.AddAvg("Player/Aerial Touch Ratio", 0.0f);
					}
				} else {
					report.AddAvg("Player/Air Time", 0.0f);
					report.AddAvg("Player/Aerial Touch Ratio", 0.0f);
				}

				// Dribble metrics (NEW)
				Vec carToBall = state.ball.pos - player.pos;
				Vec localCarToBall = player.rotMat.Dot(carToBall);
				bool isDribbling = (localCarToBall.z > 100.0f) && (carToBall.Length() < 150.0f);
				if (isDribbling) {
					report.AddAvg("Player/Dribble Time", 1.0f);
					// Check if on wall (car not upright)
					bool isOnWall = player.isOnGround && (player.rotMat.up.z < 0.7f);
					if (isOnWall) {
						report.AddAvg("Player/Wall Dribble Time", 1.0f);
					} else {
						report.AddAvg("Player/Wall Dribble Time", 0.0f);
					}
				} else {
					report.AddAvg("Player/Dribble Time", 0.0f);
					report.AddAvg("Player/Wall Dribble Time", 0.0f);
				}

				// Airdribble metrics (NEW)
				if (!player.isOnGround && player.hasDoubleJumped) {
					float distToBall = carToBall.Length();
					bool ballClose = distToBall < 200.0f;
					bool ballAbove = (state.ball.pos.z > player.pos.z - 50.0f);
					if (ballClose && ballAbove) {
						// Check goal-facing alignment
						Vec targetGoal = (player.team == Team::BLUE) ? 
							CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
						Vec dirToGoal = (targetGoal - player.pos).Normalized();
						float goalAlignment = player.rotMat.forward.Dot(dirToGoal);
						report.AddAvg("Player/Airdribble Goal Alignment", goalAlignment);
					}
				}

				// Touch metrics
				if (player.ballTouchedStep) {
					report.AddAvg("Player/Touch Height", state.ball.pos.z);
					
					// Calculate touch force if we have previous state
					if (state.prev) {
						Vec ballVelChange = state.ball.vel - state.prev->ball.vel;
						float touchForce = ballVelChange.Length();
						report.AddAvg("Player/Average Touch Force", touchForce);
					}
				}
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
	// Path to the collision_meshes folder in your project directory
	RocketSim::Init("C:\\Users\\thark\\OneDrive\\Desktop\\GitHubStuff\\GigaLearnCPP\\collision_meshes");

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
	cfg.numGames = 1024;  // Stable value that works well with 64GB RAM for 1v1 training

	// Leave this empty to use a random seed each run
	// The random seed can have a strong effect on the outcome of a run
	cfg.randomSeed = 123;

	// Timesteps per iteration - stable configuration
	int tsPerItr = 150'000;  // Stable value that works well
	cfg.ppo.tsPerItr = tsPerItr;
	cfg.ppo.batchSize = tsPerItr;
	cfg.ppo.miniBatchSize = 150'000; // Stable value for GPU utilization

	// Using 2 epochs seems pretty optimal when comparing time training to skill
	// Perhaps 1 or 3 is better for you, test and find out!
	cfg.ppo.epochs = 1;

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
	cfg.metricsProjectName = "gigalearncpp-aerial"; // More descriptive project name
	cfg.metricsGroupName = "aerial-focused-training"; // Group for this training run
	
	// Wandb run behavior:
	// - resumeWandbRun = false: Start new run each time (separate experiments, graphs reset)
	// - resumeWandbRun = true: Resume same run from checkpoint (continuous graphs)
	cfg.resumeWandbRun = false; // Set to true for continuous graphs across training sessions
	
	if (cfg.resumeWandbRun) {
		// Use fixed run name when resuming (will resume the same run)
		cfg.metricsRunName = "aerial-bot-continuous";
	} else {
		// Generate unique run name with timestamp for new runs
		auto now = std::chrono::system_clock::now();
		auto time_t = std::chrono::system_clock::to_time_t(now);
		std::stringstream ss;
		ss << "aerial-bot-" << std::put_time(std::localtime(&time_t), "%Y%m%d-%H%M%S");
		cfg.metricsRunName = ss.str(); // Unique run name with timestamp
	}
	
	cfg.renderMode = false; // Don't render

	// Checkpoint saving: Save every 150 iterations
	// With 150,000 timesteps per iteration, this saves every 22,500,000 timesteps
	cfg.tsPerSave = 150 * tsPerItr; // 150 iterations = 22,500,000 timesteps

	// Make the learner with the environment creation function and the config we just made
	Learner* learner = new Learner(EnvCreateFunc, cfg, StepCallback);

	// Start learning!
	learner->Start();

	return EXIT_SUCCESS;
}
