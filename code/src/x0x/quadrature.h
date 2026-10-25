/** @file quadrature.h
 *  @brief A detented encoder's A and B lines to clicks, proof against
 *  contact bounce.
 *
 *  The lines rest high (11) at a detent, and one click runs them through a
 *  full Gray cycle: B leads clockwise (11, 10, 00, 01, 11), A leads
 *  counter-clockwise (11, 01, 00, 10, 11). Each line must read the same twice
 *  in a row (sampled at 1 kHz) before it counts as changed, and each step of
 *  the cycle adds or takes one from a count. A bounce adds and takes back the
 *  same step, so it cancels out. A click is reported only on reaching the
 *  rest position, in the direction the count went, if it went at least half
 *  way: a skipped state in a fast spin still counts, a wobble never does.
 */
#pragma once
#include <cstdint>

namespace x0x
{

struct Quadrature
{
    /** One sample of the lines (true = high). +1 clockwise, -1 back, 0. */
    int Sample(bool a, bool b)
    {
        // Each line changes only once it reads the same twice in a row.
        if(a == last_a_)
            a_ = a;
        if(b == last_b_)
            b_ = b;
        last_a_ = a, last_b_ = b;

        const int now = (a_ ? 2 : 0) | (b_ ? 1 : 0);
        if(now == state_)
            return 0;
        // Clockwise: 3 -> 2 -> 0 -> 1 -> 3. Two lines at once (a skipped
        // state) gives nothing.
        static const int8_t kDelta[4][4] = {
            // to: 0   1   2   3
            {0, 1, -1, 0},  // from 0 (00)
            {-1, 0, 0, 1},  // from 1 (01)
            {1, 0, 0, -1},  // from 2 (10)
            {0, -1, 1, 0},  // from 3 (11)
        };
        count_ += kDelta[state_][now];
        state_ = now;
        if(now != 3)
            return 0;
        const int c = count_;
        count_      = 0;
        return c >= 2 ? 1 : c <= -2 ? -1 : 0;
    }

  private:
    bool a_ = true, b_ = true, last_a_ = true, last_b_ = true;
    int  state_ = 3, count_ = 0;
};

} // namespace x0x
