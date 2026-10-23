#pragma once
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include "tank.h"

class Game
{
public:
	Game();

	// True for the colours we have textures for.
	static bool IsValidColour(const std::string& colour);

	void HandleEvents(const std::optional<sf::Event> event);
	// Updates the Player's own tank (if there is one). Other tanks only move when told to by the network.
	void Update(float dt);
	void Render(sf::RenderWindow& window);

	// The Player's own tank, controlled with the keyboard. The Observer doesn't have one.
	void AddLocalTank(const std::string& colour, sf::Vector2f position);
	// The state of the local tank, for sending to the Observer.
	TankMessage GetNetworkUpdate();

	// Tanks that are driven by the network: the Observer has one per Player, and a Player has one per
	// other Player. They are keyed by an ID chosen by whoever adds them (the Observer hands the IDs out),
	// so removing one tank doesn't change the IDs of the others.
	void AddTank(int id, const std::string& colour, sf::Vector2f position);
	void RemoveTank(int id);
	void ClearTanks();
	// Applies a tank message to the tank with the matching ID (ignored if there isn't one).
	void NetworkUpdate(float dt, TankMessage data);

private:
	void LoadTextures();
	// Builds a tank of the given colour. Colours that aren't valid fall back to blue.
	std::unique_ptr<Tank> CreateTank(const std::string& colour, sf::Vector2f position);

	// We'll hold all textures here so we only need to load them once at game creation.
	// Declared first so they outlast the sprites that use them.
	std::unordered_map<std::string, std::shared_ptr<sf::Texture>> textures;

	std::unique_ptr<Tank> localTank;
	std::map<int, std::unique_ptr<Tank>> tanks;
	std::unique_ptr<sf::Sprite> background;
};