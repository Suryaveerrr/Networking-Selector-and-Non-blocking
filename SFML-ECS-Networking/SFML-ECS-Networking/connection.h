#pragma once

#include <SFML\Network.hpp>

enum ConnState {NotReady, Ready, Disconnected};

// Fixme:: ideally we should link specific tank/character to each connection,
// rather than assuming the order in which connections are made is the same as
// the order in which tanks/characters are stored in the game.
struct Connection
{
	std::unique_ptr<sf::TcpSocket> socket;
	ConnState state = Disconnected;
};