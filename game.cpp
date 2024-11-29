#include <iostream>
#include <pthread.h>
#include <mutex>
#include <atomic>
#include <vector>
#include <ncurses.h>
#include <unistd.h>
#include <ctime>
#include <condition_variable>
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
std::mutex mtx_missiles;
std::mutex mtx_dinosaurs;
std::mutex mtx_trucks;
std::atomic<bool> running(true);

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
    std::mutex mtx;
    int direction; // 1 for right, -1 for left

    // Jumping variables
    bool is_jumping;
    double vertical_velocity;

    Dinosaur(double startX, double startY, int initial_health, int initial_direction = -1)
        : x(startX), y(startY), health(initial_health), active(true), th(0),
          direction(initial_direction), is_jumping(false), vertical_velocity(0) {}

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
        std::lock_guard<std::mutex> lock(mtx);
        health--;
        if (health <= 0)
        {
            active = false;
        }
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
    std::mutex mtx;
    std::condition_variable cv_truck;
    std::condition_variable cv_helicopter;

    Depot(int capacity)
        : capacity(capacity), missiles(capacity),
          is_truck_unloading(false), is_helicopter_reloading(false) {}

    void truck_unload(int amount);
    void helicopter_reload(int amount);
};

// Class to represent the helicopter
class Helicopter
{
public:
    double x;
    double y;
    std::atomic<int> remaining_missiles;
    std::mutex mtx;
    int last_horizontal_direction; // -1 for left, 1 for right

    Helicopter(int startX, int startY, int capacity)
        : x(startX), y(startY), remaining_missiles(capacity),
          last_horizontal_direction(1) {}

    void move(double dx, double dy)
    {
        std::lock_guard<std::mutex> lock(mtx);
        x += dx;
        y += dy;
    }

    double get_x()
    {
        std::lock_guard<std::mutex> lock(mtx);
        return x;
    }

    double get_y()
    {
        std::lock_guard<std::mutex> lock(mtx);
        return y;
    }

    void set_x(double new_x)
    {
        std::lock_guard<std::mutex> lock(mtx);
        x = new_x;
    }

    void set_y(double new_y)
    {
        std::lock_guard<std::mutex> lock(mtx);
        y = new_y;
    }

    bool can_fire()
    {
        return remaining_missiles > 0;
    }

    void fire()
    {
        if (remaining_missiles > 0)
        {
            remaining_missiles--;
        }
    }

    void reload(int amount)
    {
        remaining_missiles += amount;
    }

    void set_last_horizontal_direction(int dir)
    {
        std::lock_guard<std::mutex> lock(mtx);
        last_horizontal_direction = dir;
    }

    int get_last_horizontal_direction()
    {
        std::lock_guard<std::mutex> lock(mtx);
        return last_horizontal_direction;
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
    depot.helicopter_reload(n - remaining_missiles.load());
}

// Implement Depot methods
void Depot::truck_unload(int amount)
{
    std::unique_lock<std::mutex> lock(mtx);
    cv_truck.wait(lock, [this]()
                  { return missiles < capacity && !is_helicopter_reloading; });

    is_truck_unloading = true;
    int unload_amount = std::min(amount, capacity - missiles);
    missiles += unload_amount;
    is_truck_unloading = false;

    cv_helicopter.notify_all();
}

void Depot::helicopter_reload(int amount)
{
    std::unique_lock<std::mutex> lock(mtx);
    cv_helicopter.wait(lock, [this]()
                       { return missiles > 0 && !is_truck_unloading; });

    is_helicopter_reloading = true;
    int reload_amount = std::min(amount, missiles);
    missiles -= reload_amount;
    heli.reload(reload_amount);
    is_helicopter_reloading = false;

    cv_truck.notify_all();
}

// Helper function to check if a position is occupied by an active dinosaur or the depot
bool is_position_occupied(double x, double y)
{
    std::lock_guard<std::mutex> lock(mtx_dinosaurs);
    for (const auto &d : dinosaurs)
    {
        if (d->active)
        {
            // Dinosaur body
            if (static_cast<int>(d->x) == static_cast<int>(x) &&
                static_cast<int>(d->y) == static_cast<int>(y))
                return true;
            // Dinosaur head
            int head_x = static_cast<int>(d->x + d->direction);
            if (head_x == static_cast<int>(x) &&
                static_cast<int>(d->y - 1) == static_cast<int>(y))
                return true;
        }
    }

    // Depot position
    if (static_cast<int>(x) == DEPOT_X && static_cast<int>(y) == DEPOT_Y)
        return true;

    return false;
}

const int TRUCK_INTERVAL = 1000000; // 1 second in microseconds

// Function to manage the truck
void *thread_truck(void *arg)
{
    while (running)
    {
        usleep(TRUCK_INTERVAL);

        {
            std::lock_guard<std::mutex> lock(mtx_trucks);
            if (!active_trucks.empty())
            {
                continue;
            }
        }

        Truck *truck = new Truck(1, DEPOT_Y, DEPOT_X - 1, 1);
        truck->start();

        {
            std::lock_guard<std::mutex> lock(mtx_trucks);
            active_trucks.push_back(truck);
        }

        while (running && truck->active)
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
    while (running)
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
                    std::lock_guard<std::mutex> lock(mtx_missiles);
                    missiles.push_back(m);
                }
                m->start();
            }
            break;
        case 'q':
            running = false;
            break;
        default:
            break;
        }

        // Reload if near depot
        if (is_near_depot(heli.get_x(), heli.get_y()) && heli.remaining_missiles.load() < n)
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
    while (running)
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
            std::lock_guard<std::mutex> lock(mtx_missiles);
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
        }

        // Draw dinosaurs
        {
            std::lock_guard<std::mutex> lock(mtx_dinosaurs);
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
        }

        // Draw depot
        mvprintw(DEPOT_Y, DEPOT_X, "S");

        // Draw active trucks
        {
            std::lock_guard<std::mutex> lock(mtx_trucks);
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
        }

        mvprintw(HEIGHT, 0, "Remaining missiles: %d  Depot missiles: %d  Dinosaurs: %lu",
                 heli.remaining_missiles.load(), depot.missiles, dinosaurs.size());

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
        std::lock_guard<std::mutex> lock(mtx_trucks);
        for (auto truck : active_trucks)
        {
            truck->join();
            delete truck;
        }
        active_trucks.clear();
    }

    return nullptr;
}

