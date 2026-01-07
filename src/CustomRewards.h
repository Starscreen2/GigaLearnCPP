#pragma once
#include <RLGymCPP/Rewards/Reward.h>
#include <RLGymCPP/Gamestates/GameState.h>
#include <unordered_map>

namespace RLGC {

	// Helper reward for touches that set up double touches
	// Rewards the first touch in a potential double-touch sequence
	// Guides the agent to make touches that enable follow-up double touches
	class DoubleTouchHelperReward : public Reward {
	private:
		float minHeightForBonus; // Minimum height to reward (default 300 units)
		float maxHeightForBonus; // Maximum height for max bonus (default 1200 units)
		float maxTimeWindow; // Time window to consider this a "setup" touch (default 3.0s)
		
		struct SetupInfo {
			float timeSinceTouch;
			Vec touchPos;
			Vec touchVel;
			bool hasSetupTouch;
			bool wasAerial;
		};
		std::unordered_map<int, SetupInfo> setupInfo;

	public:
		DoubleTouchHelperReward(float minHeight = 300.0f, float maxHeight = 1200.0f, float maxTime = 3.0f)
			: minHeightForBonus(minHeight), maxHeightForBonus(maxHeight), maxTimeWindow(maxTime) {}

		virtual void Reset(const GameState& initialState) override {
			setupInfo.clear();
			for (const auto& player : initialState.players) {
				setupInfo[player.carId] = { 0.0f, Vec(), Vec(), false, false };
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			int carId = player.carId;
			SetupInfo& info = setupInfo[carId];

			// Check if this is a new touch
			if (player.ballTouchedStep) {
				// Calculate reward for this setup touch
				float baseReward = 0.2f;
				
				// Height bonus (higher = better for double touch setup)
				float ballHeight = state.ball.pos.z;
				float heightScore = 0.0f;
				if (ballHeight >= minHeightForBonus) {
					if (ballHeight >= maxHeightForBonus) {
						heightScore = 1.0f; // Max height bonus
					} else {
						heightScore = (ballHeight - minHeightForBonus) / (maxHeightForBonus - minHeightForBonus);
					}
				}
				
				// Aerial bonus (aerial touches are better for double touches)
				float aerialBonus = (!player.isOnGround) ? 0.4f : 0.0f;
				
				// Direction toward goal bonus
				Vec targetGoal = (player.team == Team::BLUE) ? 
					CommonValues::ORANGE_GOAL_CENTER : CommonValues::BLUE_GOAL_CENTER;
				Vec dirToGoal = (targetGoal - state.ball.pos).Normalized();
				
				float directionBonus = 0.0f;
				if (state.ball.vel.Length() > FLT_EPSILON && dirToGoal.Length() > FLT_EPSILON) {
					Vec ballVelDir = state.ball.vel.Normalized();
					float goalAlignment = ballVelDir.Dot(dirToGoal);
					directionBonus = RS_MAX(0.0f, goalAlignment) * 0.3f; // Up to 0.3 bonus
				}
				
				// Upward velocity bonus (ball going up is good for double touches)
				float upwardBonus = 0.0f;
				if (state.ball.vel.z > 200.0f) {
					upwardBonus = RS_MIN(0.3f, (state.ball.vel.z - 200.0f) / 800.0f); // 200-1000 units/s
				}
				
				float reward = baseReward + (heightScore * 0.5f) + aerialBonus + directionBonus + upwardBonus;
				
				// Store touch info for tracking
				info.hasSetupTouch = true;
				info.timeSinceTouch = 0.0f;
				info.touchPos = state.ball.pos;
				info.touchVel = state.ball.vel;
				info.wasAerial = !player.isOnGround;
				
				return reward;
			} else {
				// Track time since setup touch
				if (info.hasSetupTouch) {
					info.timeSinceTouch += state.deltaTime;
					
					// Reset if too much time has passed
					if (info.timeSinceTouch > maxTimeWindow) {
						info.hasSetupTouch = false;
						info.timeSinceTouch = 0.0f;
					}
				}
			}

			return 0;
		}
	};

	// Rewards good ball trajectory that enables double touches
	// Continuous reward (not just on touches) - rewards maintaining good trajectory
	class DoubleTouchTrajectoryReward : public Reward {
	private:
		float minHeight; // Minimum height to reward (default 300 units)
		float maxHeight; // Maximum height for reward (default 1500 units)
		float minUpwardVelocity; // Minimum upward velocity to reward (default 100 units/s)
		float trajectoryDecayTime; // Time to decay reward after touch (default 2.0s)
		
