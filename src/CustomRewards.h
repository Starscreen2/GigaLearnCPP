#pragma once
#include <RLGymCPP/Rewards/Reward.h>
#include <RLGymCPP/Gamestates/GameState.h>
#include <unordered_map>

namespace RLGC {

	// Rewards aerial touches (touches while in air) with multipliers based on sustained aerial control
	// Multiplier increases every 0.5 seconds of sustained aerial ball control
	class AerialTouchReward : public Reward {
	private:
		float intervalSeconds; // Time interval for multiplier increases (default 0.5s)
		std::unordered_map<int, float> aerialControlTime; // Track aerial control time per player (by carId)
		std::unordered_map<int, float> lastAerialTouchTime; // Track last aerial touch time per player

	public:
		AerialTouchReward(float intervalSec = 0.5f) : intervalSeconds(intervalSec) {}

		virtual void Reset(const GameState& initialState) override {
			aerialControlTime.clear();
			lastAerialTouchTime.clear();
			// Initialize for all players
			for (const auto& player : initialState.players) {
				aerialControlTime[player.carId] = 0.0f;
				lastAerialTouchTime[player.carId] = 0.0f;
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			int carId = player.carId;
			float& controlTime = aerialControlTime[carId];

			// Check if player is in air and has ball contact
			bool isInAir = !player.isOnGround;
			bool hasBallContact = player.ballTouchedStep || player.ballTouchedTick;
			bool isAerialTouch = isInAir && player.ballTouchedStep; // New touch this step

			if (isInAir && hasBallContact) {
				// In air with ball contact - accumulate control time
				controlTime += state.deltaTime;
				
				if (isAerialTouch) {
					// Aerial touch! Calculate multiplier based on sustained control
					// Multiplier: 1.0 + (controlTime / intervalSeconds) * 0.5
					// So: 0.0s = 1.0x, 0.5s = 1.5x, 1.0s = 2.0x, 1.5s = 2.5x, etc.
					float multiplier = 1.0f + (controlTime / intervalSeconds) * 0.5f;
					lastAerialTouchTime[carId] = controlTime;
					
					return multiplier; // Base reward of 1.0 * multiplier
				}
				else {
					// Maintaining aerial control but no new touch - no reward, just accumulate time
					return 0;
				}
			}
			else {
				// Not in aerial control - reset control time
				controlTime = 0.0f;
				return 0;
			}
		}
	};

	// Bonus reward for scoring after sustained aerial ball control
	// Gives bonus points if goal is scored after maintaining aerial control for specified durations
	// Uses a decay-based approach: control time accumulates when in aerial control, decays slowly when not
	class SustainedAerialGoalReward : public Reward {
	private:
		float minDurationForBonus; // Minimum duration (e.g., 2.0s) to get bonus
		float maxDurationForBonus; // Maximum duration (e.g., 4.0s) for max bonus
		float decayRate; // How fast control time decays when not in aerial control (0.95 = 5% decay per step)
		std::unordered_map<int, float> aerialControlTime; // Track accumulated aerial control time per player
		std::unordered_map<int, float> maxRecentControlTime; // Track max control time in recent window

	public:
		SustainedAerialGoalReward(float minDuration = 2.0f, float maxDuration = 4.0f, float decay = 0.95f) 
			: minDurationForBonus(minDuration), maxDurationForBonus(maxDuration), decayRate(decay) {}

		virtual void Reset(const GameState& initialState) override {
			aerialControlTime.clear();
			maxRecentControlTime.clear();
			// Initialize for all players
			for (const auto& player : initialState.players) {
				aerialControlTime[player.carId] = 0.0f;
				maxRecentControlTime[player.carId] = 0.0f;
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			int carId = player.carId;
			float& controlTime = aerialControlTime[carId];
			float& maxControl = maxRecentControlTime[carId];

			// Check if player is in air and has ball contact
			bool isInAir = !player.isOnGround;
			bool hasBallContact = player.ballTouchedStep || player.ballTouchedTick;
			bool isAerialControl = isInAir && hasBallContact;

			if (isAerialControl) {
				// In aerial control - accumulate time
				controlTime += state.deltaTime;
				// Update max if this is a new peak
				if (controlTime > maxControl) {
					maxControl = controlTime;
				}
			}
			else {
				// Not in aerial control - decay slowly instead of resetting immediately
				// This allows brief losses of contact without completely resetting progress
				controlTime *= decayRate;
				// Also decay max, but more slowly to preserve recent achievements
				maxControl *= (decayRate + 0.02f); // Decay max slightly slower
			}

			// Check if goal was scored
			if (state.goalScored) {
				// Determine if this player's team scored
				bool scored = (player.team != RS_TEAM_FROM_Y(state.ball.pos.y));
				
				if (scored) {
					// Use the maximum control time achieved recently (more forgiving)
					float duration = maxControl;
					
					if (duration >= maxDurationForBonus) {
						// Max bonus for 4+ seconds
						return 1.0f; // Will be multiplied by weight (e.g., 100)
					}
					else if (duration >= minDurationForBonus) {
						// Partial bonus for 2-4 seconds
						// Linear interpolation: (duration - min) / (max - min)
						float ratio = (duration - minDurationForBonus) / (maxDurationForBonus - minDurationForBonus);
						return 0.5f + (ratio * 0.5f); // 0.5 to 1.0
					}
				}
				
				// Reset on goal (whether scored or not)
				controlTime = 0.0f;
				maxControl = 0.0f;
			}

			return 0;
		}
	};

