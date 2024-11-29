#include <iostream>
#include <pthread.h>
#include <vector>
#include <ncurses.h>
#include <unistd.h>
#include <ctime>
#include <algorithm>

// Scenario dimensions
const int WIDTH = 50;
const int HEIGHT = 20;

// Difficulty parameters
int m = 3;  // Hits required to kill a dinosaur
int n = 5;  // Helicopter missile capacity
int t = 10; // Time interval between dinosaur spawns (in seconds)

// Forward declarations
class Truck;
class Depot;
class Helicopter;
class Missile;
class Dinosaur;

// Global variables
Helicopter *heli_ptr; // Pointer to the helicopter object
std::vector<Missile *> missiles;
std::vector<Dinosaur *> dinosaurs;
std::vector<Truck *> active_trucks;
pthread_mutex_t mtx_missiles = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_dinosaurs = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_trucks = PTHREAD_MUTEX_INITIALIZER;
bool running = true;
pthread_mutex_t mtx_running = PTHREAD_MUTEX_INITIALIZER;

// Function to safely set the running flag
void set_running(bool value)
{
    pthread_mutex_lock(&mtx_running);
    running = value;
    pthread_mutex_unlock(&mtx_running);
}

// Function to safely check if the game is running
bool is_running()
{
    pthread_mutex_lock(&mtx_running);
    bool result = running;
    pthread_mutex_unlock(&mtx_running);
    return result;
}

// Depot position
const int DEPOT_X = WIDTH / 2;
const int DEPOT_Y = HEIGHT - 2; // Bottom center of the screen

// Class to represent a missile
class Missile
{
public:
    double x;
    double y;
    int direction; // -1 for left, 1 for right
    bool active;
    pthread_t th;

    Missile(double startX, double startY, int dir)
        : x(startX), y(startY), direction(dir), active(true), th(0) {}

    static void *move_wrapper(void *arg)
    {
        Missile *m = static_cast<Missile *>(arg);
        m->move();
        return nullptr;
    }

    void start()
    {
        pthread_create(&th, nullptr, Missile::move_wrapper, this);
    }

    void move()
    {
        double speed = 0.5;
        while (active && x > 1 && x < WIDTH - 2)
        {
            double prev_x = x;
            x += direction * speed;
            check_collision(prev_x, x);
            usleep(25000);
        }
        active = false;
    }

    void draw()
    {
        if (active)
        {
            char missile_char = (direction == 1) ? '>' : '<';
            mvprintw(static_cast<int>(y), static_cast<int>(x), "%c", missile_char);
        }
    }

    void join()
    {
        if (th)
        {
            pthread_join(th, nullptr);
            th = 0;
        }
    }

    void check_collision(double prev_x, double curr_x);
};

// Class to represent a dinosaur
class Dinosaur
{
public:
    double x;
    double y;
    int health;
    bool active;
    pthread_t th;
    pthread_mutex_t mtx;
    int direction; // 1 for right, -1 for left

    // Jumping variables
    bool is_jumping;
    double vertical_velocity;

    Dinosaur(double startX, double startY, int initial_health, int initial_direction = -1)
        : x(startX), y(startY), health(initial_health), active(true), th(0),
          direction(initial_direction), is_jumping(false), vertical_velocity(0)
    {
        pthread_mutex_init(&mtx, nullptr);
    }

    ~Dinosaur()
    {
        pthread_mutex_destroy(&mtx);
    }

    static void *move_wrapper(void *arg)
    {
        Dinosaur *d = static_cast<Dinosaur *>(arg);
        d->move();
        return nullptr;
    }

    void start()
    {
        pthread_create(&th, nullptr, Dinosaur::move_wrapper, this);
    }