		struct TrajectoryInfo {
			float timeSinceLastTouch;
			Vec lastTouchPos;
			Vec lastTouchVel;
			bool hasRecentTouch;
		};
		std::unordered_map<int, TrajectoryInfo> trajectoryInfo;

	public:
		DoubleTouchTrajectoryReward(float minH = 300.0f, float maxH = 1500.0f, 
		                            float minUpVel = 100.0f, float decayTime = 2.0f)
			: minHeight(minH), maxHeight(maxH), minUpwardVelocity(minUpVel), trajectoryDecayTime(decayTime) {}

		virtual void Reset(const GameState& initialState) override {
			trajectoryInfo.clear();
			for (const auto& player : initialState.players) {
				trajectoryInfo[player.carId] = { 0.0f, Vec(), Vec(), false };
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			int carId = player.carId;
			TrajectoryInfo& info = trajectoryInfo[carId];

			// Update touch info
			if (player.ballTouchedStep) {
				info.hasRecentTouch = true;
				info.timeSinceLastTouch = 0.0f;
				info.lastTouchPos = state.ball.pos;
				info.lastTouchVel = state.ball.vel;
			} else {
				info.timeSinceLastTouch += state.deltaTime;
				
				// Reset if too much time has passed
				if (info.timeSinceLastTouch > trajectoryDecayTime) {
					info.hasRecentTouch = false;
				}
			}

			// Only reward if we have a recent touch
			if (!info.hasRecentTouch)
				return 0;

			// Check if ball is in good position for double touch
			float ballHeight = state.ball.pos.z;
			
			// Height check
			if (ballHeight < minHeight || ballHeight > maxHeight)
				return 0;

			// Calculate height score (optimal around 600-1000 units)
			float heightScore = 0.0f;
			if (ballHeight >= minHeight && ballHeight <= maxHeight) {
				float optimalHeight = (minHeight + maxHeight) / 2.0f; // 900 units
				float distFromOptimal = abs(ballHeight - optimalHeight);
				float maxDist = (maxHeight - minHeight) / 2.0f; // 600 units
				heightScore = 1.0f - RS_MIN(1.0f, distFromOptimal / maxDist);
			}

			// Upward velocity bonus
			float upwardScore = 0.0f;
			if (state.ball.vel.z > minUpwardVelocity) {
				upwardScore = RS_MIN(1.0f, (state.ball.vel.z - minUpwardVelocity) / 500.0f); // 100-600 units/s
			}

			// Direction toward goal bonus
			Vec targetGoal = (player.team == Team::BLUE) ? 
				CommonValues::ORANGE_GOAL_CENTER : CommonValues::BLUE_GOAL_CENTER;
			Vec dirToGoal = (targetGoal - state.ball.pos).Normalized();
			
			float directionScore = 0.0f;
			if (state.ball.vel.Length() > FLT_EPSILON && dirToGoal.Length() > FLT_EPSILON) {
				Vec ballVelDir = state.ball.vel.Normalized();
				float goalAlignment = ballVelDir.Dot(dirToGoal);
				directionScore = RS_MAX(0.0f, goalAlignment) * 0.5f; // Up to 0.5 bonus
			}

			// Player proximity bonus (player should be close to follow up)
			float proximityScore = 0.0f;
			float distToBall = (state.ball.pos - player.pos).Length();
			if (distToBall < 1000.0f) {
				proximityScore = 1.0f - RS_MIN(1.0f, distToBall / 1000.0f);
			}

			// Decay reward over time since touch
			float timeDecay = 1.0f;
			if (info.timeSinceLastTouch > 0.5f) {
				timeDecay = 1.0f - RS_MIN(1.0f, (info.timeSinceLastTouch - 0.5f) / (trajectoryDecayTime - 0.5f));
			}

			float reward = (heightScore * 0.4f + upwardScore * 0.3f + directionScore * 0.2f + proximityScore * 0.1f) * timeDecay;
			
			return reward;
		}
	};

	// Rewards collecting big boost pads (100 boost) more than small pads (12 boost)
	// Encourages bot to go out of its way for big boosts
	class BigBoostReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!player.prev)
				return 0;