	// Rewards being in the air AND close to the ball (encourages purposeful aerial play)
	// Helps prevent aimless flying by only rewarding aerials when near the ball
	class AerialProximityReward : public Reward {
	private:
		float maxDistance; // Maximum distance to reward (default 1000 units, ~1/3 field length)
		float minDistance; // Minimum distance to start rewarding (prevents reward when too close/on ground)

	public:
		AerialProximityReward(float maxDist = 1000.0f, float minDist = 200.0f) 
			: maxDistance(maxDist), minDistance(minDist) {}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Only reward when in the air
			if (player.isOnGround)
				return 0;

			// Calculate distance to ball
			float distToBall = (state.ball.pos - player.pos).Length();

			// Too close (likely on ground or touching) or too far - no reward
			if (distToBall < minDistance || distToBall > maxDistance)
				return 0;

			// Reward based on proximity: closer = higher reward
			// Normalize: 1.0 at minDistance, 0.0 at maxDistance
			float normalizedDist = (maxDistance - distToBall) / (maxDistance - minDistance);
			return normalizedDist; // Returns 0.0 to 1.0 based on proximity
		}
	};

	// Rewards double touches (two touches in quick succession)
	// Especially rewards high aerial double touches that go toward goal
	class DoubleTouchReward : public Reward {
	private:
		float minTimeBetweenTouches; // Minimum time between touches (default 0.3s - prevents same-tick double touches)
		float maxTimeBetweenTouches; // Maximum time window for double touch (default 4.0s)
		float minHeightForBonus; // Minimum height for height bonus (default 300 units)
		float maxHeightForBonus; // Maximum height for max bonus (default 1000 units)
		
		struct TouchInfo {
			float time;
			Vec ballPos;
			bool hasFirstTouch;
		};
		std::unordered_map<int, TouchInfo> lastTouchInfo;

	public:
		DoubleTouchReward(float minTime = 0.3f, float maxTime = 4.0f, float minHeight = 300.0f, float maxHeight = 1000.0f)
			: minTimeBetweenTouches(minTime), maxTimeBetweenTouches(maxTime), 
			  minHeightForBonus(minHeight), maxHeightForBonus(maxHeight) {}

		virtual void Reset(const GameState& initialState) override {
			lastTouchInfo.clear();
			for (const auto& player : initialState.players) {
				lastTouchInfo[player.carId] = { 0.0f, Vec(), false };
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			int carId = player.carId;
			TouchInfo& touchInfo = lastTouchInfo[carId];

			// Check if this is a new touch
			if (player.ballTouchedStep) {
				bool hadFirstTouch = touchInfo.hasFirstTouch;
				float timeSinceFirstTouch = touchInfo.time;
				
				// Check if this is a double touch (second touch within time window)
				if (hadFirstTouch && timeSinceFirstTouch >= minTimeBetweenTouches && timeSinceFirstTouch <= maxTimeBetweenTouches) {
					// Calculate reward components
					float baseReward = 0.5f;
					
					// Height bonus (higher = more reward)
					float ballHeight = state.ball.pos.z;
					float heightScore = 0.0f;
					if (ballHeight >= minHeightForBonus) {
						if (ballHeight >= maxHeightForBonus) {
							heightScore = 1.0f; // Max height bonus
						} else {
							// Linear interpolation between min and max height
							heightScore = (ballHeight - minHeightForBonus) / (maxHeightForBonus - minHeightForBonus);
						}
					}
					
					// Aerial bonus (in air = extra reward)
					float aerialBonus = (!player.isOnGround) ? 0.5f : 0.0f;
					
					// Direction toward goal bonus - STRONG CHECK: only reward if on target
					Vec targetGoal = (player.team == Team::BLUE) ? 
						CommonValues::ORANGE_GOAL_CENTER : CommonValues::BLUE_GOAL_CENTER;
					Vec dirToGoal = (targetGoal - state.ball.pos).Normalized();
					
					float reward = baseReward;
					if (state.ball.vel.Length() > FLT_EPSILON && dirToGoal.Length() > FLT_EPSILON) {
						Vec ballVelDir = state.ball.vel.Normalized();
						float goalAlignment = ballVelDir.Dot(dirToGoal);
						
						// CRITICAL: Only reward if ball is actually heading toward goal (on target)
						if (goalAlignment < 0.5f) {
							// Ball is not on target - return 0 to prevent mindless double touches
							touchInfo.hasFirstTouch = false;
							touchInfo.time = 0.0f;
							return 0;
						}
						
						float directionBonus = RS_MAX(0.0f, goalAlignment) * 0.3f; // Up to 0.3 bonus
						
						// Check if ball is heading toward own goal (dangerous)
						Vec ownGoal = (player.team == Team::BLUE) ? 
							CommonValues::BLUE_GOAL_CENTER : CommonValues::ORANGE_GOAL_CENTER;
						Vec dirToOwnGoal = (ownGoal - state.ball.pos).Normalized();
						float ownGoalAlignment = ballVelDir.Dot(dirToOwnGoal);
						
						if (ownGoalAlignment > 0.3f) {
							// Ball heading toward own goal - punish this
							touchInfo.hasFirstTouch = false;
							touchInfo.time = 0.0f;
							return -0.2f; // Small punishment
						}
						
						reward = baseReward + (heightScore * 0.8f) + aerialBonus + directionBonus;
					} else {
						// No velocity or goal direction - not on target
						touchInfo.hasFirstTouch = false;
						touchInfo.time = 0.0f;
						return 0;
					}
					
					// Time bonus (faster follow-up = more reward, but not too fast)
					// Optimal time is around 1-2 seconds
					float timeScore = 1.0f;
					if (timeSinceFirstTouch < 1.0f) {
						// Too fast - reduce reward slightly
						timeScore = 0.7f + (timeSinceFirstTouch / 1.0f) * 0.3f;
					} else if (timeSinceFirstTouch > 2.5f) {
						// Too slow - reduce reward
						timeScore = 1.0f - ((timeSinceFirstTouch - 2.5f) / (maxTimeBetweenTouches - 2.5f)) * 0.4f;
					}
					
					reward *= timeScore;
					
					// Reset for next potential double touch
					touchInfo.hasFirstTouch = false;
					touchInfo.time = 0.0f;
					
					return reward;
				}
				
				// Update touch info for next potential double touch
				touchInfo.hasFirstTouch = true;
				touchInfo.time = 0.0f; // Reset timer
				touchInfo.ballPos = state.ball.pos;
			} else {
				// Accumulate time since last touch
				if (touchInfo.hasFirstTouch) {
					touchInfo.time += state.deltaTime;
					
					// Reset if too much time has passed
					if (touchInfo.time > maxTimeBetweenTouches) {
						touchInfo.hasFirstTouch = false;
						touchInfo.time = 0.0f;
					}
				}
			}

			return 0;
		}
	};

