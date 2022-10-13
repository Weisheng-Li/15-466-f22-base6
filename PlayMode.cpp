#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/quaternion.hpp>

#include <random>
#include <array>
#include <chrono>
#include <thread>


GLuint phonebank_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > phonebank_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("plane.pnct"));
	phonebank_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > phonebank_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("plane.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = phonebank_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = phonebank_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

WalkMesh const *walkmesh = nullptr;
Load< WalkMeshes > phonebank_walkmeshes(LoadTagDefault, []() -> WalkMeshes const * {
	WalkMeshes *ret = new WalkMeshes(data_path("plane.w"));
	walkmesh = &ret->lookup("WalkMesh.001");
	return ret;
});

PlayMode::PlayMode(Client &client_) : scene(*phonebank_scene), client(client_) {
	for (auto &trans : scene.transforms) {
		if (trans.name == "Cone") target = &trans;
	}

	//create a player transform:
	scene.transforms.emplace_back();
	transform = &scene.transforms.back();
	transform->position = glm::vec3(0.0f, 0.0f, 0.0f);

	//create a player camera attached to a child of the player transform:
	scene.transforms.emplace_back();
	scene.cameras.emplace_back(&scene.transforms.back());
	camera = &scene.cameras.back();
	camera->fovy = glm::radians(60.0f);
	camera->near = 0.01f;
	camera->transform->parent = transform;

	//player's eyes are 1.8 units above the ground:
	camera->transform->position = glm::vec3(0.0f, 0.0f, 1.8f);

	//rotate camera facing direction (-z) to player facing direction (+y):
	camera->transform->rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

	//start player walking at nearest walk point:
	at = walkmesh->nearest_walk_point(transform->position);
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	Player::Controls &controls = local_player.controls;

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.keysym.sym == SDLK_ESCAPE) {
			SDL_SetRelativeMouseMode(SDL_FALSE);
			return true;
		} else if (evt.key.keysym.sym == SDLK_a) {
			controls.left.downs += 1;
			controls.left.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			controls.right.downs += 1;
			controls.right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			controls.up.downs += 1;
			controls.up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			controls.down.downs += 1;
			controls.down.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_SPACE) {
			controls.jump.downs += 1;
			controls.jump.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			controls.left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			controls.right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			controls.up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			controls.down.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_SPACE) {
			controls.jump.pressed = false;
			return true;
		}
	} else if (evt.type == SDL_MOUSEBUTTONDOWN) {
		if (SDL_GetRelativeMouseMode() == SDL_FALSE) {
			SDL_SetRelativeMouseMode(SDL_TRUE);
			return true;
		}
	} else if (evt.type == SDL_MOUSEMOTION) {
		if (SDL_GetRelativeMouseMode() == SDL_TRUE) {
			glm::vec2 motion = glm::vec2(
				evt.motion.xrel / float(window_size.y),
				-evt.motion.yrel / float(window_size.y)
			);
			glm::vec3 upDir = walkmesh->to_world_smooth_normal(at);
			transform->rotation = glm::angleAxis(-motion.x * camera->fovy, upDir) * transform->rotation;

			float pitch = glm::pitch(camera->transform->rotation);
			pitch += motion.y * camera->fovy;
			//camera looks down -z (basically at the player's feet) when pitch is at zero.
			pitch = std::min(pitch, 0.95f * 3.1415926f);
			pitch = std::max(pitch, 0.05f * 3.1415926f);
			camera->transform->rotation = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));

			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {
	if (countdown > 0.0f) {
		countdown -= elapsed;
		if (countdown <= 0) {
			countdown = 0.0f;
			reset_game();
		}
		return;
	}

	static bool start_pos_set = false;

	Player::Controls &controls = local_player.controls;
	//player walking:
	{
		//combine inputs into a move:
		constexpr float PlayerSpeed = 3.0f;
		glm::vec2 move = glm::vec2(0.0f);
		if (controls.left.pressed && !controls.right.pressed) move.x =-1.0f;
		if (!controls.left.pressed && controls.right.pressed) move.x = 1.0f;
		if (controls.down.pressed && !controls.up.pressed) move.y =-1.0f;
		if (!controls.down.pressed && controls.up.pressed) move.y = 1.0f;

		//make it so that moving diagonally doesn't go faster:
		if (move != glm::vec2(0.0f)) move = glm::normalize(move) * PlayerSpeed * elapsed;

		//get move in world coordinate system:
		glm::vec3 remain = transform->make_local_to_world() * glm::vec4(move.x, move.y, 0.0f, 0.0f);

		//using a for() instead of a while() here so that if walkpoint gets stuck in
		// some awkward case, code will not infinite loop:
		for (uint32_t iter = 0; iter < 10; ++iter) {
			if (remain == glm::vec3(0.0f)) break;
			WalkPoint end;
			float time;
			walkmesh->walk_in_triangle(at, remain, &end, &time);
			at = end;
			if (time == 1.0f) {
				//finished within triangle:
				remain = glm::vec3(0.0f);
				break;
			}
			//some step remains:
			remain *= (1.0f - time);
			//try to step over edge:
			glm::quat rotation;
			if (walkmesh->cross_edge(at, &end, &rotation)) {
				//stepped to a new triangle:
				at = end;
				//rotate step to follow surface:
				remain = rotation * remain;
			} else {
				//ran into a wall, bounce / slide along it:
				glm::vec3 const &a = walkmesh->vertices[at.indices.x];
				glm::vec3 const &b = walkmesh->vertices[at.indices.y];
				glm::vec3 const &c = walkmesh->vertices[at.indices.z];
				glm::vec3 along = glm::normalize(b-a);
				glm::vec3 normal = glm::normalize(glm::cross(b-a, c-a));
				glm::vec3 in = glm::cross(normal, along);

				//check how much 'remain' is pointing out of the triangle:
				float d = glm::dot(remain, in);
				if (d < 0.0f) {
					//bounce off of the wall:
					remain += (-1.25f * d) * in;
				} else {
					//if it's just pointing along the edge, bend slightly away from wall:
					remain += 0.01f * d * in;
				}
			}
		}

		if (remain != glm::vec3(0.0f)) {
			std::cout << "NOTE: code used full iteration budget for walking." << std::endl;
		}

		//update player's position to respect walking:
		transform->position = walkmesh->to_world_point(at);
		local_player.position = transform->position;
		{ //update player's rotation to respect local (smooth) up-vector:
			
			glm::quat adjust = glm::rotation(
				transform->rotation * glm::vec3(0.0f, 0.0f, 1.0f), //current up vector
				walkmesh->to_world_smooth_normal(at) //smoothed up vector at walk location
			);
			transform->rotation = glm::normalize(adjust * transform->rotation);
		}
	}

	// send player location to the server to query the current state
	local_player.send_player_message(&client.connection);

	//reset button press counters:
	controls.left.downs = 0;
	controls.right.downs = 0;
	controls.up.downs = 0;
	controls.down.downs = 0;
	controls.jump.downs = 0;

	// receive data:
	client.poll([this](Connection *c, Connection::Event event){
		if (event == Connection::OnOpen) {
			std::cout << "[" << c->socket << "] opened" << std::endl;
		} else if (event == Connection::OnClose) {
			std::cout << "[" << c->socket << "] closed (!)" << std::endl;
			throw std::runtime_error("Lost connection to server!");
		} else { assert(event == Connection::OnRecv);
			//std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush(); //DEBUG
			bool handled_message;
			try {
				do {
					handled_message = false;
					if (game.recv_state_message(c)) handled_message = true;
				} while (handled_message);
			} catch (std::exception const &e) {
				std::cerr << "[" << c->socket << "] malformed message from server: " << e.what() << std::endl;
				//quit the game:
				throw e;
			}
		}
	}, 0.0);

	if (game.players.size() > 0) {
		Player &me = game.players.front();
		if (me.start_position != glm::vec3(0.0f, 0.0f, 0.0f) && !start_pos_set) {
			// reset player
			transform->position = me.start_position;
			local_player.position = transform->position;
			if (me.start_position.x < 0 && me.start_position.y < 0)
				transform->rotation = glm::angleAxis(glm::radians(-45.0f), glm::vec3(0.0f, 0.0f, 1.0f));
			else
				transform->rotation = glm::angleAxis(glm::radians(180.0f - 45.0f), glm::vec3(0.0f, 0.0f, 1.0f));

			//rotate camera facing direction (-z) to player facing direction (+y):
			camera->transform->rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

			//start player walking at nearest walk point:
			at = walkmesh->nearest_walk_point(transform->position);

			start_pos_set = true;
		}
	}

	if (game.players.size() > 1) {
		// std::cout << "now we have more than one players!" << std::endl;
		assert(game.players.size() == 2);
		target->position.x = game.players.back().position.x;
		target->position.y = game.players.back().position.y;
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	int16_t current_state = game.players.front().current_state;

	{	// set background color based on current state
		const std::vector<glm::vec3> color_pallete = {
			glm::vec3(0.9176f, 0.8828f, 0.7176f), // cream
			glm::vec3(0.9882f, 0.7490f, 0.2863f), // yellow
			glm::vec3(0.9686f, 0.4980f, 0.0000f), // orange
			glm::vec3(0.8392f, 0.1569f, 0.1569f), // red
			glm::vec3(0.0000f, 0.1882f, 0.2863f)  // dark blue
		};
		size_t pallete_size = 4;	// the last color is reserved for end-game
		glm::vec3 current_color;
		
		size_t color_idx = current_state;
		if (current_state >= pallete_size)
			color_idx = pallete_size - 1;
		
		if (current_state == -1) current_color = color_pallete[pallete_size];
		else if (current_state == -2) current_color = glm::vec3(1.0f, 1.0f, 1.0f);
		else if (current_state == -3) current_color = color_pallete[2];
		else {
			assert(color_idx >= 0);
			current_color = color_pallete[color_idx];
		}

		glClearColor(current_color.x, current_color.y, current_color.z, 1.0f);
	}

	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	scene.draw(*camera);

	// In case you are wondering if your walkmesh is lining up with your scene, try:
	// {
	// 	glDisable(GL_DEPTH_TEST);
	// 	DrawLines lines(player.camera->make_projection() * glm::mat4(player.camera->transform->make_world_to_local()));
	// 	for (auto const &tri : walkmesh->triangles) {
	// 		lines.draw(walkmesh->vertices[tri.x], walkmesh->vertices[tri.y], glm::u8vec4(0x88, 0x00, 0xff, 0xff));
	// 		lines.draw(walkmesh->vertices[tri.y], walkmesh->vertices[tri.z], glm::u8vec4(0x88, 0x00, 0xff, 0xff));
	// 		lines.draw(walkmesh->vertices[tri.z], walkmesh->vertices[tri.x], glm::u8vec4(0x88, 0x00, 0xff, 0xff));
	// 	}
	// }

	glDisable(GL_DEPTH_TEST);
	float aspect = float(drawable_size.x) / float(drawable_size.y);
	DrawLines lines(glm::mat4(
		1.0f / aspect, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	));


	// show text on the screen
	if (current_state == -1) {
		constexpr float H2 = 0.3f;
		lines.draw_text("You Lose",
		glm::vec3(-aspect + 4.0f * H2, -1.0 + 5.0f * H2, 0.0),
		glm::vec3(H2, 0.0f, 0.0f), glm::vec3(0.0f, H2, 0.0f),
		glm::u8vec4(0xff, 0xff, 0xff, 0xff));

		if (countdown == 0.0f)
			countdown = 200.0f;
	} else if (current_state == -3) {
		constexpr float H2 = 0.3f;
		lines.draw_text("You Win",
		glm::vec3(-aspect + 4.0f * H2, -1.0 + 5.0f * H2, 0.0),
		glm::vec3(H2, 0.0f, 0.0f), glm::vec3(0.0f, H2, 0.0f),
		glm::u8vec4(0xff, 0xff, 0xff, 0xff));
		if (countdown == 0.0f)
			countdown = 200.0f;
	} else if (current_state == -2) {	// no text shown
	} else {
		constexpr float H2 = 0.3f;
		glm::u8vec4 color;
		if (current_state < 2) color = glm::u8vec4(0x0, 0x0, 0x0, 0x0);
		else 				   color = glm::u8vec4(0xff, 0xff, 0xff, 0x00);

		lines.draw_text(std::to_string(current_state),
		glm::vec3(-aspect + 5.5f * H2, -1.0 + 5.0f * H2, 0.0),
		glm::vec3(H2, 0.0f, 0.0f), glm::vec3(0.0f, H2, 0.0f),
		color);
	}

	if (game.players.front().role == Player::Role::HUNTER) {
		constexpr float H = 0.09f;
		lines.draw_text("Hunting Time",
			glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		float ofs = 2.0f / drawable_size.y;
		lines.draw_text("Hunting Time",
			glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + + 0.1f * H + ofs, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0xff, 0xff, 0xff, 0x00));
	} else {
		constexpr float H = 0.09f;
		lines.draw_text("Don't get caught",
			glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		float ofs = 2.0f / drawable_size.y;
		lines.draw_text("Don't get caught",
			glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + + 0.1f * H + ofs, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0xff, 0xff, 0xff, 0x00));
	}

	GL_ERRORS();
}

void PlayMode::reset_game() {
	std::cout << "reset!" << std::endl;
	// reset player
	Player &me = game.players.front();
	transform->position = me.start_position;
	local_player.position = transform->position;
	transform->rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

	//rotate camera facing direction (-z) to player facing direction (+y):
	camera->transform->rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

	//start player walking at nearest walk point:
	at = walkmesh->nearest_walk_point(transform->position);

	game.players.front().current_state = -2;

	update(0);
}
