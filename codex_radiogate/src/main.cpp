#include "NodeController.h"

namespace {

NodeController g_controller;

}  // namespace

void setup() {
    g_controller.begin();
}

void loop() {
    g_controller.loop();
}
