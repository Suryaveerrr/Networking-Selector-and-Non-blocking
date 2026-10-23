#pragma once

// A simple tank update message
// FIXME: Consider what else we need to send and include it here.
struct TankMessage {
	// The coordinates of the tank within the game world.
	int id = 0;
	float x, y;
};