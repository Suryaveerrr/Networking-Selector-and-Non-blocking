#include "game.h"

Game::Game()
{
	LoadTextures();
	// Initialis sprite for background.
	background = std::make_unique<sf::Sprite>(*textures.at("ground_sand"));
	background->setTextureRect(sf::IntRect({ 0, 0 }, { 640, 480 }));

	// We're not loading any tanks by default.
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

bool Game::IsValidColour(const std::string& colour)
{
	return colour == "blue" || colour == "green" || colour == "black";
}

std::unique_ptr<Tank> Game::CreateTank(const std::string& colour, sf::Vector2f position)
{
	// The colour can come off the network, so never look up a texture we don't have.
	std::string safe_colour = IsValidColour(colour) ? colour : "blue";

	std::unique_ptr<Tank> tank = std::make_unique<Tank>(textures.at("body_" + safe_colour), textures.at("barrel_" + safe_colour));
	tank->position = position;
	tank->Update(0.f); // Apply the position to the sprites straight away.
	return tank;
}

void Game::HandleEvents(const std::optional<sf::Event> event)
{
	// Only a Player has a tank to control.
	if (!localTank)
		return;
	Tank& tank = *localTank;

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
		if (keyPressed->scancode == sf::Keyboard::Scancode::Q) {
			tank.isBarrelRotating.left = true;
			tank.isBarrelRotating.right = false;
		}
		else if (keyPressed->scancode == sf::Keyboard::Scancode::E) {
			tank.isBarrelRotating.left = false;
			tank.isBarrelRotating.right = true;
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
		if (keyReleased->scancode == sf::Keyboard::Scancode::Q)
			tank.isBarrelRotating.left = false;
		if (keyReleased->scancode == sf::Keyboard::Scancode::E)
			tank.isBarrelRotating.right = false;
	}
}

void Game::Update(float dt)
{
	if (localTank)
		localTank->Update(dt);
}

void Game::Render(sf::RenderWindow& window)
{
	window.draw(*background);
	// The other players first, so the Player's own tank is drawn on top.
	for (auto& entry : tanks) {
		entry.second->Render(window);
	}
	if (localTank)
		localTank->Render(window);
}

void Game::AddLocalTank(const std::string& colour, sf::Vector2f position)
{
	localTank = CreateTank(colour, position);
}

TankMessage Game::GetNetworkUpdate()
{
	TankMessage message;
	if (!localTank)
		return message;

	// The ID doesn't matter for the local tank: the Observer works out who an update came from.
	message.x = localTank->position.x;
	message.y = localTank->position.y;
	message.rotation = localTank->bodyRotation.asDegrees();
	message.barrelRotation = localTank->barrelRotation.asDegrees();
	return message;
}

void Game::AddTank(int id, const std::string& colour, sf::Vector2f position)
{
	// If there is already a tank with this ID it is replaced.
	tanks[id] = CreateTank(colour, position);
}

void Game::RemoveTank(int id)
{
	tanks.erase(id);
}

void Game::ClearTanks()
{
	tanks.clear();
}

// Tank data includes the tank's ID, so the right tank gets updated for each player.
void Game::NetworkUpdate(float dt, TankMessage data) {
	auto it = tanks.find(data.id);
	if (it == tanks.end())
		return; // No tank with that ID (it may have just been removed), so ignore the update.

	// Force position and rotations from network data.
	it->second->position = { data.x, data.y };
	it->second->bodyRotation = sf::degrees(data.rotation);
	it->second->barrelRotation = sf::degrees(data.barrelRotation);
	// Update tank so the new values are applied to its sprites.
	// NOTE: No inputs are ever set on these tanks, so they only move according to network updates.
	it->second->Update(dt);
}