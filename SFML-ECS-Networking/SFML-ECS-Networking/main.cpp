#include <SFML\Graphics.hpp>
#include <SFML\Network.hpp>
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
		while (const std::optional event = window.pollEvent())	{
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
			if(!is_observer)
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
				player_connections.push_back({std::move(socket)});
				selector.add(*player_connections.back().socket); // Add to persistent selector
				// Set the deafult connection state to NotReady.
				player_connections.back().state = NotReady;

				// Add new tank with blue body and black barrel at the centre of the window.
				// FIXME: Ideally we should wait until the first receive gets some data about the trank
				// position and colour before we add it to the Observer's game.
				game.AddTank("body_blue", "barrel_black", { 320, 240 });
			}
			
			// Check which connections have some data waiting to be received from sockets.
			if (selector.wait(sf::seconds(0.f))) {
				// If at least one of the sockets is ready to receive, check which ones are ready.
				for (int i = 0; i < player_connections.size(); i++) {
					if (selector.isReady(*player_connections.at(i).socket))
						player_connections.at(i).state = Ready;
					else
						player_connections.at(i).state = NotReady;
					// FIXME: We may not reset the state to NotReady if selector doesn't detect any
					// changes on any of the sockets (outer if statement). Ensure you set it back to
					// NotReady when you finished reading from the socket.
				}
			}

			// FIXME: we never remove disconnected players from the vector of connections. This will
			// cause us to continue to update and draw them. Handle disconnections and fix this.
			for (int i = 0; i < player_connections.size(); i++) {
				// FIXME: Recieve can fail, check for errors.
				if (player_connections.at(i).state == Ready) {
					sf::Socket::Status status = player_connections.at(i).socket->receive(packet);
					if (status == sf::Socket::Status::Done) {
						Utils::printMsg("Recieved message from: "
							+ player_connections.at(i).socket->getRemoteAddress().value().toString()
							+ ":"
							+ std::to_string(player_connections.at(i).socket->getRemotePort()));

						TankMessage message;
						message.id = i;
						// Read recieved data into TankMessage struct.
						// FIXME: reading from packet can fail. Refer to documentation on how to
						// handle errors and add error checking here.
						packet >> message.x >> message.y;
						// Use the message data to update the game.
						game.NetworkUpdate(dt, message);
					}
					else if (status == sf::Socket::Status::Disconnected) {
						Utils::printMsg("Player disconnected: "
							+ player_connections.at(i).socket->getRemoteAddress().value().toString()
							+ ":"
							+ std::to_string(player_connections.at(i).socket->getRemotePort()), warning);

						// For now just set the connection state to Disconnected. 
						// FIXME: But we should also remove the inactive connection from the vector and remove the tank from the game.
						player_connections.at(i).state = Disconnected;
					}
				}
			}
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

