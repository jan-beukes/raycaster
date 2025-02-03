#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "ext/stb_image.h"
#include <stdlib.h>
#include <SDL3/SDL.h>

#define RESX 620
#define RESY 400

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 800
#define FRAME_RATE 1000
#define DESIRED_FRAME_TIME (1.0 / FRAME_RATE)

#define RAY_COUNT RESX
#define ANIM_FRAME_TIME (1.0f / 12.0f) // 12fps

#define WALL_SCALE 15.0f // scale multiplier for height of projections
#define OBJECT_SCALE 10.0f
#define OBJECT_OFFSET_FACTOR 0.3f
#define ENEMY_SCALE 10.0f

// These goobers make sure that function calls passed to the macro only get called once
#define MIN(X, Y) ({ __typeof__(X) _X = X;\
                    __typeof__(Y) _Y = Y;\
                   (_X) < (_Y) ? (_X) : (_Y);\
                   })
#define MAX(X, Y) ({ __typeof__(X) _X = X;\
                    __typeof__(Y) _Y = Y;\
                   (_X) > (_Y) ? (_X) : (_Y);\
                   })

#define DISTANCE(X1, Y1, X2, Y2) ({ __typeof__(X1) _xdist = X2 - X1; __typeof__(Y1) _ydist = Y2 - Y1;\
                                    SDL_sqrtf(_xdist*_xdist + _ydist*_ydist);})

#define NORM_ANGLE(A) ({__typeof__(A) _A = A; _A < 0 ? _A + 360 : (_A > 360 ? _A - 360 : _A); })

#define PANIC(fmt, ...) ({ fprintf(stderr, fmt, ##__VA_ARGS__); exit(1); })

#define EXIT(code) ({SDL_Quit(); exit(code);)}

#define DEG2RAD (SDL_PI_F / 180.0f)
#define RAD2DEG (180.0f / SDL_PI_F)

//---Structures---
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Color;

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
    float mouse_sens;
    float mouse_x_pos;
    float mouse_y_pos;
    float mouse_xrel;
    float mouse_yrel;
} EngineState;


typedef struct {
    float x;
    float y;
    SDL_Texture *texture;
    Color tint;
} Sprite;

typedef struct {
    SDL_Texture **frames;
    int current_frame;
    int frame_count;
    float frame_time;
    float timer;
} AnimatedSprite;

typedef enum {
    OBJECT_STATIC,
    OBJECT_ANIMATED,
} ObjectSpriteType;

typedef struct {
    float x;
    float y;
    int id; // Which object is this
    ObjectSpriteType sprite_type;
    union {
        AnimatedSprite animated;
        SDL_Texture *static_frame;
    } sprite;
} Object;

typedef enum {
    ENEMY_NORMAL,
    ENEMY_HURT,
} EnemyState;

typedef struct {
    float x;
    float y;
    float radius;
    int health;
    bool dead;
    int damage;

    float timer;
    EnemyState state;
    AnimatedSprite sprite;
} Enemy;

typedef enum {
    WEAPON_IDLE,
    WEAPON_FIRE,
    WEAPON_RELOAD,
} WeaponState;

typedef struct {
    AnimatedSprite sprite;
    int reload_frame_count;
    int shoot_frame_count;
    WeaponState state;

    int base_damage;
    int max_ammo;
    int ammo;
} Weapon;

typedef struct {
    float x;
    float y;
    int health;

    Weapon weapon;

    float speed;
    float radius;
    float angle;
    float fov;
} Player;

typedef struct {
    int *map; // wall layout array
    int width;
    int height;

    // for 2d view
    float x_scale;
    float y_scale;

    Object *objects;
    int object_count;
    int object_type_count;

    Enemy *enemies;
    int enemy_count;
    int enemy_type_count;

} Map;

