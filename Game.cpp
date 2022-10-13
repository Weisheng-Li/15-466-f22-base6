#include "Game.hpp"

#include "Connection.hpp"

#include <stdexcept>
#include <iostream>
#include <cstring>

#include <glm/gtx/norm.hpp>

void Player::send_player_message(Connection *connection_) const {
	assert(connection_);
	auto &connection = *connection_;

	uint32_t size = sizeof(glm::vec3);
	assert(sizeof(glm::vec3) == 12);
	connection.send(Message::C2S_PlayerPos);
	connection.send(uint8_t(size));
	connection.send(uint8_t(size >> 8));
	connection.send(uint8_t(size >> 16));

	connection.send(position);
}

bool Player::recv_player_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;

	auto &recv_buffer = connection.recv_buffer;

	//expecting [type, size_low0, size_mid8, size_high8]:
	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::C2S_PlayerPos)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	if (size != 12) throw std::runtime_error("Controls message with size " + std::to_string(size) + " != 5!");
	
	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	std::memcpy(&position, &recv_buffer[4+0], sizeof(glm::vec3));

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}


//-----------------------------------------

Game::Game() : mt(0x15466666) {
	map_setup();
}

Player *Game::spawn_player() {
	players.emplace_back();
	Player &player = players.back();

	//random point in the middle area of the arena:
	for (auto &pos: start_pos) {
		if (pos.w == 0.0f) {
			pos.w = 1.0f;
			player.start_position = glm::vec3(pos);
			// std::cout << player.start_position.x << " " << player.start_position.y << " " << player.start_position.z << std::endl;
			break;
		}
	}

	player.position = player.start_position;
	player.current_state = pos_to_layout(player.position);
	player.role = player.start_position.x > 0 ? Player::Role::HUNTER : Player::Role::PREY;

	if (players.size() >= 2 && !is_clock_start) {
		begin = std::chrono::steady_clock::now();
		is_clock_start = true;
	}

	return &player;
}

void Game::remove_player(Player *player) {
	bool found = false;
	for (auto pi = players.begin(); pi != players.end(); ++pi) {
		if (&*pi == player) {
			players.erase(pi);
			for (auto &pos: start_pos) {
				auto is_equal = [] (const glm::vec4 &v1, const glm::vec3 &v2) {
					return v1.x == v2.x &&
					       v1.y == v2.y &&
						   v1.z == v2.z;
				};
				if (is_equal(pos, pi->start_position)) {
					assert(pos.w != 0);
					pos.w = 0;
				}
			}
			found = true;
			break;
		}
	}

	if (players.size() < 2 && is_clock_start) {
		is_clock_start = false;
	}
	assert(found);
}

void Game::map_setup() {
	unsigned int mine_count = 10;

	for (int i = 0; i < 10; i++)
		for (int j = 0; j < 10; j++)
			layout[i][j] = 0;

	// set up the mine
	while (mine_count) {
		unsigned int row, col;
		row = mt() % 10;
		col = mt() % 10;

		// don't set mine on start position
		if (row == 0 && col == 0) continue;
		if (row == 9 && col == 9) continue;

		if (layout[row][col] == 0) {
			layout[row][col] = -1;
			mine_count -= 1;
		}
	}

	// return 1 if there is a mine in given index
	auto get_layout = [this](int i, int j) {
		if (i >= 0 && i < 10 && j >= 0 && j < 10) 
			return layout[i][j] == -1;
		else
			return false;
	};

	// then other cells
	for (int i = 0; i < 10; i++) {
		for (int j = 0; j < 10; j++) {
			if (layout[i][j] == -1) continue;

			assert(layout[i][j] == 0);
			layout[i][j] += get_layout(i-1, j-1) + get_layout(i, j-1) + get_layout(i-1, j)
						  + get_layout(i+1, j+1) + get_layout(i, j+1) + get_layout(i+1, j)
						  + get_layout(i+1, j-1) + get_layout(i-1, j+1);
		}
	}
}

