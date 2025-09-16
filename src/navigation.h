#pragma once

enum ScreenID {
  SCREEN_MAIN,
  SCREEN_MORE,
  SCREEN_SETTINGS,
  SCREEN_LED,
  SCREEN_BL,
  SCREEN_TOUCH,
  SCREEN_CELL_VOLTAGES,
  SCREEN_CELL_RESISTANCES
};

// Navigation stack size
#define NAVIGATION_STACK_SIZE 8

// Navigation functions
void nav_push(ScreenID id);
ScreenID nav_pop();

// Navigation stack (exposed for debugging if needed)
extern ScreenID nav_stack[NAVIGATION_STACK_SIZE];
extern int nav_stack_top;