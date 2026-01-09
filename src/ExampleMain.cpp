#include <GigaLearnCPP/Learner.h>

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>
#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>
#include <RLGymCPP/TerminalConditions/GoalScoreCondition.h>
#include <RLGymCPP/OBSBuilders/AdvancedObs.h>
#include <RLGymCPP/OBSBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include "CustomRewards.h" // Includes CustomBasicMechanicsRewards.h and CustomKickoffRewards.h
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <unordered_map>

using namespace GGL; // GigaLearn
using namespace RLGC; // RLGymCPP

// Create the RLGymCPP environment for each of our games
EnvCreateResult EnvCreateFunc(int index) {
	// Optimized rewards for scoring and shots on target
	std::vector<WeightedReward> rewards = {

		// Movement
		{ new AirReward(), 0.25f },
		{ new KickoffSpeedFlipReward(3.0f, 1000.0f), 60.f }, // INCREASED: Encourage aggressive speed flips on kickoffs (30 → 60)
		{ new KickoffFirstTouchReward(100.0f, 8.0f, 5.0f, 60.0f), 100.f }, // Reward first touch (100), punishment matches speed flip weight (60) to encourage going for ball
		{ new GroundToAirPopReward(340.0f, 180.0f, 260.0f, 350.0f, 120.0f, 1.0f, 0.35f, 0.5f, 0.6f, 0.4f), 30.f }, // NEW: Ground→air pop & chase (non-farmable, no goal bonus, dynamic scaling)
		{ new GroundDribbleJumpReward(340.0f, 180.0f, 260.0f, 350.0f, 200.0f, 0.5f, 1.0f, 0.8f), 10.f }, // LOWERED: Ground dribble → aerial transition (reduced to emphasize air dribbles)
		{ new PowerslideReward(500.0f, 1.0f), 3.f },  // Low weight: Keep basic mechanics but don't prioritize over aerial play
		{ new HalfFlipReward(1.0f, 300.0f), 3.f },    // Low weight: Keep basic mechanics but don't prioritize over aerial play
		{ new CustomWavedashReward(0.3f, 400.0f, 2.0f), 4.f },  // Low weight: Remember wavedash for recovery
		{ new DirectionalFlipReward(600.0f, 1.5f), 3.f },  // Low weight: Remember directional flips for speed
		{ new FastAerialReward(400.0f, 3.0f), 4.f },  // Low weight: Remember fast aerials for quick intercepts
		{ new RecoveryLandingReward(0.5f, 300.0f), 3.f },  // Low weight: Remember proper landing after aerials
		{ new LandOnBoostReward(0.3f, 200.0f, 2.0f), 4.f },  // Low weight: Remember to land on boost pads for efficient recovery

		// Player-ball
		{ new FaceBallReward(), 0.25f },
		{ new VelocityPlayerToBallReward(), 1.0f },  // Low weight: Encourage moving toward ball without rushing
		{ new StrongTouchReward(), 1.0f },           // Low weight: Encourage powerful touches without conflicting with air dribbles
		{ new TouchAccelReward(), 1.0f },            // Low weight: Encourage speeding up ball without delicate control conflicts

		// Boost - enhanced collection
		{ new PickupBoostReward(), 12.f },  // Increased from 8 to encourage more collection
		{ new BigBoostReward(), 35.f },     // Increased from 25 to prioritize big boosts
		{ new BoostPadProximityReward(2000.0f, 30.0f), 18.f }, // NEW: Reward moving toward pads when low
		{ new BoostEfficiencyReward(), 12.f }, // NEW: Reward collecting when boost is needed
		{ new SaveBoostReward(), 1.0f },    // Low weight: Encourage boost conservation without conflicting with air dribbles

		// Physical play - encourage strategic bumps and demos
		{ new BumpReward(), 4.0f },          // Encourage strategic bumps to disrupt opponents
		{ new DemoReward(), 20.0f },         // Encourage demos for tactical advantage (5x bump reward)

		// Scoring rewards - REDUCED to prioritize air dribbles
		{ new ShotReward(), 2.0f },          // Low weight: Encourage shots without overshadowing air dribbles
		{ new GoalReward(), 350 },
		{ new OpenNetConcedePunishment(3.0f), 40.f }, // NEW: Punish conceding open-net goals
		
		// Own goal punishment
		{ new OwnGoalPunishment(), 50.f },
		
		// Air dribble rewards - focus on air dribbling mechanics
		{ new AirDribbleReward(0.5f, 500.0f), 140.f },  // Combined: Main + Boost alignment (80 + 60 = 140)
		{ new AirDribbleSetupReward(2.0f, 0.3f), 35.f }, // Setup phase reward (ground/wall touches)
		{ new AirDribbleStartReward(3000.0f), 40.f },     // First aerial touch reward
		{ new AirDribbleDistanceReward(3.0f), 100.f },   // Distance-based reward (includes 5x goal bonus)
		{ new AirDribbleGoalCountReward(), 1.0f }        // Metric-only: counts air-dribble goals (1 per goal)
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
	
	// Randomize team assignment to prevent team bias
	// This ensures the model trains equally on both teams and index positions
	bool blueFirst = (RocketSim::Math::RandInt(0, 2) == 0);
	
	for (int i = 0; i < playersPerTeam; i++) {
		if (blueFirst) {
			arena->AddCar(Team::BLUE);
			arena->AddCar(Team::ORANGE);
		} else {
			arena->AddCar(Team::ORANGE);
			arena->AddCar(Team::BLUE);
		}
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

	// Track kickoff state per player (for speed tracking)
	static std::unordered_map<int, bool> inKickoff;
	static std::unordered_map<int, float> kickoffStartTime;
	constexpr float maxKickoffTime = 3.0f; // Match KickoffSpeedFlipReward default

	// Add our metrics
	for (auto& state : states) {
		// Detect kickoff: ball at center, low velocity (same logic as KickoffSpeedFlipReward)
		bool ballAtCenter = (state.ball.pos.Length() < 500.0f && abs(state.ball.pos.z) < 100.0f);
		bool ballStationary = state.ball.vel.Length() < 100.0f;

		if (doExpensiveMetrics) {
			for (auto& player : state.players) {
				int carId = player.carId;

				// Track kickoff state per player
				if (ballAtCenter && ballStationary) {
					inKickoff[carId] = true;
					kickoffStartTime[carId] = 0.0f;
				} else if (inKickoff[carId]) {
					kickoffStartTime[carId] += state.deltaTime;
					if (kickoffStartTime[carId] > maxKickoffTime || state.ball.vel.Length() > 500.0f) {
						inKickoff[carId] = false;
					}
				}

				// Basic metrics
				report.AddAvg("Player/In Air Ratio", !player.isOnGround);
				report.AddAvg("Player/Ball Touch Ratio", player.ballTouchedStep);
				report.AddAvg("Player/Demoed Ratio", player.isDemoed);

				// Speed metrics
				report.AddAvg("Player/Speed", player.vel.Length());
				Vec dirToBall = (state.ball.pos - player.pos).Normalized();
				report.AddAvg("Player/Speed Towards Ball", RS_MAX(0, player.vel.Dot(dirToBall)));

				// Kickoff speed tracking (only during kickoff phase and on ground)
				if (inKickoff[carId] && player.isOnGround) {
					float kickoffSpeed = player.vel.Length();
					report.AddAvg("Kickoff/Speed", kickoffSpeed);
				}

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
	cfg.checkpointsToKeep = 5000; // Keep last 5000 checkpoints (~31.25 GB)

	// Make the learner with the environment creation function and the config we just made
	Learner* learner = new Learner(EnvCreateFunc, cfg, StepCallback);

	// Start learning!
	learner->Start();

	return EXIT_SUCCESS;
}
