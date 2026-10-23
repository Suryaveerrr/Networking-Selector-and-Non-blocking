#include <SFML\Graphics.hpp>
#include <SFML\Network.hpp>
#include <algorithm>
#include <deque>
#include <exception>
#include <sstream>
#include <iomanip>
#include "game.h"
#include "utils.h"
#include "connection.h"


/*
 This is a TCP version of the Lab 3 game, which uses non-blocking networking for Observer communication,
 with multiple players. Each player connects to the Observer, says what colour its tank is, and then sends
 updates about its tank whenever it changes. The Observer accepts multiple connections, draws every
 player's tank, and acts as a relay server: it forwards each player's updates to all the other players, so
 every player sees everyone else's tank as well as their own.
*/

// Sends as much of the queue as the socket will take right now, front to back. A packet is only removed
// once it has been sent completely. If the socket takes only part of it (Partial) or none of it
// (NotReady), we stop. The same, unchanged packet is then sent again next time, and SFML carries on from
// where it left off. Returns Done if the queue is empty, otherwise the status that stopped us.
static sf::Socket::Status FlushOutbox(sf::TcpSocket& socket, std::deque<sf::Packet>& outbox)
{
	while (!outbox.empty()) {
		sf::Socket::Status status = socket.send(outbox.front());
		if (status != sf::Socket::Status::Done)
			return status;
		outbox.pop_front();
	}
	return sf::Socket::Status::Done;
}

// Queues a copy of the packet for every player except the one it came from. State updates are
// "droppable": if a player is so far behind that a lot is already queued for them, they skip this one
// and catch up with the next, because a newer update replaces an older one anyway. Joins and leaves
// are never dropped.
static void RelayToOthers(std::vector<Connection>& connections, const Connection* from,
	const sf::Packet& packet, bool droppable)
{
	const std::size_t max_queued = 64;

	for (auto& connection : connections) {
		if (&connection == from || connection.state == Disconnected || connection.tankId < 0)
			continue;
		if (droppable && connection.outbox.size() >= max_queued)
			continue;
		connection.outbox.push_back(packet);
	}
}