	// Rewards double touches that involve wall bounces
	// Different rewards for different wall types:
	// - Opponent backwall: Highest reward (most dangerous)
	// - Sidewalls: Medium reward (good for redirects)
	// - Own backwall: Lower reward (defensive plays)
	// - Ceiling: Punishment (wastes ball momentum)
	class WallDoubleTouchReward : public Reward {
	private:
		float minTimeBetweenTouches;
		float maxTimeBetweenTouches;
		float wallDetectionRadius;
		float minVelocityChange;
		float ceilingPunishmentMultiplier;
		
		enum WallType {
			NONE = 0,
			SIDE_WALL = 1,
			OPPONENT_BACKWALL = 2,
			OWN_BACKWALL = 3,
			CEILING = 4
		};
		
		struct TouchInfo {
			float time;
			Vec ballPos;
			Vec ballVel;
			bool hasFirstTouch;
			WallType lastWallHit;
			float timeSinceWallHit;
			bool touchedTowardCeiling;
			float timeSinceCeilingTouch;
		};
		std::unordered_map<int, TouchInfo> lastTouchInfo;

		WallType DetectWallBounce(const Vec& ballPos, const Vec& prevVel, const Vec& currVel, Team playerTeam) {
			using namespace CommonValues;
			
			float velChange = (currVel - prevVel).Length();
			if (velChange < minVelocityChange)
				return NONE;
			
			// Side walls
			if (abs(ballPos.x) > SIDE_WALL_X - wallDetectionRadius) {
				if ((prevVel.x > 0 && currVel.x < 0) || (prevVel.x < 0 && currVel.x > 0)) {
					return SIDE_WALL;
				}
			}
			
			// Back walls
			if (abs(ballPos.y) > BACK_WALL_Y - wallDetectionRadius) {
				if ((prevVel.y > 0 && currVel.y < 0) || (prevVel.y < 0 && currVel.y > 0)) {
					bool isOpponentBackwall = (playerTeam == Team::BLUE && ballPos.y > 0) ||
					                          (playerTeam == Team::ORANGE && ballPos.y < 0);
					return isOpponentBackwall ? OPPONENT_BACKWALL : OWN_BACKWALL;
				}
			}
			
			// Ceiling - detect bounce DOWN (ball hit ceiling and came back)
			if (ballPos.z > CEILING_Z - wallDetectionRadius) {
				if (prevVel.z > 0 && currVel.z < 0) {
					return CEILING;
				}
			}
			
			return NONE;
		}
		
		bool IsHeadingTowardCeiling(const Vec& ballVel, const Vec& ballPos) {
			using namespace CommonValues;
			
			if (ballVel.z < 500.0f)
				return false;
			
			if (ballPos.z < 1000.0f)
				return false;
			
			float upwardComponent = ballVel.z;
			float horizontalComponent = Vec(ballVel.x, ballVel.y, 0).Length();
			
			return upwardComponent > horizontalComponent * 0.5f;
		}

	public:
		WallDoubleTouchReward(float minTime = 0.3f, float maxTime = 5.0f, float wallRadius = 200.0f, 
		                       float minVelChange = 500.0f, float ceilingPunishMult = 0.5f)
			: minTimeBetweenTouches(minTime), maxTimeBetweenTouches(maxTime),
			  wallDetectionRadius(wallRadius), minVelocityChange(minVelChange),
			  ceilingPunishmentMultiplier(ceilingPunishMult) {}

