#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "ext/stb_image.h"
#include <stdlib.h>
#include <SDL3/SDL.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 800
#define FRAME_RATE 120

#define DESIRED_FRAME_TIME (1.0 / FRAME_RATE)
#define MOUSE_SENS 60.0f

// These goobers make sure that function calls passed to the macro only get called once
#define MIN(X, Y) ({ __typeof__(X) _X = X;\
                    __typeof__(Y) _Y = Y;\
                   (_X) < (_Y) ? (_X) : (_Y);\
                   })
#define MAX(X, Y) ({ __typeof__(X) _X = X;\
                    __typeof__(Y) _Y = Y;\
                   (_X) > (_Y) ? (_X) : (_Y);\
                   })

// statement expression is compiler extension
#define DISTANCE(X1, Y1, X2, Y2) ({ __typeof__(X1) _xdist = X2 - X1; __typeof__(Y1) _ydist = Y2 - Y1;\
                                    SDL_sqrtf(_xdist*_xdist + _ydist*_ydist);})

#define PANIC(fmt, ...) ({ fprintf(stderr, fmt, ##__VA_ARGS__); exit(1); })

#define DEG2RAD (SDL_PI_F / 180.0f)
#define RAD2DEG (180.0f / SDL_PI_F)

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} Color;

typedef struct {
    float x;
    float y;
    float speed;
    float radius;
    float angle;
    float fov;
} Player;

typedef struct {
    float x;
    float y;
    int wall_id;
    int wall_orient;
} RayData;

typedef struct {
    bool quit;
    bool map_mode;

    // time in seconds
    double last_frame; 
    double delta_time;

    // Mouse
    float mouse_x_pos;
    float mouse_y_pos;
    float mouse_xrel;
    float mouse_yrel;
} EngineState;

enum {
    TEXTURE_WALL1 = 1,
    TEXTURE_WALL2,
    TEXTURE_WALL3,
    TEXTURE_WALL4,
};

#define MAX_TEXTURES 32
SDL_Texture *g_textures[MAX_TEXTURES];
SDL_Texture *g_sprites[MAX_TEXTURES];

#define MAP_WIDTH 8
#define MAP_HEIGHT 8

#define MAP_X_SCALE ((float)SCREEN_WIDTH / MAP_WIDTH)
#define MAP_Y_SCALE ((float)SCREEN_HEIGHT / MAP_HEIGHT)

enum {
    WALL_HORIZONTAL,
    WALL_VERTICAL,
};

int map[MAP_WIDTH * MAP_HEIGHT] = {
    2, 2, 2, 2, 2, 2, 2, 2,
    2, 0, 0, 0, 0, 0, 0, 2,
    2, 0, 0, 0, 0, 0, 0, 2,
    2, 0, 0, 0, 0, 0, 0, 2,
    2, 0, 0, 2, 2, 0, 0, 2,
    2, 0, 0, 0, 0, 0, 0, 2,
    2, 0, 0, 0, 0, 0, 0, 2,
    2, 2, 2, 2, 2, 2, 2, 2,
};

// Global state

EngineState e_state = {
    .quit = false,
    .map_mode = false,
    .last_frame = 0.0,
    .delta_time = 0.0f,
};

Player player = {
    .x = 2.0f,
    .y = 2.0f,
    .speed = 1.0f,
    .radius = 0.1f,
    .angle = 0.0f,
    .fov = 45.0f,
};

void init_sdl(SDL_Renderer **renderer, SDL_Window **window, int width, int height) {
    if(!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("ERROR: Failed to initialize SDL\n%s", SDL_GetError());
        SDL_Quit();
    }
    if (!SDL_CreateWindowAndRenderer("Racaster", width, height, 0, window, renderer)) {
        SDL_Log("ERROR: Failed to create window\n%s", SDL_GetError());
        SDL_Quit();
    }
    
    SDL_SetWindowRelativeMouseMode(*window, true);

}