// Function to manage dinosaurs
void *thread_dinosaur_manager(void *arg)
{
    // Spawn the initial dinosaur
    {
        std::lock_guard<std::mutex> lock(mtx_dinosaurs);
        double spawn_y = HEIGHT - 2;
        int initial_direction = (rand() % 2 == 0) ? -1 : 1;
        double spawn_x = (initial_direction == -1) ? WIDTH - 2 : 1;
        Dinosaur *d = new Dinosaur(spawn_x, spawn_y, m, initial_direction);
        dinosaurs.push_back(d);
        d->start();
    }

    time_t last_spawn_time = time(nullptr);
    while (running)
    {
        time_t current_time = time(nullptr);

        // Spawn a new dinosaur if the time interval t has elapsed
        if (difftime(current_time, last_spawn_time) >= t)
        {
            std::lock_guard<std::mutex> lock(mtx_dinosaurs);

            // Check if the maximum number of dinosaurs has been reached
            if (dinosaurs.size() >= 5)
            {
                running = false;
                break;
            }

            // Spawn a new dinosaur
            double spawn_y = HEIGHT - 2;
            int initial_direction = (rand() % 2 == 0) ? -1 : 1;
            double spawn_x = (initial_direction == -1) ? WIDTH - 2 : 1;
            Dinosaur *d = new Dinosaur(spawn_x, spawn_y, m, initial_direction);
            dinosaurs.push_back(d);
            d->start();

            last_spawn_time = current_time;
        }

        usleep(500000);
    }
    return nullptr;
}

// Missile collision detection with dinosaurs
void Missile::check_collision(double prev_x, double curr_x)
{
    std::lock_guard<std::mutex> lock(mtx_dinosaurs);
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
}

// Dinosaur collision detection with helicopter
void Dinosaur::check_collision()
{
    double heli_x = heli.get_x();
    double heli_y = heli.get_y();

    std::lock_guard<std::mutex> lock(mtx);

    // Collision with dinosaur's body
    bool collision_body = (static_cast<int>(x) == static_cast<int>(heli_x) &&
                           static_cast<int>(y) == static_cast<int>(heli_y));

    // Collision with dinosaur's head
    int head_x = static_cast<int>(x + direction);
    bool collision_head = (head_x == static_cast<int>(heli_x) &&
                           static_cast<int>(y - 1) == static_cast<int>(heli_y));

    if (collision_body || collision_head)
    {
        running = false;
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
        std::lock_guard<std::mutex> lock(mtx_missiles);
        for (auto m : missiles)
        {
            m->active = false;
            m->join();
            delete m;
        }
        missiles.clear();
    }

    // Clear remaining dinosaurs
    {
        std::lock_guard<std::mutex> lock(mtx_dinosaurs);
        for (auto d : dinosaurs)
        {
            d->active = false;
            d->join();
            delete d;
        }
        dinosaurs.clear();
    }

    return 0;
}
