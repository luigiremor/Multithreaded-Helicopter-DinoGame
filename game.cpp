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
int m = 3; // Number of hits required to kill a dinosaur
int n = 5; // Helicopter missile capacity
int t = 5; // Time interval between dinosaur spawns (in seconds)

// Class to represent the helicopter
class Helicopter
{
public:
    std::atomic<int> x;
    std::atomic<int> y;
    std::atomic<int> remaining_missiles;
    std::mutex mtx;

    Helicopter(int startX, int startY, int capacity)
        : x(startX), y(startY), remaining_missiles(capacity) {}

    void move(int dx, int dy)
    {
        std::lock_guard<std::mutex> lock(mtx);
        x += dx;
        y += dy;
    }

    bool can_fire()
    {
        return remaining_missiles > 0;
    }

    void fire()
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (remaining_missiles > 0)
            remaining_missiles--;
    }

    void reload(int amount)
    {
        std::lock_guard<std::mutex> lock(mtx);
        remaining_missiles += amount;
    }
};

// Class to represent a missile
class Missile
{
public:
    int x;
    int y;
    bool active;
    pthread_t th;

    Missile(int startX, int startY)
        : x(startX), y(startY), active(true), th(0) {}

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
        while (active && x < WIDTH - 1)
        {
            usleep(50000); // Sleep for 50 milliseconds
            x++;
            check_collision();
        }
        active = false;
    }

    void draw()
    {
        if (active)
        {
            mvprintw(y, x, "-");
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

    void check_collision();
};

// Class to represent a dinosaur
class Dinosaur
{
public:
    int x;
    int y;
    int health;
    bool active;
    pthread_t th;
    std::mutex mtx;

    Dinosaur(int startX, int startY, int initial_health)
        : x(startX), y(startY), health(initial_health), active(true), th(0) {}

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
        while (active && x > 1)
        {
            usleep(200000); // Sleep for 200 milliseconds
            x--;            // Move left
            check_collision();
        }
        active = false;
    }

    void draw()
    {
        if (active)
        {
            mvprintw(y, x, "D");     // Dinosaur body
            mvprintw(y - 1, x, "O"); // Dinosaur head
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
            if (heli.y > 1)
                heli.y--;
            break;
        case KEY_DOWN:
        case 's':
            if (heli.y < HEIGHT - 2)
                heli.y++;
            break;
        case KEY_LEFT:
        case 'a':
            if (heli.x > 1)
                heli.x--;
            break;
        case KEY_RIGHT:
        case 'd':
            if (heli.x < WIDTH - 2)
                heli.x++;
            break;
        case ' ':
            if (heli.can_fire())
            {
                heli.fire();
                // Create and start a new missile
                Missile *m = new Missile(heli.x + 1, heli.y);
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
        mvprintw(heli.y.load(), heli.x.load(), "H");

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
        usleep(50000); // Sleep for 50 milliseconds
    }
    return nullptr;
}

// Function to manage dinosaurs
void *thread_dinosaur_manager(void *arg)
{
    time_t last_spawn_time = time(nullptr);
    while (running)
    {
        if (dinosaurs.size() >= 5)
        {
            // Game over condition
            running = false;
            break;
        }
        time_t current_time = time(nullptr);
        if (difftime(current_time, last_spawn_time) >= t)
        {
            // Spawn a new dinosaur
            Dinosaur *d = new Dinosaur(WIDTH - 2, HEIGHT - 2, m);
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
void Missile::check_collision()
{
    std::lock_guard<std::mutex> lock(mtx_dinosaurs);
    for (auto d : dinosaurs)
    {
        if (d->active)
        {
            // Check collision with dinosaur's head
            if (x == d->x && y == d->y - 1)
            {
                d->take_damage();
                active = false;
                break;
            }
            // Check collision with dinosaur's body (ineffective)
            else if (x == d->x && y == d->y)
            {
                active = false;
                break;
            }
        }
    }
}

// Dinosaur collision detection with helicopter
void Dinosaur::check_collision()
{
    if (x == heli.x.load() && (y == heli.y.load() || y - 1 == heli.y.load()))
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
