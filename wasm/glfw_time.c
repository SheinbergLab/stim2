#include <emscripten/emscripten.h>

double glfwGetTime(void)
{
    return emscripten_get_now() / 1000.0;
}