SDL_Texture *load_texture(SDL_Renderer *r, const char *filepath) {
    int width, height, n_channels;
    uint8_t *data = stbi_load(filepath, &width, &height, &n_channels, 0);
    if (data == NULL) {
        fprintf(stderr, "Failed to load image %s\n", filepath);
    } else {
        printf("Loaded image %s: %dx%dx%d\n", filepath, width, height, n_channels);
    }
    SDL_PixelFormat format = n_channels == 3 ? SDL_PIXELFORMAT_RGB24 : SDL_PIXELFORMAT_RGBA32;

    SDL_Texture *texture = SDL_CreateTexture(r, format, SDL_TEXTUREACCESS_STATIC, width, height);
    bool res = SDL_UpdateTexture(texture, NULL, data, width * n_channels);
    if (!res) {
        fprintf(stderr, "%s\n", SDL_GetError());
    }

    stbi_image_free(data);
    return texture;
}

void load_all_textures(SDL_Renderer *r) {
    for (int i = 1; i <= 4; i++) {
        char buf[32];
        sprintf(buf, "res/textures/%d.png", i);
        g_textures[i] = load_texture(r, buf);
    }
}

void handle_events() {
    e_state.mouse_xrel = 0.0f;
    e_state.mouse_yrel = 0.0f;

    SDL_Event e;
    while(SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT ||
            (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) e_state.quit = true;

        if (e.type == SDL_EVENT_KEY_DOWN) {
            switch (e.key.scancode) {
                case SDL_SCANCODE_M:
                    e_state.map_mode = !e_state.map_mode;
                break;
                default:
            }
        }
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            e_state.mouse_xrel = e.motion.xrel;
            e_state.mouse_yrel = e.motion.yrel;
            e_state.mouse_x_pos = e.motion.x;
            e_state.mouse_y_pos = e.motion.y;
        }
    }

}

void player_collide() {
    int cell_x;
    int cell_y;
    cell_y = (int)(player.y - player.radius);
    if (map[cell_y*MAP_WIDTH + (int)player.x] != 0)
        player.y = (cell_y + 1) + player.radius; // need to add one since cell coords are top left

    cell_y = (int)(player.y + player.radius);
    if (map[cell_y*MAP_WIDTH + (int)player.x] != 0)
        player.y = cell_y - player.radius;

    cell_x = (int)(player.x + player.radius);
    if (map[(int)player.y*MAP_WIDTH + cell_x] != 0)
        player.x = cell_x - player.radius;

    cell_x = (int)(player.x - player.radius);
    if (map[(int)player.y*MAP_WIDTH + cell_x] != 0)
        player.x = (cell_x + 1) + player.radius;
}

void handle_player_input() {
    //---Keyboard Input---
    const bool *keys = SDL_GetKeyboardState(NULL);

    float dx = 0.0f, dy = 0.0f, sprint = 1.0f;
    if (keys[SDL_SCANCODE_W]) {
        dx += SDL_cos(player.angle * DEG2RAD);
        dy += SDL_sin(player.angle * DEG2RAD);
    }
    if (keys[SDL_SCANCODE_S]) {
        dx += -SDL_cos(player.angle * DEG2RAD);
        dy += -SDL_sin(player.angle * DEG2RAD);
    }
    // horrizontal
    if (keys[SDL_SCANCODE_D]) {
        float angle = player.angle + 90.0f;
        dx += SDL_cos(angle * DEG2RAD);
        dy += SDL_sin(angle * DEG2RAD);
    }
    if (keys[SDL_SCANCODE_A]) {
        float angle = player.angle - 90.0f;
        dx += SDL_cos(angle * DEG2RAD);
        dy += SDL_sin(angle * DEG2RAD);
    }
    if (keys[SDL_SCANCODE_LSHIFT]) sprint = 2.0f;
    dx = MIN(dx, 1.0f);
    dy = MIN(dy, 1.0f);
    player.y += dy * player.speed*sprint * e_state.delta_time;
    player.x += dx * player.speed*sprint * e_state.delta_time;
    player.x = MIN(MAP_WIDTH, MAX(player.x, 0));
    player.y = MIN(MAP_HEIGHT, MAX(player.y, 0));

    //--Collision---
    player_collide();

    //---Mouse Input---
    if (keys[SDL_SCANCODE_LEFT]) {
        player.angle -= 2 * MOUSE_SENS * e_state.delta_time;
    } else if (keys[SDL_SCANCODE_RIGHT]) {
        player.angle += 2 * MOUSE_SENS * e_state.delta_time;
    } else {
        player.angle += MOUSE_SENS * e_state.mouse_xrel * e_state.delta_time;
    }
}