    void move()
    {
        double speed = 0.25;
        double gravity = 0.05;
        double jump_strength = -0.5;

        while (active)
        {
            x += direction * speed;

            // Change direction at boundaries
            if (x <= 1)
            {
                x = 1;
                direction = 1;
            }
            else if (x >= WIDTH - 2)
            {
                x = WIDTH - 2;
                direction = -1;
            }

            // Handle vertical movement
            if (is_jumping)
            {
                vertical_velocity += gravity;
                y += vertical_velocity;

                if (y >= HEIGHT - 2)
                {
                    y = HEIGHT - 2;
                    is_jumping = false;
                    vertical_velocity = 0;
                }
            }
            else
            {
                y = HEIGHT - 2;

                // Random chance to start a jump
                if (rand() % 100 < 5)
                {
                    is_jumping = true;
                    vertical_velocity = jump_strength;
                }
            }

            check_collision();
            usleep(50000);
        }
    }

    void draw()
    {
        if (active)
        {
            int draw_x = static_cast<int>(x);
            int draw_y = static_cast<int>(y);

            mvprintw(draw_y, draw_x, "D"); // Dinosaur body
            int head_x = draw_x + direction;
            mvprintw(draw_y - 1, head_x, "O"); // Dinosaur head
        }
    }

    void join()
    {
        if (th)
        {
            pthread_join(th, nullptr);
            th = 0;
        }
    }

    void take_damage()
    {
        pthread_mutex_lock(&mtx);
        health--;
        if (health <= 0)
        {
            active = false;
        }
        pthread_mutex_unlock(&mtx);
    }

    void check_collision();
};

// Class to represent the depot
class Depot
{
public:
    int capacity; // Total capacity (n slots)
    int missiles; // Current number of missiles
    bool is_truck_unloading;
    bool is_helicopter_reloading;
    pthread_mutex_t mtx;
    pthread_cond_t cv_truck;
    pthread_cond_t cv_helicopter;

    Depot(int capacity)
        : capacity(capacity), missiles(capacity),
          is_truck_unloading(false), is_helicopter_reloading(false)
    {
        pthread_mutex_init(&mtx, nullptr);
        pthread_cond_init(&cv_truck, nullptr);
        pthread_cond_init(&cv_helicopter, nullptr);
    }

    ~Depot()
    {
        pthread_mutex_destroy(&mtx);
        pthread_cond_destroy(&cv_truck);
        pthread_cond_destroy(&cv_helicopter);
    }

    void truck_unload(int amount);
    void helicopter_reload(int amount);
};

// Class to represent the helicopter
class Helicopter
{
public:
    double x;
    double y;
    int remaining_missiles;
    pthread_mutex_t mtx_remaining_missiles;
    pthread_mutex_t mtx;
    int last_horizontal_direction; // -1 for left, 1 for right

    Helicopter(int startX, int startY, int capacity)
        : x(startX), y(startY), remaining_missiles(capacity),
          last_horizontal_direction(1)
    {
        pthread_mutex_init(&mtx_remaining_missiles, nullptr);
        pthread_mutex_init(&mtx, nullptr);
    }

    ~Helicopter()
    {
        pthread_mutex_destroy(&mtx_remaining_missiles);
        pthread_mutex_destroy(&mtx);
    }

    int get_remaining_missiles()
    {
        pthread_mutex_lock(&mtx_remaining_missiles);
        int value = remaining_missiles;
        pthread_mutex_unlock(&mtx_remaining_missiles);
        return value;
    }

    void move(double dx, double dy)
    {
        pthread_mutex_lock(&mtx);
        x += dx;
        y += dy;
        pthread_mutex_unlock(&mtx);
    }

    double get_x()
    {
        pthread_mutex_lock(&mtx);
        double value = x;
        pthread_mutex_unlock(&mtx);
        return value;
    }

    double get_y()
    {
        pthread_mutex_lock(&mtx);
        double value = y;
        pthread_mutex_unlock(&mtx);
        return value;
    }

    void set_x(double new_x)
    {
        pthread_mutex_lock(&mtx);
        x = new_x;
        pthread_mutex_unlock(&mtx);
    }

    void set_y(double new_y)
    {
        pthread_mutex_lock(&mtx);
        y = new_y;
        pthread_mutex_unlock(&mtx);
    }

    bool can_fire()
    {
        pthread_mutex_lock(&mtx_remaining_missiles);
        bool result = remaining_missiles > 0;
        pthread_mutex_unlock(&mtx_remaining_missiles);
        return result;
    }

