#pragma once
#include <RLGymCPP/Rewards/Reward.h>
#include <RLGymCPP/Gamestates/GameState.h>
#include <unordered_map>

namespace RLGC {

	// Rewards powersliding (handbrake) during turns to maintain speed and make sharper turns
	class PowerslideReward : public Reward {
	private:
		float minSpeed; // Minimum speed to reward powerslide (default 500)
		float minTurnRate; // Minimum angular velocity to consider it a turn (default 1.0 rad/s)
		
	public:
		PowerslideReward(float minSpd = 500.0f, float minTurn = 1.0f)
			: minSpeed(minSpd), minTurnRate(minTurn) {}
		
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;
			
			// Only reward on ground
			if (!player.isOnGround)
				return 0;
			
			// Must be using handbrake
			if (player.prevAction.handbrake < 0.5f)
				return 0;
			
			// Must have sufficient speed
			float speed = player.vel.Length();
			if (speed < minSpeed)
				return 0;
			
			// Check if turning (angular velocity in yaw)
			float turnRate = abs(player.angVel.z); // Z is yaw rotation
			if (turnRate < minTurnRate)
				return 0;
			
			// Reward proportional to speed and turn rate
			// Higher speed + sharper turn = better powerslide
			float speedScore = RS_MIN(1.0f, speed / CommonValues::CAR_MAX_SPEED);
			float turnScore = RS_MIN(1.0f, turnRate / 5.5f); // Max ang vel is ~5.5 rad/s
			
			// Bonus if maintaining speed while turning (good powerslide)
			float prevSpeed = state.prev->players[player.index].vel.Length();
			float speedMaintained = (speed >= prevSpeed * 0.9f) ? 1.0f : 0.5f; // 90% speed maintained
			
