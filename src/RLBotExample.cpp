#include "RLBotClient.h"

#include <RLGymCPP/OBSBuilders/AdvancedObs.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include <RLGymCPP/Gamestates/GameState.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <GigaLearnCPP/Util/ModelConfig.h>

#include <filesystem>
#include <iostream>
#include <rlbot/platform.h>

using namespace GGL;
using namespace RLGC;

int main(int argc, char* argv[]) {
	// Get the executable directory to find checkpoints relative to it
	std::filesystem::path exeDir = rlbot::platform::GetExecutableDirectory();
	
	// Initialize RocketSim with collision meshes
	// Try relative path first, then absolute fallback
	std::filesystem::path collisionPath = exeDir / ".." / "collision_meshes";
	if (!std::filesystem::exists(collisionPath)) {
		collisionPath = "C:\\Users\\thark\\OneDrive\\Desktop\\GitHubStuff\\GigaLearnCPP\\collision_meshes";
	}
	RocketSim::Init(collisionPath.string());

	// Path to your checkpoint folder
	// Replace this with the path to your desired checkpoint (e.g., "6546944" for the latest)
	std::filesystem::path checkpointPath;
	
	// Skip -dll-path and its value if present (RLBot passes these)
	int argStart = 1;
	if (argc > 1 && std::string(argv[1]) == "-dll-path") {
		argStart = 3; // Skip -dll-path and its value
	}
	
	if (argc > argStart) {
		// If checkpoint path provided as argument (after -dll-path if present)
		checkpointPath = argv[argStart];
		// Make it absolute if it's relative
		if (checkpointPath.is_relative()) {
			checkpointPath = exeDir / checkpointPath;
		}
	} else {
		// Default: Use the most recent checkpoint in build/checkpoints (relative to executable)
		std::filesystem::path checkpointsDir = exeDir / "checkpoints";
		int64_t highest = -1;
		
		// Find the highest numbered checkpoint folder
		for (const auto& entry : std::filesystem::directory_iterator(checkpointsDir)) {
			if (entry.is_directory()) {
				try {
					int64_t timesteps = std::stoll(entry.path().filename().string());
					if (timesteps > highest) {
						highest = timesteps;
						checkpointPath = entry.path();
					}
				} catch (...) {
					// Skip non-numeric folder names
				}
			}
		}
		
		if (highest == -1) {
			std::cerr << "ERROR: No checkpoints found in " << checkpointsDir << std::endl;
			std::cerr << "Searched in: " << std::filesystem::absolute(checkpointsDir) << std::endl;
			std::cerr << "Please train a model first or specify a checkpoint path as an argument." << std::endl;
			return 1;
		}
	}

	std::cout << "Loading checkpoint from: " << checkpointPath << std::endl;
	std::cout << "Full path: " << std::filesystem::absolute(checkpointPath) << std::endl;

	// Create the observation builder and action parser (must match your training setup!)
	AdvancedObs* obsBuilder = new AdvancedObs();
	DefaultAction* actionParser = new DefaultAction();

	// Get observation size by creating a proper Arena and GameState (must match training setup!)
	// This matches exactly how the training code determines observation size
	auto arena = Arena::Create(GameMode::SOCCAR);
	arena->AddCar(Team::BLUE);
	arena->AddCar(Team::ORANGE);  // 1v1 setup matching training
	
	KickoffState* stateSetter = new KickoffState();
	stateSetter->ResetArena(arena);
	
	GameState testState = GameState(arena);
	obsBuilder->Reset(testState);
	FList testObs = obsBuilder->BuildObs(testState.players[0], testState);
	int obsSize = testObs.size();
	
	std::cout << "Observation size: " << obsSize << std::endl;
	
	// Cleanup temporary objects
	delete stateSetter;
	delete arena;

	// Model configuration (must match your training configuration!)
	PartialModelConfig sharedHeadConfig = {};
	sharedHeadConfig.layerSizes = { 256, 256 };
	sharedHeadConfig.activationType = ModelActivationType::RELU;
	sharedHeadConfig.optimType = ModelOptimType::ADAM;
	sharedHeadConfig.addLayerNorm = true;
	sharedHeadConfig.addOutputLayer = false; // Shared head doesn't have output layer

	PartialModelConfig policyConfig = {};
	policyConfig.layerSizes = { 256, 256, 256 };
	policyConfig.activationType = ModelActivationType::RELU;
	policyConfig.optimType = ModelOptimType::ADAM;
	policyConfig.addLayerNorm = true;

	// Create InferUnit to load and use the model
	// Set useGPU to true if you want GPU inference (faster), false for CPU
	bool useGPU = true;
	InferUnit* inferUnit = new InferUnit(
		obsBuilder, obsSize, actionParser,
		sharedHeadConfig, policyConfig,
		checkpointPath, useGPU
	);

	// Set up RLBot parameters
	RLBotParams params = {};
	params.port = 42653;  // Match rlbot/port.cfg
	params.tickSkip = 8;  // Must match your training tickSkip
	params.actionDelay = 7;  // Must match your training actionDelay
	params.inferUnit = inferUnit;

	std::cout << "Starting RLBot client on port " << params.port << std::endl;
	std::cout << "Make sure RLBot is running and ready to accept bots!" << std::endl;

	// Start the RLBot client (this will block until you stop it)
	RLBotClient::Run(params);

	// Cleanup
	delete inferUnit;
	delete obsBuilder;
	delete actionParser;

	return 0;
}