    void fire()
    {
        pthread_mutex_lock(&mtx_remaining_missiles);
        if (remaining_missiles > 0)
        {
            remaining_missiles--;
        }
        pthread_mutex_unlock(&mtx_remaining_missiles);
    }

    void reload(int amount)
    {
        pthread_mutex_lock(&mtx_remaining_missiles);
        remaining_missiles += amount;
        pthread_mutex_unlock(&mtx_remaining_missiles);
    }

    void set_last_horizontal_direction(int dir)
    {
        pthread_mutex_lock(&mtx);
        last_horizontal_direction = dir;
        pthread_mutex_unlock(&mtx);
    }

    int get_last_horizontal_direction()
    {
        pthread_mutex_lock(&mtx);
        int value = last_horizontal_direction;
        pthread_mutex_unlock(&mtx);
        return value;
    }

    void reload_from_depot();
};

// Global instances
Helicopter heli(WIDTH / 2, HEIGHT / 2, n);
Depot depot(n);

// Class to represent the truck
class Truck
{
public:
    double x;
    double y;
    double target_x;
    double speed;
    bool active;
    pthread_t th;

    Truck(double startX, double startY, double targetX, double spd)
        : x(startX), y(startY), target_x(targetX),
          speed(spd), active(true), th(0) {}

    static void *move_wrapper(void *arg)
    {
        Truck *truck = static_cast<Truck *>(arg);
        truck->move();
        return nullptr;
    }

    void start()
    {
        pthread_create(&th, nullptr, Truck::move_wrapper, this);
    }

    void move()
    {
        // Move towards the depot
        while (active && x < target_x)
        {
            x += speed;
            usleep(500000);
        }

        // Unload missiles
        if (active)
        {
            depot.truck_unload(n);
            usleep(2000000);
        }

        // Exit the screen
        double exit_x = WIDTH;
        while (active && x < exit_x)
        {
            x += speed;
            usleep(500000);
        }

        active = false;
    }

    void join()
    {
        if (th)
        {
            pthread_join(th, nullptr);
            th = 0;
        }
    }

    void draw()
    {
        if (active)
        {
            mvprintw(static_cast<int>(y), static_cast<int>(x), "T");
        }
    }
};

// Function declarations
void *thread_input(void *arg);
void *thread_render(void *arg);
void *thread_dinosaur_manager(void *arg);
void *thread_truck(void *arg);

// Methods relying on 'depot'
void Helicopter::reload_from_depot()
{
    depot.helicopter_reload(n - heli.get_remaining_missiles());
}

// Implement Depot methods
void Depot::truck_unload(int amount)
{
    pthread_mutex_lock(&mtx);
    while (!(missiles < capacity && !is_helicopter_reloading))
    {
        pthread_cond_wait(&cv_truck, &mtx);
    }

    is_truck_unloading = true;
    int unload_amount = std::min(amount, capacity - missiles);
    missiles += unload_amount;
    is_truck_unloading = false;

    pthread_cond_broadcast(&cv_helicopter);
    pthread_mutex_unlock(&mtx);
}

void Depot::helicopter_reload(int amount)
{
    pthread_mutex_lock(&mtx);
    while (!(missiles > 0 && !is_truck_unloading))
    {
        pthread_cond_wait(&cv_helicopter, &mtx);
    }

    is_helicopter_reloading = true;
    int reload_amount = std::min(amount, missiles);
    missiles -= reload_amount;
    heli.reload(reload_amount);
    is_helicopter_reloading = false;

    pthread_cond_broadcast(&cv_truck);
    pthread_mutex_unlock(&mtx);
}

