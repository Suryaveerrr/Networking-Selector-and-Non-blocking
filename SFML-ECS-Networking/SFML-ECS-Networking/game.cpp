#include "game.h"

Game::Game()
{
	LoadTextures();
	// Initialis sprite for background.
	background = std::make_unique<sf::Sprite>(*textures["ground_sand"]);
	background->setTextureRect(sf::IntRect({ 0, 0 }, { 640, 480 }));

	// We're not loading the tank by default any more.
}

void Game::LoadTextures() {
	textures["ground_sand"] = std::make_shared<sf::Texture>("Assets/tileSand1.png");
	textures["ground_sand"]->setRepeated(true);

	textures["barrel_black"] = std::make_shared<sf::Texture>("Assets/blackBarrel.png");
	textures["body_black"] = std::make_shared<sf::Texture>("Assets/blackTank.png");
	textures["barrel_blue"] = std::make_shared<sf::Texture>("Assets/blueBarrel.png");
	textures["body_blue"] = std::make_shared<sf::Texture>("Assets/blueTank.png");
	textures["barrel_green"] = std::make_shared<sf::Texture>("Assets/greenBarrel.png");
	textures["body_green"] = std::make_shared<sf::Texture>("Assets/greenTank.png");
}

void Game::HandleEvents(const std::optional<sf::Event> event)
{
	// The Player's tank is only added once it has connected to the Observer, so before that
	// there is nothing to control. (It is the first tank added, so its ID is 0.)
	auto local_tank = tanks.find(0);
	if (local_tank == tanks.end())
		return;
	Tank& tank = *local_tank->second;

	// Handle key press events passed from window.
	if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
		if (keyPressed->scancode == sf::Keyboard::Scancode::W) {
			tank.isMoving.forward = true;
			tank.isMoving.backward = false;
		}
		else if (keyPressed->scancode == sf::Keyboard::Scancode::S) {
			tank.isMoving.forward = false;
			tank.isMoving.backward = true;
		}
		if (keyPressed->scancode == sf::Keyboard::Scancode::A) {
			tank.isMoving.left = true;
			tank.isMoving.right = false;
		}
		else if (keyPressed->scancode == sf::Keyboard::Scancode::D) {
			tank.isMoving.left = false;
			tank.isMoving.right = true;
		}
	}

	// Handle key release events passed from window.
	else if (const auto* keyReleased = event->getIf<sf::Event::KeyReleased>()) {
		if (keyReleased->scancode == sf::Keyboard::Scancode::W)
			tank.isMoving.forward = false;
		if (keyReleased->scancode == sf::Keyboard::Scancode::S)
			tank.isMoving.backward = false;
		if (keyReleased->scancode == sf::Keyboard::Scancode::A)
			tank.isMoving.left = false;
		if (keyReleased->scancode == sf::Keyboard::Scancode::D)
			tank.isMoving.right = false;
	}
}

void Game::Update(float dt)
{
	// Update every tank (this used to update tank 0 over and over, whatever the loop index was).
	for (auto& entry : tanks) {
		entry.second->Update(dt);
	}
}

// Tank data includes the tank's ID, so the Observer can update the right tank for each player.
void Game::NetworkUpdate(float dt, TankMessage data) {
	auto it = tanks.find(data.id);
	if (it == tanks.end())
		return; // No tank with that ID (it may have just been removed), so ignore the update.

	// Force position updates from network data.
	it->second->position = { data.x, data.y };
	// Update tank with new position.
	// NOTE: This assumets no inputs were detected and so the tank will only move according to 
	// network updates. This is not ideal and prone to unexpected behaviour if game is extended
	// to be fully multiplayer. 
	it->second->Update(dt);
}

void Game::Render(sf::RenderWindow& window)
{
	window.draw(*background);
	for (auto& entry : tanks) {
		entry.second->Render(window);
	}
}

int Game::AddTank(std::string body_tex, std::string barrel_tex, sf::Vector2f position)
{
	std::unique_ptr<Tank> tank = std::make_unique<Tank>(Tank(textures[body_tex], textures[barrel_tex]));
	tank->position = position;

	int id = nextTankId++;
	tanks[id] = std::move(tank);
	return id;
}

void Game::RemoveTank(int id)
{
	tanks.erase(id);
}

TankMessage Game::GetNetworkUpdate()
{
	// This is hardcoded for the local tan for Player.
	// First value can be anything as we don't care about player ID for local Player updates.
	return { 0, tanks.at(0)->position.x, tanks.at(0)->position.y };
}