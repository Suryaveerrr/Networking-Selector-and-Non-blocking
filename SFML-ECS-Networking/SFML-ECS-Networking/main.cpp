#include <SFML\Graphics.hpp>
#include <SFML\Network.hpp>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include "game.h"
#include "utils.h"
#include "connection.h"


/*
 This is a TCP version of the Lab 3 game, which uses non-blocking networking for Observer communication,
 with multiple players. The player connects to the Observer, and sends updates about its tank position.
 The Observer accepts multiple connections from players, and receives their tank position updates.
*/

int main() {
	bool is_observer = false;
	unsigned short observer_port = 53000;
	unsigned short player_port = sf::Socket::AnyPort;

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

	// Prepare window.
	sf::RenderWindow window(sf::VideoMode({ 640, 480 }), "CMP425/CMP501 - Lab 5: Non-blocking (" + title_type + ")");
	window.setFramerateLimit(60);	//Request 60 frames per second
	Utils::printMsg("Window ready, configuring networking...");

	// Prepare networking.
	sf::TcpListener listener;
	// Socket for communication with observer
	std::unique_ptr<sf::TcpSocket> player_socket = std::make_unique<sf::TcpSocket>();

	auto port = is_observer ? observer_port : player_port;
	listener.setBlocking(false); // set listener to non-blocking mode so we can keep checking for connections

	if (listener.listen(port) == sf::Socket::Status::Error) {
		// FIXME: we need to handle the error and make sure we don't try to accept, etc. if the listen fails.
	}

	// Clock for timing the 'dt' value
	sf::Clock clock;

	// Game object.
	Game game;
	// Container of player connections for Observer.
	std::vector<Connection> player_connections;

	// Other game parameters.
	float game_speed = 1.0f;
	float send_rate = 0.1f;
	float reconnect_delay = 1.f;
	float send_timer = 1.f;
	bool is_connected = false;

	// Selector for handling multiple connections (Observer only).
	sf::SocketSelector selector;

	while (window.isOpen()) {
		// Calculate dt.
		float dt = clock.restart().asSeconds() * game_speed;
		send_timer += dt; // Count how much time has passed.
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
		// Packet to hold our updates.
		sf::Packet packet;


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

				// Add new tank with blue body and black barrel at the centre of the window.
				// FIXME: Ideally we should wait until the first receive gets some data about the trank
				// position and colour before we add it to the Observer's game.
				// Remember the tank's ID in the connection, so this player's updates always go to
				// this player's tank, whatever order connections come and go in.
				player_connections.back().tankId = game.AddTank("body_blue", "barrel_black", { 320, 240 });
			}

			// Start every live connection as NotReady each frame. This way a state can never go
			// stale when the selector finds nothing (wait returns false). Disconnected is final,
			// so those connections are left alone.
			for (auto& connection : player_connections) {
				if (connection.state != Disconnected)
					connection.state = NotReady;
			}

			// Check which connections have some data waiting to be received from sockets.
			if (selector.wait(sf::seconds(0.f))) {
				for (auto& connection : player_connections) {
					// Check the state first: a disconnected socket has been removed from the
					// selector and closed, so we must not ask the selector about it.
					if (connection.state != Disconnected && selector.isReady(*connection.socket))
						connection.state = Ready;
				}
			}

			for (int i = 0; i < player_connections.size(); i++) {
				if (player_connections.at(i).state != Ready)
					continue;

				sf::Socket::Status status = player_connections.at(i).socket->receive(packet);
				if (status == sf::Socket::Status::Done) {
					Utils::printMsg("Recieved message from: "
						+ player_connections.at(i).socket->getRemoteAddress().value().toString()
						+ ":"
						+ std::to_string(player_connections.at(i).socket->getRemotePort()));

					TankMessage message;
					// The connection knows which tank is its own, so we don't rely on its position in the vector.
					message.id = player_connections.at(i).tankId;
					// Read recieved data into TankMessage struct. Reading can fail if the packet
					// is too short, so check it before using the values.
					if (packet >> message.x >> message.y) {
						// Use the message data to update the game.
						game.NetworkUpdate(dt, message);
					}
					else {
						Utils::printMsg("Malformed packet from player " + std::to_string(message.id) + ", ignoring it.", warning);
					}
				}
				else if (status == sf::Socket::Status::Disconnected || status == sf::Socket::Status::Error) {
					// Work out who this was before we close the socket (the address is gone afterwards).
					auto remote_address = player_connections.at(i).socket->getRemoteAddress();
					std::string who = remote_address
						? remote_address->toString() + ":" + std::to_string(player_connections.at(i).socket->getRemotePort())
						: "unknown address";

					if (status == sf::Socket::Status::Disconnected)
						Utils::printMsg("Player disconnected: " + who, warning);
					else
						Utils::printMsg("Connection error, dropping player: " + who, error);

					// Stop watching the socket (before closing it, the selector needs the handle),
					// then close it. A closed connection always counts as "ready", so leaving it in
					// the selector would flag it every frame.
					selector.remove(*player_connections.at(i).socket);
					player_connections.at(i).socket->disconnect();
					player_connections.at(i).state = Disconnected;

					// Remove this player's tank from the game, so it is no longer updated or drawn.
					game.RemoveTank(player_connections.at(i).tankId);
				}
				// NotReady and Partial need nothing from us: Partial means only part of a packet
				// has arrived, and SFML keeps what it has until the rest shows up on a later receive.
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
			// If we're not connected to the observer, try to connect.
			// We want the socket in non-blocking mode, so if the connection fails, we just try again next time.
			if (!is_connected) {
				Utils::printMsg("Attempting connection to observer...");
				// SFML TCP sockets are fiddly when it comes to non-blocking connect calls,
				// we could specify a very small delay timeout in the blocking mode, but this would cause lag while it tries.
				// Ideally, we just handle connection in blocking mode, and wait for a successful connection before we continue.
				sf::Socket::Status status = player_socket->connect(sf::IpAddress::LocalHost, observer_port);
				if (status == sf::Socket::Status::Done) {
					Utils::printMsg("Connected!", MessageType::success);
					is_connected = true;
					// Add new tank with blue body and black barrel at the centre of the window.
					// The Player's only tank is the first one added, so its ID is 0 (the local-tank
					// code in Game relies on that).
					game.AddTank("body_blue", "barrel_black", { 320, 240 });
				}
				else {
					Utils::printMsg("Failed to connect to observer. Will try again later.", MessageType::warning);
				}
				send_timer = 0;
			}
			else {
				game.Update(dt);
				// Get data structure from game, containing update message.
				TankMessage message = game.GetNetworkUpdate();
				// Translate our messgage struct to sf::Packet (very rudimentary conversion).
				packet << message.x << message.y;

				// Send messages only as often as the send rate allows.
				if (send_timer >= send_rate) {
					sf::Socket::Status status = player_socket->send(packet);
					// FIXME: Send can fail, check for errors and adjust logic accordingly.
					if (status == sf::Socket::Status::Done) {
						Utils::printMsg("Sent message to: "
							+ player_socket->getRemoteAddress().value().toString()
							+ ":"
							+ std::to_string(observer_port));
					}
					else {
						Utils::printMsg("Failed to send message to Observer", error);
					}
					// Reset timer after sending the message
					send_timer = 0;
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