static int runGame() {
	bool is_observer = false;
	unsigned short observer_port = 53000;

	Utils::printMsg("Game startup...");

	std::string title_type = "";

	bool ready = false;

	while (!ready) {
		Utils::printMsg("Are we playing or observing? Enter 1 for Player, enter 2 for Observer:");
		std::string input_line;
		std::getline(std::cin, input_line);

		if (!input_line.empty() && input_line.at(0) == '1') {
			Utils::printMsg("Player chosen, preparing window...");
			title_type = "Player";
			is_observer = false;
			ready = true;
		}
		else if (!input_line.empty() && input_line.at(0) == '2') {
			Utils::printMsg("Observer chosen, preparing window...");
			title_type = "Observer";
			is_observer = true;
			ready = true;
		}
		else {
			Utils::printMsg("Incorrect input, please try again!", warning);
		}
	}

	// Players choose their tank colour. The Observer doesn't have a tank of its own.
	std::string player_colour = "blue";
	if (!is_observer) {
		while (true) {
			Utils::printMsg("Choose your tank colour (blue, green or black):");
			std::string colour_input;
			std::getline(std::cin, colour_input);

			if (Game::IsValidColour(colour_input)) {
				player_colour = colour_input;
				break;
			}
			Utils::printMsg("Incorrect input, please try again!", warning);
		}
	}

	// Prepare window.
	sf::RenderWindow window(sf::VideoMode({ 640, 480 }), "CMP425/CMP501 - Lab 5: Non-blocking (" + title_type + ")");
	window.setFramerateLimit(60);	//Request 60 frames per second
	Utils::printMsg("Window ready, configuring networking...");

	// Prepare networking.
	sf::TcpListener listener;
	// Socket for communication with observer
	std::unique_ptr<sf::TcpSocket> player_socket = std::make_unique<sf::TcpSocket>();

	// Only the Observer listens for connections. Players connect to it, so they don't need a listener.
	if (is_observer) {
		listener.setBlocking(false); // set listener to non-blocking mode so we can keep checking for connections

		// Without a listener the Observer can never accept a Player, so there is no point carrying on.
		if (listener.listen(observer_port) != sf::Socket::Status::Done) {
			Utils::printMsg("Could not listen on port " + std::to_string(observer_port)
				+ ". Is another Observer already running?", error);
			return 1;
		}
		Utils::printMsg("Listening on port " + std::to_string(observer_port));
	}

	// Clock for timing the 'dt' value
	sf::Clock clock;

	// Game object.
	Game game;

	// Where every tank starts.
	const sf::Vector2f spawn_position = { 320.f, 240.f };

	// A Player has its own tank from the start, so it can drive even before it has connected.
	if (!is_observer)
		game.AddLocalTank(player_colour, spawn_position);

	// Container of player connections for Observer, and the next tank ID to hand out.
	std::vector<Connection> player_connections;
	int next_tank_id = 0;

	// Other game parameters.
	float game_speed = 1.0f;
	float send_rate = 0.05f;
	float reconnect_delay = 1.f;
	float reconnect_timer = reconnect_delay; // Start "due", so the first attempt happens straight away.
	float send_timer = 1.f;
	bool is_connected = false;

	// Player: packets waiting to go to the Observer, and the most we let build up before we hold back
	// new updates (so a slow connection can't make the queue grow for ever).
	std::deque<sf::Packet> outbox;
	const std::size_t max_queued_updates = 4;
	// Player: the last state we sent, so we only send again when it changes.
	TankMessage last_sent;
	// Player: send once straight after (re)connecting, even if nothing has changed, so the Observer
	// learns where the tank really is.
	bool force_send = true;

	// Selector for handling multiple connections (Observer only).
	sf::SocketSelector selector;

	// Observer: stops dealing with a player (they left, or their connection broke), removes their tank
	// and tells everyone else. The connection itself is erased from the vector at the end of the frame.
	auto drop_connection = [&](Connection& connection, sf::Socket::Status why) {
		// Work out who this was before we close the socket (the address is gone afterwards).
		auto remote_address = connection.socket->getRemoteAddress();
		std::string who = remote_address
			? remote_address->toString() + ":" + std::to_string(connection.socket->getRemotePort())
			: "unknown address";

		if (why == sf::Socket::Status::Disconnected)
			Utils::printMsg("Player disconnected: " + who, warning);
		else
			Utils::printMsg("Connection error, dropping player: " + who, error);

		// Stop watching the socket (before closing it, the selector needs the handle), then close it.
		// A closed connection always counts as "ready", so leaving it in the selector would flag it
		// every frame.
		selector.remove(*connection.socket);
		connection.socket->disconnect();
		connection.state = Disconnected;

		// Remove their tank from our game, and tell the other players it has gone.
		if (connection.tankId >= 0) {
			game.RemoveTank(connection.tankId);
			RelayToOthers(player_connections, &connection, MakePlayerLeftPacket(connection.tankId), false);
		}
		};

	// Player: the connection to the Observer has broken. Go back to trying to connect.
	auto lose_connection = [&](const std::string& reason) {
		Utils::printMsg(reason + " Will try to reconnect.", error);
		player_socket->disconnect();
		is_connected = false;
		outbox.clear();
		// The other players' tanks came from this connection, so they go too. We get them again
		// when we reconnect.
		game.ClearTanks();
		force_send = true;
		reconnect_timer = 0;
		};

	while (window.isOpen()) {
		// Calculate dt.
		float dt = clock.restart().asSeconds() * game_speed;
		send_timer += dt; // Count how much time has passed.
		reconnect_timer += dt;
		// Handle window events (e.g. key press).
		while (const std::optional event = window.pollEvent()) {
			if (event->is<sf::Event::Closed>()) {
				Utils::printMsg("Window closed, unbinding socket...", MessageType::warning);
				window.close();
				listener.close();
			}
			if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
				if (keyPressed->scancode == sf::Keyboard::Scancode::Escape) {
					Utils::printMsg("ESC pressed, closing window, unbinding socket...", MessageType::warning);
					window.close();
					listener.close();
				}
			}
			// We only handle inputs for the player. Observer will do direct updates from network data.
			if (!is_observer)
				game.HandleEvents(event);
		}


		if (is_observer) {
			// Socket for communication with player
			std::unique_ptr<sf::TcpSocket> socket = std::make_unique<sf::TcpSocket>();

			// Check for incoming connections
			if (listener.accept(*socket) == sf::Socket::Status::Done) {
				Utils::printMsg("New connection from: "
					+ socket->getRemoteAddress().value().toString()
					+ ":"
					+ std::to_string(socket->getRemotePort()));

				// Add connection the vector.
				player_connections.push_back({ std::move(socket) });
				// Accepted sockets are blocking by default. Make this one non-blocking, so a receive
				// with nothing to read returns NotReady instead of freezing the whole Observer.
				player_connections.back().socket->setBlocking(false);
				selector.add(*player_connections.back().socket); // Add to persistent selector
				// Set the deafult connection state to NotReady.
				player_connections.back().state = NotReady;

				// There is no tank yet: it is added when the player's Hello arrives and we know its colour.
			}

			// Start every live connection as NotReady each frame. This way a state can never go
			// stale when the selector finds nothing (wait returns false). Disconnected is final,
			// so those connections are left alone.
			for (auto& connection : player_connections) {
				if (connection.state != Disconnected)
					connection.state = NotReady;
			}

			// Check which connections have some data waiting to be received from sockets.
			// The timeout is 0, so this only polls and never holds up the frame. Players only send
			// when something changes, so this returns false whenever everyone is standing still.
			if (selector.wait(sf::seconds(0.f))) {
				for (auto& connection : player_connections) {
					// Check the state first: a disconnected socket has been removed from the
					// selector and closed, so we must not ask the selector about it.
					if (connection.state != Disconnected && selector.isReady(*connection.socket))
						connection.state = Ready;
				}
			}

			// Read everything that has arrived from each ready connection.
			for (std::size_t i = 0; i < player_connections.size(); i++) {
				Connection& connection = player_connections[i];
				if (connection.state != Ready)
					continue;

				while (true) {
					sf::Packet packet;
					sf::Socket::Status status = connection.socket->receive(packet);

					if (status == sf::Socket::Status::Disconnected || status == sf::Socket::Status::Error) {
						drop_connection(connection, status);
						break;
					}
					if (status != sf::Socket::Status::Done) {
						// NotReady means nothing more to read. Partial means only part of a packet has
						// arrived, and SFML keeps what it has until the rest shows up on a later receive.
						break;
					}

					std::int32_t tag;
					if (!(packet >> tag)) {
						Utils::printMsg("Received a packet with no type tag, ignoring it.", warning);
						continue;
					}
					PacketType type = static_cast<PacketType>(tag);

					if (type == PacketType::Hello) {
						std::string colour;
						if (!(packet >> colour)) {
							Utils::printMsg("Malformed Hello packet, ignoring it.", warning);
							continue;
						}
						if (connection.tankId >= 0) {
							Utils::printMsg("Player " + std::to_string(connection.tankId) + " sent a second Hello, ignoring it.", warning);
							continue;
						}
						if (!Game::IsValidColour(colour)) {
							Utils::printMsg("Unknown tank colour from a new player, using blue instead.", warning);
							colour = "blue";
						}

						// Now we know the colour, the player gets a tank. The ID is stored in the connection,
						// so this player's updates always go to this player's tank.
						connection.tankId = next_tank_id++;
						connection.colour = colour;
						game.AddTank(connection.tankId, colour, spawn_position);
						Utils::printMsg("Player " + std::to_string(connection.tankId) + " joined (" + colour + ").", success);

						// Tell the new player about everyone who is already here...
						for (auto& other : player_connections) {
							if (&other == &connection || other.state == Disconnected || other.tankId < 0)
								continue;
							connection.outbox.push_back(MakePlayerJoinedPacket(other.tankId, other.colour));
							if (other.hasState)
								connection.outbox.push_back(MakePlayerUpdatePacket(other.tankId, other.lastState));
						}
						// ...and tell everyone who is already here about the new player.
						RelayToOthers(player_connections, &connection, MakePlayerJoinedPacket(connection.tankId, colour), false);
					}
					else if (type == PacketType::Update) {
						TankMessage state;
						// Reading can fail if the packet is too short, so check it before using the values.
						if (!ReadTankState(packet, state)) {
							Utils::printMsg("Malformed Update packet, ignoring it.", warning);
							continue;
						}
						if (connection.tankId < 0) {
							Utils::printMsg("Got an Update before a Hello, ignoring it.", warning);
							continue;
						}

						// The connection knows which tank is its own, so we don't rely on its position in the vector.
						state.id = connection.tankId;
						game.NetworkUpdate(dt, state);
						connection.lastState = state;
						connection.hasState = true;

						// Relay it to everyone else straight away. (The alternative is to wait until we have
						// heard from every player and send one combined update. That sends less, but
						// everyone's view lags behind the slowest player.)
						RelayToOthers(player_connections, &connection, MakePlayerUpdatePacket(state.id, state), true);
					}
					else {
						Utils::printMsg("Unexpected packet type from a player, ignoring it.", warning);
					}
				}
			}

			// Send out whatever is queued for each player (relayed updates, joins and leaves).
			for (auto& connection : player_connections) {
				if (connection.state == Disconnected || connection.outbox.empty())
					continue;

				sf::Socket::Status status = FlushOutbox(*connection.socket, connection.outbox);
				if (status == sf::Socket::Status::Disconnected || status == sf::Socket::Status::Error)
					drop_connection(connection, status);
				// Partial and NotReady: the packet at the front stays queued and is sent again next frame.
			}

			// Disconnected players' sockets are closed and out of the selector, and their tanks are
			// gone from the game, so now it is safe to drop their connections from the vector too.
			player_connections.erase(
				std::remove_if(player_connections.begin(), player_connections.end(),
					[](const Connection& connection) { return connection.state == Disconnected; }),
				player_connections.end());
		}
		else
		{
			// The Player's own tank is always updated, connected or not.
			game.Update(dt);

			// If we're not connected to the observer, try to connect.
			if (!is_connected) {
				// Only try once per reconnect_delay, instead of on every single frame.
				if (reconnect_timer >= reconnect_delay) {
					reconnect_timer = 0;

					Utils::printMsg("Attempting connection to observer...");
					// SFML TCP sockets are fiddly when it comes to non-blocking connect calls,
					// we could specify a very small delay timeout in the blocking mode, but this would cause lag while it tries.
					// Ideally, we just handle connection in blocking mode, and wait for a successful connection before we continue.
					// So switch to blocking mode for each attempt (we go non-blocking again once connected).
					player_socket->setBlocking(true);
					sf::Socket::Status status = player_socket->connect(sf::IpAddress::LocalHost, observer_port);
					if (status == sf::Socket::Status::Done) {
						Utils::printMsg("Connected!", MessageType::success);
						is_connected = true;
						// From now on neither sending nor receiving may hold up the game loop.
						player_socket->setBlocking(false);

						// Introduce ourselves first (this is how the Observer learns our colour), and then
						// tell it where our tank is.
						outbox.clear();
						outbox.push_back(MakeHelloPacket(player_colour));
						force_send = true;
						send_timer = send_rate;
					}
					else {
						Utils::printMsg("Failed to connect to observer. Will try again later.", MessageType::warning);
					}
				}
			}
			else {
				// Read whatever the Observer has relayed to us from the other players.
				while (is_connected) {
					sf::Packet incoming;
					sf::Socket::Status status = player_socket->receive(incoming);

					if (status == sf::Socket::Status::Disconnected || status == sf::Socket::Status::Error) {
						lose_connection("Lost connection to Observer.");
						break;
					}
					if (status != sf::Socket::Status::Done)
						break; // NotReady, or only part of a packet so far: nothing more to read right now.

					std::int32_t tag;
					if (!(incoming >> tag)) {
						Utils::printMsg("Received a packet with no type tag, ignoring it.", warning);
						continue;
					}
					PacketType type = static_cast<PacketType>(tag);

					if (type == PacketType::PlayerJoined) {
						std::int32_t id;
						std::string colour;
						if (!(incoming >> id >> colour)) {
							Utils::printMsg("Malformed PlayerJoined packet, ignoring it.", warning);
							continue;
						}
						game.AddTank(id, colour, spawn_position);
						Utils::printMsg("Player " + std::to_string(id) + " joined (" + colour + ").", success);
					}
					else if (type == PacketType::PlayerUpdate) {
						std::int32_t id;
						TankMessage state;
						if (!(incoming >> id) || !ReadTankState(incoming, state)) {
							Utils::printMsg("Malformed PlayerUpdate packet, ignoring it.", warning);
							continue;
						}
						state.id = id;
						game.NetworkUpdate(dt, state);
					}
					else if (type == PacketType::PlayerLeft) {
						std::int32_t id;
						if (!(incoming >> id)) {
							Utils::printMsg("Malformed PlayerLeft packet, ignoring it.", warning);
							continue;
						}
						game.RemoveTank(id);
						Utils::printMsg("Player " + std::to_string(id) + " left.", warning);
					}
					else {
						Utils::printMsg("Unexpected packet type from the Observer, ignoring it.", warning);
					}
				}

				if (is_connected) {
					// At most one update per send_rate, and only if there is something new to say. If a
					// lot is already queued the connection is struggling, so hold back: the newest state
					// goes out once it catches up, and nothing is lost by skipping the ones in between.
					if (send_timer >= send_rate && outbox.size() < max_queued_updates) {
						TankMessage state = game.GetNetworkUpdate();

						bool changed = force_send
							|| state.x != last_sent.x
							|| state.y != last_sent.y
							|| state.rotation != last_sent.rotation
							|| state.barrelRotation != last_sent.barrelRotation;

						if (changed) {
							outbox.push_back(MakeUpdatePacket(state));
							last_sent = state;
							force_send = false;
							// Reset timer after queueing a message. If nothing changed we leave it running,
							// so the next change is sent on the very next frame.
							send_timer = 0;
						}
					}

					// Send what we can. A Partial or NotReady result means the packet at the front of the
					// queue is only part-way out, so it stays queued and is sent again next frame.
					sf::Socket::Status status = FlushOutbox(*player_socket, outbox);
					if (status == sf::Socket::Status::Disconnected || status == sf::Socket::Status::Error)
						lose_connection("Lost connection to Observer.");
				}
			}
		}
		// Render
		window.clear();
		game.Render(window); // This takes window as a reference.
		window.display();
	}

	return 0;
}

int main() {
	// A texture that can't be loaded is reported by throwing, so catch that here and exit with a
	// readable message instead of crashing.
	try {
		return runGame();
	}
	catch (const std::exception& e) {
		Utils::printMsg(std::string("Fatal error: ") + e.what(), error);
		return 1;
	}
}