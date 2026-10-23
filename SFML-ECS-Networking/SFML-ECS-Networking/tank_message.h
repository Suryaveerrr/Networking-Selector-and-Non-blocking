#pragma once
#include <cstdint>
#include <string>
#include <SFML/Network.hpp>

// Every packet starts with one of these, so the receiver knows what follows.
enum class PacketType : std::int32_t {
	// Player -> Observer
	Hello,         // The Player's colour. Sent once, straight after connecting.
	Update,        // The Player's tank state. Sent whenever it changes.
	// Observer -> Player (the Observer relays what the other Players do)
	PlayerJoined,  // Another Player's ID and colour.
	PlayerUpdate,  // Another Player's ID and tank state.
	PlayerLeft     // The ID of a Player who has gone.
};

// The state of one tank.
struct TankMessage {
	// Which tank this is. Only the Observer hands these out; a Player's own updates don't need one.
	int id = 0;
	// The coordinates of the tank within the game world.
	float x = 0.f, y = 0.f;
	// Body and barrel rotation, in degrees.
	float rotation = 0.f;
	float barrelRotation = 0.f;
};

// The tank state is written and read by these two functions only, on both sides of the connection,
// so the order of the fields can never get out of step.
inline void WriteTankState(sf::Packet& packet, const TankMessage& state)
{
	packet << state.x << state.y << state.rotation << state.barrelRotation;
}

inline bool ReadTankState(sf::Packet& packet, TankMessage& state)
{
	return static_cast<bool>(packet >> state.x >> state.y >> state.rotation >> state.barrelRotation);
}

// Player -> Observer: "my colour is...".
inline sf::Packet MakeHelloPacket(const std::string& colour)
{
	sf::Packet packet;
	packet << static_cast<std::int32_t>(PacketType::Hello) << colour;
	return packet;
}

// Player -> Observer: "my tank is now like this".
inline sf::Packet MakeUpdatePacket(const TankMessage& state)
{
	sf::Packet packet;
	packet << static_cast<std::int32_t>(PacketType::Update);
	WriteTankState(packet, state);
	return packet;
}

// Observer -> Player: "another player has joined, with this colour".
inline sf::Packet MakePlayerJoinedPacket(std::int32_t id, const std::string& colour)
{
	sf::Packet packet;
	packet << static_cast<std::int32_t>(PacketType::PlayerJoined) << id << colour;
	return packet;
}

// Observer -> Player: "that player's tank is now like this".
inline sf::Packet MakePlayerUpdatePacket(std::int32_t id, const TankMessage& state)
{
	sf::Packet packet;
	packet << static_cast<std::int32_t>(PacketType::PlayerUpdate) << id;
	WriteTankState(packet, state);
	return packet;
}

// Observer -> Player: "that player has gone".
inline sf::Packet MakePlayerLeftPacket(std::int32_t id)
{
	sf::Packet packet;
	packet << static_cast<std::int32_t>(PacketType::PlayerLeft) << id;
	return packet;
}