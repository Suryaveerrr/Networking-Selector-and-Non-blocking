#pragma once

#include <deque>
#include <memory>
#include <string>
#include <SFML\Network.hpp>
#include "tank_message.h"

enum ConnState { NotReady, Ready, Disconnected };

// Everything the Observer knows about one connected player, including which tank is theirs.
struct Connection
{
	std::unique_ptr<sf::TcpSocket> socket;
	ConnState state = Disconnected;

	// ID of this player's tank in the Game. It stays -1 until the player's Hello arrives and we know
	// their colour. We use this instead of assuming the connection's position in the vector matches the
	// tank's position, because connections get removed and the order changes.
	int tankId = -1;
	std::string colour;

	// The last tank state this player sent, so a player who joins later can be told where everyone is.
	TankMessage lastState;
	bool hasState = false;

	// Packets waiting to be sent to this player. Sockets are non-blocking, so a send may only take part
	// of a packet. A packet stays at the front until it has gone out completely.
	std::deque<sf::Packet> outbox;
};