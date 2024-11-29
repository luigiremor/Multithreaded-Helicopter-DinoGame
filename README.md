# Terminal Helicopter vs Dinosaurs Game

## Description

The Terminal Helicopter vs Dinosaurs Game is an action game implemented in C++ that runs in the terminal. It uses the `ncurses` library for terminal input and output, and `pthread` for managing threads.

## Features

- Control a helicopter to shoot missiles at dinosaurs
- Terminal-based interface
- Multithreading for smooth gameplay
- Dynamic reloading of missiles from a depot

## Prerequisites

- `clang++` compiler
- `ncurses` library
- `pthread` library

## Building and Running

To compile and run the game, use the following command:

```bash
clang++ -std=c++11 -o game game.cpp -lncurses -lpthread
```
or
```bash
g++ -std=c++11 -o game game.cpp -pthread -lncurses
```

After compiling, you can run the game with:

```bash
./game
```

## Controls

- Use the arrow keys or 'w', 'a', 's', 'd' to move the helicopter.
- Press the space bar to fire a missile.
- Press 'q' to quit the game.

## Objective

- Shoot down dinosaurs before they reach the left side of the screen.
- Reload missiles by hovering over the depot.