// Helper function to check if a position is occupied by an active dinosaur or the depot
bool is_position_occupied(double x, double y)
{
    pthread_mutex_lock(&mtx_dinosaurs);
    for (const auto &d : dinosaurs)
    {
        if (d->active)
        {
            // Dinosaur body
            if (static_cast<int>(d->x) == static_cast<int>(x) &&
                static_cast<int>(d->y) == static_cast<int>(y))
            {
                pthread_mutex_unlock(&mtx_dinosaurs);
                return true;
            }
            // Dinosaur head
            int head_x = static_cast<int>(d->x + d->direction);
            if (head_x == static_cast<int>(x) &&
                static_cast<int>(d->y - 1) == static_cast<int>(y))
            {
                pthread_mutex_unlock(&mtx_dinosaurs);
                return true;
            }
        }
    }
    pthread_mutex_unlock(&mtx_dinosaurs);

    // Depot position
    if (static_cast<int>(x) == DEPOT_X && static_cast<int>(y) == DEPOT_Y)
        return true;

    return false;
}

const int TRUCK_INTERVAL = 1000000; // 1 second in microseconds

// Function to manage the truck
void *thread_truck(void *arg)
{
    while (is_running())
    {
        usleep(TRUCK_INTERVAL);

        {
            pthread_mutex_lock(&mtx_trucks);
            if (!active_trucks.empty())
            {
                pthread_mutex_unlock(&mtx_trucks);
                continue;
            }
            pthread_mutex_unlock(&mtx_trucks);
        }

        Truck *truck = new Truck(1, DEPOT_Y, DEPOT_X - 1, 1);
        truck->start();

        {
            pthread_mutex_lock(&mtx_trucks);
            active_trucks.push_back(truck);
            pthread_mutex_unlock(&mtx_trucks);
        }

        while (is_running() && truck->active)
        {
            usleep(500000);
        }
    }
    return nullptr;
}

bool is_near_depot(double heli_x, double heli_y)
{
    int dx = std::abs(static_cast<int>(heli_x) - DEPOT_X);
    int dy = std::abs(static_cast<int>(heli_y) - DEPOT_Y);
    return (dx <= 1 && dy <= 1);
}

// Function to manage player input
void *thread_input(void *arg)
{
    int ch;
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    while (is_running())
    {
        ch = getch();
        switch (ch)
        {
        case KEY_UP:
        case 'w':
        {
            double new_y = heli.get_y() - 1;
            if (new_y > 1 && !is_position_occupied(heli.get_x(), new_y))
                heli.set_y(new_y);
            break;
        }
        case KEY_DOWN:
        case 's':
        {
            double new_y = heli.get_y() + 1;
            if (new_y < HEIGHT - 2 && !is_position_occupied(heli.get_x(), new_y))
                heli.set_y(new_y);
            break;
        }
        case KEY_LEFT:
        case 'a':
        {
            double new_x = heli.get_x() - 1;
            if (new_x > 1 && !is_position_occupied(new_x, heli.get_y()))
                heli.set_x(new_x);
            heli.set_last_horizontal_direction(-1);
            break;
        }
        case KEY_RIGHT:
        case 'd':
        {
            double new_x = heli.get_x() + 1;
            if (new_x < WIDTH - 2 && !is_position_occupied(new_x, heli.get_y()))
                heli.set_x(new_x);
            heli.set_last_horizontal_direction(1);
            break;
        }
        case ' ':
            if (heli.can_fire())
            {
                heli.fire();
                int missile_direction = heli.get_last_horizontal_direction();
                double missile_start_x = heli.get_x() + missile_direction;
                Missile *m = new Missile(missile_start_x, heli.get_y(), missile_direction);
                {
                    pthread_mutex_lock(&mtx_missiles);
                    missiles.push_back(m);
                    pthread_mutex_unlock(&mtx_missiles);
                }
                m->start();
            }
            break;
        case 'q':
            set_running(false);
            break;
        default:
            break;
        }

        // Reload if near depot
        if (is_near_depot(heli.get_x(), heli.get_y()) && heli.get_remaining_missiles() < n)
        {
            heli.reload_from_depot();
        }

        usleep(10000);
    }
    return nullptr;
}

