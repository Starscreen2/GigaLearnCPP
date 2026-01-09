#pragma once
#include <RLGymCPP/Rewards/Reward.h>
#include <RLGymCPP/Gamestates/GameState.h>
#include <unordered_map>
#include <cfloat> // For FLT_EPSILON

namespace RLGC {

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
			
			// Stronger punishment if not flipping and slow
			if (!player.isFlipping && speed < minSpeedForReward) {
				// Punishment scales with time spent being slow
				float t = kickoffStartTime[carId] / maxKickoffTime; // 0 → 1 over kickoff window
				return -0.2f * (0.5f + t); // Between -0.1 and -0.2, increasing over time
			}
			
			if (speed < minSpeedForReward)
				return 0;

			Vec dirToBall = (state.ball.pos - player.pos).Normalized();
			float speedTowardBall = player.vel.Dot(dirToBall);
			
			// Require mostly driving toward ball (not sideways)
			float dirCos = 0.0f;
			if (speed > FLT_EPSILON && dirToBall.Length() > FLT_EPSILON) {
				dirCos = speedTowardBall / speed; // Cosine of angle between velocity and direction to ball
			}
			if (dirCos < 0.6f) // Not driving mostly toward ball
				return 0;

			// Base reward for high speed toward ball
			float reward = RS_MIN(1.0f, speedTowardBall / CommonValues::CAR_MAX_SPEED);

			// Incentivize boost usage during kickoff
			bool isBoosting = (player.boost > 0 && player.prevAction.boost > 0.1f);
			if (isBoosting) {
				reward += 0.2f; // Small additive bonus for using boost
			}

			// Speed tier bonuses - reward reaching higher speeds
			if (speed > 1500.0f) {
				reward += 0.2f; // Bonus for reaching 1500+ speed
			}
			if (speed > 2000.0f) {
				reward += 0.3f; // Additional bonus for reaching 2000+ speed (near supersonic)
			}

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

	// Highly rewards first touch on kickoff (works from all spawn positions)
	// Punishes if goal is conceded within 8 seconds of kickoff start
	class KickoffFirstTouchReward : public Reward {
	private:
		float rewardMagnitude;      // Base reward for first touch (default 100.0)
		float punishmentMagnitude; // Base punishment for early concede (default 60.0, matches speed flip reward)
		float earlyConcedeWindow;   // Time window for early concede punishment (default 8.0s)
		float maxKickoffTime;       // Max time to consider still in kickoff (default 5.0s)
		
		std::unordered_map<int, bool> inKickoff;
		std::unordered_map<int, float> kickoffStartTime;
		std::unordered_map<int, bool> gotFirstTouch;
		std::unordered_map<int, bool> firstTouchRewarded;
		std::unordered_map<int, bool> trackingTime; // Keep tracking time even after kickoff ends (for early concede check)
		
	public:
		KickoffFirstTouchReward(float reward = 100.0f, float concedeWindow = 8.0f, float maxKickoff = 5.0f, float punishment = 60.0f)
			: rewardMagnitude(reward), punishmentMagnitude(punishment), earlyConcedeWindow(concedeWindow), maxKickoffTime(maxKickoff) {}
		
		virtual void Reset(const GameState& initialState) override {
			inKickoff.clear();
			kickoffStartTime.clear();
			gotFirstTouch.clear();
			firstTouchRewarded.clear();
			trackingTime.clear();
			for (const auto& player : initialState.players) {
				inKickoff[player.carId] = false;
				kickoffStartTime[player.carId] = 0.0f;
				gotFirstTouch[player.carId] = false;
				firstTouchRewarded[player.carId] = false;
				trackingTime[player.carId] = false;
			}
		}
		
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;
			
			int carId = player.carId;
			float reward = 0.0f;
			
			// Detect kickoff: ball at center OR just reset (works for all spawn positions)
			// Ball at center: within 500 units of origin, low height
			bool ballAtCenter = (state.ball.pos.Length() < 500.0f && abs(state.ball.pos.z) < 100.0f);
			// Ball just reset: very low velocity (stationary or near-stationary)
			bool ballStationary = state.ball.vel.Length() < 100.0f;
			
			// Track kickoff state per player
			if (ballAtCenter && ballStationary) {
				// New kickoff detected - reset state
				if (!inKickoff[carId]) {
					inKickoff[carId] = true;
					kickoffStartTime[carId] = 0.0f;
					gotFirstTouch[carId] = false;
					firstTouchRewarded[carId] = false;
					trackingTime[carId] = true; // Start tracking time for early concede check
				}
			}
			
			// Update timer if tracking (either in kickoff or within early concede window)
			if (trackingTime[carId]) {
				kickoffStartTime[carId] += state.deltaTime;
				
				// Stop tracking if past early concede window
				if (kickoffStartTime[carId] > earlyConcedeWindow) {
					trackingTime[carId] = false;
					// Reset flags for next kickoff
					gotFirstTouch[carId] = false;
					firstTouchRewarded[carId] = false;
				}
			}
			
			// End kickoff phase if ball moves significantly or max kickoff time reached
			if (inKickoff[carId]) {
				if (kickoffStartTime[carId] > maxKickoffTime || state.ball.vel.Length() > 500.0f) {
					inKickoff[carId] = false;
					// Keep trackingTime active for early concede check
				}
			}
			
			// Check for first touch during kickoff phase
			if (inKickoff[carId] && player.ballTouchedStep && !firstTouchRewarded[carId]) {
				gotFirstTouch[carId] = true;
				firstTouchRewarded[carId] = true;
				reward = rewardMagnitude; // High reward for first touch
			}
			
			// Early concede punishment: if goal scored within 8 seconds and player got first touch
			if (isFinal && state.goalScored) {
				// Check if player conceded (ball in their goal)
				bool conceded = (player.team == RS_TEAM_FROM_Y(state.ball.pos.y));
				
				if (conceded && gotFirstTouch[carId] && trackingTime[carId] && kickoffStartTime[carId] <= earlyConcedeWindow) {
					// Punish with separate magnitude (matches speed flip reward weight)
					reward -= punishmentMagnitude;
					// Stop tracking after punishment applied
					trackingTime[carId] = false;
				}
			}
			
			return reward;
		}
	};

}

