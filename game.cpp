#include <iostream>
#include <pthread.h>
#include <mutex>
#include <atomic>
#include <vector>
#include <ncurses.h>
#include <unistd.h>
#include <ctime>

// Scenario dimensions
const int WIDTH = 50;
const int HEIGHT = 20;

// Difficulty parameters
int m = 3;    // Number of hits required to kill a dinosaur
int n = 1000; // Helicopter missile capacity
int t = 5;    // Time interval between dinosaur spawns (in seconds)

// Class to represent the helicopter// Class to represent the helicopter
class Helicopter
{
public:
    double x;
    double y;
    std::atomic<int> remaining_missiles;
    std::mutex mtx;
    int last_horizontal_direction; // -1 for left, 1 for right

    Helicopter(int startX, int startY, int capacity)
        : x(startX), y(startY), remaining_missiles(capacity), last_horizontal_direction(1) {}

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
        int missiles = remaining_missiles.load();
        if (missiles > 0)
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
};

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
        double speed = 0.5; // Move 0.5 units per update
        while (active && x > 1 && x < WIDTH - 2)
        {
            double prev_x = x;
            x += direction * speed;
            check_collision(prev_x, x);
            usleep(25000); // Sleep for 25 milliseconds
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

    Dinosaur(double startX, double startY, int initial_health, int initial_direction = -1)
        : x(startX), y(startY), health(initial_health), active(true), th(0), direction(initial_direction) {}

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
        double speed = 0.25; // Move 0.25 units per update
        while (active)
        {
            double prev_x = x;
            x += direction * speed;

            // Check for boundary collision to change direction
            if (x <= 1)
            {
                x = 1;
                direction = 1; // Change direction to right
            }
            else if (x >= WIDTH - 2)
            {
                x = WIDTH - 2;
                direction = -1; // Change direction to left
            }

            check_collision();
            usleep(50000); // Sleep for 50 milliseconds
        }
    }

    void draw()
    {
        if (active)
        {
            mvprintw(static_cast<int>(y), static_cast<int>(x), "D"); // Dinosaur body
            int head_x = static_cast<int>(x + direction);
            mvprintw(static_cast<int>(y - 1), head_x, "O"); // Dinosaur head
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

// Global variables
Helicopter heli(WIDTH / 2, HEIGHT / 2, n); // Initial missile capacity
std::vector<Missile *> missiles;
std::vector<Dinosaur *> dinosaurs;
std::mutex mtx_missiles;
std::mutex mtx_dinosaurs;
std::atomic<bool> running(true);

// Function declarations
void *thread_input(void *arg);
void *thread_render(void *arg);
void *thread_dinosaur_manager(void *arg);

// Helper function to check if a position is occupied by an active dinosaur
bool is_position_occupied(double x, double y)
{
    std::lock_guard<std::mutex> lock(mtx_dinosaurs);
    for (const auto &d : dinosaurs)
    {
        if (d->active)
        {
            // Check collision with dinosaur's body
            if (static_cast<int>(d->x) == static_cast<int>(x) && static_cast<int>(d->y) == static_cast<int>(y))
                return true;
            // Check collision with dinosaur's head
            int head_x = static_cast<int>(d->x + d->direction);
            if (head_x == static_cast<int>(x) && static_cast<int>(d->y - 1) == static_cast<int>(y))
                return true;
        }
    }
    return false;
}

// Function to manage player input
void *thread_input(void *arg)
{
    int ch;
    nodelay(stdscr, TRUE); // Does not block waiting for input
    keypad(stdscr, TRUE);  // Captures special keys
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
            heli.set_last_horizontal_direction(-1); // Moving left
            break;
        }
        case KEY_RIGHT:
        case 'd':
        {
            double new_x = heli.get_x() + 1;
            if (new_x < WIDTH - 2 && !is_position_occupied(new_x, heli.get_y()))
                heli.set_x(new_x);
            heli.set_last_horizontal_direction(1); // Moving right
            break;
        }
        case ' ':
            if (heli.can_fire())
            {
                heli.fire();
                int missile_direction = heli.get_last_horizontal_direction();
                // Adjust starting position based on direction
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
        usleep(10000); // Sleep for 10 milliseconds
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
                    // Join the thread and remove the missile from the list
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
                    // Join the thread and remove the dinosaur from the list
                    (*it)->join();
                    delete *it;
                    it = dinosaurs.erase(it);
                }
            }
        }

        // Show remaining missiles and number of dinosaurs
        mvprintw(HEIGHT, 0, "Remaining missiles: %d  Dinosaurs: %lu", heli.remaining_missiles.load(), dinosaurs.size());

        refresh();
        usleep(25000); // Sleep for 25 milliseconds
    }
    return nullptr;
}

