#pragma once

#include <stdbool.h>

#include "bot_behavior.h"
#include "glint_chase.h"
#include "pages.h"

struct page_frame {
    double t, dt;
    bool offline;
    bot_pose_t pose;
    gc_out_t chase;
};