// https://gist.github.com/Gumichan01/332c26f6197a432db91cc4327fcabb1c
int render_fill_circle(SDL_Renderer *renderer, int x, int y, int radius) {
    int offsetx, offsety, d;
    int status;

    offsetx = 0;
    offsety = radius;
    d = radius -1;
    status = 0;
    while (offsety >= offsetx) {
        status += SDL_RenderLine(renderer, x - offsety, y + offsetx,
                                     x + offsety, y + offsetx);
        status += SDL_RenderLine(renderer, x - offsetx, y + offsety,
                                 x + offsetx, y + offsety);
        status += SDL_RenderLine(renderer, x - offsetx, y - offsety,
                                 x + offsetx, y - offsety);
        status += SDL_RenderLine(renderer, x - offsety, y - offsetx,
                                     x + offsety, y - offsetx);
        if (status < 0) {
            status = -1;
            break;
        }
        if (d >= 2*offsetx) {
            d -= 2*offsetx + 1;
            offsetx +=1;
        }
        else if (d < 2 * (radius - offsety)) {
            d += 2 * offsety - 1;
            offsety -= 1;
        }
        else {
            d += 2 * (offsety - offsetx - 1);
            offsety -= 1;
            offsetx += 1;
        }
    }
    return status;
}

#define RAY_STEP 0.005f
// Cast a ray from x_start, y_start facing direction angle
// ray collision data is returned through x_end, y_end, wall_orient
RayData cast_ray(float x_start, float y_start, float angle) {
    assert(x_start > 0 && x_start < MAP_WIDTH && y_start > 0 && y_start < MAP_HEIGHT);

    // don't cast too far
    const float max_length = SDL_sqrtf(MAP_WIDTH*MAP_WIDTH + MAP_HEIGHT*MAP_HEIGHT);
    const float x_step = RAY_STEP * SDL_cos(angle * DEG2RAD);
    const float y_step = RAY_STEP * SDL_sin(angle * DEG2RAD);
    const int max_steps = max_length / RAY_STEP;
    for (int i = 0; i < max_steps; i++) {
        float curr_x = player.x + i * x_step;
        float curr_y = player.y + i * y_step;

        // in a wall
        int wall_id = map[(int)curr_y*MAP_WIDTH + (int)curr_x];
        if (wall_id != 0) {
            float eps = 1.001f;
            bool horizontal = map[(int)(curr_y - y_step*eps)*MAP_HEIGHT + (int)curr_x] == 0;
            bool vertical = map[(int)curr_y*MAP_WIDTH + (int)(curr_x - x_step*eps)] == 0;
            if (horizontal) {
                float y_end = y_step > 0 ? (int)curr_y : (int)(curr_y + 1);
                float x_end = curr_x;
                return (RayData) {
                    .x = x_end,
                    .y = y_end,
                    .wall_id = wall_id,
                    .wall_orient = WALL_HORIZONTAL,
                };
            } else if (vertical) {
                float x_end = x_step > 0 ? (int)curr_x : (int)(curr_x + 1);
                float y_end = curr_y;
                return (RayData) {
                    .x = x_end,
                    .y = y_end,
                    .wall_id = wall_id,
                    .wall_orient = WALL_VERTICAL,
                };
            }
            // hit a corner
            float x_end = x_step > 0 ? (int)curr_x : (int)(curr_x + 1);
            float y_end = y_step > 0 ? (int)curr_y : (int)(curr_y + 1);
            return (RayData) {
                .x = x_end,
                .y = y_end,
                .wall_id = wall_id,
                .wall_orient = WALL_HORIZONTAL,
            };
        }
    }
    fprintf(stderr, "Ray did not collide?\n");
    return (RayData){0};
}