// Function to manage dinosaurs
void *thread_dinosaur_manager(void *arg)
{
    time_t last_spawn_time = time(nullptr);
    while (running)
    {
        time_t current_time = time(nullptr);
        if (difftime(current_time, last_spawn_time) >= t)
        {
            // Fixed y-position for all dinosaurs (ground level)
            double spawn_y = HEIGHT - 2;

            // Randomize initial direction for diversity
            int initial_direction = (rand() % 2 == 0) ? -1 : 1;

            // Spawn a new dinosaur at the ground level
            double spawn_x = (initial_direction == -1) ? WIDTH - 2 : 1;
            Dinosaur *d = new Dinosaur(spawn_x, spawn_y, m, initial_direction);
            {
                std::lock_guard<std::mutex> lock(mtx_dinosaurs);
                dinosaurs.push_back(d);
            }
            d->start();
            last_spawn_time = current_time;
        }
        usleep(500000); // Sleep for 500 milliseconds
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
            double d_head_x = d->x + d->direction; // Shifted head position
            double d_head_y = d->y - 1;

            // Check for collision between previous and current positions
            if (y == d_head_y)
            {
                if ((prev_x <= d_head_x && curr_x >= d_head_x) || (prev_x >= d_head_x && curr_x <= d_head_x))
                {
                    d->take_damage();
                    active = false;
                    break;
                }
            }

            // Check collision with dinosaur's body (ineffective)
            if (y == d->y)
            {
                if ((prev_x <= d->x && curr_x >= d->x) || (prev_x >= d->x && curr_x <= d->x))
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

    // Check collision with dinosaur's body
    bool collision_body = (static_cast<int>(x) == static_cast<int>(heli_x) && static_cast<int>(y) == static_cast<int>(heli_y));

    // Check collision with dinosaur's head
    int head_x = static_cast<int>(x + direction); // Shifted head position
    bool collision_head = (head_x == static_cast<int>(heli_x) && static_cast<int>(y - 1) == static_cast<int>(heli_y));

    if (collision_body || collision_head)
    {
        // Collision detected
        running = false;
    }
}

// Main function
int main()
{
    // Initialize ncurses
    initscr();
    noecho();
    curs_set(FALSE);

    heli.set_y(HEIGHT - 3); // Position the helicopter just above the ground

    // Create threads
    pthread_t input_thread_id, render_thread_id, dinosaur_manager_thread_id;
    pthread_create(&input_thread_id, nullptr, thread_input, nullptr);
    pthread_create(&render_thread_id, nullptr, thread_render, nullptr);
    pthread_create(&dinosaur_manager_thread_id, nullptr, thread_dinosaur_manager, nullptr);

    // Wait for threads
    pthread_join(input_thread_id, nullptr);
    pthread_join(render_thread_id, nullptr);
    pthread_join(dinosaur_manager_thread_id, nullptr);

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

    // Game over message
    if (dinosaurs.size() >= 5)
    {
        std::cout << "Game Over! Too many dinosaurs!" << std::endl;
    }
    else
    {
        std::cout << "Game Over!" << std::endl;
    }

    return 0;
}
