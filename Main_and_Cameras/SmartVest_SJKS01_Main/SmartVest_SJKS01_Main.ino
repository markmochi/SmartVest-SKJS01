// Start with Config.h; see docs/PINOUTS.md and docs/QUICKSTART.md.
#include "src/core/Application.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);
void setup() { Application::begin(); }
void loop() { Application::tick(); }