// Function to render the scenario
void *thread_render(void *arg)
{
    while (is_running())
    {
        clear();
        // Draw borders
        for (int i = 0; i < WIDTH; i++)
        {
            mvprintw(0, i, "#");
            mvprintw(HEIGHT - 1, i, "#");
        }
        for (int i = 0; i < HEIGHT; i++)
        {
            mvprintw(i, 0, "#");
            mvprintw(i, WIDTH - 1, "#");
        }

        // Reload indicator
        if (is_near_depot(heli.get_x(), heli.get_y()))
        {
            mvprintw(DEPOT_Y - 1, DEPOT_X, "R");
        }
        else
        {
            mvprintw(DEPOT_Y - 1, DEPOT_X, " ");
        }

        // Draw helicopter
        mvprintw(static_cast<int>(heli.get_y()), static_cast<int>(heli.get_x()), "H");

        // Draw missiles
        {
            pthread_mutex_lock(&mtx_missiles);
            for (auto it = missiles.begin(); it != missiles.end();)
            {
                if ((*it)->active)
                {
                    (*it)->draw();
                    ++it;
                }
                else
                {
                    (*it)->join();
                    delete *it;
                    it = missiles.erase(it);
                }
            }
            pthread_mutex_unlock(&mtx_missiles);
        }

        // Draw dinosaurs
        {
            pthread_mutex_lock(&mtx_dinosaurs);
            for (auto it = dinosaurs.begin(); it != dinosaurs.end();)
            {
                if ((*it)->active)
                {
                    (*it)->draw();
                    ++it;
                }
                else
                {
                    (*it)->join();
                    delete *it;
                    it = dinosaurs.erase(it);
                }
            }
            pthread_mutex_unlock(&mtx_dinosaurs);
        }

        // Draw depot
        mvprintw(DEPOT_Y, DEPOT_X, "S");

        // Draw active trucks
        {
            pthread_mutex_lock(&mtx_trucks);
            for (auto it = active_trucks.begin(); it != active_trucks.end();)
            {
                if ((*it)->active)
                {
                    (*it)->draw();
                    ++it;
                }
                else
                {
                    (*it)->join();
                    delete *it;
                    it = active_trucks.erase(it);
                }
            }
            pthread_mutex_unlock(&mtx_trucks);
        }

        mvprintw(HEIGHT, 0, "Remaining missiles: %d  Depot missiles: %d  Dinosaurs: %lu",
                 heli.get_remaining_missiles(), depot.missiles, dinosaurs.size());

        refresh();
        usleep(25000);
    }

    clear();
    std::string game_over_msg = "Game Over!";
    mvprintw(HEIGHT / 2, (WIDTH - game_over_msg.length()) / 2, "%s", game_over_msg.c_str());
    refresh();
    nodelay(stdscr, FALSE);
    getch();

    // Clean up remaining trucks
    {
        pthread_mutex_lock(&mtx_trucks);
        for (auto truck : active_trucks)
        {
            truck->join();
            delete truck;
        }
        active_trucks.clear();
        pthread_mutex_unlock(&mtx_trucks);
    }

    return nullptr;
}

// Function to manage dinosaurs
void *thread_dinosaur_manager(void *arg)
{
    // Spawn the initial dinosaur
    {
        pthread_mutex_lock(&mtx_dinosaurs);
        double spawn_y = HEIGHT - 2;
        int initial_direction = (rand() % 2 == 0) ? -1 : 1;
        double spawn_x = (initial_direction == -1) ? WIDTH - 2 : 1;
        Dinosaur *d = new Dinosaur(spawn_x, spawn_y, m, initial_direction);
        dinosaurs.push_back(d);
        d->start();
        pthread_mutex_unlock(&mtx_dinosaurs);
    }

    time_t last_spawn_time = time(nullptr);
    while (is_running())
    {
        time_t current_time = time(nullptr);

        // Spawn a new dinosaur if the time interval t has elapsed
        if (difftime(current_time, last_spawn_time) >= t)
        {
            pthread_mutex_lock(&mtx_dinosaurs);

            // Check if the maximum number of dinosaurs has been reached
            if (dinosaurs.size() == 4)
            {
                pthread_mutex_unlock(&mtx_dinosaurs);
                set_running(false);
                break;
            }

            // Spawn a new dinosaur
            double spawn_y = HEIGHT - 2;
            int initial_direction = (rand() % 2 == 0) ? -1 : 1;
            double spawn_x = (initial_direction == -1) ? WIDTH - 2 : 1;
            Dinosaur *d = new Dinosaur(spawn_x, spawn_y, m, initial_direction);
            dinosaurs.push_back(d);
            d->start();

            pthread_mutex_unlock(&mtx_dinosaurs);

            last_spawn_time = current_time;
        }

        usleep(500000);
    }
    return nullptr;
}

