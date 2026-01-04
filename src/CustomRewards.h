#pragma once
#include <RLGymCPP/Rewards/Reward.h>
#include <RLGymCPP/Gamestates/GameState.h>
#include <RLGymCPP/CommonValues.h>
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

	// Rewards keeping the ball on top of the car (works for ground AND wall dribbles)
	// Uses car's local coordinate frame to work regardless of car orientation
	class DribbleReward : public Reward {
	private:
		float minHeight; // Minimum height above car in local space (default 100 UU)
		float maxDistance; // Maximum distance from car (default 150 UU)
		float intervalSeconds; // Time interval for multiplier increases (default 0.5s)
		std::unordered_map<int, float> dribbleTime; // Track dribble time per player
		std::unordered_map<int, bool> wasDribbling; // Track previous dribble state

	public:
		DribbleReward(float heightThreshold = 100.0f, float distanceThreshold = 150.0f, float intervalSec = 0.5f)
			: minHeight(heightThreshold), maxDistance(distanceThreshold), intervalSeconds(intervalSec) {}

		virtual void Reset(const GameState& initialState) override {
			dribbleTime.clear();
			wasDribbling.clear();
			for (const auto& player : initialState.players) {
				dribbleTime[player.carId] = 0.0f;
				wasDribbling[player.carId] = false;
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			int carId = player.carId;
			float& time = dribbleTime[carId];
			bool& wasDribblingState = wasDribbling[carId];

			// Use car's local coordinate frame to work for both ground and wall
			Vec carToBall = state.ball.pos - player.pos;
			Vec localCarToBall = player.rotMat.Dot(carToBall); // Transform to car's local space

			// Check if ball is on top of car (in car's local up direction)
			bool isDribbling = 
				(localCarToBall.z > minHeight) &&  // Ball above car (in car's local up direction)
				(carToBall.Length() < maxDistance); // Ball close to car

			if (isDribbling) {
				// Accumulate dribble time
				time += state.deltaTime;
				wasDribblingState = true;

				// Calculate reward based on:
				// 1. How well ball is positioned (distance and height)
				float distanceScore = 1.0f - RS_CLAMP(carToBall.Length() / maxDistance, 0.0f, 1.0f);
				float heightScore = RS_CLAMP((localCarToBall.z - minHeight) / 100.0f, 0.0f, 1.0f);
				float positionScore = (distanceScore + heightScore) / 2.0f;

				// 2. Sustained dribble time multiplier
				float timeMultiplier = 1.0f + (time / intervalSeconds) * 0.3f; // Increases every 0.5s

				return positionScore * timeMultiplier;
			} else {
				// Reset dribble time if we lose the ball
				if (wasDribblingState) {
					time = 0.0f;
					wasDribblingState = false;
				}
				return 0;
			}
		}
	};

	// Rewards double jumping while/after dribbling to transition to airdribble
	class DoubleJumpAirdribbleReward : public Reward {
	private:
		float timeWindow; // Time window to detect dribble before double jump (default 1.5s)
		std::unordered_map<int, float> lastDribbleTime; // Track when player was last dribbling
		std::unordered_map<int, bool> hasDoubleJumped; // Track if we've already rewarded this double jump

	public:
		DoubleJumpAirdribbleReward(float window = 1.5f) : timeWindow(window) {}

		virtual void Reset(const GameState& initialState) override {
			lastDribbleTime.clear();
			hasDoubleJumped.clear();
			for (const auto& player : initialState.players) {
				lastDribbleTime[player.carId] = -999.0f; // Never dribbled
				hasDoubleJumped[player.carId] = false;
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			int carId = player.carId;
			float& lastDribble = lastDribbleTime[carId];
			bool& hasDoubleJumpedState = hasDoubleJumped[carId];

			// Check if currently dribbling (ball on car)
			Vec carToBall = state.ball.pos - player.pos;
			Vec localCarToBall = player.rotMat.Dot(carToBall);
			bool isDribbling = (localCarToBall.z > 100.0f) && (carToBall.Length() < 150.0f);

			if (isDribbling) {
				lastDribble = 0.0f; // Reset timer, we're currently dribbling
			} else if (lastDribble >= 0) {
				lastDribble += state.deltaTime; // Increment time since last dribble
			}

			// Check if player just double jumped
			bool justDoubleJumped = false;
			if (state.prev) {
				// Find previous player state by carId
				for (const auto& prevPlayer : state.prev->players) {
					if (prevPlayer.carId == carId) {
						justDoubleJumped = player.hasDoubleJumped && !prevPlayer.hasDoubleJumped;
						break;
					}
				}
			}

			// Reset flag when player lands
			if (player.isOnGround) {
				hasDoubleJumpedState = false;
			}

			if (justDoubleJumped && !hasDoubleJumpedState) {
				// Check if ball was recently on car (within time window)
				bool wasRecentlyDribbling = (lastDribble >= 0 && lastDribble < timeWindow) || isDribbling;

				// Check if ball is above car after double jump
				bool ballAboveCar = (state.ball.pos.z > player.pos.z + 50.0f);

				if (wasRecentlyDribbling && ballAboveCar) {
					hasDoubleJumpedState = true;
					return 1.0f; // One-time bonus
				}
			}

			return 0;
		}
	};

	// Rewards multiple consecutive touches in air after double jump, facing opponent's goal
	class MultiTouchAirdribbleReward : public Reward {
	private:
		float maxDistance; // Maximum distance from ball to reward (default 200 UU)
		std::unordered_map<int, int> consecutiveTouches; // Track consecutive touches per player
		std::unordered_map<int, float> lastTouchTime; // Track time of last touch
		std::unordered_map<int, float> airdribbleStartTime; // Track when airdribble started
		std::unordered_map<int, bool> inAirdribble; // Track if player is in airdribble state

	public:
		MultiTouchAirdribbleReward(float distanceThreshold = 200.0f) : maxDistance(distanceThreshold) {}

		virtual void Reset(const GameState& initialState) override {
			consecutiveTouches.clear();
			lastTouchTime.clear();
			airdribbleStartTime.clear();
			inAirdribble.clear();
			for (const auto& player : initialState.players) {
				consecutiveTouches[player.carId] = 0;
				lastTouchTime[player.carId] = 0.0f;
				airdribbleStartTime[player.carId] = 0.0f;
				inAirdribble[player.carId] = false;
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			int carId = player.carId;
			int& touches = consecutiveTouches[carId];
			float& lastTouch = lastTouchTime[carId];
			float& startTime = airdribbleStartTime[carId];
			bool& inAirdribbleState = inAirdribble[carId];

			// Check if player is in air and has double jumped
			bool isInAir = !player.isOnGround;
			bool hasDoubleJumped = player.hasDoubleJumped;

			// Reset if player lands
			if (player.isOnGround) {
				touches = 0;
				lastTouch = 0.0f;
				startTime = 0.0f;
				inAirdribbleState = false;
				return 0;
			}

			// Check if we're in airdribble state (in air, double jumped, ball close)
			Vec carToBall = state.ball.pos - player.pos;
			float distToBall = carToBall.Length();
			bool ballClose = distToBall < maxDistance;
			bool ballAbove = (state.ball.pos.z > player.pos.z - 50.0f); // Ball at least near car height

			if (isInAir && hasDoubleJumped && ballClose && ballAbove) {
				if (!inAirdribbleState) {
					// Starting airdribble
					inAirdribbleState = true;
					startTime = 0.0f;
					touches = 0;
				}

				startTime += state.deltaTime;

				// Check if player touched the ball this step
				if (player.ballTouchedStep) {
					touches++;
					lastTouch = 0.0f;
				} else {
					lastTouch += state.deltaTime;
					// Reset if we lose ball for too long (> 0.5s)
					if (lastTouch > 0.5f) {
						touches = 0;
						inAirdribbleState = false;
						return 0;
					}
				}

				// Calculate goal-facing multiplier
				Vec targetGoal = (player.team == Team::BLUE) ? 
					CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
				Vec dirToGoal = (targetGoal - player.pos).Normalized();
				float goalAlignment = player.rotMat.forward.Dot(dirToGoal);

				// Goal-facing multiplier: 0.3x to 2.0x based on alignment
				float goalFacingMultiplier;
				if (goalAlignment > 0.9f) {
					goalFacingMultiplier = 2.0f; // Perfect alignment
				} else if (goalAlignment > 0.7f) {
					goalFacingMultiplier = 1.5f; // Good alignment
				} else if (goalAlignment > 0.5f) {
					goalFacingMultiplier = 1.0f; // Moderate alignment
				} else {
					goalFacingMultiplier = 0.3f; // Poor alignment (still reward but much less)
				}

				// Base reward for maintaining airdribble
				float baseReward = 0.5f;

				// Touch multiplier: increases with consecutive touches
				float touchMultiplier;
				if (touches >= 4) {
					touchMultiplier = 2.5f; // 4th+ touch
				} else if (touches == 3) {
					touchMultiplier = 2.0f; // 3rd touch
				} else if (touches == 2) {
					touchMultiplier = 1.5f; // 2nd touch
				} else {
					touchMultiplier = 1.0f; // 1st touch
				}

				// Sustained time bonus (small bonus for maintaining airdribble)
				float timeBonus = RS_MIN(startTime / 2.0f, 0.5f); // Max 0.5 bonus after 2s

				// Only reward on touches, but apply goal-facing multiplier to all rewards
				if (player.ballTouchedStep) {
					return baseReward * touchMultiplier * goalFacingMultiplier + timeBonus * goalFacingMultiplier;
				} else {
					// Small reward for maintaining proximity even without touch
					return 0.1f * goalFacingMultiplier + timeBonus * goalFacingMultiplier;
				}
			} else {
				// Not in airdribble state - reset
				if (inAirdribbleState) {
					touches = 0;
					lastTouch = 0.0f;
					startTime = 0.0f;
					inAirdribbleState = false;
				}
				return 0;
			}
		}
	};
}

