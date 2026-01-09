#pragma once
#include <RLGymCPP/Rewards/Reward.h>
#include <RLGymCPP/Gamestates/GameState.h>
#include <unordered_map>
#include "CustomBasicMechanicsRewards.h"
#include "CustomKickoffRewards.h"

namespace RLGC {

	// Helper function to check if ball is in opponent's corner or off their backboard
	// Prevents rewarding air dribbles in bad positions
	inline bool IsBallInOpponentCornerOrBackboard(const Player& player, const GameState& state) {
		Vec ballPos = state.ball.pos;
		
		// Check if ball is behind opponent's goal line (backboard)
		bool behindGoalLine = (player.team == Team::BLUE) ? 
			(ballPos.y > CommonValues::BACK_WALL_Y) : 
			(ballPos.y < -CommonValues::BACK_WALL_Y);
		
		// High up off backboard (Z > 1000 indicates clearly off backboard)
		bool offBackboard = behindGoalLine && ballPos.z > 1000.0f;
		
		// Check if in opponent corner (near side walls and opponent's back wall)
		// Corner: |X| > 3000 (close to side walls) AND in opponent's half
		bool inOpponentHalf = (player.team == Team::BLUE) ? 
			(ballPos.y > 0) : (ballPos.y < 0);
		bool nearSideWall = abs(ballPos.x) > 3000.0f;
		bool nearBackWall = (player.team == Team::BLUE) ? 
			(ballPos.y > 4000.0f) : (ballPos.y < -4000.0f);
		bool inCorner = inOpponentHalf && nearSideWall && nearBackWall;
		
		return offBackboard || inCorner;
	}

	// Detects open-net situation for a given attacking team
	// Returns true if ball is heading toward opponent goal with good speed/alignment
	// and all defenders are far from their goal
	inline bool IsOpenNetForAttackingTeam(Team attackingTeam,
	                                      const GameState& state,
	                                      float maxDefenderDist = 2000.0f,
	                                      float minBallSpeed = 1000.0f,
	                                      float maxBallHeight = 500.0f,
	                                      float minAlignment = 0.7f) {
		// Target goal is opponent's goal
		Vec goalCenter = (attackingTeam == Team::BLUE) ? 
			CommonValues::ORANGE_GOAL_CENTER : CommonValues::BLUE_GOAL_CENTER;
		
		// Check if ball is heading toward that goal
		Vec dirToGoal = (goalCenter - state.ball.pos).Normalized();
		float goalAlignment = 0.0f;
		if (state.ball.vel.Length() > FLT_EPSILON && dirToGoal.Length() > FLT_EPSILON) {
			Vec ballVelDir = state.ball.vel.Normalized();
			goalAlignment = ballVelDir.Dot(dirToGoal);
		}
		
		// Must have good alignment and speed
		if (goalAlignment < minAlignment)
			return false;
		if (state.ball.vel.Length() < minBallSpeed)
			return false;
		if (state.ball.pos.z > maxBallHeight)
			return false;
		
		// Check if all defenders are far from their own goal
		for (const auto& p : state.players) {
			if (p.team == attackingTeam)
				continue; // Skip attacking team players
			
			Vec theirGoalCenter = (p.team == Team::BLUE) ? 
				CommonValues::BLUE_GOAL_CENTER : CommonValues::ORANGE_GOAL_CENTER;
			float distToGoal = (p.pos - theirGoalCenter).Length();
			
			if (distToGoal < maxDefenderDist) {
				// Someone is back defending, not an open net
				return false;
			}
		}
		
		return true;
	}

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

	// Punishes conceding a goal when our own net was completely open
	// Encourages defensive positioning and not overcommitting
	class OpenNetConcedePunishment : public Reward {
	private:
		float penalty; // Magnitude of punishment (positive value)

	public:
		OpenNetConcedePunishment(float penaltyMag = 3.0f) : penalty(penaltyMag) {}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!isFinal || !state.goalScored)
				return 0;

			// Did we concede?
			bool conceded = (player.team == RS_TEAM_FROM_Y(state.ball.pos.y));
			if (!conceded)
				return 0;