// Missile collision detection with dinosaurs
void Missile::check_collision(double prev_x, double curr_x)
{
    pthread_mutex_lock(&mtx_dinosaurs);
    for (auto d : dinosaurs)
    {
        if (d->active)
        {
            double d_head_x = d->x + d->direction;
            double d_head_y = d->y - 1;

            int missile_y = static_cast<int>(y);
            if (missile_y == static_cast<int>(d_head_y))
            {
                if ((prev_x <= d_head_x && curr_x >= d_head_x) ||
                    (prev_x >= d_head_x && curr_x <= d_head_x))
                {
                    d->take_damage();
                    active = false;
                    break;
                }
            }

            // Collision with dinosaur's body (ineffective)
            if (missile_y == static_cast<int>(d->y))
            {
                if ((prev_x <= d->x && curr_x >= d->x) ||
                    (prev_x >= d->x && curr_x <= d->x))
                {
                    active = false;
                    break;
                }
            }
        }
    }
    pthread_mutex_unlock(&mtx_dinosaurs);
}

// Dinosaur collision detection with helicopter
void Dinosaur::check_collision()
{
    double heli_x = heli.get_x();
    double heli_y = heli.get_y();

    pthread_mutex_lock(&mtx);

    // Collision with dinosaur's body
    bool collision_body = (static_cast<int>(x) == static_cast<int>(heli_x) &&
                           static_cast<int>(y) == static_cast<int>(heli_y));

    // Collision with dinosaur's head
    int head_x = static_cast<int>(x + direction);
    bool collision_head = (head_x == static_cast<int>(heli_x) &&
                           static_cast<int>(y - 1) == static_cast<int>(heli_y));

    pthread_mutex_unlock(&mtx);

    if (collision_body || collision_head)
    {
        set_running(false);
    }
}

// Main function
int main()
{
    // Seed random number generator
    srand(time(nullptr));

    // Initialize ncurses
    initscr();
    noecho();
    curs_set(FALSE);

    // Assign the helicopter pointer
    heli_ptr = &heli;

    heli.set_y(HEIGHT - 3);

    // Create threads
    pthread_t input_thread_id, render_thread_id, dinosaur_manager_thread_id, truck_thread_id;
    pthread_create(&input_thread_id, nullptr, thread_input, nullptr);
    pthread_create(&render_thread_id, nullptr, thread_render, nullptr);
    pthread_create(&dinosaur_manager_thread_id, nullptr, thread_dinosaur_manager, nullptr);
    pthread_create(&truck_thread_id, nullptr, thread_truck, nullptr);

    // Wait for threads
    pthread_join(input_thread_id, nullptr);
    pthread_join(render_thread_id, nullptr);
    pthread_join(dinosaur_manager_thread_id, nullptr);
    pthread_join(truck_thread_id, nullptr);

    // End ncurses
    endwin();

    // Clear remaining missiles
    {
        pthread_mutex_lock(&mtx_missiles);
        for (auto m : missiles)
        {
            m->active = false;
            m->join();
            delete m;
        }
        missiles.clear();
        pthread_mutex_unlock(&mtx_missiles);
    }

    // Clear remaining dinosaurs
    {
        pthread_mutex_lock(&mtx_dinosaurs);
        for (auto d : dinosaurs)
        {
            d->active = false;
            d->join();
            delete d;
        }
        dinosaurs.clear();
        pthread_mutex_unlock(&mtx_dinosaurs);
    }

    // Destroy mutexes
    pthread_mutex_destroy(&mtx_missiles);
    pthread_mutex_destroy(&mtx_dinosaurs);
    pthread_mutex_destroy(&mtx_trucks);
    pthread_mutex_destroy(&mtx_running);

    return 0;
}