			// Detect boost collection
			if (player.boost > player.prev->boost) {
				float boostGained = player.boost - player.prev->boost;
				
				// Big boost pad gives 100 boost (threshold: >= 90 to account for partial collection)
				if (boostGained >= 90.0f) {
					return 2.0f; // 2x multiplier for big boost
				}
				// Small boost pad gives 12 boost
				else if (boostGained >= 10.0f) {
					return 0.5f; // Normal reward for small boost
				}
			}
			
			return 0;
		}
	};

	// Rewards moving toward available boost pads, especially when low on boost
	// Encourages bot to actively seek out boost pads
	class BoostPadProximityReward : public Reward {
	private:
		float maxDistance; // Maximum distance to consider (default 2000 units)
		float lowBoostThreshold; // Boost level below which to prioritize (default 30)
		
	public:
		BoostPadProximityReward(float maxDist = 2000.0f, float lowBoost = 30.0f)
			: maxDistance(maxDist), lowBoostThreshold(lowBoost) {}
		
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;
			
			// Only reward when boost is low (encourages seeking boost)
			if (player.boost >= lowBoostThreshold)
				return 0;
			
			float bestReward = 0.0f;
			
			// Check all boost pads
			for (int i = 0; i < CommonValues::BOOST_LOCATIONS_AMOUNT; i++) {
				// Pad must be available
				if (!state.boostPads[i])
					continue;
				
				Vec padPos = CommonValues::BOOST_LOCATIONS[i];
				float distToPad = (padPos - player.pos).Length();
				
				if (distToPad > maxDistance)
					continue;
				
				// Reward moving toward pad
				Vec dirToPad = (padPos - player.pos).Normalized();
				float speedTowardPad = player.vel.Dot(dirToPad);
				
				if (speedTowardPad > 0) {
					// Closer + faster = better
					float proximityScore = 1.0f - (distToPad / maxDistance);
					float speedScore = RS_MIN(1.0f, speedTowardPad / 1000.0f);
					
					float reward = proximityScore * speedScore;
					
					// Big pads worth more (z = 73.0 indicates big pad)
					if (padPos.z > 72.0f) {
						reward *= 2.0f;
					}
					
					bestReward = RS_MAX(bestReward, reward);
				}
			}
			
			return bestReward * 0.3f; // Scale down
		}
	};

	// Rewards collecting boost more when it's needed (low boost = more valuable)
	// Encourages efficient boost management
	class BoostEfficiencyReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!player.prev)
				return 0;
			
			if (player.boost > player.prev->boost) {
				float boostGained = player.boost - player.prev->boost;
				
				// Reward collecting when low on boost more
				float boostBefore = player.prev->boost;
				float efficiencyMultiplier = 1.0f;
				
				if (boostBefore <= 30.0f) {
					// Collecting when <= 30 boost is 3x more valuable
					efficiencyMultiplier = 3.0f;
				} else if (boostBefore < 50.0f) {
					// Collecting when < 50 boost is 2x more valuable
					efficiencyMultiplier = 2.0f;
				} else if (boostBefore > 80.0f) {
					// Collecting when > 80 boost is less valuable
					efficiencyMultiplier = 0.5f;
				}
				
				// Base reward scales with boost gained
				float baseReward = RS_MIN(1.0f, boostGained / 100.0f);
				
				return baseReward * efficiencyMultiplier;
			}
			
			return 0;
		}
	};

	// Punishes own goals to prevent bot from scoring on itself
	class OwnGoalPunishment : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!isFinal || !state.goalScored)
				return 0;

			// Detect own goal: check if goal was scored on player's team's goal
			// Blue team scores on Orange goal (positive Y), Orange team scores on Blue goal (negative Y)
			bool ownGoal = (player.team == RS_TEAM_FROM_Y(state.ball.pos.y));
			
			if (ownGoal) {
				return -5.0f; // Strong punishment for own goals
			}
			
			return 0;
		}
	};

	// Main air dribble reward - tracks sustained aerial ball control
	// Requires: car below ball, boosting, creating height, heading toward opponent net
	// Rewards optimal height arc (75% to ceiling = 1533 units) and multi-touch sequences
	class AirDribbleReward : public Reward {
	private:
		float intervalSeconds;
		std::unordered_map<int, float> aerialControlTime;
		std::unordered_map<int, float> lastTouchTime;
		std::unordered_map<int, float> peakBallHeight;
		std::unordered_map<int, int> touchCount;
		std::unordered_map<int, bool> inAirDribble;
		std::unordered_map<int, float> lastBoostTime; // Track when boost was last used (for feathering)

	public:
		AirDribbleReward(float intervalSec = 0.5f) : intervalSeconds(intervalSec) {}

		virtual void Reset(const GameState& initialState) override {
			aerialControlTime.clear();
			lastTouchTime.clear();
			peakBallHeight.clear();
			touchCount.clear();
			inAirDribble.clear();
			lastBoostTime.clear();
			for (const auto& player : initialState.players) {
				aerialControlTime[player.carId] = 0.0f;
				lastTouchTime[player.carId] = 0.0f;
				peakBallHeight[player.carId] = 0.0f;
				touchCount[player.carId] = 0;
				inAirDribble[player.carId] = false;
				lastBoostTime[player.carId] = -1.0f; // -1 means never used
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			int carId = player.carId;
			bool isInAir = !player.isOnGround;
			bool hasBallContact = player.ballTouchedStep || player.ballTouchedTick;
			
			// Allow feathering: boost must be available and used recently (within 0.3 seconds)
			bool hasBoostAvailable = (player.boost > 0);
			bool isBoostingNow = (player.prevAction.boost > 0.1f); // Lower threshold for feathering
			bool boostUsedRecently = false;

			if (isBoostingNow) {
				lastBoostTime[carId] = 0.0f; // Reset timer when boosting
				boostUsedRecently = true;
			} else if (lastBoostTime[carId] >= 0.0f) {
				// Update timer if we've boosted before
				lastBoostTime[carId] += state.deltaTime;
				boostUsedRecently = (lastBoostTime[carId] < 0.3f); // Allow 0.3s pause for feathering
			}

			bool isBoosting = hasBoostAvailable && (isBoostingNow || boostUsedRecently);

			// Check if conditions are met for air dribble
			bool conditionsMet = isInAir && 
			                     hasBallContact && 
			                     player.pos.z < state.ball.pos.z && // Car must be below ball
			                     isBoosting && // Must be boosting (allows feathering)
			                     state.ball.vel.z > 0; // Ball must be going up

			// Goal alignment check - ball must be heading toward opponent net
			// Use 75% of goal height (~482 units) to allow crossbar hits
			// If goal is scored, skip alignment check (goal reward handles it)
			float goalTargetHeight = CommonValues::GOAL_HEIGHT * 0.75f; // ~482 units (allows crossbar hits)
			Vec goalCenter = (player.team == Team::BLUE) ? 
				CommonValues::ORANGE_GOAL_CENTER : CommonValues::BLUE_GOAL_CENTER;
			Vec targetGoal = Vec(goalCenter.x, goalCenter.y, goalTargetHeight);
			Vec dirToGoal = (targetGoal - state.ball.pos).Normalized();
			float goalAlignment = 0.0f;
			if (state.ball.vel.Length() > FLT_EPSILON && dirToGoal.Length() > FLT_EPSILON) {
				Vec ballVelDir = state.ball.vel.Normalized();
				goalAlignment = ballVelDir.Dot(dirToGoal);
			}

			// Require minimum goal alignment (on target)
			// Skip if goal was just scored (goal reward handles it, no penalty)
			if (goalAlignment < 0.3f && !state.goalScored) {
				conditionsMet = false;
			}

			if (conditionsMet) {
				// Track air dribble state
				if (!inAirDribble[carId]) {
					// Starting new air dribble
					inAirDribble[carId] = true;
					peakBallHeight[carId] = state.ball.pos.z;
					touchCount[carId] = 0;
					aerialControlTime[carId] = 0.0f;
				}

				// Update tracking
				aerialControlTime[carId] += state.deltaTime;
				peakBallHeight[carId] = RS_MAX(peakBallHeight[carId], state.ball.pos.z);

				// Count touches
				if (player.ballTouchedStep) {
					touchCount[carId]++;
				}

				// Calculate base reward with goal alignment scaling
				float baseReward = RS_MAX(0.0f, goalAlignment); // Scale by alignment strength

				// Height arc optimization: Optimal height is 75% of way from ground (0) to ceiling (2044) = 1533 units
				constexpr float OPTIMAL_HEIGHT = 1533.0f;
				float heightDiff = abs(peakBallHeight[carId] - OPTIMAL_HEIGHT);
				float heightScore = RS_MAX(0.0f, 1.0f - (heightDiff / OPTIMAL_HEIGHT)); // 1.0 at optimal, decreases as distance increases
				baseReward *= (1.0f + heightScore * 0.5f); // Up to 50% bonus for optimal height

				// Multi-touch multiplier: 20% bonus per additional touch
				float touchMultiplier = 1.0f + (touchCount[carId] - 1) * 0.2f;
				baseReward *= touchMultiplier;

				// Duration multiplier (existing behavior)
				float durationMultiplier = 1.0f + (aerialControlTime[carId] / intervalSeconds) * 0.5f;
				baseReward *= durationMultiplier;

				lastTouchTime[carId] = aerialControlTime[carId];
				return baseReward;
			} else {
				// Reset tracking when air dribble ends
				if (inAirDribble[carId]) {
					inAirDribble[carId] = false;
					peakBallHeight[carId] = 0.0f;
					touchCount[carId] = 0;
					aerialControlTime[carId] = 0.0f;
				}
			}

			return 0;
		}
	};

	// Rewards boosting toward ball during air dribbles
	// Only rewards when conditions match valid air dribble (car below ball, goal alignment, ball going up)
	class AirDribbleBoostReward : public Reward {
	private:
		float maxDistance; // Maximum distance to consider "near ball"
		std::unordered_map<int, bool> inAirDribble;

	public:
		AirDribbleBoostReward(float maxDist = 500.0f) : maxDistance(maxDist) {}

		virtual void Reset(const GameState& initialState) override {
			inAirDribble.clear();
			for (const auto& player : initialState.players) {
				inAirDribble[player.carId] = false;
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			int carId = player.carId;
			bool isInAir = !player.isOnGround;
			bool hasBallContact = player.ballTouchedStep || player.ballTouchedTick;
			bool isBoosting = (player.boost > 0 && player.prevAction.boost > 0.5f);

			// Check if conditions are met for valid air dribble (same as AirDribbleReward)
			bool isValidAirDribble = isInAir && 
			                         hasBallContact && 
			                         player.pos.z < state.ball.pos.z && // Car must be below ball
			                         state.ball.vel.z > 0; // Ball must be going up

			// Goal alignment check - ball must be heading toward opponent net
			// Use 75% of goal height (~482 units) to allow crossbar hits
			// If goal is scored, skip alignment check (goal reward handles it)
			float goalTargetHeight = CommonValues::GOAL_HEIGHT * 0.75f; // ~482 units (allows crossbar hits)
			Vec goalCenter = (player.team == Team::BLUE) ? 
				CommonValues::ORANGE_GOAL_CENTER : CommonValues::BLUE_GOAL_CENTER;
			Vec targetGoal = Vec(goalCenter.x, goalCenter.y, goalTargetHeight);
			Vec dirToGoal = (targetGoal - state.ball.pos).Normalized();
			float goalAlignment = 0.0f;
			if (state.ball.vel.Length() > FLT_EPSILON && dirToGoal.Length() > FLT_EPSILON) {
				Vec ballVelDir = state.ball.vel.Normalized();
				goalAlignment = ballVelDir.Dot(dirToGoal);
			}

			// Require minimum goal alignment (on target)
			// Skip if goal was just scored (goal reward handles it, no penalty)
			if (goalAlignment < 0.3f && !state.goalScored) {
				isValidAirDribble = false;
			}

			// Track if in valid air dribble sequence
			if (isValidAirDribble) {
				inAirDribble[carId] = true;
			} else {
				inAirDribble[carId] = false;
			}

			// Only reward when in valid air dribble and boosting
			if (inAirDribble[carId] && isBoosting) {
				float distToBall = (state.ball.pos - player.pos).Length();
				
				if (distToBall < maxDistance) {
					// Calculate direction to ball
					Vec dirToBall = (state.ball.pos - player.pos).Normalized();
					
					// Check if player velocity aligns with direction to ball
					if (player.vel.Length() > FLT_EPSILON) {
						Vec velDir = player.vel.Normalized();
						float alignment = velDir.Dot(dirToBall);
						
						// Reward proportional to alignment (0.0 to 1.0)
						return RS_MAX(0.0f, alignment) * 0.5f;
					}
				}
			}

			return 0;
		}
	};

	// Rewards first touch that starts an air dribble
	class AirDribbleStartReward : public Reward {
	private:
		float minDistanceFromGoal;

	public:
		AirDribbleStartReward(float minDistFromGoal = 3000.0f) : minDistanceFromGoal(minDistFromGoal) {}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			// Check if this is a new aerial touch (starts air dribble)
			if (!player.isOnGround && player.ballTouchedStep) {
				float baseReward = 0.3f;
				
				// Boost bonus: requires minimum 50 boost, then scales from 50-100 boost
				// 50 boost = 0.0 bonus, 100 boost = 0.8 bonus
				// Less than 50 boost = no bonus (but still get base reward)
				float boostBonus = 0.0f;
				if (player.boost >= 50.0f) {
					// Scale from 50-100 boost: (boost - 50) / 50 gives 0.0 to 1.0
					boostBonus = ((player.boost - 50.0f) / 50.0f) * 0.8f;
				}
				baseReward += boostBonus;
				
				// Bonus based on distance from goal (further = better)
				Vec targetGoal = (player.team == Team::BLUE) ? 
					CommonValues::ORANGE_GOAL_CENTER : CommonValues::BLUE_GOAL_CENTER;
				float distFromGoal = (player.pos - targetGoal).Length();
				
				if (distFromGoal > minDistanceFromGoal) {
					float distanceBonus = (distFromGoal - minDistanceFromGoal) / 5000.0f;
					baseReward += RS_MIN(0.5f, distanceBonus); // Cap at 0.5 bonus
				}
				
				return baseReward;
			}

		return 0;
	}
};

	// Rewards setup touches for air dribbles (ground/wall hits before aerial phase)
	// Rewards: boosting toward ball, multiple touches, trajectory toward net
	class AirDribbleSetupReward : public Reward {
	private:
		float maxSetupTime; // Maximum time for setup phase (default 2.0s)
		float minGoalAlignment; // Minimum goal alignment to reward (default 0.3)
		std::unordered_map<int, bool> inSetupPhase;
		std::unordered_map<int, float> setupTime;
		std::unordered_map<int, int> setupTouchCount;
		std::unordered_map<int, Vec> setupStartPos;
		std::unordered_map<int, float> lastBoostTime;

		float CalculateSetupReward(const Player& player, const GameState& state, int carId, float goalAlignment) {
			float baseReward = RS_MAX(0.0f, goalAlignment); // Base reward from trajectory alignment

			// Boost toward ball reward
			// Check if boost is currently being used OR was used recently (within 0.3s)
			// This ensures first boost frame is rewarded immediately
			bool isBoostingNow = (player.boost > 0 && player.prevAction.boost > 0.1f);
			bool boostedRecently = (lastBoostTime[carId] >= 0.0f && lastBoostTime[carId] < 0.3f);
			
			float boostReward = 0.0f;
			if (isBoostingNow || boostedRecently) {
				// Player is boosting (or boosted recently)
				Vec dirToBall = (state.ball.pos - player.pos).Normalized();
				if (player.vel.Length() > FLT_EPSILON) {
					Vec velDir = player.vel.Normalized();
					float alignmentToBall = velDir.Dot(dirToBall);
					boostReward = RS_MAX(0.0f, alignmentToBall) * 0.4f; // Up to 0.4 bonus
				}
			}

			// Multi-touch bonus: 25% per additional touch
			float touchMultiplier = 1.0f + (setupTouchCount[carId] - 1) * 0.25f;

			// Trajectory quality bonus (stronger alignment = better)
			float trajectoryBonus = goalAlignment * 0.3f;

			return (baseReward + boostReward + trajectoryBonus) * touchMultiplier;
		}

	public:
		AirDribbleSetupReward(float maxTime = 2.0f, float minAlignment = 0.3f)
			: maxSetupTime(maxTime), minGoalAlignment(minAlignment) {}

		virtual void Reset(const GameState& initialState) override {
			inSetupPhase.clear();
			setupTime.clear();
			setupTouchCount.clear();
			setupStartPos.clear();
			lastBoostTime.clear();
			for (const auto& player : initialState.players) {
				inSetupPhase[player.carId] = false;
				setupTime[player.carId] = 0.0f;
				setupTouchCount[player.carId] = 0;
				lastBoostTime[player.carId] = -1.0f;
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			int carId = player.carId;
			bool isInAir = !player.isOnGround;
			bool hasBallContact = player.ballTouchedStep || player.ballTouchedTick;
			
			// Check if ball trajectory is toward opponent net (arc trajectory)
			// Use 75% of goal height (~482 units) to allow crossbar hits
			float goalTargetHeight = CommonValues::GOAL_HEIGHT * 0.75f; // ~482 units (allows crossbar hits)
			Vec goalCenter = (player.team == Team::BLUE) ? 
				CommonValues::ORANGE_GOAL_CENTER : CommonValues::BLUE_GOAL_CENTER;
			Vec targetGoal = Vec(goalCenter.x, goalCenter.y, goalTargetHeight);
			Vec dirToGoal = (targetGoal - state.ball.pos).Normalized();
			float goalAlignment = 0.0f;
			if (state.ball.vel.Length() > FLT_EPSILON && dirToGoal.Length() > FLT_EPSILON) {
				Vec ballVelDir = state.ball.vel.Normalized();
				goalAlignment = ballVelDir.Dot(dirToGoal);
			}

			// Check if ball is going up (arc trajectory)
			bool ballGoingUp = state.ball.vel.z > 0;

			// Detect setup phase: on ground/wall, touching ball, trajectory toward net
			bool shouldBeInSetup = !isInAir && hasBallContact && 
			                        goalAlignment >= minGoalAlignment && 
			                        ballGoingUp;

			// Start setup phase
			if (shouldBeInSetup && !inSetupPhase[carId]) {
				inSetupPhase[carId] = true;
				setupTime[carId] = 0.0f;
				setupTouchCount[carId] = 0;
				setupStartPos[carId] = player.pos;
				lastBoostTime[carId] = -1.0f;
			}

			// End setup phase if player goes aerial (air dribble starts)
			if (isInAir && inSetupPhase[carId]) {
				// Setup phase ended - calculate final reward
				float reward = CalculateSetupReward(player, state, carId, goalAlignment);
				inSetupPhase[carId] = false;
				setupTime[carId] = 0.0f;
				setupTouchCount[carId] = 0;
				return reward;
			}

			// Update setup phase
			if (inSetupPhase[carId]) {
				setupTime[carId] += state.deltaTime;
				
				// Count touches during setup
				if (player.ballTouchedStep) {
					setupTouchCount[carId]++;
				}

				// Track boost usage
				if (player.prevAction.boost > 0.1f) {
					lastBoostTime[carId] = 0.0f;
				} else if (lastBoostTime[carId] >= 0.0f) {
					lastBoostTime[carId] += state.deltaTime;
				}

				// Reset if setup takes too long or conditions no longer met
				if (setupTime[carId] > maxSetupTime || !shouldBeInSetup) {
					inSetupPhase[carId] = false;
					setupTime[carId] = 0.0f;
					setupTouchCount[carId] = 0;
				}

				// Continuous reward during setup (smaller than final reward)
				return CalculateSetupReward(player, state, carId, goalAlignment) * 0.3f;
			}

			return 0;
		}
	};

	// Rewards air dribbles based on distance traveled (further = better)
	class AirDribbleDistanceReward : public Reward {
	private:
		float maxTimeWindow;
		std::unordered_map<int, Vec> startPos;
		std::unordered_map<int, float> startTime;
		std::unordered_map<int, bool> inAirDribble;

	public:
		AirDribbleDistanceReward(float maxTime = 3.0f) : maxTimeWindow(maxTime) {}

		virtual void Reset(const GameState& initialState) override {
			startPos.clear();
			startTime.clear();
			inAirDribble.clear();
			for (const auto& player : initialState.players) {
				startPos[player.carId] = Vec();
				startTime[player.carId] = 0.0f;
				inAirDribble[player.carId] = false;
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			int carId = player.carId;
			bool isInAir = !player.isOnGround;
			bool hasBallContact = player.ballTouchedStep || player.ballTouchedTick;
			
			// Stricter validation: must meet air dribble conditions (same as AirDribbleReward)
			// Car must be below ball, ball going up, and heading toward goal
			bool isValidAirDribble = isInAir && 
			                         hasBallContact && 
			                         player.pos.z < state.ball.pos.z && // Car must be below ball
			                         state.ball.vel.z > 0; // Ball must be going up
			
			// Goal alignment check - ball must be heading toward opponent net
			// Use 75% of goal height (~482 units) to allow crossbar hits
			float goalTargetHeight = CommonValues::GOAL_HEIGHT * 0.75f; // ~482 units (allows crossbar hits)
			Vec goalCenter = (player.team == Team::BLUE) ? 
				CommonValues::ORANGE_GOAL_CENTER : CommonValues::BLUE_GOAL_CENTER;
			Vec targetGoal = Vec(goalCenter.x, goalCenter.y, goalTargetHeight);
			Vec dirToGoal = (targetGoal - state.ball.pos).Normalized();
			float goalAlignment = 0.0f;
			if (state.ball.vel.Length() > FLT_EPSILON && dirToGoal.Length() > FLT_EPSILON) {
				Vec ballVelDir = state.ball.vel.Normalized();
				goalAlignment = ballVelDir.Dot(dirToGoal);
			}
			
			// Require minimum goal alignment (on target) - skip if goal scored
			if (goalAlignment < 0.3f && !state.goalScored) {
				isValidAirDribble = false;
			}

			// Track air dribble start (only if valid)
			if (isValidAirDribble && !inAirDribble[carId]) {
				inAirDribble[carId] = true;
				startPos[carId] = player.pos;
				startTime[carId] = 0.0f;
			}

			// Update time
			if (inAirDribble[carId]) {
				startTime[carId] += state.deltaTime;
				
				// Reset if conditions no longer met, too much time passed, or lost contact
				if (!isValidAirDribble || startTime[carId] > maxTimeWindow || (!isInAir && !hasBallContact)) {
					inAirDribble[carId] = false;
					startTime[carId] = 0.0f;
				}
			}

			// Reward on goal scored from air dribble
			// Gives 1x normal goal reward (350) as bonus, scaled by distance
			if (state.goalScored && inAirDribble[carId]) {
				bool scored = (player.team != RS_TEAM_FROM_Y(state.ball.pos.y));
				
				if (scored && startTime[carId] <= maxTimeWindow) {
					float distance = (player.pos - startPos[carId]).Length();
					
					// Base reward is 1x normal goal reward (350)
					constexpr float NORMAL_GOAL_REWARD = 350.0f;
					
					// Scale reward by distance (capped at 3.0x)
					// Short air dribble (0-2000 units): 1.0x = 350
					// Medium air dribble (2000-4000 units): 2.0x = 700
					// Long air dribble (4000+ units): 3.0x = 1050
					float distanceMultiplier = RS_MIN(3.0f, 1.0f + (distance / 2000.0f));
					
					inAirDribble[carId] = false;
					startTime[carId] = 0.0f;
					
					return NORMAL_GOAL_REWARD * distanceMultiplier;
				}
			}

		return 0;
	}
};

// Rewards speed flips on kickoffs - encourages fast ground movement and quick flips
class KickoffSpeedFlipReward : public Reward {
private:
	float maxKickoffTime;
	float minSpeedForReward;
	std::unordered_map<int, float> kickoffStartTime;
	std::unordered_map<int, bool> inKickoff;

public:
	KickoffSpeedFlipReward(float maxTime = 3.0f, float minSpeed = 1000.0f)
		: maxKickoffTime(maxTime), minSpeedForReward(minSpeed) {}

	virtual void Reset(const GameState& initialState) override {
		kickoffStartTime.clear();
		inKickoff.clear();
		for (const auto& player : initialState.players) {
			kickoffStartTime[player.carId] = 0.0f;
			inKickoff[player.carId] = false;
		}
	}

	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		if (!state.prev)
			return 0;

		int carId = player.carId;
		
		// Detect kickoff: ball at center, low velocity
		bool ballAtCenter = (state.ball.pos.Length() < 500.0f && abs(state.ball.pos.z) < 100.0f);
		bool ballStationary = state.ball.vel.Length() < 100.0f;
		
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

		if (!inKickoff[carId])
			return 0;

		// Only reward ground-based speed flips (not aerial)
		if (!player.isOnGround)
			return 0;

		// Reward high ground speed toward ball
		float speed = player.vel.Length();
		if (speed < minSpeedForReward)
			return 0;

		Vec dirToBall = (state.ball.pos - player.pos).Normalized();
		float speedTowardBall = player.vel.Dot(dirToBall);
		
		// Base reward for high speed toward ball
		float reward = RS_MIN(1.0f, speedTowardBall / CommonValues::CAR_MAX_SPEED);

		// Bonus for using flip (detect flip state)
		if (player.isFlipping) {
			reward *= 1.5f; // 50% bonus for flipping
		}

		// Bonus for fast acceleration (speed flip characteristic)
		float prevSpeed = state.prev->players[player.index].vel.Length();
		float accel = (speed - prevSpeed) / state.deltaTime;
		if (accel > 2000.0f) { // Fast acceleration
			reward *= 1.3f; // 30% bonus
		}

	return reward * 0.5f; // Scale down to reasonable level
}
};

}

