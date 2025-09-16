#include "navigation.h"
#include "utils.h"

// Navigation stack
ScreenID nav_stack[NAVIGATION_STACK_SIZE];
int nav_stack_top = -1;

void nav_push(ScreenID id) {
  if (nav_stack_top < NAVIGATION_STACK_SIZE - 1) {
    nav_stack[++nav_stack_top] = id;
    DEBUG_PRINTF("Pushed %d to nav stack, new size: %d\n", id, nav_stack_top + 1);
  } else {
    DEBUG_PRINTLN("Navigation stack overflow");
  }
}

ScreenID nav_pop() {
  if (nav_stack_top >= 0) {
    DEBUG_PRINTF("Popped from nav stack, new size: %d\n", nav_stack_top + 1);
    return nav_stack[nav_stack_top--];
  } else {
    DEBUG_PRINTLN("Navigation stack underflow");
    return SCREEN_MAIN; // Default screen if stack is empty
  }
}