			// Check if opponent had an open net (they were attacking, we were defending)
			Team opponentTeam = (player.team == Team::BLUE) ? Team::ORANGE : Team::BLUE;
			if (!IsOpenNetForAttackingTeam(opponentTeam, state))
				return 0;

			// Strong punishment for leaving net completely open
			return -penalty;
		}
	};

	// Main air dribble reward - tracks sustained aerial ball control
	// Combines: base air dribble mechanics + boosting toward ball alignment
	// Requires: car below ball, boosting, creating height, heading toward opponent net
	// Rewards optimal height arc (75% to ceiling = 1533 units) and multi-touch sequences
	class AirDribbleReward : public Reward {
	private:
		float intervalSeconds;
		float maxDistance; // Maximum distance for boost alignment reward (default 500.0f)
		std::unordered_map<int, float> aerialControlTime;
		std::unordered_map<int, float> lastTouchTime;
		std::unordered_map<int, float> peakBallHeight;
		std::unordered_map<int, int> touchCount;
		std::unordered_map<int, bool> inAirDribble;
		std::unordered_map<int, float> lastBoostTime; // Track when boost was last used (for feathering)

	public:
		AirDribbleReward(float intervalSec = 0.5f, float maxDist = 500.0f) 
			: intervalSeconds(intervalSec), maxDistance(maxDist) {}

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

			// Don't reward air dribbles in opponent corners or off their backboards
			if (IsBallInOpponentCornerOrBackboard(player, state))
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

				// COMBINED: Boost alignment reward (from AirDribbleBoostReward)
				// Only reward when actively boosting (not just feathering grace period)
				float boostAlignmentReward = 0.0f;
				if (isBoostingNow && player.boost > 0 && player.prevAction.boost > 0.5f) {
					float distToBall = (state.ball.pos - player.pos).Length();
					
					if (distToBall < maxDistance) {
						// Calculate direction to ball
						Vec dirToBall = (state.ball.pos - player.pos).Normalized();
						
						// Check if player velocity aligns with direction to ball
						if (player.vel.Length() > FLT_EPSILON) {
							Vec velDir = player.vel.Normalized();
							float alignment = velDir.Dot(dirToBall);
							
							// Reward proportional to alignment (0.0 to 1.0)
							boostAlignmentReward = RS_MAX(0.0f, alignment) * 0.5f;
						}
					}
				}

				// Combine base reward with boost alignment reward
				lastTouchTime[carId] = aerialControlTime[carId];
				return baseReward + boostAlignmentReward;
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

	// Rewards first touch that starts an air dribble
	class AirDribbleStartReward : public Reward {
	private:
		float minDistanceFromGoal;

	public:
		AirDribbleStartReward(float minDistFromGoal = 3000.0f) : minDistanceFromGoal(minDistFromGoal) {}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			// Don't reward air dribbles in opponent corners or off their backboards
			if (IsBallInOpponentCornerOrBackboard(player, state))
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
		float minBallHeightDiff; // Minimum height difference ball above car to count as pop (default 150)
		float minCarGoalAlignment; // Minimum car facing alignment toward goal (default 0.5)
		float minCarSpeedTowardGoal; // Minimum car speed toward goal (default 300)
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
		AirDribbleSetupReward(float maxTime = 2.0f, float minAlignment = 0.3f,
		                      float minHeightDiff = 150.0f,
		                      float minCarGoalAlign = 0.5f,
		                      float minCarSpeedToGoal = 300.0f)
			: maxSetupTime(maxTime),
			  minGoalAlignment(minAlignment),
			  minBallHeightDiff(minHeightDiff),
			  minCarGoalAlignment(minCarGoalAlign),
			  minCarSpeedTowardGoal(minCarSpeedToGoal) {}

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

			// Don't reward air dribbles in opponent corners or off their backboards
			if (IsBallInOpponentCornerOrBackboard(player, state))
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

			// Ball must be popped clearly above the car
			bool ballAboveCar = (state.ball.pos.z > player.pos.z + minBallHeightDiff);

			// Car facing and moving toward opponent goal
			float carGoalAlignment = player.rotMat.forward.Dot(dirToGoal);
			float carSpeedTowardGoal = player.vel.Dot(dirToGoal);

			// Detect setup phase: on ground/wall, touching ball, trajectory toward net
			bool shouldBeInSetup = !isInAir && hasBallContact &&
			                       goalAlignment >= minGoalAlignment &&
			                       ballGoingUp &&
			                       ballAboveCar &&
			                       carGoalAlignment >= minCarGoalAlignment &&
			                       carSpeedTowardGoal >= minCarSpeedTowardGoal;

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

			// Don't reward air dribbles in opponent corners or off their backboards
			if (IsBallInOpponentCornerOrBackboard(player, state))
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
				
				// Reset if conditions no longer met, too much time passed, or player hits ground
				if (!isValidAirDribble || startTime[carId] > maxTimeWindow || !isInAir) {
					inAirDribble[carId] = false;
					startTime[carId] = 0.0f;
				}
			}

			// Reward on goal scored from air dribble
			// Gives 5x normal goal reward (350) as base bonus, scaled by distance
			if (isFinal && state.goalScored && inAirDribble[carId]) {
				bool scored = (player.team != RS_TEAM_FROM_Y(state.ball.pos.y));
				
				if (scored && startTime[carId] <= maxTimeWindow) {
					// Use ball's final position for distance calculation
					float distance = (state.ball.pos - startPos[carId]).Length();
					
					// Base reward is 5x normal goal reward (5 * 350 = 1750)
					constexpr float NORMAL_GOAL_REWARD = 1750.0f;
					
					// Scale reward by distance (capped at 2.0x)
					// Short air dribble (0-2000 units): ~1.0x
					// Medium air dribble (2000-4000 units): ~1.5x
					// Long air dribble (4000+ units): 2.0x
					float distanceMultiplier = RS_MIN(2.0f, 1.0f + (distance / 4000.0f));
					
					inAirDribble[carId] = false;
					startTime[carId] = 0.0f;
					
					return NORMAL_GOAL_REWARD * distanceMultiplier;
				}
			}

		return 0;
	}
};

	// Simple counter reward for air dribble goals (for metrics)
	// Returns 1.0 when a goal is scored directly from a valid air dribble, otherwise 0
	class AirDribbleGoalCountReward : public Reward {
	private:
		std::unordered_map<int, bool> inAirDribble;

	public:
		AirDribbleGoalCountReward() {}

		virtual void Reset(const GameState& initialState) override {
			inAirDribble.clear();
			for (const auto& player : initialState.players) {
				inAirDribble[player.carId] = false;
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			// Don't count air dribbles in opponent corners or off their backboards
			if (IsBallInOpponentCornerOrBackboard(player, state))
				return 0;

			int carId = player.carId;
			bool isInAir = !player.isOnGround;
			bool hasBallContact = player.ballTouchedStep || player.ballTouchedTick;

			// Valid air dribble conditions (similar to AirDribbleReward / AirDribbleDistanceReward)
			bool isValidAirDribble = isInAir &&
			                         hasBallContact &&
			                         player.pos.z < state.ball.pos.z && // Car must be below ball
			                         state.ball.vel.z > 0; // Ball must be going up

			// Goal alignment check - ball must be heading toward opponent net
			float goalTargetHeight = CommonValues::GOAL_HEIGHT * 0.75f;
			Vec goalCenter = (player.team == Team::BLUE) ?
				CommonValues::ORANGE_GOAL_CENTER : CommonValues::BLUE_GOAL_CENTER;
			Vec targetGoal = Vec(goalCenter.x, goalCenter.y, goalTargetHeight);
			Vec dirToGoal = (targetGoal - state.ball.pos).Normalized();
			float goalAlignment = 0.0f;
			if (state.ball.vel.Length() > FLT_EPSILON && dirToGoal.Length() > FLT_EPSILON) {
				Vec ballVelDir = state.ball.vel.Normalized();
				goalAlignment = ballVelDir.Dot(dirToGoal);
			}

			if (goalAlignment < 0.3f && !state.goalScored) {
				isValidAirDribble = false;
			}

			// Track if we are currently in an air dribble sequence
			if (isValidAirDribble) {
				inAirDribble[carId] = true;
			} else if (!isInAir) {
				// Leaving the air cancels the sequence
				inAirDribble[carId] = false;
			}

			// On goal: if we were in an air dribble, count it as an air dribble goal
			if (isFinal && state.goalScored && inAirDribble[carId]) {
				bool scored = (player.team != RS_TEAM_FROM_Y(state.ball.pos.y));

				if (scored) {
					inAirDribble[carId] = false;
					return 1.0f;
				}
			}

			return 0;
		}
	};

	// Ground→air pop & chase reward (non-farmable, no goal bonus)
	// Flow: ground control → pop (jump) → boost-chase while ball rises → second touch aligned to opponent net
	// Variations get less reward dynamically based on alignment
	class GroundToAirPopReward : public Reward {
	private:
		float maxGroundBallHeight;   // Max ball height for ground control (default 340)
		float maxGroundCarHeight;    // Max car height for ground control (default 180)
		float maxDistance;           // Max car-ball distance for ground control (default 260)
		float minForwardSpeed;       // Min forward speed for ground control (default 350)
		float minPopHeightGain;      // Min ball height gain after pop (default 120)
		float maxChaseTime;          // Max time window to chase after pop (default 1.0s)
		float minAlignment;          // Min ball velocity alignment toward goal (default 0.35)
		float minCarBallAlign;       // Min car facing ball during chase (default 0.5)
		float touchBonus;            // Bonus for second touch (default 0.6)
		float baseScale;             // Base scale for pop/chase reward (default 0.4)
		
		struct PopState {
			bool inPop = false;
			float time = -1.0f;
			float popStartBallZ = 0.0f;
			bool ballGoingUp = false;
			bool boostedDuringChase = false;
		};
		std::unordered_map<int, PopState> pop;
		
	public:
		GroundToAirPopReward(float maxBallH = 340.0f, float maxCarH = 180.0f, float maxDist = 260.0f,
		                     float minFwd = 350.0f, float minGain = 120.0f, float chaseTime = 1.0f,
		                     float minAlign = 0.35f, float minCarAlign = 0.5f,
		                     float touchB = 0.6f, float scale = 0.4f)
			: maxGroundBallHeight(maxBallH), maxGroundCarHeight(maxCarH),
			  maxDistance(maxDist), minForwardSpeed(minFwd),
			  minPopHeightGain(minGain), maxChaseTime(chaseTime),
			  minAlignment(minAlign), minCarBallAlign(minCarAlign),
			  touchBonus(touchB), baseScale(scale) {}
		
		virtual void Reset(const GameState& initialState) override {
			pop.clear();
			for (const auto& p : initialState.players) {
				pop[p.carId] = PopState();
			}
		}
		
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;
			
			int carId = player.carId;
			auto& ps = pop[carId];
			
			// Ground control preconditions (non-farmable: must be low and close)
			// Check previous frame for ground carry state
			bool wasGroundCarry = state.prev->players[player.index].isOnGround &&
			                       state.prev->ball.pos.z <= maxGroundBallHeight &&
			                       state.prev->players[player.index].pos.z <= maxGroundCarHeight &&
			                       (state.prev->ball.pos - state.prev->players[player.index].pos).Length() <= maxDistance &&
			                       state.prev->players[player.index].vel.Dot(state.prev->players[player.index].rotMat.forward) >= minForwardSpeed;
			
			// Detect pop start: leaving ground from a carry state
			if (wasGroundCarry && !player.isOnGround) {
				ps.inPop = true;
				ps.time = 0.0f;
				ps.popStartBallZ = state.prev->ball.pos.z;
				ps.ballGoingUp = false;
				ps.boostedDuringChase = false;
			}
			
			if (!ps.inPop)
				return 0;
			
			// Update timer
			ps.time += state.deltaTime;
			if (ps.time > maxChaseTime) {
				ps.inPop = false;
				return 0;
			}
			
			float reward = 0.0f;
			
			// Check ball rising after pop
			float heightGain = state.ball.pos.z - ps.popStartBallZ;
			ps.ballGoingUp = ps.ballGoingUp || (heightGain >= minPopHeightGain && state.ball.vel.z > 0);
			
			// Require boosting while chasing upward
			bool boosting = (player.boost > 0 && player.prevAction.boost > 0.1f);
			if (boosting)
				ps.boostedDuringChase = true;
			
			// Car facing ball
			Vec toBall = (state.ball.pos - player.pos).Normalized();
			float carBallAlign = player.rotMat.forward.Dot(toBall);
			
			// Ball + car toward opponent goal
			Vec goalCenter = (player.team == Team::BLUE) ? 
				CommonValues::ORANGE_GOAL_CENTER : CommonValues::BLUE_GOAL_CENTER;
			Vec dirToGoal = (goalCenter - state.ball.pos).Normalized();
			float ballGoalAlign = 0.0f;
			if (state.ball.vel.Length() > FLT_EPSILON && dirToGoal.Length() > FLT_EPSILON) {
				ballGoalAlign = state.ball.vel.Normalized().Dot(dirToGoal);
			}
			float carGoalAlign = player.rotMat.forward.Dot(dirToGoal);
			
			// Core conditions during chase
			bool chaseOk = ps.ballGoingUp &&
			               ps.boostedDuringChase &&
			               carBallAlign >= minCarBallAlign &&
			               ballGoalAlign >= minAlignment &&
			               carGoalAlign >= minAlignment &&
			               player.pos.z < state.ball.pos.z; // Car below ball
			
			// Continuous reward while satisfying chase (scales with alignment quality)
			if (chaseOk) {
				float alignScore = 0.5f * RS_MAX(0.0f, (ballGoalAlign - minAlignment) / (1.0f - minAlignment)) +
				                   0.5f * RS_MAX(0.0f, (carBallAlign - minCarBallAlign) / (1.0f - minCarBallAlign));
				reward += baseScale * alignScore;
			}
			
			// Bonus on second touch while conditions hold
			if ((player.ballTouchedStep || player.ballTouchedTick) && chaseOk) {
				reward += touchBonus * RS_CLAMP(ballGoalAlign, 0.0f, 1.0f);
				ps.inPop = false; // One-time reward per pop->chase->touch
			}
			
			// Abort if we land
			if (player.isOnGround)
				ps.inPop = false;
			
			return reward;
		}
	};

	// Rewards jumping and double jumping while ground dribbling, and aerial touches above minimum height
	// Encourages transitioning from ground dribbles to aerial play
	class GroundDribbleJumpReward : public Reward {
	private:
		float maxGroundBallHeight;   // Max ball height for ground dribble (default 340)
		float maxGroundCarHeight;     // Max car height for ground dribble (default 180)
		float maxDistance;            // Max car-ball distance for ground dribble (default 260)
		float minForwardSpeed;        // Min forward speed for ground dribble (default 350)
		float minAerialTouchHeight;   // Min ball height for aerial touch reward (default 200)
		float jumpReward;             // Reward for single jump (default 0.5)
		float doubleJumpReward;       // Reward for double jump (default 1.0)
		float aerialTouchReward;      // Reward for aerial touch above min height (default 0.8)
		
		struct DribbleState {
			bool inGroundDribble = false;
			bool wasOnGround = false;
			bool wasInGroundDribble = false; // Track previous frame's dribble state
			bool jumpRewarded = false;
			bool doubleJumpRewarded = false;
		};
		std::unordered_map<int, DribbleState> dribble;
		
	public:
		GroundDribbleJumpReward(
			float maxBallH = 340.0f, float maxCarH = 180.0f, float maxDist = 260.0f,
			float minFwd = 350.0f, float minAerialH = 200.0f,
			float jumpR = 0.5f, float doubleJumpR = 1.0f, float aerialTouchR = 0.8f)
			: maxGroundBallHeight(maxBallH), maxGroundCarHeight(maxCarH),
			  maxDistance(maxDist), minForwardSpeed(minFwd),
			  minAerialTouchHeight(minAerialH),
			  jumpReward(jumpR), doubleJumpReward(doubleJumpR), aerialTouchReward(aerialTouchR) {}
		
		virtual void Reset(const GameState& initialState) override {
			dribble.clear();
			for (const auto& p : initialState.players) {
				dribble[p.carId] = DribbleState();
			}
		}
		
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;
			
			int carId = player.carId;
			auto& ds = dribble[carId];
			float reward = 0.0f;
			
			// Check if in ground dribble state (ball low, car close, forward speed)
			bool isGroundDribble = player.isOnGround &&
			                       state.ball.pos.z <= maxGroundBallHeight &&
			                       player.pos.z <= maxGroundCarHeight &&
			                       (state.ball.pos - player.pos).Length() <= maxDistance &&
			                       player.vel.Dot(player.rotMat.forward) >= minForwardSpeed;
			
			// Check previous frame's actual state (recalculate from previous frame data)
			const Player& prevPlayer = state.prev->players[player.index];
			bool wasOnGround = prevPlayer.isOnGround;
			bool wasInGroundDribble = prevPlayer.isOnGround &&
			                           state.prev->ball.pos.z <= maxGroundBallHeight &&
			                           prevPlayer.pos.z <= maxGroundCarHeight &&
			                           (state.prev->ball.pos - prevPlayer.pos).Length() <= maxDistance &&
			                           prevPlayer.vel.Dot(prevPlayer.rotMat.forward) >= minForwardSpeed;
			
			// Update ground dribble state
			ds.wasInGroundDribble = ds.inGroundDribble;
			ds.inGroundDribble = isGroundDribble;
			ds.wasOnGround = wasOnGround;
			
			// Detect jump transition: was in ground dribble, now in air
			bool justLeftGround = wasInGroundDribble && wasOnGround && !player.isOnGround;
			
			// Reward single jump (when transitioning from ground dribble to air)
			if (justLeftGround && player.hasJumped && !player.hasDoubleJumped && !ds.jumpRewarded) {
				reward += jumpReward;
				ds.jumpRewarded = true;
			}
			
			// Reward double jump (higher reward, replaces single jump if both happen)
			if (justLeftGround && player.hasDoubleJumped && !ds.doubleJumpRewarded) {
				if (ds.jumpRewarded) {
					// Already gave single jump reward, so give difference
					reward += (doubleJumpReward - jumpReward);
				} else {
					// Give full double jump reward
					reward += doubleJumpReward;
				}
				ds.doubleJumpRewarded = true;
			}
			
			// Also check for delayed double jump (if we're in air and just double jumped after single jump)
			// This handles cases where double jump happens a few frames after single jump
			if (!player.isOnGround && player.hasDoubleJumped && !ds.doubleJumpRewarded && ds.jumpRewarded) {
				// Double jump happened after single jump - give the difference
				reward += (doubleJumpReward - jumpReward);
				ds.doubleJumpRewarded = true;
			}
			
			// Reward aerial touch above minimum height when ball is gaining height
			// Only reward if we jumped from a ground dribble (tracked by jumpRewarded flag)
			// Car should be below ball (proper aerial touch position)
			if ((ds.jumpRewarded || ds.doubleJumpRewarded) && 
			    (player.ballTouchedStep || player.ballTouchedTick) &&
			    state.ball.pos.z >= minAerialTouchHeight &&
			    state.ball.vel.z > 0 && // Ball gaining height
			    player.pos.z < state.ball.pos.z) { // Car below ball (proper aerial position)
				reward += aerialTouchReward;
			}
			
			// Reset jump tracking when back on ground
			if (player.isOnGround) {
				ds.jumpRewarded = false;
				ds.doubleJumpRewarded = false;
			}
			
			return reward;
		}
	};

}