enum {
    TEXTURE_WALL1 = 1, // 0 for open space
    TEXTURE_WALL2,
    TEXTURE_WALL3,
    TEXTURE_WALL4,
    TEXTURE_WALL_FLAG,

    TEXTURE_SKY,
};

enum {
    WALL_HORIZONTAL,
    WALL_VERTICAL,
};


//---Globals---

EngineState e_state = {
    .quit = false,
    .map_mode = false,
    .last_frame = 0.0,
    .delta_time = 0.0,
    .mouse_sens = 60.0f,
};

Map g_map = {0};

Player player = {
    .x = 2.0f,
    .y = 2.0f,
    .health = 100,

    .speed = 1.5f,
    .radius = 0.1f,
    .angle = 0.0f,
    .fov = 60.0f,
};

#define MAX_TEXTURES 16
SDL_Texture *g_textures[MAX_TEXTURES];


// func declaration
RayData cast_ray(float x_start, float y_start, float angle);

void init_sdl(SDL_Renderer **renderer, SDL_Window **window, int width, int height) {
    if(!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("ERROR: Failed to initialize SDL\n%s", SDL_GetError());
        SDL_Quit();
        exit(1);
    }
    if (!SDL_CreateWindowAndRenderer("Racaster", width, height, 0, window, renderer)) {
        SDL_Log("ERROR: Failed to create window\n%s", SDL_GetError());
        SDL_Quit();
        exit(1);
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

// loads all images in directory into an animated sprite. File names: 0.png 1.png ...;
// allocates frames
AnimatedSprite load_animated_sprite(SDL_Renderer *r, const char *dirname, int count, float frame_time) {

    AnimatedSprite as = {0};
    as.frames = malloc(sizeof(SDL_Texture *) * count);
    as.frame_count = count;
    char buf[256];
    for (int i = 0; i < count; i++) {
        sprintf(buf, "%s/%d.png", dirname, i);
        as.frames[i] = load_texture(r, buf);
    }
    as.frame_time = frame_time;
    as.current_frame = 0;
    as.timer = frame_time;

    return as;
}

//---Map Loading---
void load_map_textures(SDL_Renderer *r) {
    // Walls
    int i;
    for (i = 1; i <= 5; i++) {
        char buf[32];
        sprintf(buf, "res/textures/%d.png", i);
        g_textures[i] = load_texture(r, buf);
    }
    // sky
    g_textures[i++] = load_texture(r, "res/textures/sky.png");
}

// loads all the map objects into the global map struct
void load_map_objects(SDL_Renderer *r) {
    // candlebra
    Object objects[128];
    int count = 0;

    int id = 0;
    SDL_Texture *candlebra = load_texture(r, "res/sprites/static_sprites/candlebra.png");
    id++;
    objects[count++] = (Object) {
        .x = 4.5f,
        .y = 5.5f,
        .id = id,
        .sprite_type = OBJECT_STATIC,
        .sprite.static_frame = candlebra,
    };

    Object obj = {0};
    // green light
    AnimatedSprite green_light = load_animated_sprite(r, "res/sprites/animated_sprites/green_light", 4, ANIM_FRAME_TIME);
    id++;
    obj.x = 4.0f;
    obj.y = 3.0f;
    obj.id = id;
    obj.sprite_type = OBJECT_ANIMATED;
    obj.sprite.animated = green_light;
    objects[count++] = obj;

    // red light
    AnimatedSprite red_light = load_animated_sprite(r, "res/sprites/animated_sprites/red_light", 4, ANIM_FRAME_TIME);
    id++;
    obj.x = 9.5f;
    obj.y = 3.5f;
    obj.id = id;
    obj.sprite_type = OBJECT_ANIMATED;
    obj.sprite.animated = red_light;
    objects[count++] = obj;

    obj.x = 10.5f;
    obj.y = 3.5f;
    objects[count++] = obj;

    obj.x = 9.5f;
    obj.y = 4.5f;
    objects[count++] = obj;

    obj.x = 10.5f;
    obj.y = 4.5f;
    objects[count++] = obj;

    g_map.objects = malloc(count * sizeof(Object));
    memcpy(g_map.objects, objects, count * sizeof(Object));
    g_map.object_count = count;
    g_map.object_type_count = id;
}

void load_map_enemies(SDL_Renderer *r) {

    Enemy enemies[128];
    int count = 0;

    Enemy e = {
        .x = 8,
        .y = 7,
        .radius = 0.5f,
        .health = 100,
        .dead = false,
        .damage = 0,
        .state = ENEMY_NORMAL,
        .sprite = load_animated_sprite(r, "res/sprites/npc/amog", 1, 1),
    };
    enemies[count++] = e;
    e.x++;
    enemies[count++] = e;
    e.x++;
    enemies[count++] = e;
    e.y = 1.5f;
    enemies[count++] = e;
    e.x--;
    enemies[count++] = e;
    e.x--;
    enemies[count++] = e;

    // hidden
    e.y = 10;
    e.x = 3;
    enemies[count++] = e;
    e.x++;
    enemies[count++] = e;

    // chunker
    e.x = 10;
    e.y = 4;
    e.health = 600;
    e.sprite = load_animated_sprite(r, "res/sprites/npc/vsauce", 1, 1);
    e.radius = 0.7f;
    enemies[count++] = e;

    g_map.enemies = malloc(count * sizeof(Enemy));
    memcpy(g_map.enemies, enemies, count * sizeof(Enemy));
    g_map.enemy_count = count;
}

void create_map(SDL_Renderer *r) {
    // load all map assets
    load_map_textures(r);
    load_map_objects(r);
    load_map_enemies(r);

    // map layout
    g_map.width = 14;
    g_map.height = 15;
    int map_layout[14*15] = {
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 
        2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
        2, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 0, 2,
        2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 2,
        2, 0, 0, 5, 5, 5, 0, 0, 0, 0, 0, 4, 0, 2,
        2, 0, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4, 0, 2,
        2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
        2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
        2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 2, 2, 2, 2,
        2, 0, 0, 0, 0, 0, 0, 2, 0, 0, 2, 5, 5, 2,
        2, 0, 0, 0, 0, 0, 0, 2, 0, 0, 2, 0, 0, 2,
        2, 0, 0, 3, 3, 0, 0, 2, 0, 0, 2, 0, 0, 2,
        2, 0, 0, 3, 3, 0, 0, 2, 0, 0, 2, 0, 0, 2,
        2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    };
    size_t num_bytes = g_map.width*g_map.height * sizeof(int);
    g_map.map = malloc(num_bytes);
    memcpy(g_map.map, map_layout, num_bytes);

    g_map.x_scale = (float)RESX / g_map.width;
    g_map.y_scale = (float)RESY / g_map.height;

    // load player weapon
    // Format of dir: (Idle)0.png, (Shoot)..., (Reload)...
    const float anim_frame_time = 4 * ANIM_FRAME_TIME;
    player.weapon = (Weapon){
        .sprite = load_animated_sprite(r, "res/sprites/weapon/shotgun", 8, anim_frame_time),
        .state = WEAPON_IDLE,
        .shoot_frame_count = 3,
        .reload_frame_count = 4,
        .max_ammo = 6,
        .ammo = 6,
        .base_damage = 30,
    };
}

// free all map stuff
void destroy_map() {
    // Objects

    // We need to loop through the different object types and free the sprites 
    // of the first one we find of that type. This makes sure each sprite is only freed once.
    // A different options would be to store all sprites together in a seperate array when a new one is loaded
    // and then just loop through that array.
    for (int t = 0; t < g_map.object_type_count; t++) {
        for (int i = 0; i < g_map.object_count; i++) {
            Object obj = g_map.objects[i];
            if (obj.id != t) continue;

            if (obj.sprite_type == OBJECT_STATIC) {
                SDL_DestroyTexture(obj.sprite.static_frame);
            } else {
                for (int j = 0; j < obj.sprite.animated.frame_count; j++) {
                    SDL_DestroyTexture(obj.sprite.animated.frames[j]);
                }
                free(obj.sprite.animated.frames);
            }
            break;
        }
    }
    // unload enemies

    // weapon
    for (int i = 0; i < player.weapon.sprite.frame_count; i++) {
        SDL_DestroyTexture(player.weapon.sprite.frames[i]);
    }
    free(player.weapon.sprite.frames);

    // free map
    free(g_map.map);
    free(g_map.objects);
}

bool check_collision_circle_line(float cx, float cy, float radius, float p1x, float p1y, float p2x, float p2y) {
    float dx = p1x - p2x;
    float dy = p1y - p2y;

    float length = ((dx*dx) + (dy*dy));
    float dot_product = (((cx - p1x)*(p2x - p1x)) + ((cy - p1y)*(p2y - p1y)))/(length);

    if (dot_product > 1.0f) dot_product = 1.0f;
    else if (dot_product < 0.0f) dot_product = 0.0f;

    float dx2 = (p1x - dot_product*dx) - cx;
    float dy2 = (p1y - dot_product*dy) - cy;
    float distance = dx2*dx2 + dy2*dy2;

    return (distance <= radius*radius);
}

#define SHOTGUN_RAYS 12
#define SHOTGUN_SPREAD 6.0f

// only shotgun rn
void fire_weapon() {
    if (player.weapon.state != WEAPON_IDLE) return;
    if (player.weapon.ammo <= 0) {
        player.weapon.state = WEAPON_RELOAD;
        return;
    }

    // shoot
    float angle_step = SHOTGUN_SPREAD / SHOTGUN_RAYS;
    float start_angle = player.angle - SHOTGUN_SPREAD / 2.0f;
    for (int i = 0; i < g_map.enemy_count; i++) {
        Enemy enemy = g_map.enemies[i];
        for (int r = 0; r < SHOTGUN_RAYS; r++) {
            float angle = start_angle + r * angle_step;
            RayData ray_data = cast_ray(player.x, player.y, angle);
            if (check_collision_circle_line(
                enemy.x, enemy.y, enemy.radius,
                player.x, player.y, ray_data.x, ray_data.y)
            ) {
                g_map.enemies[i].state = ENEMY_HURT;
                g_map.enemies[i].timer = 0.6f;
                g_map.enemies[i].health -= player.weapon.base_damage;
                break;
            }
        }

    }
    player.weapon.ammo--;

    player.weapon.state = WEAPON_FIRE;
}

void handle_events() {
    e_state.mouse_xrel = 0.0f;
    e_state.mouse_yrel = 0.0f;

    SDL_Event e;
    while(SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT ||
            (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) e_state.quit = true;

        // Keyboards event
        if (e.type == SDL_EVENT_KEY_DOWN) {
            switch (e.key.scancode) {
                case SDL_SCANCODE_R:
                    if (player.weapon.ammo < player.weapon.max_ammo)
                        player.weapon.state = WEAPON_RELOAD;
                break;
                case SDL_SCANCODE_M:
                    e_state.map_mode = !e_state.map_mode;
                break;
                case SDL_SCANCODE_LCTRL:
                    fire_weapon();
                break;
                default:
            }
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (e.button.button == 1) fire_weapon();
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
    if (g_map.map[cell_y*g_map.width + (int)player.x] != 0)
        player.y = (cell_y + 1) + player.radius; // need to add one since cell coords are top left

    cell_y = (int)(player.y + player.radius);
    if (g_map.map[cell_y*g_map.width + (int)player.x] != 0)
        player.y = cell_y - player.radius;

    cell_x = (int)(player.x + player.radius);
    if (g_map.map[(int)player.y*g_map.width + cell_x] != 0)
        player.x = cell_x - player.radius;

    cell_x = (int)(player.x - player.radius);
    if (g_map.map[(int)player.y*g_map.width + cell_x] != 0)
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
    float length = SDL_sqrt(dx*dx + dy*dy);
    if (length > 1.0f) {
        dx /= length;
        dy /= length;
    }
    player.y += dy * player.speed*sprint * e_state.delta_time;
    player.x += dx * player.speed*sprint * e_state.delta_time;
    player.x = MIN(g_map.width, MAX(player.x, 0));
    player.y = MIN(g_map.height, MAX(player.y, 0));

    //--Collision---
    player_collide();

    //---Mouse Input---
    // look
    if (keys[SDL_SCANCODE_LEFT]) {
        player.angle -= 2 * e_state.mouse_sens * e_state.delta_time;
    } else if (keys[SDL_SCANCODE_RIGHT]) {
        player.angle += 2 * e_state.mouse_sens * e_state.delta_time;
    } else {
        player.angle += e_state.mouse_sens * e_state.mouse_xrel * e_state.delta_time;
    }
    player.angle = NORM_ANGLE(player.angle);

}

void update_animations() {
    // Objects 
    for (int i = 0; i < g_map.object_count; i++) {
        Object *obj = &g_map.objects[i];
        if (obj->sprite_type == OBJECT_STATIC) continue;
        obj->sprite.animated.timer -= e_state.delta_time;
        if (obj->sprite.animated.timer <= 0) {
            obj->sprite.animated.current_frame++;
            obj->sprite.animated.current_frame %= obj->sprite.animated.frame_count;
            obj->sprite.animated.timer = obj->sprite.animated.frame_time;
        }
    }

    // Enemies

    // Gun
    switch (player.weapon.state) {
        case WEAPON_FIRE:
            if (player.weapon.sprite.current_frame == 0)
                player.weapon.sprite.current_frame = 1;
            player.weapon.sprite.timer -= e_state.delta_time;
            if (player.weapon.sprite.timer <= 0) {
                player.weapon.sprite.current_frame++;
                player.weapon.sprite.timer = player.weapon.sprite.frame_time;
                // past fire animations
                if (player.weapon.sprite.current_frame > player.weapon.shoot_frame_count) {
                    player.weapon.sprite.current_frame = 0;
                    player.weapon.sprite.timer = player.weapon.sprite.frame_time;
                    player.weapon.state = WEAPON_IDLE;
                }
            }
        break;
        case WEAPON_RELOAD:
            if (player.weapon.sprite.current_frame == 0)
                player.weapon.sprite.current_frame = player.weapon.shoot_frame_count + 1;
            player.weapon.sprite.timer -= e_state.delta_time;
            if (player.weapon.sprite.timer <= 0) {
                player.weapon.sprite.current_frame++;
                player.weapon.sprite.timer = player.weapon.sprite.frame_time;
                // past fire animations
                if (player.weapon.sprite.current_frame == player.weapon.sprite.frame_count) {
                    player.weapon.ammo = player.weapon.max_ammo;
                    player.weapon.sprite.current_frame = 0;
                    player.weapon.sprite.timer = player.weapon.sprite.frame_time;
                    player.weapon.state = WEAPON_IDLE;
                }
            }
        break;
        case WEAPON_IDLE:
        break;
    }
}

void update_enemies() {
    for (int i = 0; i < g_map.enemy_count; i++) {
        Enemy *e = &g_map.enemies[i];
        if (e->state == ENEMY_HURT) {
            e->timer -= e_state.delta_time;
            if (e->timer <= 0) e->state = ENEMY_NORMAL;
        }
        if (e->health <=0 )
            e->dead = true;
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
// Cast a ray from x_start, y_start facing angle
RayData cast_ray(float x_start, float y_start, float angle) {
    assert(x_start > 0 && x_start < g_map.width && y_start > 0 && y_start < g_map.height);

    // don't cast too far
    const float max_length = SDL_sqrtf(g_map.width*g_map.width + g_map.height*g_map.height);
    const float x_step = RAY_STEP * SDL_cos(angle * DEG2RAD);
    const float y_step = RAY_STEP * SDL_sin(angle * DEG2RAD);
    const int max_steps = max_length / RAY_STEP;
    for (int i = 0; i < max_steps; i++) {
        float curr_x = player.x + i * x_step;
        float curr_y = player.y + i * y_step;

        // in a wall
        int wall_id = g_map.map[(int)curr_y*g_map.width + (int)curr_x];
        if (wall_id != 0) {
            float eps = 1.1f;
            bool horizontal = g_map.map[(int)(curr_y - y_step*eps)*g_map.width + (int)curr_x] == 0;
            bool vertical = g_map.map[(int)curr_y*g_map.width + (int)(curr_x - x_step*eps)] == 0;
            if (horizontal) {
                float y_end = y_step > 0 ? (int)curr_y : (int)curr_y + 1;
                float x_end = curr_x;
                return (RayData) {
                    .x = x_end,
                    .y = y_end,
                    .wall_id = wall_id,
                    .wall_orient = WALL_HORIZONTAL,
                };
            } else if (vertical) {
                float x_end = x_step > 0 ? (int)curr_x : (int)curr_x + 1;
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
    for (int row = 0; row < g_map.height; row++) {
        for (int col = 0; col < g_map.width; col++) {
            SDL_FRect rect = {
                .x = col * g_map.x_scale,
                .y = row * g_map.y_scale,
                .w = g_map.x_scale,
                .h = g_map.y_scale,
            };
            if (g_map.map[row * g_map.width + col] != 0) {
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
    render_fill_circle(renderer, g_map.x_scale * player.x, g_map.y_scale * player.y, g_map.x_scale * player.radius);

    // Rays
    float ray_step = player.fov / RAY_COUNT;
    float angle_start = player.angle - player.fov / 2.0f;
    float angle_end = player.angle + player.fov / 2.0f;
    for (float angle = angle_start; angle <= angle_end; angle += ray_step) {
        RayData ray_data = cast_ray(player.x, player.y, angle);
        if (ray_data.wall_orient == WALL_VERTICAL) SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
        else SDL_SetRenderDrawColor(renderer, 255, 127, 80, 255);
        SDL_RenderLine(renderer, g_map.x_scale * player.x, g_map.y_scale * player.y,
                       g_map.x_scale * ray_data.x, g_map.y_scale * ray_data.y);
    }

    // sprite
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    for (int i = 0; i < g_map.object_count; i++)
        render_fill_circle(renderer, g_map.x_scale * g_map.objects[i].x, g_map.y_scale * g_map.objects[i].y, g_map.x_scale * 0.05f);

}

int sprite_compare(const void *lhs, const void *rhs) {
    Sprite *s_left = (Sprite *)lhs;
    Sprite *s_right = (Sprite *)rhs;

    float dist_left = DISTANCE(player.x, player.y, s_left->x, s_left->y);
    float dist_right = DISTANCE(player.x, player.y, s_right->x, s_right->y);

    if (dist_left < dist_right) return 1;
    else if (dist_left > dist_right) return -1;
    else return 0;
}

void draw_sprite(SDL_Renderer *r, Sprite s, float *z_buffer, float ray_delta) {
    float dir_x = s.x - player.x, dir_y = s.y - player.y;
    float distance = DISTANCE(player.x, player.y, s.x, s.y);

    // This is some absolute diabolical stuff
    float sprite_angle = NORM_ANGLE(SDL_atan2(dir_y, dir_x) * RAD2DEG);
    float theta =  player.angle - sprite_angle;
    if (360 - theta < theta) theta = theta - 360;
    else if (SDL_abs(theta + 360) < SDL_abs(theta)) theta += 360;

    if (SDL_abs(theta) > player.fov)
        return;

    float depth = distance;
    float w, h;
    SDL_GetTextureSize(s.texture, &w, &h);
    float sprite_height = RESY * (OBJECT_SCALE * player.radius / depth);
    float sprite_width = sprite_height * (w / h);

    int ray_count = sprite_width / ray_delta;
    int start_ray = (-theta + player.fov/2.0f) / player.fov * RAY_COUNT;

    // sprite strips
    start_ray -= 0.5f * sprite_width/ray_delta; // start from left
    for (int i = start_ray; i < start_ray + ray_count; i++) {
        float x = i * ray_delta;
        if (x < 0 || x >= RESX) continue;
        if (z_buffer[i] < depth) continue;

        SDL_FRect src_rect = {
            .x = w * (i - start_ray) / ray_count,
            .y = 0,
            .w = ray_delta,
            .h = h,
        };
        SDL_FRect dest_rect = {
            .x = x, 
            .y = RESY / 2.0f - sprite_height * (0.5 - OBJECT_OFFSET_FACTOR), // move sprites down a little
            .w = ray_delta,
            .h = sprite_height,
        };
        SDL_RenderTexture(r, s.texture, &src_rect, &dest_rect);
    }

}

#define WEAPON_WIDTH (RESX / 4.0f)
void render_interface(SDL_Renderer *renderer) {
    // Shotgun
    SDL_Texture *weapon_texture = player.weapon.sprite.frames[player.weapon.sprite.current_frame];
    float w, h;
    SDL_GetTextureSize(weapon_texture, &w, &h);
    float weapon_height = h * (WEAPON_WIDTH / w);
    SDL_FRect weapon_rect = {
        .x = RESX / 2.0f - WEAPON_WIDTH / 2.0f,
        .y = RESY - weapon_height,
        .w = WEAPON_WIDTH,
        .h = weapon_height,
    };
    SDL_RenderTexture(renderer, weapon_texture, NULL, &weapon_rect);

}

// Render the game using raycasting
void render_scene(SDL_Renderer *renderer) {
    //---Environment---

    // Clear
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
    SDL_RenderClear(renderer);
    // Sky
    const float sky_width = 1200;
    float sky_fov = player.fov * 2.0f;
    float sky_angle = -SDL_fmodf(player.angle, sky_fov);
    float sky_offset = sky_angle < 0 ? sky_width : -sky_width; // sky2 offset 
    float sky1_x = sky_angle * sky_width / sky_fov;
    float sky2_x = sky_angle * sky_width / sky_fov + sky_offset;
    SDL_RenderTexture(renderer, g_textures[TEXTURE_SKY], NULL, &(SDL_FRect){sky1_x, 0, sky_width, RESY/2.0f});
    SDL_RenderTexture(renderer, g_textures[TEXTURE_SKY], NULL, &(SDL_FRect){sky2_x, 0, sky_width, RESY/2.0f});

    // Raycast Walls
    const float ray_delta = (float)RESX / RAY_COUNT;
    float angle_delta = player.fov / RAY_COUNT;
    float angle = player.angle - player.fov / 2.0f;
    float z_buffer[RAY_COUNT];

    SDL_SetRenderDrawColor(renderer, 0, 0, 200, 255);
    for (int i = 0; i < RAY_COUNT; i++) {
        angle += angle_delta;
        float rect_x = i * ray_delta;
        RayData ray_data = cast_ray(player.x, player.y, angle);

        float texture_u;
        SDL_Texture *texture = g_textures[ray_data.wall_id];
        if (ray_data.wall_orient == WALL_HORIZONTAL) {
            texture_u = (ray_data.x - (int)ray_data.x);
            SDL_SetTextureColorMod(texture, 255, 255, 255);
        } else {
            texture_u = (ray_data.y - (int)ray_data.y);
            SDL_SetTextureColorMod(texture, 100, 100, 100);
        }
        texture_u = texture_u - (int)texture_u;

        // Take only direct component of a ray as the distance to wall
        float distance = DISTANCE(player.x, player.y, ray_data.x, ray_data.y);
        float depth = SDL_cos((player.angle - angle) * DEG2RAD) * distance;
        z_buffer[i] = distance;
        float rect_height = RESY * (WALL_SCALE * player.radius / depth);
        float tex_height, tex_width;
        SDL_GetTextureSize(texture, &tex_width, &tex_height);

        SDL_FRect dest_rect = {
            .x = rect_x, 
            .y = RESY / 2.0f - rect_height / 2.0f,
            .w = ray_delta,
            .h = rect_height,
        };
        SDL_FRect src_rect = {
            .x = texture_u * tex_width,
            .y = 0,
            .w = ray_delta,
            .h = tex_height,
        };

        SDL_RenderTexture(renderer, texture, &src_rect, &dest_rect);
    }

    //---Sprites---
    Sprite sprites[g_map.object_count*g_map.enemy_count];
    int count = 0;

    // Objects
    for (int i = 0; i < g_map.object_count; i++) {
        Object obj = g_map.objects[i];
        SDL_Texture *tex;
        if (obj.sprite_type == OBJECT_STATIC)
            tex = obj.sprite.static_frame;
        else
            tex = obj.sprite.animated.frames[obj.sprite.animated.current_frame];

        sprites[count++] = (Sprite){obj.x, obj.y, tex, (Color){0xFF, 0xFF, 0xFF}};
    }
    // Enemies
    for (int i = 0; i < g_map.enemy_count; i++) {
        Enemy e = g_map.enemies[i];
        if (e.dead) continue;
        SDL_Texture *tex = e.sprite.frames[e.sprite.current_frame];

        Color tint = {0xFF, 0xFF, 0xFF};
        if (e.state == ENEMY_HURT) tint = (Color){0xFA, 0x81, 0x81};

        sprites[count++] = (Sprite){e.x, e.y, tex, tint};
    }
    qsort(sprites, count, sizeof(Sprite), sprite_compare);
    for (int i = 0; i < count; i++) {
        Sprite s = sprites[i];
        SDL_SetTextureColorMod(s.texture, s.tint.r, s.tint.g, s.tint.b);
        draw_sprite(renderer, sprites[i], z_buffer, ray_delta);
    }

}

int main() {
    SDL_Window *window;
    SDL_Renderer *renderer;
    init_sdl(&renderer, &window, SCREEN_WIDTH, SCREEN_HEIGHT);
    SDL_Texture *fbo = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET, RESX, RESY);

    create_map(renderer);

    while(!e_state.quit) {
        // update time
        double time = SDL_GetTicksNS() * 1e-9;
        if (time - e_state.last_frame < DESIRED_FRAME_TIME) {
            double delay = 1000 * (DESIRED_FRAME_TIME - (time - e_state.last_frame));
            SDL_Delay(delay);
        }
        time = SDL_GetTicksNS() * 1e-9;
        e_state.delta_time = time - e_state.last_frame;
        e_state.last_frame = time;

        // inputs and player update
        handle_events();
        handle_player_input();

        // game updates
        update_animations();
        update_enemies();

        // render
        SDL_SetRenderTarget(renderer, fbo);
        if (!e_state.map_mode) {
            render_scene(renderer);
            render_interface(renderer);
        } else {
            draw_level_map(renderer);
        }

        SDL_SetRenderTarget(renderer, NULL);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, fbo, NULL, NULL);

        SDL_RenderPresent(renderer);
    }

    // Cleanup
    destroy_map();

    SDL_DestroyWindow(window);
    SDL_DestroyTexture(fbo);
    SDL_DestroyRenderer(renderer);
    return 0;
}