		virtual void Reset(const GameState& initialState) override {
			lastTouchInfo.clear();
			for (const auto& player : initialState.players) {
				lastTouchInfo[player.carId] = { 0.0f, Vec(), Vec(), false, NONE, 0.0f, false, 0.0f };
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			int carId = player.carId;
			TouchInfo& touchInfo = lastTouchInfo[carId];

			// Check for wall bounce
			WallType wallHit = DetectWallBounce(state.ball.pos, state.prev->ball.vel, state.ball.vel, player.team);
			
			// Check if ceiling was hit after an intentional touch
			if (wallHit == CEILING && touchInfo.touchedTowardCeiling) {
				// Player touched ball toward ceiling and it hit - PUNISH THIS
				float timeSinceTouch = touchInfo.timeSinceCeilingTouch;
				
				float punishment = -0.8f;
				
				if (touchInfo.hasFirstTouch) {
					punishment = -1.2f;
				}
				
				if (timeSinceTouch < 1.0f) {
					punishment *= 1.5f;
				}
				
				touchInfo.touchedTowardCeiling = false;
				touchInfo.timeSinceCeilingTouch = 0.0f;
				touchInfo.lastWallHit = NONE;
				
				return punishment;
			}
			
			if (wallHit != NONE && touchInfo.hasFirstTouch && wallHit != CEILING) {
				touchInfo.lastWallHit = wallHit;
				touchInfo.timeSinceWallHit = 0.0f;
			}

			// Check if this is a new touch
			if (player.ballTouchedStep) {
				// Check if touch sends ball toward ceiling
				if (IsHeadingTowardCeiling(state.ball.vel, state.ball.pos)) {
					touchInfo.touchedTowardCeiling = true;
					touchInfo.timeSinceCeilingTouch = 0.0f;
				}
				
				// Check if this is a double touch after wall bounce
				if (touchInfo.hasFirstTouch && touchInfo.lastWallHit != NONE && touchInfo.lastWallHit != CEILING) {
					float timeSinceFirstTouch = touchInfo.time;
					float timeSinceWall = touchInfo.timeSinceWallHit;
					
					if (timeSinceFirstTouch >= minTimeBetweenTouches && 
					    timeSinceFirstTouch <= maxTimeBetweenTouches &&
					    timeSinceWall <= 2.0f) {
						
						// REMOVED: Own backwall double touches (keep clears, remove double touch rewards)
						if (touchInfo.lastWallHit == OWN_BACKWALL) {
							touchInfo.hasFirstTouch = false;
							touchInfo.time = 0.0f;
							touchInfo.lastWallHit = NONE;
							touchInfo.timeSinceWallHit = 0.0f;
							return 0; // No reward for own backwall double touches
						}
						
						float baseReward = 0.0f;
						float wallMultiplier = 1.0f;
						
						switch (touchInfo.lastWallHit) {
							case OPPONENT_BACKWALL:
								baseReward = 1.0f;
								wallMultiplier = 1.5f;
								break;
							case SIDE_WALL:
								baseReward = 0.6f;
								wallMultiplier = 1.2f;
								break;
							default:
								return 0;
						}
						
						float ballHeight = state.ball.pos.z;
						float heightBonus = 0.0f;
						if (ballHeight > 300.0f && ballHeight < 1500.0f) {
							heightBonus = RS_MIN(1.0f, (ballHeight - 300.0f) / 700.0f);
						}
						
						float aerialBonus = (!player.isOnGround) ? 0.3f : 0.0f;
						
						Vec targetGoal = (player.team == Team::BLUE) ? 
							CommonValues::ORANGE_GOAL_CENTER : CommonValues::BLUE_GOAL_CENTER;
						Vec dirToGoal = (targetGoal - state.ball.pos).Normalized();
						
						float directionBonus = 0.0f;
						
						// CRITICAL: Only reward if ball is on target toward opponent's goal
						if (state.ball.vel.Length() > FLT_EPSILON && dirToGoal.Length() > FLT_EPSILON) {
							Vec ballVelDir = state.ball.vel.Normalized();
							float goalAlignment = ballVelDir.Dot(dirToGoal);
							
							// Must be heading toward goal (on target)
							if (goalAlignment < 0.5f) {
								// Not on target - return 0
								touchInfo.hasFirstTouch = false;
								touchInfo.time = 0.0f;
								touchInfo.lastWallHit = NONE;
								touchInfo.timeSinceWallHit = 0.0f;
								return 0;
							}
							
							// Check if ball bounces back toward own goal (dangerous)
							Vec ownGoal = (player.team == Team::BLUE) ? 
								CommonValues::BLUE_GOAL_CENTER : CommonValues::ORANGE_GOAL_CENTER;
							Vec dirToOwnGoal = (ownGoal - state.ball.pos).Normalized();
							float ownGoalAlignment = ballVelDir.Dot(dirToOwnGoal);
							
							if (ownGoalAlignment > 0.3f) {
								// Ball heading toward own goal - punish
								touchInfo.hasFirstTouch = false;
								touchInfo.time = 0.0f;
								touchInfo.lastWallHit = NONE;
								touchInfo.timeSinceWallHit = 0.0f;
								return -0.2f; // Small punishment
							}
							
							directionBonus = RS_MAX(0.0f, goalAlignment) * 0.2f;
						} else {
							// No valid velocity or goal direction - not on target
							touchInfo.hasFirstTouch = false;
							touchInfo.time = 0.0f;
							touchInfo.lastWallHit = NONE;
							touchInfo.timeSinceWallHit = 0.0f;
							return 0;
						}
						
						float timeScore = 1.0f;
						if (timeSinceWall < 0.5f) {
							timeScore = 1.2f;
						} else if (timeSinceWall > 1.5f) {
							timeScore = 0.8f;
						}
						
						float reward = (baseReward + heightBonus * 0.5f + aerialBonus + directionBonus) * wallMultiplier * timeScore;
						
						touchInfo.hasFirstTouch = false;
						touchInfo.time = 0.0f;
						touchInfo.lastWallHit = NONE;
						touchInfo.timeSinceWallHit = 0.0f;
						
						return reward;
					}
				}
				
				touchInfo.hasFirstTouch = true;
				touchInfo.time = 0.0f;
				touchInfo.ballPos = state.ball.pos;
				touchInfo.ballVel = state.ball.vel;
			} else {
				if (touchInfo.hasFirstTouch) {
					touchInfo.time += state.deltaTime;
					
					if (touchInfo.lastWallHit != NONE) {
						touchInfo.timeSinceWallHit += state.deltaTime;
					}
					
					if (touchInfo.time > maxTimeBetweenTouches) {
						touchInfo.hasFirstTouch = false;
						touchInfo.time = 0.0f;
						touchInfo.lastWallHit = NONE;
						touchInfo.timeSinceWallHit = 0.0f;
					}
				}
				
				if (touchInfo.touchedTowardCeiling) {
					touchInfo.timeSinceCeilingTouch += state.deltaTime;
					
					if (touchInfo.timeSinceCeilingTouch > 3.0f) {
						touchInfo.touchedTowardCeiling = false;
						touchInfo.timeSinceCeilingTouch = 0.0f;
					}
				}
			}

			return 0;
		}
	};

