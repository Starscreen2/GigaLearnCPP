#include "AdvancedObsPadded.h"
#include "../Gamestates/StateUtil.h"
#include <algorithm>
#include <random>

RLGC::FList RLGC::AdvancedObsPadded::BuildObs(const Player& player, const GameState& state) {
	FList obs = {};

	bool inv = player.team == Team::ORANGE;

	auto ball = InvertPhys(state.ball, inv);
	auto& pads = state.GetBoostPads(inv);
	auto& padTimers = state.GetBoostPadTimers(inv);

	obs += ball.pos * POS_COEF;
	obs += ball.vel * VEL_COEF;
	obs += ball.angVel * ANG_VEL_COEF;

	for (int i = 0; i < player.prevAction.ELEM_AMOUNT; i++)
		obs += player.prevAction[i];

	for (int i = 0; i < CommonValues::BOOST_LOCATIONS_AMOUNT; i++) {
		// A clever trick that blends the boost pads using their timers
		if (pads[i]) {
			obs += 1.f; // Pad is already available
		} else {
			obs += 1.f / (1.f + padTimers[i]); // Approaches 1 as the pad becomes available
		}
	}

	// Get the size of a single player observation (for padding) - calculate before adding self
	FList testPlayerObs = {};
	AddPlayerToObs(testPlayerObs, player, inv, ball);
	int playerObsSize = testPlayerObs.size();
	
	// Add self observation
	AddPlayerToObs(obs, player, inv, ball);

	std::vector<FList> teammates = {}, opponents = {};

	// Collect all other players
	for (auto& otherPlayer : state.players) {
		if (otherPlayer.carId == player.carId)
			continue;

		FList playerObs = {};
		AddPlayerToObs(
			playerObs,
			otherPlayer, inv, ball
		);
		((otherPlayer.team == player.team) ? teammates : opponents).push_back(playerObs);
	}

	// Check if we have too many players
	if (teammates.size() > maxPlayers - 1)
		RG_ERR_CLOSE("AdvancedObsPadded: Too many teammates for Obs, maximum is " << (maxPlayers - 1));
	
	if (opponents.size() > maxPlayers)
		RG_ERR_CLOSE("AdvancedObsPadded: Too many opponents for Obs, maximum is " << maxPlayers);

	// Pad missing slots with zeros
	for (int i = 0; i < 2; i++) {
		auto& playerList = i ? teammates : opponents;
		int targetCount = i ? maxPlayers - 1 : maxPlayers;

		while (playerList.size() < targetCount) {
			FList pad = FList(playerObsSize); // Zero-filled vector
			playerList.push_back(pad);
		}
	}

	// Shuffle both lists to prevent slot bias
	std::shuffle(teammates.begin(), teammates.end(), ::Math::GetRandEngine());
	std::shuffle(opponents.begin(), opponents.end(), ::Math::GetRandEngine());

	// Add all teammates and opponents
	for (auto& teammate : teammates)
		obs += teammate;
	for (auto& opponent : opponents)
		obs += opponent;

	return obs;
}