			return (speedScore * 0.4f + turnScore * 0.4f + speedMaintained * 0.2f) * 0.3f;
		}
	};

	// Rewards half-flips: backward flip + air roll cancel for quick 180-degree turns
	class HalfFlipReward : public Reward {
	private:
		std::unordered_map<int, bool> inHalfFlip;
		std::unordered_map<int, float> halfFlipStartTime;
		std::unordered_map<int, Vec> halfFlipStartVel;
		float maxHalfFlipTime; // Maximum time for half-flip sequence (default 1.0s)
		float minBackwardSpeed; // Minimum backward speed to start half-flip (default 300)
		
	public:
		HalfFlipReward(float maxTime = 1.0f, float minBackSpeed = 300.0f)
			: maxHalfFlipTime(maxTime), minBackwardSpeed(minBackSpeed) {}
		
		virtual void Reset(const GameState& initialState) override {
			inHalfFlip.clear();
			halfFlipStartTime.clear();
			halfFlipStartVel.clear();
			for (const auto& player : initialState.players) {
				inHalfFlip[player.carId] = false;
				halfFlipStartTime[player.carId] = 0.0f;
			}
		}
		
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;
			
			int carId = player.carId;
			
			// Detect start of half-flip: backward movement + backward flip
			bool movingBackward = player.vel.Dot(player.rotMat.forward) < -minBackwardSpeed;
			bool backwardFlip = player.isFlipping && player.flipRelTorque.y < -0.5f; // Negative Y = backward flip
			
			if (movingBackward && backwardFlip && !inHalfFlip[carId]) {
				// Starting half-flip
				inHalfFlip[carId] = true;
				halfFlipStartTime[carId] = 0.0f;
				halfFlipStartVel[carId] = player.vel;
			}
			
			// Update half-flip state
			if (inHalfFlip[carId]) {
				halfFlipStartTime[carId] += state.deltaTime;
				
				// Check for air roll cancel (roll input during flip)
				bool isRolling = abs(player.prevAction.roll) > 0.3f;
				
				// Reward if rolling during flip (cancel)
				if (isRolling && player.isFlipping) {
					// Good half-flip execution
					float reward = 0.5f;
					
					// Bonus for fast execution
					if (halfFlipStartTime[carId] < 0.5f) {
						reward *= 1.5f; // 50% bonus for quick half-flip
					}
					
					// Check if successfully turned around (velocity reversed)
					if (halfFlipStartTime[carId] > 0.3f) {
						float currentForwardSpeed = player.vel.Dot(player.rotMat.forward);
						float startBackwardSpeed = halfFlipStartVel[carId].Dot(player.rotMat.forward);
						
						// If we went from backward to forward, successful half-flip
						if (startBackwardSpeed < -200.0f && currentForwardSpeed > 200.0f) {
							reward *= 2.0f; // 2x bonus for successful 180-degree turn
						}
					}
					
					return reward;
				}
				
				// Reset if too much time passed or flip ended without roll
				if (halfFlipStartTime[carId] > maxHalfFlipTime || (!player.isFlipping && !isRolling)) {
					inHalfFlip[carId] = false;
					halfFlipStartTime[carId] = 0.0f;
				}
			}
			
			return 0;
		}
	};

	// Rewards wavedashing: landing from air with dodge to maintain speed
	// Prevents bot from forgetting this essential recovery mechanic
	class CustomWavedashReward : public Reward {
	private:
		std::unordered_map<int, bool> wasInAir;
		std::unordered_map<int, float> lastWavedashTime;
		std::unordered_map<int, float> accumulatedTime;
		std::unordered_map<int, float> airTime;
		float minAirTime; // Minimum time in air to count (default 0.3s)
		float minLandingSpeed; // Minimum speed after landing (default 400)
		float cooldownTime; // Cooldown between wavedashes (default 2.0s)
		
	public:
		CustomWavedashReward(float minAir = 0.3f, float minLandSpeed = 400.0f, float cooldown = 2.0f)
			: minAirTime(minAir), minLandingSpeed(minLandSpeed), cooldownTime(cooldown) {}
		
		virtual void Reset(const GameState& initialState) override {
			wasInAir.clear();
			lastWavedashTime.clear();
			accumulatedTime.clear();
			airTime.clear();
			for (const auto& player : initialState.players) {
				wasInAir[player.carId] = !player.isOnGround;
				lastWavedashTime[player.carId] = -cooldownTime; // Allow immediate wavedash
				accumulatedTime[player.carId] = 0.0f;
				airTime[player.carId] = 0.0f;
			}
		}
		
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;
			
			int carId = player.carId;
			accumulatedTime[carId] += state.deltaTime;
			float currentTime = accumulatedTime[carId];
			
			// Track air time
			if (!player.isOnGround) {
				wasInAir[carId] = true;
				airTime[carId] += state.deltaTime;
				return 0;
			}
			
			// Check if just landed (was in air, now on ground)
			if (wasInAir[carId] && player.isOnGround) {
				wasInAir[carId] = false;
				
				// Must have been in air long enough (not just a small hop)
				float timeInAir = airTime[carId];
				airTime[carId] = 0.0f;
				
				if (timeInAir < minAirTime)
					return 0;
				
				// Check cooldown
				if (currentTime - lastWavedashTime[carId] < cooldownTime)
					return 0;
				
				// Must have used dodge (flip) during landing
				if (!player.isFlipping)
					return 0;
				
				// Check landing speed (good wavedash maintains speed)
				float landingSpeed = player.vel.Length();
				if (landingSpeed < minLandingSpeed)
					return 0;
				
				// Reward successful wavedash
				lastWavedashTime[carId] = currentTime;
				float speedScore = RS_MIN(1.0f, landingSpeed / CommonValues::CAR_MAX_SPEED);
				return 0.4f * speedScore; // Small reward, scales with speed maintained
			}
			
			return 0;
		}
	};

	// Rewards directional flips (forward/side) when moving at speed
	// Helps bot maintain speed and recover quickly
	class DirectionalFlipReward : public Reward {
	private:
		std::unordered_map<int, float> lastFlipTime;
		std::unordered_map<int, float> accumulatedTime;
		float minSpeed; // Minimum speed to reward flip (default 600)
		float cooldownTime; // Cooldown between flips (default 1.5s)
		
	public:
		DirectionalFlipReward(float minSpd = 600.0f, float cooldown = 1.5f)
			: minSpeed(minSpd), cooldownTime(cooldown) {}
		
		virtual void Reset(const GameState& initialState) override {
			lastFlipTime.clear();
			accumulatedTime.clear();
			for (const auto& player : initialState.players) {
				lastFlipTime[player.carId] = -cooldownTime; // Allow immediate flip
				accumulatedTime[player.carId] = 0.0f;
			}
		}
		
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;
			
			int carId = player.carId;
			accumulatedTime[carId] += state.deltaTime;
			float currentTime = accumulatedTime[carId];
			
			// Only reward on ground
			if (!player.isOnGround)
				return 0;
			
			// Check cooldown
			if (currentTime - lastFlipTime[carId] < cooldownTime)
				return 0;
			
			// Must be flipping
			if (!player.isFlipping)
				return 0;
			
			// Must have sufficient speed
			float speed = player.vel.Length();
			if (speed < minSpeed)
				return 0;
			
			// Check if it's a directional flip (forward or side, not backward)
			// Backward flips are handled by HalfFlipReward
			float flipForward = player.flipRelTorque.y; // Y component: forward/backward
			if (flipForward < -0.3f) // Backward flip, skip
				return 0;
			
			// Reward forward or side flips
			lastFlipTime[carId] = currentTime;
			float speedScore = RS_MIN(1.0f, speed / CommonValues::CAR_MAX_SPEED);
			
			// Bonus for forward flips (better for speed)
			float forwardBonus = (flipForward > 0.5f) ? 1.2f : 1.0f;
			
			return 0.3f * speedScore * forwardBonus;
		}
	};

	// Rewards fast aerials: double jump + boost for quick aerials
	// Only rewards when ball is high enough to warrant fast aerial
	class FastAerialReward : public Reward {
	private:
		std::unordered_map<int, float> lastFastAerialTime;
		std::unordered_map<int, float> accumulatedTime;
		float minBallHeight; // Minimum ball height to reward fast aerial (default 400)
		float cooldownTime; // Cooldown between fast aerials (default 3.0s)
		
	public:
		FastAerialReward(float minBallH = 400.0f, float cooldown = 3.0f)
			: minBallHeight(minBallH), cooldownTime(cooldown) {}
		
		virtual void Reset(const GameState& initialState) override {
			lastFastAerialTime.clear();
			accumulatedTime.clear();
			for (const auto& player : initialState.players) {
				lastFastAerialTime[player.carId] = -cooldownTime;
				accumulatedTime[player.carId] = 0.0f;
			}
		}
		
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;
			
			int carId = player.carId;
			accumulatedTime[carId] += state.deltaTime;
			float currentTime = accumulatedTime[carId];
			
			// Only reward when ball is high enough
			if (state.ball.pos.z < minBallHeight)
				return 0;
			
			// Check cooldown
			if (currentTime - lastFastAerialTime[carId] < cooldownTime)
				return 0;
			
			// Must be in air
			if (player.isOnGround)
				return 0;
			
			// Must have double jumped
			if (!player.hasDoubleJumped)
				return 0;
			
			// Must be boosting
			if (player.boost <= 0 || player.prevAction.boost < 0.3f)
				return 0;
			
			// Check if car is moving upward (good fast aerial)
			if (player.vel.z < 200.0f) // Not going up fast enough
				return 0;
			
			// Reward successful fast aerial
			lastFastAerialTime[carId] = currentTime;
			float upwardSpeed = RS_MIN(1.0f, player.vel.z / 1000.0f); // Normalize upward speed
			
			return 0.5f * upwardSpeed; // Small reward, scales with upward velocity
		}
	};

	// Rewards good recovery landings: landing on wheels with speed maintained
	// Prevents bot from forgetting to land properly after aerials
	class RecoveryLandingReward : public Reward {
	private:
		std::unordered_map<int, bool> wasInAir;
		std::unordered_map<int, float> airTime;
		float minAirTime; // Minimum time in air to count (default 0.5s)
		float minLandingSpeed; // Minimum speed after landing (default 300)
		
	public:
		RecoveryLandingReward(float minAir = 0.5f, float minLandSpeed = 300.0f)
			: minAirTime(minAir), minLandingSpeed(minLandSpeed) {}
		
		virtual void Reset(const GameState& initialState) override {
			wasInAir.clear();
			airTime.clear();
			for (const auto& player : initialState.players) {
				wasInAir[player.carId] = !player.isOnGround;
				airTime[player.carId] = 0.0f;
			}
		}
		
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;
			
			int carId = player.carId;
			
			// Track air time
			if (!player.isOnGround) {
				wasInAir[carId] = true;
				airTime[carId] += state.deltaTime;
				return 0;
			}
			
			// Check if just landed (was in air, now on ground)
			if (wasInAir[carId] && player.isOnGround) {
				wasInAir[carId] = false;
				
				// Must have been in air long enough (not just a small hop)
				if (airTime[carId] < minAirTime) {
					airTime[carId] = 0.0f;
					return 0;
				}
				
				// Check if landing on wheels (not on roof/back)
				// Car's up vector should be mostly upward (not inverted)
				float carUp = player.rotMat.up.z; // Z component of up vector
				if (carUp < 0.5f) // Car is upside down or on side
					return 0;
				
				// Check landing speed (good recovery maintains speed)
				float landingSpeed = player.vel.Length();
				if (landingSpeed < minLandingSpeed)
					return 0;
				
				// Reward good recovery landing
				float speedScore = RS_MIN(1.0f, landingSpeed / CommonValues::CAR_MAX_SPEED);
				float orientationScore = RS_MAX(0.0f, (carUp - 0.5f) / 0.5f); // 0.5 to 1.0 -> 0.0 to 1.0
				
				airTime[carId] = 0.0f;
				return 0.3f * (speedScore * 0.6f + orientationScore * 0.4f);
			}
			
			return 0;
		}
	};

	// Rewards landing on boost pads after being in air
	// Combines recovery mechanics with efficient boost collection
	class LandOnBoostReward : public Reward {
	private:
		std::unordered_map<int, bool> wasInAir;
		std::unordered_map<int, float> airTime;
		float minAirTime; // Minimum time in air to count (default 0.3s)
		float maxPadDistance; // Maximum distance to boost pad to count as "landing on it" (default 200 units)
		float cooldownTime; // Cooldown between rewards (default 2.0s)
		std::unordered_map<int, float> lastLandOnBoostTime;
		std::unordered_map<int, float> accumulatedTime;
		
	public:
		LandOnBoostReward(float minAir = 0.3f, float maxDist = 200.0f, float cooldown = 2.0f)
			: minAirTime(minAir), maxPadDistance(maxDist), cooldownTime(cooldown) {}
		
		virtual void Reset(const GameState& initialState) override {
			wasInAir.clear();
			airTime.clear();
			lastLandOnBoostTime.clear();
			accumulatedTime.clear();
			for (const auto& player : initialState.players) {
				wasInAir[player.carId] = !player.isOnGround;
				airTime[player.carId] = 0.0f;
				lastLandOnBoostTime[player.carId] = -cooldownTime;
				accumulatedTime[player.carId] = 0.0f;
			}
		}
		
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;
			
			int carId = player.carId;
			accumulatedTime[carId] += state.deltaTime;
			float currentTime = accumulatedTime[carId];
			
			// Track air time
			if (!player.isOnGround) {
				wasInAir[carId] = true;
				airTime[carId] += state.deltaTime;
				return 0;
			}
			
			// Check if just landed (was in air, now on ground)
			if (wasInAir[carId] && player.isOnGround) {
				wasInAir[carId] = false;
				
				// Must have been in air long enough (not just a small hop)
				float timeInAir = airTime[carId];
				airTime[carId] = 0.0f;
				
				if (timeInAir < minAirTime)
					return 0;
				
				// Check cooldown
				if (currentTime - lastLandOnBoostTime[carId] < cooldownTime)
					return 0;
				
				// Check if landing near/on a boost pad
				float bestReward = 0.0f;
				bool landedOnPad = false;
				
				for (int i = 0; i < CommonValues::BOOST_LOCATIONS_AMOUNT; i++) {
					// Pad must be available
					if (!state.boostPads[i])
						continue;
					
					Vec padPos = CommonValues::BOOST_LOCATIONS[i];
					float distToPad = (padPos - player.pos).Length();
					
					if (distToPad <= maxPadDistance) {
						landedOnPad = true;
						
						// Reward based on proximity (closer = better)
						float proximityScore = 1.0f - (distToPad / maxPadDistance);
						
						// Big pads worth significantly more (100 boost vs 12 boost = ~8.3x value)
						// Big pads indicated by z = 73.0
						float padBonus = (padPos.z > 72.0f) ? 3.0f : 1.0f;
						
						bestReward = RS_MAX(bestReward, proximityScore * padBonus);
					}
				}
				
				if (landedOnPad) {
					lastLandOnBoostTime[carId] = currentTime;
					return 0.5f * bestReward; // Small reward, scales with proximity and pad size
				}
			}
			
			return 0;
		}
	};

}