	// Major reward for scoring goals from double touches (within 5 seconds)
	// Accounts for double touches off own backwall into opponent's net
	class DoubleTouchGoalReward : public Reward {
	private:
		float maxTimeWindow;
		float wallDetectionRadius;
		float minVelocityChange;
		
		enum WallType {
			NONE = 0,
			SIDE_WALL = 1,
			OPPONENT_BACKWALL = 2,
			OWN_BACKWALL = 3,
			CEILING = 4
		};
		
		struct DoubleTouchSequence {
			float firstTouchTime;
			float secondTouchTime;
			Vec firstTouchPos;
			Vec secondTouchPos;
			bool hasFirstTouch;
			bool hasSecondTouch;
			WallType wallHit;
			bool firstTouchWasAerial;
			bool secondTouchWasAerial;
			float ballHeightAtSecondTouch;
		};
		std::unordered_map<int, DoubleTouchSequence> sequences;

		WallType DetectWallBounce(const Vec& ballPos, const Vec& prevVel, const Vec& currVel, Team playerTeam) {
			using namespace CommonValues;
			
			float velChange = (currVel - prevVel).Length();
			if (velChange < minVelocityChange)
				return NONE;
			
			if (abs(ballPos.x) > SIDE_WALL_X - wallDetectionRadius) {
				if ((prevVel.x > 0 && currVel.x < 0) || (prevVel.x < 0 && currVel.x > 0)) {
					return SIDE_WALL;
				}
			}
			
			if (abs(ballPos.y) > BACK_WALL_Y - wallDetectionRadius) {
				if ((prevVel.y > 0 && currVel.y < 0) || (prevVel.y < 0 && currVel.y > 0)) {
					bool isOpponentBackwall = (playerTeam == Team::BLUE && ballPos.y > 0) ||
					                          (playerTeam == Team::ORANGE && ballPos.y < 0);
					return isOpponentBackwall ? OPPONENT_BACKWALL : OWN_BACKWALL;
				}
			}
			
			if (ballPos.z > CEILING_Z - wallDetectionRadius) {
				if (prevVel.z > 0 && currVel.z < 0) {
					return CEILING;
				}
			}
			
			return NONE;
		}

	public:
		DoubleTouchGoalReward(float maxTime = 5.0f, float wallRadius = 200.0f, float minVelChange = 500.0f)
			: maxTimeWindow(maxTime), wallDetectionRadius(wallRadius), minVelocityChange(minVelChange) {}

		virtual void Reset(const GameState& initialState) override {
			sequences.clear();
			for (const auto& player : initialState.players) {
				sequences[player.carId] = { 0.0f, 0.0f, Vec(), Vec(), false, false, NONE, false, false, 0.0f };
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			int carId = player.carId;
			DoubleTouchSequence& seq = sequences[carId];

			if (state.goalScored) {
				bool scored = (player.team != RS_TEAM_FROM_Y(state.ball.pos.y));
				
				if (scored && seq.hasFirstTouch) {
					float timeSinceFirstTouch = seq.firstTouchTime;
					
					if (timeSinceFirstTouch <= maxTimeWindow) {
						float baseReward = 2.0f;
						
						if (seq.hasSecondTouch) {
							baseReward = 3.0f;
							
							if (seq.wallHit == OWN_BACKWALL) {
								baseReward += 1.5f;
							}
							
							if (seq.wallHit == OPPONENT_BACKWALL) {
								baseReward += 1.0f;
							}
							
							if (seq.wallHit == SIDE_WALL) {
								baseReward += 0.5f;
							}
							
							if (seq.firstTouchWasAerial && seq.secondTouchWasAerial) {
								baseReward += 1.0f;
							} else if (seq.secondTouchWasAerial) {
								baseReward += 0.5f;
							}
							
							if (seq.ballHeightAtSecondTouch > 300.0f) {
								float heightBonus = RS_MIN(1.0f, (seq.ballHeightAtSecondTouch - 300.0f) / 700.0f);
								baseReward += heightBonus * 0.8f;
							}
							
							float timeBetweenTouches = seq.secondTouchTime - seq.firstTouchTime;
							if (timeBetweenTouches > 0.0f) {
								if (timeBetweenTouches < 1.0f) {
									baseReward += 0.5f;
								} else if (timeBetweenTouches < 2.0f) {
									baseReward += 0.3f;
								}
							}
						} else {
							baseReward = 1.0f;
						}
						
						if (timeSinceFirstTouch < 2.0f) {
							baseReward += 0.5f;
						} else if (timeSinceFirstTouch < 3.0f) {
							baseReward += 0.3f;
						}
						
						seq.hasFirstTouch = false;
						seq.hasSecondTouch = false;
						seq.firstTouchTime = 0.0f;
						seq.secondTouchTime = 0.0f;
						seq.wallHit = NONE;
						
						return baseReward;
					}
				}
				
				seq.hasFirstTouch = false;
				seq.hasSecondTouch = false;
				seq.firstTouchTime = 0.0f;
				seq.secondTouchTime = 0.0f;
				seq.wallHit = NONE;
			}

			if (player.ballTouchedStep) {
				if (!seq.hasFirstTouch) {
					seq.hasFirstTouch = true;
					seq.firstTouchTime = 0.0f;
					seq.firstTouchPos = state.ball.pos;
					seq.firstTouchWasAerial = !player.isOnGround;
				} else if (!seq.hasSecondTouch) {
					seq.hasSecondTouch = true;
					seq.secondTouchTime = seq.firstTouchTime;
					seq.secondTouchPos = state.ball.pos;
					seq.secondTouchWasAerial = !player.isOnGround;
					seq.ballHeightAtSecondTouch = state.ball.pos.z;
				}
			}

			if (seq.hasFirstTouch && !seq.hasSecondTouch) {
				WallType wallHit = DetectWallBounce(state.ball.pos, state.prev->ball.vel, state.ball.vel, player.team);
				if (wallHit != NONE && wallHit != CEILING) {
					seq.wallHit = wallHit;
				}
			}

			if (seq.hasFirstTouch) {
				seq.firstTouchTime += state.deltaTime;
				
				if (seq.firstTouchTime > maxTimeWindow) {
					seq.hasFirstTouch = false;
					seq.hasSecondTouch = false;
					seq.firstTouchTime = 0.0f;
					seq.secondTouchTime = 0.0f;
					seq.wallHit = NONE;
				}
			}

			return 0;
		}
	};

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

