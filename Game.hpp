#pragma once

#include <glm/glm.hpp>

#include <string>
#include <list>
#include <array>
#include <random>

struct Connection;

//Game state, separate from rendering.

//Currently set up for a "client sends controls" / "server sends whole state" situation.

enum class Message : uint8_t {
	C2S_PlayerPos = 1,
	S2C_State = 2,
};

//used to represent a control input:
struct Button {
	uint8_t downs = 0; //times the button has been pressed
	bool pressed = false; //is the button pressed now
};

//state of one player in the game:
struct Player {
	//player inputs (sent from client):
	struct Controls {
		Button left, right, up, down, jump;
	} controls;

	//player state (sent from server):
	// glm::vec2 position = glm::vec2(0.0f, 0.0f);
	// glm::vec2 velocity = glm::vec2(0.0f, 0.0f);

	// glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);
	// std::string name = "";
	glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 start_position = glm::vec3(0.0f, 0.0f, 0.0f);
	int16_t current_state = 0;

	enum class Role : uint8_t {
		PREY = 1,
		HUNTER = 2,
	};

	Role role;

	//returns 'false' if no message or not a controls message,
	//returns 'true' if read a controls message,
	//throws on malformed controls message
	void send_player_message(Connection *connection) const;
	bool recv_player_message(Connection *connection);
};

struct Game {
	std::list< Player > players; //(using list so they can have stable addresses)
	Player *spawn_player(); //add player the end of the players list (may also, e.g., play some spawn anim)
	void remove_player(Player *); //remove player from game (may also, e.g., play some despawn anim)

	std::mt19937 mt; //used for spawning players
	uint32_t next_player_number = 1; //used for naming players

	Game();

	//state update function:
	void update(float elapsed);

	//constants:
	//the update rate on the server:
	inline static constexpr float Tick = 1.0f / 30.0f;

	//arena size:
	inline static constexpr glm::vec2 ArenaMin = glm::vec2(-0.75f, -1.0f);
	inline static constexpr glm::vec2 ArenaMax = glm::vec2( 0.75f,  1.0f);

	//player constants:
	inline static constexpr float PlayerRadius = 0.06f;
	inline static constexpr float PlayerSpeed = 2.0f;
	inline static constexpr float PlayerAccelHalflife = 0.25f;

	//----------------- New ---------------------
	// map layout:
	// std::array<std::array<int16_t, 4>, 4> layout = {{
	// 	{1, 1, 1, 1}, 
	// 	{-1, 2, -1, 2}, 
	// 	{1, 3, 3, -1}, 
	// 	{0, 1, -1, 2}
	// }};

	std::array<std::array<int16_t, 10>, 10> layout;
	void map_setup();

	// conversion between player pos and map grid
	int16_t pos_to_layout(glm::vec3 player_pos);

	// the last component (vec4.w) indicates whether
	// the start pos has been used
	// 0 - not used; anything else - used
	 std::array<glm::vec4, 2> start_pos = {
		glm::vec4(-20.0f, -20.0f, 0.0f, 0.0f),
		glm::vec4(20.0f, 20.0f, 0.0f, 0.0f)
	};
	//-------------------------------------------
	

	//---- communication helpers ----

	//used by client:
	//set game state from data in connection buffer
	// (return true if data was read)
	bool recv_state_message(Connection *connection);

	//used by server:
	//send game state.
	//  Will move "connection_player" to the front of the front of the sent list.
	void send_state_message(Connection *connection, Player *connection_player = nullptr) const;
};
