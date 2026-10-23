#pragma once

#include <memory>
#include <SFML\Network.hpp>

enum ConnState { NotReady, Ready, Disconnected };

// Links a player's network connection to the tank it controls.
struct Connection
{
	std::unique_ptr<sf::TcpSocket> socket;
	ConnState state = Disconnected;

	// ID of this player's tank in the Game (the value returned by Game::AddTank).
	// We use this instead of assuming the connection's position in the vector matches
	// the tank's position, because connections get removed and the order changes.
	int tankId = -1;
};