	// Punishes own goals to prevent bot from scoring on itself
	class OwnGoalPunishment : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!isFinal || !state.goalScored)
				return 0;

			// Detect own goal: ball in own half when goal scored
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
			Vec targetGoal = (player.team == Team::BLUE) ? 
				CommonValues::ORANGE_GOAL_CENTER : CommonValues::BLUE_GOAL_CENTER;
			Vec dirToGoal = (targetGoal - state.ball.pos).Normalized();
			float goalAlignment = 0.0f;
			if (state.ball.vel.Length() > FLT_EPSILON && dirToGoal.Length() > FLT_EPSILON) {
				Vec ballVelDir = state.ball.vel.Normalized();
				goalAlignment = ballVelDir.Dot(dirToGoal);
			}

			// Require minimum goal alignment (on target)
			if (goalAlignment < 0.3f) {
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

			// Track if in air dribble sequence
			if (isInAir && hasBallContact) {
				inAirDribble[carId] = true;
			} else if (!isInAir || !hasBallContact) {
				inAirDribble[carId] = false;
			}

			// Only reward when in air dribble and boosting
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

	// Rewards air rolling during air dribbles
	class AirRollReward : public Reward {
	private:
		float maxDistance;

	public:
		AirRollReward(float maxDist = 500.0f) : maxDistance(maxDist) {}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!player.prev)
				return 0;

			// Only reward when in air, near ball, and air rolling
			if (!player.isOnGround) {
				float distToBall = (state.ball.pos - player.pos).Length();
				
				if (distToBall < maxDistance) {
					// Check if air rolling (roll input)
					float rollInput = abs(player.prevAction.roll);
					
					if (rollInput > 0.1f) {
						// Continuous reward while air rolling near ball
						return rollInput * 0.3f; // Scale by roll intensity
					}
				}
			}

			return 0;
		}
	};

	// Low-priority reward for flip resets - use when available, don't seek
	class FlipResetReward : public Reward {
	private:
		std::unordered_map<int, bool> hadFlipReset;
		std::unordered_map<int, bool> usedFlipReset;

