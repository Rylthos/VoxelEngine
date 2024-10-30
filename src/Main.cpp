#define VMA_IMPLEMENTATION
#define GLM_ENABLE_EXPERIMENTAL

#include "Engine.hpp"

int main()
{
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    engine->init();
    engine->start();
    engine->cleanup();

    return 0;
}