// helper function
int16_t Game::pos_to_layout(glm::vec3 player_pos) {
	// checker board has grid_size * grid_size grid cells
	const unsigned int grid_size = 10;
	const unsigned int grid_cell_size = 4;

	const glm::vec3 origin = glm::vec3(-20.0f, -20.0f, 0.0f);

	// define initial player position (also defined in)
	glm::vec2 offset = glm::vec2(player_pos - origin);

	glm::uvec2 quantized_offset = glm::vec2(floor(offset.x)/grid_cell_size, floor(offset.y)/grid_cell_size);
	quantized_offset.x = quantized_offset.x >= grid_size? grid_size - 1 : quantized_offset.x;
	quantized_offset.y = quantized_offset.y >= grid_size? grid_size - 1 : quantized_offset.y;

	const glm::uvec2 start_layout = glm::uvec2(0, grid_size - 1);
	glm::uint row = start_layout.y - quantized_offset.y;
	glm::uint col = start_layout.x + quantized_offset.x;

	auto is_valid = [grid_size](glm::uint index) {
		return index >= 0 && index < grid_size;
	};

	if (!is_valid(row) || !is_valid(col)) return -2;

	return layout[row][col];
}

void Game::update(float elapsed) {	
	if (is_clock_start)
	since_begin = 
		std::chrono::duration_cast<std::chrono::seconds>
		(std::chrono::steady_clock::now() - begin).count();

	if (since_begin == 20) {
		for (auto &p : players) {
			if (p.role == Player::Role::PREY) 
				p.current_state = -3;
			else 
				p.current_state = -1;
		}
		return;
	}

	//position/velocity update:
	for (auto &p : players) {
		p.current_state = pos_to_layout(p.position);

		// resolve collision
		for (auto &p2: players) {
			if (&p == &p2) break;

			const float threshold = 1.0f;
			if (glm::distance(p.position, p2.position) < threshold) {
				if (p.role == Player::Role::HUNTER) {
					// win
					p.current_state = -3;
				}
				else {
					// lose
					p.current_state = -1;
				}
			}
		}

		if (p.current_state == -1 || p.current_state == -3) {
			for (auto &p3 : players) {
				if (&p == &p3) break;

				if (p.current_state == -1) p3.current_state = -3;
				if (p.current_state == -3) p3.current_state = -1;
			}

			break;
		}
	}
}


void Game::send_state_message(Connection *connection_, Player *connection_player) const {
	assert(connection_);
	auto &connection = *connection_;

	connection.send(Message::S2C_State);

	//will patch message size in later, for now placeholder bytes:
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	size_t mark = connection.send_buffer.size(); //keep track of this position in the buffer

	//send player info helper:
	auto send_player = [&](Player const &player) {
		connection.send(player.position);
		connection.send(player.start_position);
		connection.send(player.current_state);
		connection.send(player.role);
		connection.send(since_begin);
		// connection.send(player.velocity);
		// connection.send(player.color);
	
		//NOTE: can't just 'send(name)' because player.name is not plain-old-data type.
		//effectively: truncates player name to 255 chars
		// uint8_t len = uint8_t(std::min< size_t >(255, player.name.size()));
		// connection.send(len);
		// connection.send_buffer.insert(connection.send_buffer.end(), player.name.begin(), player.name.begin() + len);
	};

	//player count:
	assert(players.size() <= 255);
	connection.send(uint8_t(players.size()));
	// send the connection player before every other players
	if (connection_player) send_player(*connection_player);
	for (auto const &player : players) {
		if (&player == connection_player) continue;
		send_player(player);
	}

	//compute the message size and patch into the message header:
	uint32_t size = uint32_t(connection.send_buffer.size() - mark);
	connection.send_buffer[mark-3] = uint8_t(size);
	connection.send_buffer[mark-2] = uint8_t(size >> 8);
	connection.send_buffer[mark-1] = uint8_t(size >> 16);
}

bool Game::recv_state_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;
	auto &recv_buffer = connection.recv_buffer;

	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::S2C_State)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	uint32_t at = 0;
	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	//copy bytes from buffer and advance position:
	auto read = [&](auto *val) {
		if (at + sizeof(*val) > size) {
			throw std::runtime_error("Ran out of bytes reading state message.");
		}
		std::memcpy(val, &recv_buffer[4 + at], sizeof(*val));
		at += sizeof(*val);
	};

	players.clear();
	uint8_t player_count;
	read(&player_count);
	for (uint8_t i = 0; i < player_count; ++i) {
		players.emplace_back();
		Player &player = players.back();
		read(&player.position);
		read(&player.start_position);
		read(&player.current_state);
		read(&player.role);
		read(&since_begin);
		// read(&player.velocity);
		// read(&player.color);
		// uint8_t name_len;
		// read(&name_len);
		//n.b. would probably be more efficient to directly copy from recv_buffer, but I think this is clearer:
		// player.name = "";
		// for (uint8_t n = 0; n < name_len; ++n) {
		// 	char c;
		// 	read(&c);
		// 	player.name += c;
		// }
	}

	if (at != size) throw std::runtime_error("Trailing data in state message.");

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}
