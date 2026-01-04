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
}

