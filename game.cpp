#include <iostream>
#include <pthread.h>
#include <mutex>
#include <atomic>
#include <vector>
#include <ncurses.h>
#include <unistd.h>

// Scenario dimensions
const int WIDTH = 50;
const int HEIGHT = 20;
const int MAX_DINOSAURS = 5;

// Difficulty parameters (can be adjusted)
int m = 3;  // Missiles needed to kill a dinosaur
int n = 5;  // Helicopter missile capacity
int t = 10; // Time between dinosaur spawns (in seconds)

// Global variables
std::atomic<bool> running(true);
std::mutex mtx_draw;

// Forward declarations
class Helicopter;
class Missile;
class Dinosaur;

// Global objects
Helicopter *heli;
std::vector<Missile *> missiles;
std::vector<Dinosaur *> dinosaurs;

// Function prototypes
void *thread_input(void *arg);
void *thread_render(void *arg);
void *thread_dinosaur_manager(void *arg);
void initialize();
void cleanup();

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
    pthread_t thread_id;

    Missile(int startX, int startY)
        : x(startX), y(startY), active(true) {}

    static void *move_wrapper(void *arg)
    {
        Missile *m = static_cast<Missile *>(arg);
        m->move();
        return nullptr;
    }

    void start()
    {
        pthread_create(&thread_id, nullptr, Missile::move_wrapper, this);
    }

    void move()
    {
        while (active && x < WIDTH - 1)
        {
            usleep(50000); // 50 ms
            x++;
            check_collision();
        }
        active = false;
    }

    void draw()
    {
        if (active)
        {
            mtx_draw.lock();
            mvprintw(y, x, "-");
            mtx_draw.unlock();
        }
    }

    void join()
    {
        if (thread_id)
        {
            pthread_join(thread_id, nullptr);
            thread_id = 0;
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
    pthread_t thread_id;
    std::mutex mtx;

    Dinosaur(int startX, int startY, int initial_health)
        : x(startX), y(startY), health(initial_health), active(true) {}

    static void *move_wrapper(void *arg)
    {
        Dinosaur *d = static_cast<Dinosaur *>(arg);
        d->move();
        return nullptr;
    }

    void start()
    {
        pthread_create(&thread_id, nullptr, Dinosaur::move_wrapper, this);
    }

    void move()
    {
        while (active && x > 1)
        {
            usleep(200000); // 200 ms
            x--;
            check_collision();
        }
        if (active)
            active = false; // Dinosaur has reached the left edge
    }

    void draw()
    {
        if (active)
        {
            mtx_draw.lock();
            mvprintw(y, x, "D");     // Body
            mvprintw(y - 1, x, "O"); // Head
            mtx_draw.unlock();
        }
    }

    void join()
    {
        if (thread_id)
        {
            pthread_join(thread_id, nullptr);
            thread_id = 0;
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

void Missile::check_collision()
{
    std::lock_guard<std::mutex> lock(mtx_draw);
    for (auto d : dinosaurs)
    {
        if (d->active)
        {
            // Check if missile hits the head
            if (x == d->x && y == d->y - 1)
            {
                d->take_damage();
                active = false;
                break;
            }
            // Check if missile hits the body (ineffective)
            else if (x == d->x && y == d->y)
            {
                active = false;
                break;
            }
        }
    }
}

void Dinosaur::check_collision()
{
    // Check collision with helicopter
    if (x == heli->x.load() && (y == heli->y.load() || y - 1 == heli->y.load()))
    {
        running = false;
    }
}

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
            if (heli->y > 1)
                heli->y--;
            break;
        case KEY_DOWN:
        case 's':
            if (heli->y < HEIGHT - 2)
                heli->y++;
            break;
        case KEY_LEFT:
        case 'a':
            if (heli->x > 1)
                heli->x--;
            break;
        case KEY_RIGHT:
        case 'd':
            if (heli->x < WIDTH - 2)
                heli->x++;
            break;
        case ' ':
            if (heli->can_fire())
            {
                heli->fire();
                // Create and start a new missile
                Missile *m = new Missile(heli->x.load() + 1, heli->y.load());
                missiles.push_back(m);
                m->start();
            }
            break;
        case 'q':
            running = false;
            break;
        default:
            break;
        }
        usleep(10000); // 10 ms
    }
    return nullptr;
}

void *thread_render(void *arg)
{
    while (running)
    {
        mtx_draw.lock();
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
        mvprintw(heli->y.load(), heli->x.load(), "H");

        // Draw missiles
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

        // Draw dinosaurs
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

        // Show game info
        mvprintw(HEIGHT, 0, "Missiles: %d  Dinosaurs: %d", heli->remaining_missiles.load(), (int)dinosaurs.size());

        refresh();
        mtx_draw.unlock();
        usleep(50000); // 50 ms
    }
    return nullptr;
}

void *thread_dinosaur_manager(void *arg)
{
    int last_spawn_time = time(nullptr);
    while (running)
    {
        if (dinosaurs.size() >= MAX_DINOSAURS)
        {
            running = false;
            break;
        }

        int current_time = time(nullptr);
        if (current_time - last_spawn_time >= t)
        {
            // Spawn new dinosaur
            Dinosaur *d = new Dinosaur(WIDTH - 2, HEIGHT - 2, m);
            dinosaurs.push_back(d);
            d->start();
            last_spawn_time = current_time;
        }
        usleep(500000); // 500 ms
    }
    return nullptr;
}

void initialize()
{
    // Initialize ncurses
    initscr();
    noecho();
    curs_set(FALSE);

    // Initialize helicopter
    heli = new Helicopter(WIDTH / 2, HEIGHT / 2, n);

    // Create threads
    pthread_t input_thread_id, render_thread_id, dinosaur_manager_thread_id;
    pthread_create(&input_thread_id, nullptr, thread_input, nullptr);
    pthread_create(&render_thread_id, nullptr, thread_render, nullptr);
    pthread_create(&dinosaur_manager_thread_id, nullptr, thread_dinosaur_manager, nullptr);

    // Wait for threads
    pthread_join(input_thread_id, nullptr);
    pthread_join(render_thread_id, nullptr);
    pthread_join(dinosaur_manager_thread_id, nullptr);

    // Clean up
    cleanup();

    // End ncurses
    endwin();
}

void cleanup()
{
    // Clean up missiles
    for (auto m : missiles)
    {
        m->active = false;
        m->join();
        delete m;
    }
    missiles.clear();

    // Clean up dinosaurs
    for (auto d : dinosaurs)
    {
        d->active = false;
        d->join();
        delete d;
    }
    dinosaurs.clear();

    // Delete helicopter
    delete heli;
}

int main()
{
    initialize();
    return 0;
}