	public:
		virtual void Reset(const GameState& initialState) override {
			hadFlipReset.clear();
			usedFlipReset.clear();
			for (const auto& player : initialState.players) {
				hadFlipReset[player.carId] = false;
				usedFlipReset[player.carId] = false;
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!player.prev)
				return 0;

			int carId = player.carId;
			bool hasFlipReset = player.HasFlipReset();
			bool prevHadFlipReset = hadFlipReset[carId];

			// Detect when flip reset is obtained
			if (hasFlipReset && !prevHadFlipReset) {
				hadFlipReset[carId] = true;
				// Small reward for obtaining flip reset naturally
				return 0.3f;
			}

			// Detect when flip reset is used (flip while having reset)
			if (prevHadFlipReset && !hasFlipReset && !player.isOnGround) {
				usedFlipReset[carId] = true;
				hadFlipReset[carId] = false;
				
				// Check if flip was used toward ball/goal
				Vec dirToBall = (state.ball.pos - player.pos).Normalized();
				if (player.vel.Length() > FLT_EPSILON) {
					Vec velDir = player.vel.Normalized();
					float alignment = velDir.Dot(dirToBall);
					
					if (alignment > 0.3f) {
						// Flip reset used effectively
						return 0.5f;
					}
				}
				
				return 0.2f; // Small reward for using flip reset
			}

			hadFlipReset[carId] = hasFlipReset;
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
				
				// Bonus if player has boost
				if (player.boost > 30.0f) {
					baseReward += 0.2f;
				}
				
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

			// Track air dribble start
			if (isInAir && hasBallContact && !inAirDribble[carId]) {
				inAirDribble[carId] = true;
				startPos[carId] = player.pos;
				startTime[carId] = 0.0f;
			}

			// Update time
			if (inAirDribble[carId]) {
				startTime[carId] += state.deltaTime;
				
				// Reset if too much time passed or lost contact
				if (startTime[carId] > maxTimeWindow || (!isInAir && !hasBallContact)) {
					inAirDribble[carId] = false;
					startTime[carId] = 0.0f;
				}
			}

			// Reward on goal scored from air dribble
			if (state.goalScored && inAirDribble[carId]) {
				bool scored = (player.team != RS_TEAM_FROM_Y(state.ball.pos.y));
				
				if (scored && startTime[carId] <= maxTimeWindow) {
					float distance = (player.pos - startPos[carId]).Length();
					float baseReward = 1.0f;
					
					// Scale reward by distance (capped at 3.0x)
					float distanceMultiplier = RS_MIN(3.0f, 1.0f + (distance / 2000.0f));
					
					inAirDribble[carId] = false;
					startTime[carId] = 0.0f;
					
					return baseReward * distanceMultiplier;
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

// Detects truly open nets (guaranteed unsaveable) and rewards taking shots
	// Only rewards when ball is GUARANTEED to go in, not just "probably"
	// Also punishes giving the ball away to opponents (counter-attack risk)
	class OpenNetReward : public Reward {
	private:
		float defenderDetectionRadius;
		float minBallSpeedForUnsaveable;
		constexpr static float GOAL_THRESHOLD_Y = 5120.0f; // BACK_WALL_Y
		constexpr static float GOAL_HALF_WIDTH = 892.755f;
		constexpr static float GOAL_HEIGHT = 642.775f;
		constexpr static float GRAVITY_Z = -650.0f;
		
		// Calculate time for ball to reach goal (matches Arena::IsBallProbablyGoingIn logic)
		float CalculateTimeToGoal(const Vec& ballPos, const Vec& ballVel) {
			if (abs(ballVel.y) < FLT_EPSILON) {
				return 999.0f;
			}
			
			float scoreDirSgn = RS_SGN(ballVel.y);
			float goalY = GOAL_THRESHOLD_Y * scoreDirSgn;
			float distToGoal = abs(ballPos.y - goalY);
			float timeToGoal = distToGoal / abs(ballVel.y);
			
			return RS_MIN(timeToGoal, 5.0f);
		}
		
		// Calculate where ball will be at future time (with gravity)
		Vec CalculateBallPositionAtTime(const Vec& ballPos, const Vec& ballVel, float time) {
			Vec gravity = Vec(0, 0, GRAVITY_Z);
			return ballPos + (ballVel * time) + (gravity * time * time * 0.5f);
		}
		
		// Determine which goal ball is heading toward based on velocity
		Vec GetTargetGoalFromVelocity(const Vec& ballVel) {
			float scoreDirSgn = RS_SGN(ballVel.y);
			float goalY = GOAL_THRESHOLD_Y * scoreDirSgn;
			return Vec(0, goalY, GOAL_HEIGHT / 2);
		}
		
		// Check if ball is GUARANTEED to score (strict requirements)
		bool IsBallGuaranteedToScore(const Vec& ballPos, const Vec& ballVel) {
			if (abs(ballVel.y) < FLT_EPSILON) {
				return false;
			}
			
			float scoreDirSgn = RS_SGN(ballVel.y);
			float goalY = GOAL_THRESHOLD_Y * scoreDirSgn;
			float distToGoal = abs(ballPos.y - goalY);
			float timeToGoal = distToGoal / abs(ballVel.y);
			
			// STRICT: Must be close to goal (within 2 seconds)
			if (timeToGoal > 2.0f) {
				return false; // Too far away - not guaranteed
			}
			
			// STRICT: Ball must be moving fast enough to be unsaveable
			if (ballVel.Length() < minBallSpeedForUnsaveable) {
				return false; // Too slow - can be saved
			}
			
			// Calculate where ball will be (with gravity)
			Vec extrapPosWhenScore = CalculateBallPositionAtTime(ballPos, ballVel, timeToGoal);
			
			// STRICT: Ball must be WELL on target (smaller margin = more certain)
			float margin = 100.0f; // Reduced from 200 - stricter requirement
			
			// Check height - Arena code uses: if (z > GOAL_HEIGHT + margin) reject
			// So we allow up to GOAL_HEIGHT + margin, reject if above that
			if (extrapPosWhenScore.z > GOAL_HEIGHT + margin) {
				return false; // Too high - will hit crossbar
			}
			
			if (extrapPosWhenScore.z < 100.0f) {
				return false; // Too low - might hit ground
			}
			
			// Check width - Arena code uses: if (abs(x) > GOAL_HALF_WIDTH + margin) reject
			// So we allow up to GOAL_HALF_WIDTH + margin, reject if beyond that
			if (abs(extrapPosWhenScore.x) > GOAL_HALF_WIDTH + margin) {
				return false; // Too far to the side - will hit post
			}
			
			// STRICT: Ball must be heading directly at goal (not just toward it)
			Vec dirToGoal = (GetTargetGoalFromVelocity(ballVel) - ballPos).Normalized();
			Vec ballVelDir = ballVel.Normalized();
			float goalAlignment = ballVelDir.Dot(dirToGoal);
			
			if (goalAlignment < 0.85f) {
				return false; // Not heading directly at goal - might miss
			}
			
			return true;
		}
		
		// Estimate if opponent can intercept/save the ball (conservative - assume they can save unless proven otherwise)
		bool CanOpponentSave(const Player& player, const GameState& state, float timeToGoal) {
			Vec ballAtGoal = CalculateBallPositionAtTime(state.ball.pos, state.ball.vel, timeToGoal);
			Vec targetGoal = GetTargetGoalFromVelocity(state.ball.vel);
			
			for (const auto& opponent : state.players) {
				if (opponent.team == player.team) {
					continue;
				}
				
				// CONSERVATIVE: Check larger radius - assume opponents can defend from further away
				float distToGoal = (opponent.pos - targetGoal).Length();
				
				if (distToGoal > defenderDetectionRadius * 1.5f) {
					continue; // Way too far
				}
				
				// Check if opponent is between ball and goal
				Vec dirToGoal = (targetGoal - state.ball.pos).Normalized();
				Vec dirToOpponent = (opponent.pos - state.ball.pos).Normalized();
				float alignment = dirToGoal.Dot(dirToOpponent);
				
				if (alignment < 0.1f) {
					continue; // Not between ball and goal
				}
				
				// CONSERVATIVE: Assume opponent can move faster (more generous speed estimate)
				Vec dirToBallTrajectory = (ballAtGoal - opponent.pos);
				float distToBallTrajectory = dirToBallTrajectory.Length();
				
				float opponentSpeed = opponent.vel.Length();
				// More generous speed assumption - assume they can boost/aerial
				float effectiveSpeed = RS_MAX(opponentSpeed, CommonValues::CAR_MAX_SPEED * 0.6f);
				
				// Account for aerial saves
				float heightDiff = abs(ballAtGoal.z - opponent.pos.z);
				if (heightDiff > 100.0f) {
					// Even if aerial, assume they can move reasonably fast
					effectiveSpeed *= 0.8f; // Less penalty for aerial
				}
				
				// Safety check: prevent division by zero
				if (effectiveSpeed < FLT_EPSILON) {
					continue; // Opponent not moving - can't save
				}
				
				float timeToReachBall = distToBallTrajectory / effectiveSpeed;
				
				// CONSERVATIVE: Less margin needed (assume good players react faster)
				timeToReachBall += 0.2f; // Reduced from 0.4f
				
				// If opponent MIGHT be able to save, assume they can
				if (timeToReachBall < timeToGoal + 0.1f) { // Added buffer - if close, assume they can save
					return true; // Opponent can likely save
				}
			}
			
			return false; // No opponent can save
		}
		
		// Detect if player is giving ball away
		bool IsGivingBallAway(const Player& player, const GameState& state) {
			if (!state.prev || !player.ballTouchedStep) {
				return false;
			}
			
			float ownGoalY = (player.team == Team::BLUE) ? -GOAL_THRESHOLD_Y : GOAL_THRESHOLD_Y;
			float ballY = state.ball.pos.y;
			float ballVelY = state.ball.vel.y;
			
			float ownGoalDirSgn = RS_SGN(ownGoalY - ballY);
			if (RS_SGN(ballVelY) == ownGoalDirSgn && abs(ballVelY) > 500.0f) {
				return true;
			}
			
			bool ballInOpponentHalf = (player.team == Team::BLUE && state.ball.pos.y > 0) ||
			                          (player.team == Team::ORANGE && state.ball.pos.y < 0);
			
			if (ballInOpponentHalf) {
				Vec dirToBall = (state.ball.pos - player.pos).Normalized();
				Vec ballVelDir = state.ball.vel.Normalized();
				float controlAlignment = ballVelDir.Dot(dirToBall);
				
				if (controlAlignment < -0.3f && state.ball.vel.Length() > 300.0f) {
					return true;
				}
			}
			
			return false;
		}

	public:
		OpenNetReward(float defenderRadius = 2000.0f, float minSpeed = 1000.0f) // Increased minSpeed default
			: defenderDetectionRadius(defenderRadius), 
			  minBallSpeedForUnsaveable(minSpeed) {}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;
			
			// PUNISHMENT: Detect if player is giving ball away
			if (IsGivingBallAway(player, state)) {
				return -0.5f;
			}
			
			float ballVelY = state.ball.vel.y;
			if (abs(ballVelY) < FLT_EPSILON) {
				return 0;
			}
			
			float scoreDirSgn = RS_SGN(ballVelY);
			bool headingToOpponentGoal = (player.team == Team::BLUE && scoreDirSgn > 0) ||
			                             (player.team == Team::ORANGE && scoreDirSgn < 0);
			
			if (!headingToOpponentGoal) {
				return 0;
			}
			
			// STRICT: Only reward if ball is GUARANTEED to score
			if (!IsBallGuaranteedToScore(state.ball.pos, state.ball.vel)) {
				return 0; // Not guaranteed - don't reward
			}
			
			float timeToGoal = CalculateTimeToGoal(state.ball.pos, state.ball.vel);
			
			if (timeToGoal > 2.0f) {
				return 0; // Too far - not guaranteed
			}
			
			// CONSERVATIVE: Assume opponents can save unless we're very confident they can't
			bool canSave = CanOpponentSave(player, state, timeToGoal);
			
			if (canSave) {
				return 0; // Opponents might save - not guaranteed
			}
			
			// Ball is GUARANTEED to go in!
			
			if (player.eventState.shot) {
				return 2.0f;
			}
			
			if (state.goalScored && isFinal) {
				bool scored = (player.team != RS_TEAM_FROM_Y(state.ball.pos.y));
				if (scored) {
					return 3.0f;
				}
			}
			
			// Continuous reward (only when guaranteed)
			float ballSpeed = state.ball.vel.Length();
			Vec targetGoal = GetTargetGoalFromVelocity(state.ball.vel);
			float distToGoal = (state.ball.pos - targetGoal).Length();
			float speedScore = RS_MIN(1.0f, (ballSpeed - minBallSpeedForUnsaveable) / 2000.0f);
			float distanceScore = RS_MAX(0.0f, 1.0f - (distToGoal / 3000.0f)); // Reduced from 4000
			
			return speedScore * distanceScore * 0.4f;
		}
	};
}

