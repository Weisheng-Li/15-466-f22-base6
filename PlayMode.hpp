#include "Mode.hpp"

#include "Connection.hpp"
#include "Game.hpp"

#include "Scene.hpp"
#include "WalkMesh.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>
#include <array>

struct PlayMode : Mode {
	PlayMode(Client &client);
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	Scene scene;
	Scene::Transform *target = nullptr;

	// local player info
	WalkPoint at;
	//transform is at player's feet and will be yawed by mouse left/right motion:
	Scene::Transform *transform = nullptr;
	//camera is at player's head and will be pitched by mouse up/down motion:
	Scene::Camera *camera = nullptr;

	Player local_player;

	//latest game state (from server):
	Game game;

	//last message from server:
	std::string server_message;

	//connection to server:
	Client &client;

};
