#pragma once
#include <map>
#include <memory>
#include "tank.h"

class Game
{
public:
	Game();

	void LoadTextures();
	void HandleEvents(const std::optional<sf::Event> event);
	void Update(float dt);
	// Applies a tank message to the tank with the matching ID.
	void NetworkUpdate(float dt, TankMessage data);
	void Render(sf::RenderWindow& window);
	// Adds a tank and returns its ID. IDs are never reused, so an ID always refers to one tank.
	int AddTank(std::string body_tex, std::string barrel_tex, sf::Vector2f position);
	// Removes the tank with this ID from the game (does nothing if there isn't one).
	void RemoveTank(int id);
	TankMessage GetNetworkUpdate();

private:
	// We can now have more thank 1 tank in the game. They are keyed by ID rather than stored
	// by position, so removing one tank doesn't change the IDs of the others.
	std::map<int, std::unique_ptr<Tank>> tanks;
	int nextTankId = 0;
	std::unique_ptr<sf::Sprite> background;
	// We'll hold all textures here so we only need to load them once at game creation.
	std::unordered_map<std::string, std::shared_ptr<sf::Texture>> textures;
};