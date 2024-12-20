#define VMA_IMPLEMENTATION

#define FRAMES_IN_FLIGHT 2

#include <memory>

#include "Engine.hpp"

int main()
{
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();

    engine->init();
    engine->start();
    engine->cleanup();

    return 0;
}