// 2d map view
void draw_level_map(SDL_Renderer *renderer) {
    // Clear Black
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // White grid
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    for (size_t row = 0; row < MAP_HEIGHT; row++) {
        for (size_t col = 0; col < MAP_WIDTH; col++) {
            SDL_FRect rect = {
                .x = col * MAP_X_SCALE,
                .y = row * MAP_Y_SCALE,
                .w = MAP_X_SCALE,
                .h = MAP_Y_SCALE,
            };
            if (map[row * MAP_WIDTH + col] != 0) {
               SDL_SetRenderDrawColor(renderer, 0, 0, 155, 255);
               SDL_RenderFillRect(renderer, &rect);
               SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            } else {
               SDL_RenderRect(renderer, &rect);
            }
        }
    }

    // Player
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    render_fill_circle(renderer, MAP_X_SCALE * player.x, MAP_Y_SCALE * player.y, MAP_X_SCALE * player.radius);

    // Rays
    float angle_start = player.angle - player.fov / 2.0f;
    float angle_end = player.angle + player.fov / 2.0f;
    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
    for (float angle = angle_start; angle <= angle_end; angle += 0.5f) {
        RayData ray_data = cast_ray(player.x, player.y, angle);
        SDL_RenderLine(renderer, MAP_X_SCALE * player.x, MAP_Y_SCALE * player.y,
                       MAP_X_SCALE * ray_data.x, MAP_Y_SCALE * ray_data.y);
    }

}

#define RAY_COUNT 400
#define WALL_HEIGHT 4.0f
void render_scene(SDL_Renderer *renderer) {
    //---Environment---

    // Clear
    SDL_SetRenderDrawColor(renderer, 80, 80, 100, 255);
    SDL_RenderClear(renderer);
    // Floor
    SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
    SDL_RenderFillRect(renderer, &(SDL_FRect){0, SCREEN_HEIGHT/2.0f, SCREEN_WIDTH, SCREEN_HEIGHT/2.0f});

    // Raycast Walls
    float angle_delta = player.fov / RAY_COUNT;
    float rect_delta = SCREEN_WIDTH / RAY_COUNT;
    float angle = player.angle - player.fov / 2.0f;

    SDL_SetRenderDrawColor(renderer, 0, 0, 200, 255);
    for (int i = 0; i < RAY_COUNT; i++) {
        angle += angle_delta;
        float rect_x = i * rect_delta;
        RayData ray_data = cast_ray(player.x, player.y, angle);

        float texture_u;
        SDL_Texture *texture = g_textures[ray_data.wall_id];
        if (ray_data.wall_orient == WALL_HORIZONTAL) {
            texture_u = 4*(ray_data.x - (int)ray_data.x);
            SDL_SetTextureColorMod(texture, 100, 100, 100);
        } else {
            texture_u = 4*(ray_data.y - (int)ray_data.y);
            SDL_SetTextureColorMod(texture, 255, 255, 255);
        }
        texture_u = texture_u - (int)texture_u;

        // Take only direct component of a ray as the distance to wall

        float distance = DISTANCE(player.x, player.y, ray_data.x, ray_data.y);
        float depth = SDL_cos((player.angle - angle) * DEG2RAD) * distance;
        float rect_height = SCREEN_HEIGHT * (WALL_HEIGHT * player.radius / depth);
        float tex_height, tex_width;
        SDL_GetTextureSize(texture, &tex_width, &tex_height);
        float tex_delta = SDL_sin(angle_delta * DEG2RAD) * distance;

        SDL_FRect dest_rect = {
            .x = rect_x, 
            .y = SCREEN_HEIGHT / 2.0f - rect_height / 2.0f,
            .w = rect_delta,
            .h = rect_height,
        };
        SDL_FRect src_rect = {
            .x = texture_u * tex_width,
            .y = 0,
            .w = tex_delta,
            .h = tex_height,
        };

        SDL_RenderTexture(renderer, texture, &src_rect, &dest_rect);
    }
}

int main() {
    SDL_Window *window;
    SDL_Renderer *renderer;
    init_sdl(&renderer, &window, SCREEN_WIDTH, SCREEN_HEIGHT);
    load_all_textures(renderer);

    while(!e_state.quit) {
        // update time
        double time = SDL_GetTicks() / 1000.0f;
        if (time - e_state.last_frame < DESIRED_FRAME_TIME) {
            double delay = 1000 * (DESIRED_FRAME_TIME - (time - e_state.last_frame));
            SDL_Delay(delay);
        }
        time = SDL_GetTicks() / 1000.0f;
        e_state.delta_time = time - e_state.last_frame;
        e_state.last_frame = time;
        char buf[32];
        sprintf(buf, "%.0f", 1.0f / e_state.delta_time);
        SDL_SetWindowTitle(window, buf);

        // inputs
        handle_events();
        handle_player_input();

        // render
        if (e_state.map_mode)
            draw_level_map(renderer);
        else
            render_scene(renderer);

        SDL_RenderPresent(renderer);
    }

    return 0;
}
